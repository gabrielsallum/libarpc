/*
 * Copyright (C) 2010  2Wire, Inc.
 * All Rights Reserved.
 *
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 */

/*
 * svc_generic.c, Server side for RPC.
 *
 */

#include "compat.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <libarpc/arpc.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

#include "rpc_com.h"

#if 0
/*
 * The highest level interface for server creation.
 * It tries for all the nettokens in that particular class of token
 * and returns the number of handles it can create and/or find.
 *
 * It creates a link list of all the handles it could create.
 * If svc_create() is called multiple times, it uses the handle
 * created earlier instead of creating a new handle every time.
 */
int
svc_create(dispatch, prognum, versnum, nettype)
	void (*dispatch)(struct svc_req *, SVCXPRT *);
	rpcprog_t prognum;		/* Program number */
	rpcvers_t versnum;		/* Version number */
	const char *nettype;		/* Networktype token */
{
	struct xlist {
		SVCXPRT *xprt;		/* Server handle */
		struct xlist *next;	/* Next item */
	} *l;
	static struct xlist *xprtlist;	/* A link list of all the handles */
	int num = 0;
	SVCXPRT *xprt;
	struct netconfig *nconf;
	void *handle;
	extern mutex_t xprtlist_lock;

/* VARIABLES PROTECTED BY xprtlist_lock: xprtlist */

	if ((handle = __rpc_setconf(nettype)) == NULL) {
		warnx("svc_create: unknown protocol");
		return (0);
	}
	while ((nconf = __rpc_getconf(handle)) != NULL) {
		mutex_lock(&xprtlist_lock);
		for (l = xprtlist; l; l = l->next) {
			if (strcmp(l->xprt->xp_netid, nconf->nc_netid) == 0) {
				/* Found an old one, use it */
				(void) rpcb_unset(prognum, versnum, nconf);
				if (svc_reg(l->xprt, prognum, versnum,
					dispatch, nconf) == FALSE)
					warnx(
		"svc_create: could not register prog %u vers %u on %s",
					(unsigned)prognum, (unsigned)versnum,
					 nconf->nc_netid);
				else
					num++;
				break;
			}
		}
		if (l == NULL) {
			/* It was not found. Now create a new one */
			xprt = svc_tp_create(dispatch, prognum, versnum, nconf);
			if (xprt) {
				l = (struct xlist *)malloc(sizeof (*l));
				if (l == NULL) {
					warnx("svc_create: no memory");
					mutex_unlock(&xprtlist_lock);
					return (0);
				}
				l->xprt = xprt;
				l->next = xprtlist;
				xprtlist = l;
				num++;
			}
		}
		mutex_unlock(&xprtlist_lock);
	}
	__rpc_endconf(handle);
	/*
	 * In case of num == 0; the error messages are generated by the
	 * underlying layers; and hence not needed here.
	 */
	return (num);
}

/*
 * The high level interface to svc_tli_create().
 * It tries to create a server for "nconf" and registers the service
 * with the rpcbind. It calls svc_tli_create();
 */
SVCXPRT *
svc_tp_create(dispatch, prognum, versnum, nconf)
	void (*dispatch)(struct svc_req *, SVCXPRT *);
	rpcprog_t prognum;		/* Program number */
	rpcvers_t versnum;		/* Version number */
	const struct netconfig *nconf; /* Netconfig structure for the network */
{
	SVCXPRT *xprt;

	if (nconf == NULL) {
		warnx(
	"svc_tp_create: invalid netconfig structure for prog %u vers %u",
				(unsigned)prognum, (unsigned)versnum);
		return (NULL);
	}
	xprt = svc_tli_create(RPC_ANYFD, nconf, NULL, 0, 0);
	if (xprt == NULL) {
		return (NULL);
	}
	/*LINTED const castaway*/
	(void) rpcb_unset(prognum, versnum, (struct netconfig *) nconf);
	if (svc_reg(xprt, prognum, versnum, dispatch, nconf) == FALSE) {
		warnx(
		"svc_tp_create: Could not register prog %u vers %u on %s",
				(unsigned)prognum, (unsigned)versnum,
				nconf->nc_netid);
		SVC_DESTROY(xprt);
		return (NULL);
	}
	return (xprt);
}
#endif

int
ar_svc_tli_create(ar_ioctx_t ioctx, const char *netid, 
		  const arpc_addr_t *bindaddr, ar_svc_attr_t *attr, 
		  arpc_err_t *errp, ar_svc_xprt_t **retp)
{
	int err;
	ar_stat_t stat;
	ar_sockinfo_t si;
	ar_netid_t *info;
	ar_vcd_t drv;
	int fd;

	if (!bindaddr || !retp || !ioctx) {
		stat = ARPC_ERRNO;
		err = EINVAL;
		goto error;
	}

	if (netid == NULL) {
		stat = ARPC_UNKNOWNPROTO;
		err = EINVAL;
		goto error;
	}

	err  = ar_str2sockinfo(ioctx, netid, &si);
	err |= ar_str2netid(ioctx, netid, &info);
	if (err != EOK) {
		stat = ARPC_UNKNOWNPROTO;
		err = EINVAL;
		goto error;
	}

	if (si.si_af != ((struct sockaddr *)bindaddr->buf)->sa_family) {
		stat = ARPC_UNKNOWNHOST;
		err = EINVAL;
		goto error;
	}

	switch (info->an_semantics) {
	case AR_SEM_COTS: {
		void *stream;

		err = ar_vcd_lookup(ioctx, netid, &drv);
		if (err != 0) {
			stat = ARPC_UNKNOWNPROTO;
			goto error;
		}
		err = ar_vcd_listen(drv, bindaddr, &stream);
		if (err != 0) {
			stat = ARPC_UNKNOWNADDR;
			goto error;
		}

		err = ar_svc_vc_create(ioctx, drv, stream, attr, errp, retp);
		if (err != 0) {
			ar_vcd_close(drv, stream);
		}
		return err;
	}
	case AR_SEM_CLTS:
		fd = socket(info->an_family, SOCK_DGRAM, info->an_proto);
		if (fd < 0) {
			err = errno;
			stat = ARPC_ERRNO;
			goto error;
		}

		err = bind(fd, (struct sockaddr *)bindaddr->buf, 
			   bindaddr->len);
		if (err != 0) {
			err = errno;
			stat = ARPC_ERRNO;
			close(fd);
			fd = -1;
			goto error;
		}

		err = ar_svc_dg_create(ioctx, fd, attr, errp, retp);
		if (err != 0) {
			close(fd);
			fd = -1;
		}
		return err;
	default:
		stat = ARPC_UNKNOWNPROTO;
		err = EINVAL;
		break;
	}

error:
	if (errp) {
		errp->re_status = stat;
		if (stat == ARPC_ERRNO) {
			errp->re_errno = err;
		}
	}
	return err;
}
