/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2025 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_NET_TIMESTAMPING_H
#define _EVL_NET_TIMESTAMPING_H

#include <linux/circ_buf.h>
#include <linux/refcount.h>
#include <linux/rcupdate.h>
#include <linux/log2.h>
#include <evl/wait.h>
#include <evl/work.h>
#include <uapi/evl/net/socket-abi.h>

struct sk_buff;
struct evl_socket;

/*
 * The number of timestamping records we can keep for any given socket
 * at any point in time.
 */
#define EVL_NET_TIMESTAMP_BUFSZ  128

struct evl_net_timestamps {
	refcount_t refcnt;
	struct circ_buf ring;
	struct evl_wait_queue wait_queue;
	struct evl_work work;
	struct rcu_head	rcu;
	struct {
		struct evl_net_iotimes iots;
		u32 __padding;
	} data[EVL_NET_TIMESTAMP_BUFSZ];
};

int evl_setup_socket_iots(struct evl_socket *esk, int tsflags);

struct evl_net_timestamps *evl_get_socket_iots(struct evl_socket *esk);

void evl_put_socket_iots(struct evl_net_timestamps *t);

void evl_queue_iots_tx(struct sk_buff *skb);

int evl_copy_iots_rx(struct sk_buff *skb,
		struct user_oob_msghdr __user *u_msghdr);

ssize_t evl_collect_socket_iots(struct evl_socket *esk,
	struct user_oob_msghdr __user *u_msghdr,
	__u32 msg_flags,
	struct iovec *iov, size_t iovlen,
	ktime_t timeout, enum evl_tmode tmode);

bool __evl_test_socket_iots(struct evl_socket *esk);

static inline bool evl_test_socket_iots(struct evl_socket *esk) /* in-band / oob */
{
	bool ret;

	rcu_read_lock();
	ret =  __evl_test_socket_iots(esk);
	rcu_read_unlock();

	return ret;
}

extern refcount_t evl_net_rx_timestamping;

#endif /* !_EVL_NET_TIMESTAMPING_H */
