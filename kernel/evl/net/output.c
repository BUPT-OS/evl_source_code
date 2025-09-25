/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/irq_work.h>
#include <evl/list.h>
#include <evl/lock.h>
#include <evl/flag.h>
#include <evl/net/socket.h>
#include <evl/net/output.h>
#include <evl/net/qdisc.h>

static void xmit_inband(struct irq_work *work);

static DEFINE_IRQ_WORK(oob_xmit_work, xmit_inband);

static DEFINE_PER_CPU(struct evl_net_skb_queue, oob_tx_relay);

static void timestamp_at_device(struct sk_buff *skb)
{
	if (skb_is_oob_timestamped(skb)) {
		skb_shinfo_oob(skb)->device_time = evl_ktime_monotonic();
		evl_queue_iots_tx(skb);
	}
}

static void timestamp_at_sched(struct sk_buff *skb)
{
	struct evl_socket *esk = EVL_NET_CB(skb)->tracker;
	int tsflags;

	if (likely(esk)) {
		tsflags = READ_ONCE(esk->timestamping);
		if (tsflags & EVL_SOF_TIMESTAMP_TX &&
			tsflags & EVL_SOF_TIMESTAMP_QUEUING)
			skb_shinfo_oob(skb)->queuing_time = evl_ktime_monotonic();
	}
}

static inline netdev_tx_t
oob_start_xmit(struct net_device *dev, struct sk_buff *skb, bool more)
{
	struct netdev_queue *txq;
	netdev_tx_t ret;

	/*
	 * We don't check the queue index using netdev_cap_txqueue(),
	 * assuming the inband code does so routinely for in-band
	 * traffic.
	 */
	txq = netdev_get_tx_queue(dev, skb_get_queue_mapping(skb));

	/*
	 * If we got there, @dev is deemed oob-capable
	 * (IFF_OOB_CAPABLE, see evl_net_transmit()). The driver should
	 * check the current execution stage for handling the
	 * out-of-band packet properly.
	 */
	netif_tx_lock_oob(txq);
	ret = netdev_start_xmit(skb, dev, txq, more);
	netif_tx_unlock_oob(txq);

	return ret;
}

static inline void do_tx(struct evl_net_qdisc *qdisc,
			struct net_device *dev, struct sk_buff *skb,
			bool more)
{
	/*
	 * CAUTION: We must timestamp before uncharging which clears
	 * the socket tracking info.
	 */
	timestamp_at_device(skb);
	evl_net_uncharge_skb_wmem(skb);

	switch (oob_start_xmit(dev, skb, more)) {
	case NETDEV_TX_OK:
		break;
	default: /* busy, or whatever */
		qdisc->packet_dropped++;
		/* FIXME: we need to do better wrt error handling. */
		evl_net_free_skb(skb);
		break;
	}
}

void evl_net_do_tx(void *arg)
{
	struct net_device *dev = arg;
	struct evl_netdev_state *est;
	struct evl_net_qdisc *qdisc;
	struct sk_buff *skb;
	LIST_HEAD(list);
	int ret;

	est = dev->oob_state.estate;

	while (!evl_kthread_should_stop()) {
		ret = evl_wait_flag(&est->tx_flag);
		if (ret)
			break;

		/*
		 * FIXME: stax-protect this against swap while pulling
		 * packets.
		 */
		qdisc = est->qdisc;

		/*
		 * Transmit the traffic according to the
		 * prioritization implemented by the queueing
		 * discipline attached to our device.
		 */
		for (;;) {
			bool more;
			skb = qdisc->oob_ops->dequeue(qdisc, &more);
			if (skb == NULL)
				break;
			do_tx(qdisc, dev, skb, more);
		}
	}
}

static void skb_xmit_inband(struct sk_buff *skb)
{
	/*
	 * Timestamping at device here is technically wrong because
	 * dev_queue_xmit() may queue and delay the buffer for
	 * transmission, but this is harmless, we don't have to be
	 * accurate when measuring output delays in best-effort mode,
	 * i.e. via the in-band stack, what matters is the oob path.
	 *
	 * CAUTION: wait for the timestamping to take place before
	 * uncharging the socket for the memory, which might allow the
	 * tracker to go stale on a different CPU (see how the wmem
	 * crossing is used to synchronize with in-flight TX buffers
	 * in disable_oob_port().
	 */
	timestamp_at_device(skb);
	evl_net_uncharge_skb_wmem(skb);
	skb->prev = NULL;
	skb->next = NULL;
	dev_queue_xmit(skb);
}

