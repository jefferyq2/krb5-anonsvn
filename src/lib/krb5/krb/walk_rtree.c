/*
 * $Source$
 * $Author$
 *
 * Copyright 1990,1991 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America is assumed
 *   to require a specific license from the United States Government.
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
 * permission.  M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 *
 * krb5_walk_realm_tree()
 */

#if !defined(lint) && !defined(SABER)
static char rcsid_walk_rtree_c[] =
"$Id$";
#endif	/* !lint & !SABER */

#include <krb5/krb5.h>
#include <krb5/ext-proto.h>
#include "int-proto.h"

/* internal function, used by krb5_get_cred_from_kdc() */

#ifndef min
#define min(x,y) ((x) < (y) ? (x) : (y))
#define max(x,y) ((x) > (y) ? (x) : (y))
#endif

krb5_error_code
#ifdef NARROW_PROTOTYPES
krb5_walk_realm_tree(const krb5_data *client, const krb5_data *server,
		     krb5_principal **tree, char realm_branch_char)
#else
krb5_walk_realm_tree(client, server, tree, realm_branch_char)
const krb5_data *client, *server;
krb5_principal **tree;
char realm_branch_char;
#endif
{
    krb5_error_code retval;
    krb5_principal *rettree;
    register char *ccp, *scp;
    register char *prevccp = 0, *prevscp = 0;
    char *com_sdot = 0, *com_cdot = 0;
    register int i, links = 0;
    int clen, slen;
    krb5_data tmpcrealm, tmpsrealm;
    int nocommon = 1;

    clen = client->length;
    slen = server->length;

    for (com_cdot = ccp = client->data + clen - 1,
	 com_sdot = scp = server->data + slen - 1;
	 clen && slen && *ccp == *scp ;
	 ccp--, scp--, 	clen--, slen--) {
	if (*ccp == realm_branch_char) {
	    com_cdot = ccp;
	    com_sdot = scp;
	    nocommon = 0;
	}
    }

    /* ccp, scp point to common root.
       com_cdot, com_sdot point to common components. */
    /* handle case of one ran out */
    if (!clen) {
	/* construct path from client to server, down the tree */
	if (!slen)
	    /* in the same realm--this means there is no ticket
	       in this realm. */
	    return KRB5_NO_TKT_IN_RLM;
	if (*scp == realm_branch_char) {
	    /* one is a subdomain of the other */
	    com_cdot = client->data;
	    com_sdot = scp;
	} /* else normal case of two sharing parents */
    }
    if (!slen) {
	/* construct path from client to server, up the tree */
	if (*ccp == realm_branch_char) {
	    /* one is a subdomain of the other */
	    com_sdot = server->data;
	    com_cdot = ccp;
	} /* else normal case of two sharing parents */
    }
    /* determine #links to/from common ancestor */
    if (nocommon)
	links = 1;
    else
	links = 2;
    /* if no common ancestor, artificially set up common root at the last
       component, then join with special code */
    for (ccp = client->data; ccp < com_cdot; ccp++) {
	if (*ccp == realm_branch_char) {
	    links++;
	    if (nocommon)
		prevccp = ccp;
	}
    }

    for (scp = server->data; scp < com_sdot; scp++) {
	if (*scp == realm_branch_char) {
	    links++;
	    if (nocommon)
		prevscp = scp;
	}
    }
    if (nocommon) {
	if (prevccp)
	    com_cdot = prevccp;
	if (prevscp)
	    com_sdot = prevscp;

	if(com_cdot == client->data + client->length -1)
	   com_cdot = client->data - 1 ;
	if(com_sdot == server->data + server->length -1)
	   com_sdot = server->data - 1 ;
    }

    if (!(rettree = (krb5_principal *)calloc(links+2,
					     sizeof(krb5_principal)))) {
	return ENOMEM;
    }
    i = 1;
    if (retval = krb5_tgtname(client,
			      client, &rettree[0])) {
	xfree(rettree);
	return retval;
    }
    for (prevccp = ccp = client->data;
	 ccp <= com_cdot;
	 ccp++) {
	if (*ccp != realm_branch_char)
	    continue;
	++ccp;				/* advance past dot */
	tmpcrealm.data = prevccp;
	tmpcrealm.length = client->length -
	    (prevccp - client->data);
	tmpsrealm.data = ccp;
	tmpsrealm.length = client->length -
	    (ccp - client->data);
	if (retval = krb5_tgtname(&tmpsrealm, &tmpcrealm, &rettree[i])) {
	    while (i) {
		krb5_free_principal(rettree[i-1]);
		i--;
	    }
	    xfree(rettree);
	    return retval;
	}
	prevccp = ccp;
	i++;
    }
    if (nocommon) {
	tmpcrealm.data = com_cdot + 1;
	tmpcrealm.length = client->length -
	    (com_cdot + 1 - client->data);
	tmpsrealm.data = com_sdot + 1;
	tmpsrealm.length = server->length -
	    (com_sdot + 1 - server->data);
	if (retval = krb5_tgtname(&tmpsrealm, &tmpcrealm, &rettree[i])) {
	    while (i) {
		krb5_free_principal(rettree[i-1]);
		i--;
	    }
	    xfree(rettree);
	    return retval;
	}
	i++;
    }

    for (prevscp = com_sdot + 1, scp = com_sdot - 1;
	 scp > server->data;
	 scp--) {
	if (*scp != realm_branch_char)
	    continue;
	if (scp - 1 < server->data)
	    break;			/* XXX only if . starts realm? */
	tmpcrealm.data = prevscp;
	tmpcrealm.length = server->length -
	    (prevscp - server->data);
	tmpsrealm.data = scp + 1;
	tmpsrealm.length = server->length -
	    (scp + 1 - server->data);
	if (retval = krb5_tgtname(&tmpsrealm, &tmpcrealm, &rettree[i])) {
	    while (i) {
		krb5_free_principal(rettree[i-1]);
		i--;
	    }
	    xfree(rettree);
	    return retval;
	}
	prevscp = scp + 1;
	i++;
    }
    if (slen && com_sdot >= server->data) {
	/* only necessary if building down tree from ancestor or client */
	/* however, we can get here if we have only one component
	   in the server realm name, hence we make sure we found a component
	   separator there... */
	tmpcrealm.data = prevscp;
	tmpcrealm.length = server->length -
	    (prevscp - server->data);
	if (retval = krb5_tgtname(server, &tmpcrealm,
				  &rettree[i])) {
	    while (i) {
		krb5_free_principal(rettree[i-1]);
		i--;
	    }
	    xfree(rettree);
	    return retval;
	}
    }
    *tree = rettree;
    return 0;
}
