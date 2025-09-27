/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_EVL_SKBUFF_H
#define _ASM_GENERIC_EVL_SKBUFF_H

#ifdef CONFIG_EVL_NET

#include <linux/ktime.h>

struct net_device;

struct skb_shared_oob {
	/* Device owning the storage. */
	struct net_device *owner;
	/* Time at device I/O. */
	ktime_t device_time;
	/* Time at RX/TX thread dequeuing/queuing point. */
	ktime_t queuing_time;
	/* Time at kernel/user boundary. */
	ktime_t delivery_time;
};

#else  /* !CONFIG_EVL_NET */

struct skb_shared_oob {
};

#endif	/* !CONFIG_EVL_NET */

#endif /* !_ASM_GENERIC_EVL_SKBUFF_H */
