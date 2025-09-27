/*
 * SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_UAPI_NET_SOCKET_ABI_H
#define _EVL_UAPI_NET_SOCKET_ABI_H

#include <linux/socket.h>
#include <evl/types-abi.h>

#define AF_OOB		46	/* Out-of-band domain sockets */

#define SOCK_OOB	O_OOB	/* Request out-of-band capabilities */

#define MSG_TIMESTAMP	MSG_ERRQUEUE	/* Alias to collect I/O timestamps */

/*
 * CAUTION: the oob-specific socket options must not conflict with
 * standard definitions (check uapi/asm-generic/socket.h).
 */
#define SO_TIMESTAMP_OOB	1024

struct evl_net_iotimes {
	/* Time at device<->netstack boundary (monotonic). */
	__u64 device_time;
	/* Time at RX/TX thread dequeuing/queuing point (monotonic). */
	__u64 queuing_time;
	/* Time at kernel/user boundary (monotonic). */
	__u64 delivery_time;
};

/* Keep this distinct from SOCK_IOC_TYPE (0x89) */
#define EVL_SOCKET_IOCBASE  0xee

struct user_oob_msghdr {
	__u64 name_ptr;		/* (struct sockaddr __user *name) */
	__u64 iov_ptr;		/* (struct iovec __user *iov) */
	__u64 ctl_ptr;		/* (void __user *control) */
	__u32 namelen;
	__u32 iovlen;
	__u32 ctllen;
	__s32 count;		/* Receive only (actual byte count). */
	__u32 flags;
	struct __evl_timespec timeout;
};

struct evl_netdev_activation {
	__u64 poolsz;
	__u64 bufsz;
};

#define EVL_NEIGH_PERMANENT  0x1

struct evl_net_solicit {
	struct __kernel_sockaddr_storage addr;
	__u32 flags;
};

enum {
	EVL_SOCKOPT_RECVSZ,
	EVL_SOCKOPT_SENDSZ,
	EVL_SOCKOPT_TIMESTAMPING,
};

enum {
	EVL_SOF_TIMESTAMP_RX = (1 << 0),
	EVL_SOF_TIMESTAMP_TX = (1 << 1),
	EVL_SOF_TIMESTAMP_DEVICE = (1 << 2),
	EVL_SOF_TIMESTAMP_QUEUING = (1 << 3),
};

#define EVL_SOF_TIMESTAMPS  \
	(EVL_SOF_TIMESTAMP_RX|EVL_SOF_TIMESTAMP_TX| \
		EVL_SOF_TIMESTAMP_DEVICE|EVL_SOF_TIMESTAMP_QUEUING)

struct evl_net_sockopt {
	int level;
	int option;
	__u64 optval_ptr;		/* ([const] void __user *optval) */
	__u64 optlen_ptr;		/* ([const] socklen_t __user *optlen) */
};

#define EVL_SOCKIOC_ACTIVATE	_IOW(EVL_SOCKET_IOCBASE, 1, struct evl_netdev_activation)
#define EVL_SOCKIOC_DEACTIVATE	_IO(EVL_SOCKET_IOCBASE, 2)
#define EVL_SOCKIOC_SENDMSG	_IOW(EVL_SOCKET_IOCBASE, 3, struct user_oob_msghdr)
#define EVL_SOCKIOC_RECVMSG	_IOWR(EVL_SOCKET_IOCBASE, 4, struct user_oob_msghdr)
#define EVL_SOCKIOC_SOLICIT	_IOW(EVL_SOCKET_IOCBASE, 5, struct evl_net_solicit)
#define EVL_SOCKIOC_SETOPT	_IOW(EVL_SOCKET_IOCBASE, 6, struct evl_net_sockopt)
#define EVL_SOCKIOC_GETOPT	_IOR(EVL_SOCKET_IOCBASE, 7, struct evl_net_sockopt)

#endif /* !_EVL_UAPI_NET_SOCKET_ABI_H */