/* in-band hook, called upon NET_TX_SOFTIRQ. */
void process_inband_tx_backlog(struct softnet_data *sd)
{
	struct sk_buff *skb, *n;
	LIST_HEAD(list);

	if (evl_net_move_skb_queue(this_cpu_ptr(&oob_tx_relay), &list)) {
		list_for_each_entry_safe(skb, n, &list, list) {
			list_del(&skb->list);
			skb_xmit_inband(skb);
		}
	}
}

static void xmit_inband(struct irq_work *work) /* in-band, stalled */
{
	/*
	 * process_inband_tx_backlog() should run soon, kicked by tx_action.
	 */
	__raise_softirq_irqoff(NET_TX_SOFTIRQ);
}

/* oob or in-band */
static int xmit_oob(struct net_device *dev, struct sk_buff *skb)
{
	struct evl_netdev_state *est = dev->oob_state.estate;
	int ret;

	ret = evl_net_sched_packet(dev, skb);
	if (ret)
		return ret;

	evl_raise_flag(&est->tx_flag);

	return 0;
}

/**
 *	evl_net_transmit - queue an egress packet for out-of-band
 *	transmission to the device.
 *
 *	Add an outgoing packet to the out-of-band transmit queue, so
 *	that it will be handed over to the device referred to by
 *	@skb->dev. The packet is complete (e.g. the VLAN tag is set if
 *	@skb->dev is a VLAN device).
 *
 *	@skb the packet to queue. Must not be linked to any upstream
 *	queue.
 *
 *	Prerequisites:
 *	- skb->dev is a valid (real) device. The caller must prevent from
 *        the interface going down.
 *	- skb->sk == NULL.
 */
int evl_net_transmit(struct sk_buff *skb) /* oob or in-band */
{
	struct evl_net_skb_queue *rl = this_cpu_ptr(&oob_tx_relay);
	struct net_device *dev = skb->dev;
	unsigned long flags;
	bool kick;

	if (EVL_WARN_ON(NET, !dev))
		return -EINVAL;

	if (EVL_WARN_ON(NET, skb->sk))
		return -EINVAL;

	timestamp_at_sched(skb);

	skb_mark_not_on_list(skb);

	if (netdev_is_oob_capable(dev))
		return xmit_oob(dev, skb);

	/*
	 * If running in-band, just push the skb for transmission
	 * immediately to the in-band stack. Otherwise relay it via
	 * xmit_inband().
	 */
	if (running_inband()) {
		skb_xmit_inband(skb);
		return 0;
	}

	/*
	 * Running oob but net device is not oob-capable, resort to
	 * relaying the traffic to the in-band stage for enqueuing.
	 * Dovetail does ensure that __raise_softirq_irqoff() is safe
	 * to call from the oob stage provided hard irqs are off, but
	 * we want the softirq to be raised as soon as in-band resumes
	 * with interrupts enabled, so we go through the irq_work
	 * indirection first.
	 */
	raw_spin_lock_irqsave(&rl->lock, flags);
	kick = list_empty(&rl->queue);
	list_add_tail(&skb->list, &rl->queue);
	raw_spin_unlock_irqrestore(&rl->lock, flags);

	if (kick)	/* Rare false positives are ok. */
		irq_work_queue(&oob_xmit_work);

	return 0;
}

void netif_tx_lock_oob(struct netdev_queue *txq) /* oob or in-band */
{
	evl_lock_stax(&txq->oob.tx_lock);
}
EXPORT_SYMBOL_GPL(netif_tx_lock_oob);

void netif_tx_unlock_oob(struct netdev_queue *txq) /* oob or in-band */
{
	evl_unlock_stax(&txq->oob.tx_lock);
}
EXPORT_SYMBOL_GPL(netif_tx_unlock_oob);

void __init evl_net_init_tx(void)
{
	struct evl_net_skb_queue *txq;
	int cpu;

	for_each_online_cpu(cpu) {
		txq = &per_cpu(oob_tx_relay, cpu);
		evl_net_init_skb_queue(txq);
	}
}
