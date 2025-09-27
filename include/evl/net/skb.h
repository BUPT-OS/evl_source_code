/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_NET_SKB_H
#define _EVL_NET_SKB_H

#include <linux/types.h>
#include <linux/list.h>
#include <evl/lock.h>
#include <evl/timeout.h>

struct sk_buff;
struct net_device;
struct evl_net_handler;
struct evl_net_skb_queue;
struct evl_socket;
struct iovec;

/*
 * The content of our control block is valid as long as EVL owns the
 * buffer. Any layer from the in-band netstack may happily reuse it
 * for its own purpose.
 */
struct evl_net_cb {
	/* Cached source/destination device. */
	struct net_device *dev;
	union {
		/* Low-level handler at ingress. */
		struct evl_net_handler *handler;
		/* Socket tracking memory consumption. */
		struct evl_socket *tracker;
	};
	/* Protocol-specific information. */
	union {
	};
};
#define EVL_NET_CB(__skb)  ((struct evl_net_cb *)&((__skb)->cb[0]))

void evl_net_init_skb_queue(struct evl_net_skb_queue *txq);

void evl_net_destroy_skb_queue(struct evl_net_skb_queue *txq);

void evl_net_add_skb_queue(struct evl_net_skb_queue *skbq,
			struct sk_buff *skb);

struct sk_buff *
evl_net_get_skb_queue(struct evl_net_skb_queue *skbq,
		bool *more);

bool evl_net_move_skb_queue(struct evl_net_skb_queue *skbq,
			struct list_head *list);

int evl_net_dev_build_pool(struct net_device *dev);

void evl_net_dev_purge_pool(struct net_device *dev);

struct sk_buff *evl_net_wget_skb(struct evl_socket *esk,
				struct net_device *dev,
				ktime_t timeout);

void evl_net_wput_skb(struct sk_buff *skb);

struct sk_buff *evl_net_dev_alloc_skb(struct net_device *dev,
				      ktime_t timeout,
				      enum evl_tmode tmode);

void evl_net_free_skb(struct sk_buff *skb);

void evl_net_free_skb_list(struct list_head *list);

struct sk_buff *evl_net_clone_skb(struct sk_buff *skb);

bool evl_net_charge_skb_rmem(struct evl_socket *esk,
			struct sk_buff *skb);

void evl_net_uncharge_skb_rmem(struct sk_buff *skb);

int evl_net_charge_skb_wmem(struct evl_socket *esk,
			struct sk_buff *skb,
			ktime_t timeout, enum evl_tmode tmode);

void evl_net_uncharge_skb_wmem(struct sk_buff *skb);

ssize_t evl_net_skb_to_uio(const struct iovec *iov, size_t iovlen,
			struct sk_buff *skb, size_t skip,
			bool *short_write);

#endif /* !_EVL_NET_SKB_H */
