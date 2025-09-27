/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2025 Philippe Gerum  <rpm@xenomai.org>
 */

#include <linux/slab.h>
#include <linux/err.h>
#include <linux/refcount.h>
#include <evl/uio.h>
#include <evl/uaccess.h>
#include <evl/net/socket.h>
#include <evl/net/timestamping.h>
#include <evl/net/skb.h>

/* Tracks the number of sockets with RX timestamping enabled (+1). */
refcount_t evl_net_rx_timestamping = REFCOUNT_INIT(1);

static void iots_free_work(struct evl_work *work);

static struct evl_net_timestamps *alloc_iots(void) /* in-band */
{
	struct evl_net_timestamps *t;

	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return ERR_PTR(-ENOMEM);

	refcount_set(&t->refcnt, 1);
	t->ring.head = t->ring.tail = 0;
	t->ring.buf = (char *)t->data;
	evl_init_wait(&t->wait_queue, &evl_mono_clock, EVL_WAIT_FIFO);
	evl_init_work(&t->work, iots_free_work);

	return t;
}

static void free_iots(struct evl_net_timestamps *t) /* in-band */
{
	evl_destroy_wait(&t->wait_queue);
	kfree_rcu(t, rcu);
}

int evl_setup_socket_iots(struct evl_socket *esk, int tsflags) /* in-band */
{
	struct evl_net_timestamps *t, *nt;
	unsigned long flags;
	int ret = 0;

	inband_context_only();

	/*
	 * Setting the iots buffer amounts to a full reset if already
	 * set.
	 */
	nt = tsflags ? alloc_iots() : NULL;
	if (IS_ERR(nt))
		return PTR_ERR(t);

	spin_lock_irqsave(&esk->ts_lock, flags); /* irqsave guards against lock inversion. */

	t = rcu_dereference_protected(esk->tx_timestamps,
				lockdep_is_held(&esk->ts_lock));
	if (t && !tsflags) {
		if (esk->timestamping & EVL_SOF_TIMESTAMP_RX)
			refcount_dec(&evl_net_rx_timestamping);
		rcu_assign_pointer(esk->tx_timestamps, NULL);
	} else if (tsflags) {
		rcu_assign_pointer(esk->tx_timestamps, nt);
		if (tsflags & EVL_SOF_TIMESTAMP_RX)
			refcount_inc(&evl_net_rx_timestamping);
	}

	WRITE_ONCE(esk->timestamping, tsflags);

	spin_unlock_irqrestore(&esk->ts_lock, flags);

	if (t)
		free_iots(t);

	return ret;
}

struct evl_net_timestamps *evl_get_socket_iots(struct evl_socket *esk) /* in-band / oob */
{
	struct evl_net_timestamps *t;

	rcu_read_lock();

	t = rcu_dereference(esk->tx_timestamps);
	if (t)
		refcount_inc(&t->refcnt);

	rcu_read_unlock();

	return t;
}

void evl_put_socket_iots(struct evl_net_timestamps *t) /* in-band / oob */
{
	if (refcount_dec_and_test(&t->refcnt)) {
		if (running_inband())
			free_iots(t);
		else
			evl_call_inband(&t->work);
	}
}

static void iots_free_work(struct evl_work *work) /* in-band */
{
	struct evl_net_timestamps *t = container_of(work, struct evl_net_timestamps, work);
	free_iots(t);
}

static bool queue_iots_tx(struct evl_net_timestamps *t,
		struct sk_buff *skb)
{
	struct evl_net_iotimes *iots;
	int head, tail, room;
	unsigned long flags;
	const int tsz = sizeof(t->data[0]);
	bool ret = false;

	/* The size of the ring space must be a power of 2. */
	BUILD_BUG_ON(sizeof(t->data) & (sizeof(t->data) - 1));

	raw_spin_lock_irqsave(&t->wait_queue.wchan.lock, flags);

	head = t->ring.head;
	tail = smp_load_acquire(&t->ring.tail);

	room = CIRC_SPACE(head, tail, sizeof(t->data));
	if (likely(room >= tsz)) {
		iots = &t->data[head / tsz].iots;
		iots->device_time = skb_shinfo_oob(skb)->device_time;
		iots->queuing_time = skb_shinfo_oob(skb)->queuing_time;
		iots->delivery_time = skb_shinfo_oob(skb)->delivery_time;
		smp_store_release(&t->ring.head,
				(head + tsz) & (sizeof(t->data) - 1));
		ret = true;
		if (room == sizeof(t->data) - 1 &&  evl_wait_active(&t->wait_queue))
			evl_wake_up_head(&t->wait_queue);
	}

	raw_spin_unlock_irqrestore(&t->wait_queue.wchan.lock, flags);

	return ret;
}

