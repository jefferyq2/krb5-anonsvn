/*
 * lib/krb5/os/changepw.c
 *
 * Copyright 1990,1999,2001 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 */

#define NEED_SOCKETS
#include "k5-int.h"
#include "os-proto.h"
#include "adm_err.h"

#include <stdio.h>
#include <errno.h>

/* Win32 defines. */
#if defined(_WIN16) || (defined(_WIN32) && !defined(__CYGWIN32__))
#ifndef ECONNABORTED
#define ECONNABORTED WSAECONNABORTED
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED WSAECONNREFUSED
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH WSAEHOSTUNREACH
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT WSAETIMEDOUT
#endif
#endif /* _WIN32 && !__CYGWIN32__ */

/*
 * Wrapper function for the two backends
 */

static krb5_error_code
krb5_locate_kpasswd(context, realm, addr_pp, naddrs)
    krb5_context context;
    const krb5_data *realm;
    struct sockaddr ***addr_pp;
    int *naddrs;
{
    krb5_error_code code;

    code = krb5int_locate_server (context, realm, addr_pp, naddrs, 0,
				  "kpasswd_server", "_kpasswd", 0,
				  DEFAULT_KPASSWD_PORT, 0);
    if (code) {
	code = krb5int_locate_server (context, realm, addr_pp, naddrs, 0,
				      "admin_server", "_kerberos-adm", 1,
				      DEFAULT_KPASSWD_PORT, 0);
	if (!code) {
	    /* Success with admin_server but now we need to change the
	       port number to use DEFAULT_KPASSWD_PORT.  */
	    int i;
	    for ( i=0;i<*naddrs;i++ ) {
		struct sockaddr *a = (*addr_pp)[i];
		if (a->sa_family == AF_INET)
		    sa2sin (a)->sin_port = htons(DEFAULT_KPASSWD_PORT);
	    }
	}
    }
    return (code);
}


