/*
 * $Source$
 * $Author$
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information, please see the file
 * <krb5/mit-copyright.h>.
 *
 * This file contains the source code for krb5_fcc_resolve.
 */

#if !defined(lint) && !defined(SABER)
static char fcc_reslve_c[] = "$Id$";
#endif /* !lint && !SABER */

#include <krb5/copyright.h>

#include "fcc.h"

extern krb5_cc_ops krb5_fcc_ops;

/*
 * Requires:
 * residual is a legal path name, and a null-terminated string
 *
 * Modifies:
 * id
 * 
 * Effects:
 * creates a file-based cred cache that will reside in the file
 * residual.  The cache is not opened, but the filename is reserved.
 * 
 * Returns:
 * A filled in krb5_ccache structure "id".
 *
 * Errors:
 * KRB5_NOMEM - there was insufficient memory to allocate the
 * 		krb5_ccache.  id is undefined.
 * permission errors
 */
krb5_error_code
krb5_fcc_resolve (id, residual)
   krb5_ccache *id;
   char *residual;
{
     krb5_ccache lid;
     int ret;
     
     lid = (krb5_ccache) malloc(sizeof(struct _krb5_ccache));
     if (lid == NULL)
	  return KRB5_NOMEM;

     lid->ops = &krb5_fcc_ops;
     
     lid->data = (krb5_fcc_data *) malloc(sizeof(krb5_fcc_data));
     if (((krb5_fcc_data *) lid->data) == NULL) {
	  xfree(lid);
	  return KRB5_NOMEM;
     }

     ((krb5_fcc_data *) lid->data)->filename = (char *)
	  malloc(strlen(residual) + 1);

     if (((krb5_fcc_data *) lid->data)->filename == NULL) {
	  xfree(((krb5_fcc_data *) lid->data));
	  xfree(lid);
	  return KRB5_NOMEM;
     }

     /* default to open/close on every trn */
     ((krb5_fcc_data *) lid->data)->flags = KRB5_TC_OPENCLOSE;
     ((krb5_fcc_data *) lid->data)->fd = -1;
     
     /* Set up the filename */
     strcpy(((krb5_fcc_data *) lid->data)->filename, residual);

     /* Make sure we can open the file name */
     ret = open(((krb5_fcc_data *) lid->data)->filename, O_CREAT|O_RDWR,0600);
     if (ret == -1)
	  return errno;
     else {
	  close(ret);
	  *id = lid;
	  return KRB5_OK;
     }
}