void evl_queue_iots_tx(struct sk_buff *skb) /* oob */
{
	struct evl_socket *esk = EVL_NET_CB(skb)->tracker;
	struct evl_net_timestamps *t;

	/*
	 * The tracking socket of an egress skb is covered by the wmem
	 * crossing for staleness. IOW, if esk is referred to by an
	 * skb, then esk is valid memory. See sock_oob_release().
	 */
	if (likely(esk) &&
		READ_ONCE(esk->timestamping) & EVL_SOF_TIMESTAMP_TX) {
		t = evl_get_socket_iots(esk);
		if (likely(t)) {
			if (!queue_iots_tx(t, skb))
				set_bit(EVL_SOCK_TSOVERFLOW, &esk->flags);
			evl_put_socket_iots(t);
			evl_schedule();
		}
	}
}

/*
 * Copy the I/O timestamps to the control area referred to by the
 * message header. This area must be large enough to hold the iotimes
 * struct.
 */
int evl_copy_iots_rx(struct sk_buff *skb,
		struct user_oob_msghdr __user *u_msghdr) /* oob */
{
	struct evl_net_iotimes iots, __user *u_iots;
	__u64 ctl_ptr;
	__u32 ctllen;
	int ret;

	ret = raw_get_user(ctl_ptr, &u_msghdr->ctl_ptr);
	if (ret)
		return -EFAULT;

	ret = raw_get_user(ctllen, &u_msghdr->ctllen);
	if (ret)
		return -EFAULT;

	if (!ctl_ptr || !ctllen) /* Skip if unwanted. */
		return 0;

	if (ctllen < sizeof(struct evl_net_iotimes))
		return -EINVAL;

	/*
	 * We might be asked to process unstamped buffers, queued and
	 * unconsumed before timestamping was turned on. Tell user
	 * about this by zeroing the control len, otherwise copy the
	 * timestamps back.
	 */
	if (unlikely(!skb_is_oob_timestamped(skb))) {
		ret = raw_put_user(0, &u_msghdr->ctllen);
	} else {
		iots.device_time = skb_shinfo_oob(skb)->device_time;
		iots.queuing_time = skb_shinfo_oob(skb)->queuing_time;
		iots.delivery_time = skb_shinfo_oob(skb)->delivery_time;
		u_iots = evl_valptr64(ctl_ptr, struct evl_net_iotimes);
		ret = raw_copy_to_user(u_iots, &iots, sizeof(iots));
		ctllen = sizeof(struct evl_net_iotimes);
		ret |= raw_put_user(ctllen, &u_msghdr->ctllen);
	}

	return ret ? -EFAULT : 0;
}

ssize_t evl_collect_socket_iots(struct evl_socket *esk,
				struct user_oob_msghdr __user *u_msghdr,
				__u32 msg_flags,
				struct iovec *iov, size_t iovlen,
				ktime_t timeout, enum evl_tmode tmode) /* oob */
{
	struct evl_net_timestamps *t;
	struct evl_net_iotimes *iots;
	ssize_t count = 0, ret = 0;
	int head, tail, avail;
	unsigned long flags;
	const int tsz = sizeof(t->data[0]); /* Includes padding. */

	t = evl_get_socket_iots(esk);
	if (!t)	/* Nah, timestamping is off. */
		return -EINVAL;

	do {
		raw_spin_lock_irqsave(&t->wait_queue.wchan.lock, flags);

		head = smp_load_acquire(&t->ring.head);
		tail = t->ring.tail;

		avail = CIRC_CNT(head, tail, sizeof(t->data));
		if (avail >= tsz) {
			raw_spin_unlock_irqrestore(&t->wait_queue.wchan.lock, flags);
			iots = &t->data[tail / tsz].iots;
			ret = evl_copy_to_uio_incremental(&iov, &iovlen, iots, sizeof(*iots));
			if (ret <= 0) /* Allow for null reads. */
				break;
			if (ret < sizeof(*iots)) {
				/* We need space for at least one record. */
				ret = -EINVAL;
				break;
			}
			smp_store_release(&t->ring.tail,
					(tail + tsz) & (sizeof(t->data) - 1));
			count += sizeof(*iots);
			continue;
		}

		raw_spin_unlock_irqrestore(&t->wait_queue.wchan.lock, flags);

		if (count > 0)
			break;

		ret = evl_wait_event_timeout(&t->wait_queue, timeout, tmode,
					CIRC_CNT(head, tail, sizeof(t->data)) >= tsz);
	} while (!ret);

	evl_put_socket_iots(t);

	if (!ret && test_and_clear_bit(EVL_SOCK_TSOVERFLOW, &esk->flags))
		ret = raw_put_user(msg_flags | MSG_TRUNC, &u_msghdr->flags);

	return ret ?: count;
}

/* in-band / oob, rcu locked. */
bool __evl_test_socket_iots(struct evl_socket *esk)
{
	struct evl_net_timestamps *t;
	bool ret = false;
	int head, tail;

	t = rcu_dereference(esk->tx_timestamps);
	if (t) {
		head = READ_ONCE(t->ring.head);
		tail = READ_ONCE(t->ring.tail);
		ret = CIRC_CNT(head, tail, sizeof(t->data)) > 0;
	}

	return ret;
}