KRB5_DLLIMP krb5_error_code KRB5_CALLCONV
krb5_change_password(context, creds, newpw, result_code,
		     result_code_string, result_string)
    krb5_context context;
    krb5_creds *creds;
    char *newpw;
    int *result_code;
    krb5_data *result_code_string;
    krb5_data *result_string;
{
    krb5_auth_context auth_context;
    krb5_data ap_req, chpw_req, chpw_rep;
    krb5_address local_kaddr, remote_kaddr;
    char *code_string;
    krb5_error_code code = 0;
    int i, addrlen;
    struct sockaddr **addr_p;
    struct sockaddr_storage local_addr, remote_addr, tmp_addr;
    int naddr_p;
    int cc, local_result_code, tmp_len;
    SOCKET s1 = INVALID_SOCKET, s2 = INVALID_SOCKET;
    int tried_one = 0;


    /* Initialize values so that cleanup call can safely check for NULL */
    auth_context = NULL;
    addr_p = NULL;
    memset(&chpw_req, 0, sizeof(krb5_data));
    memset(&chpw_rep, 0, sizeof(krb5_data));
    memset(&ap_req, 0, sizeof(krb5_data));

    /* initialize auth_context so that we know we have to free it */
    if ((code = krb5_auth_con_init(context, &auth_context)))
	  goto cleanup;

    if ((code = krb5_mk_req_extended(context, &auth_context,
				     AP_OPTS_USE_SUBKEY,
				     NULL, creds, &ap_req)))
	  goto cleanup;

    if ((code = krb5_locate_kpasswd(context,
                                    krb5_princ_realm(context, creds->client),
                                    &addr_p, &naddr_p)))
        goto cleanup;

    /* this is really obscure.  s1 is used for all communications.  it
       is left unconnected in case the server is multihomed and routes
       are asymmetric.  s2 is connected to resolve routes and get
       addresses.  this is the *only* way to get proper addresses for
       multihomed hosts if routing is asymmetric.

       A related problem in the server, but not the client, is that
       many os's have no way to disconnect a connected udp socket, so
       the s2 socket needs to be closed and recreated for each
       request.  The s1 socket must not be closed, or else queued
       requests will be lost.

       A "naive" client implementation (one socket, no connect,
       hostname resolution to get the local ip addr) will work and
       interoperate if the client is single-homed. */

    if ((s1 = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
	code = SOCKET_ERRNO;
	goto cleanup;
    }

    if ((s2 = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
	code = SOCKET_ERRNO;
	goto cleanup;
    }

    for (i=0; i<naddr_p; i++) {
	fd_set fdset;
	struct timeval timeout;

	/* XXX Now the locate_ functions can return IPv6 addresses.  */
	if (addr_p[i]->sa_family != AF_INET)
	    continue;

	tried_one = 1;
	if (connect(s2, addr_p[i], socklen(addr_p[i])) == SOCKET_ERROR) {
	    if (SOCKET_ERRNO == ECONNREFUSED || SOCKET_ERRNO == EHOSTUNREACH)
		continue; /* try the next addr */

	    code = SOCKET_ERRNO;
	    goto cleanup;
	}

        addrlen = sizeof(local_addr);

	if (getsockname(s2, ss2sa(&local_addr), &addrlen) < 0) {
	    if (SOCKET_ERRNO == ECONNREFUSED || SOCKET_ERRNO == EHOSTUNREACH)
		continue; /* try the next addr */

	    code = SOCKET_ERRNO;
	    goto cleanup;
	}

	/* some brain-dead OS's don't return useful information from
	 * the getsockname call.  Namely, windows and solaris.  */

	if (ss2sin(&local_addr)->sin_addr.s_addr != 0) {
	    local_kaddr.addrtype = ADDRTYPE_INET;
	    local_kaddr.length = sizeof(ss2sin(&local_addr)->sin_addr);
	    local_kaddr.contents = (krb5_octet *) &ss2sin(&local_addr)->sin_addr;
	} else {
	    krb5_address **addrs;

	    krb5_os_localaddr(context, &addrs);

	    local_kaddr.magic = addrs[0]->magic;
	    local_kaddr.addrtype = addrs[0]->addrtype;
	    local_kaddr.length = addrs[0]->length;
	    local_kaddr.contents = malloc(addrs[0]->length);
	    memcpy(local_kaddr.contents, addrs[0]->contents, addrs[0]->length);

	    krb5_free_addresses(context, addrs);
	}

	addrlen = sizeof(remote_addr);
	if (getpeername(s2, ss2sa(&remote_addr), &addrlen) < 0) {
	    if (SOCKET_ERRNO == ECONNREFUSED || SOCKET_ERRNO == EHOSTUNREACH)
		continue; /* try the next addr */

	    code = SOCKET_ERRNO;
	    goto cleanup;
	}

	remote_kaddr.addrtype = ADDRTYPE_INET;
	remote_kaddr.length = sizeof(ss2sin(&remote_addr)->sin_addr);
	remote_kaddr.contents = (krb5_octet *) &ss2sin(&remote_addr)->sin_addr;

	/* mk_priv requires that the local address be set.
	   getsockname is used for this.  rd_priv requires that the
	   remote address be set.  recvfrom is used for this.  If
	   rd_priv is given a local address, and the message has the
	   recipient addr in it, this will be checked.  However, there
	   is simply no way to know ahead of time what address the
	   message will be delivered *to*.  Therefore, it is important
	   that either no recipient address is in the messages when
	   mk_priv is called, or that no local address is passed to
	   rd_priv.  Both is a better idea, and I have done that.  In
	   summary, when mk_priv is called, *only* a local address is
	   specified.  when rd_priv is called, *only* a remote address
	   is specified.  Are we having fun yet?  */

	if ((code = krb5_auth_con_setaddrs(context, auth_context,
					   &local_kaddr, NULL))) {
	    code = SOCKET_ERRNO;
	    goto cleanup;
	}

	if ((code = krb5_mk_chpw_req(context, auth_context, &ap_req,
				     newpw, &chpw_req)))
	{
	    code = SOCKET_ERRNO;
	    goto cleanup;
	}

	if ((cc = sendto(s1, chpw_req.data, (int) chpw_req.length, 0,
			 addr_p[i], socklen(addr_p[i]))) != chpw_req.length)
	{
	    if ((cc < 0) && ((SOCKET_ERRNO == ECONNREFUSED) ||
			     (SOCKET_ERRNO == EHOSTUNREACH)))
		continue; /* try the next addr */

	    code = (cc < 0) ? SOCKET_ERRNO : ECONNABORTED;
	    goto cleanup;
	}

	chpw_rep.length = 1500;
	chpw_rep.data = (char *) malloc(chpw_rep.length);

	/* XXX need a timeout/retry loop here */
	FD_ZERO (&fdset);
	FD_SET (s1, &fdset);
	timeout.tv_sec = 120;
	timeout.tv_usec = 0;
	switch (select (s1 + 1, &fdset, 0, 0, &timeout)) {
	case -1:
	    code = SOCKET_ERRNO;
	    goto cleanup;
	case 0:
	    code = ETIMEDOUT;
	    goto cleanup;
	default:
	    /* fall through */
	    ;
	}

	/* "recv" would be good enough here... except that Windows/NT
	   commits the atrocity of returning -1 to indicate failure,
	   but leaving errno set to 0.

	   "recvfrom(...,NULL,NULL)" would seem to be a good enough
	   alternative, and it works on NT, but it doesn't work on
	   SunOS 4.1.4 or Irix 5.3.  Thus we must actually accept the
	   value and discard it. */
	tmp_len = sizeof(tmp_addr);
	if ((cc = recvfrom(s1, chpw_rep.data, (int) chpw_rep.length,
			   0, ss2sa(&tmp_addr), &tmp_len)) < 0)
	{
	    code = SOCKET_ERRNO;
	    goto cleanup;
	}

	closesocket(s1);
	s1 = INVALID_SOCKET;
	closesocket(s2);
	s2 = INVALID_SOCKET;

	chpw_rep.length = cc;

	if ((code = krb5_auth_con_setaddrs(context, auth_context,
					   NULL, &remote_kaddr)))
	    goto cleanup;

	if ((code = krb5_rd_chpw_rep(context, auth_context, &chpw_rep,
				     &local_result_code,
				     result_string)))
	    goto cleanup;

	if (result_code)
	    *result_code = local_result_code;

	if (result_code_string) {
	    if ((code = krb5_chpw_result_code_string(context,
						     local_result_code,
						     &code_string)))
		goto cleanup;

	    result_code_string->length = strlen(code_string);
	    if ((result_code_string->data =
		 (char *) malloc(result_code_string->length)) == NULL)
		return(ENOMEM);
	    strncpy(result_code_string->data, code_string, result_code_string->length);
	}

	code = 0;
	goto cleanup;
    }

    if (tried_one)
	/* Got some non-fatal errors, but didn't get any successes.  */
	code = SOCKET_ERRNO;
    else
	/* Had some addresses, but didn't try any because they weren't
	   AF_INET addresses and we don't support AF_INET6 addresses
	   here yet.  */
	code = EHOSTUNREACH;

cleanup:
    if (auth_context != NULL)
	krb5_auth_con_free(context, auth_context);

    if (addr_p != NULL) {
	for (i = 0; i < naddr_p; i++)
	    krb5_xfree (addr_p[i]);
	krb5_xfree(addr_p);
    }

    if (s1 != INVALID_SOCKET)
	closesocket(s1);

    if (s2 != INVALID_SOCKET)
	closesocket(s2);

    krb5_free_data_contents(context, &chpw_req);
    krb5_free_data_contents(context, &chpw_rep);
    krb5_free_data_contents(context, &ap_req);

    return(code);
}
