/*
 * status.c:  return the status of a working copy dirent
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>

#include "client.h"

#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_test.h"
#include "svn_io.h"



/*** Getting update information ***/


/* Open an RA session to URL, providing PATH/AUTH_BATON for
   authentication callbacks.

   STATUSHASH has presumably been filled with status structures that
   contain only local-mod information.  Ask RA->do_status() to drive a
   custom editor that will add update information to this collection
   of structures.  Also, use the RA session to fill in the "youngest
   revnum" field in each structure.

   If DESCEND is zero, only immediate children of PATH will be edited
   or added to the hash.  Else, the dry-run update will be fully
   recursive. */
static svn_error_t *
add_update_info_to_status_hash (apr_hash_t *statushash,
                                svn_stringbuf_t *path,
                                svn_client_auth_baton_t *auth_baton,
                                svn_boolean_t descend,
                                apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;  
  svn_ra_callbacks_t *ra_callbacks;
  void *ra_baton, *cb_baton, *session, *edit_baton, *report_baton;
  svn_delta_edit_fns_t *status_editor;
  const svn_ra_reporter_t *reporter;
  svn_stringbuf_t *anchor, *target, *URL;
  svn_wc_entry_t *entry;

  /* Use PATH to get the update's anchor and targets. */
  SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));

  /* Get full URL from the ANCHOR. */
  SVN_ERR (svn_wc_entry (&entry, anchor, pool));
  if (! entry)
    return svn_error_createf
      (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, pool,
       "svn_client_update: %s is not under revision control", anchor->data);
  if (! entry->url)
    return svn_error_createf
      (SVN_ERR_WC_ENTRY_MISSING_URL, 0, NULL, pool,
       "svn_client_update: entry '%s' has no URL", anchor->data);
  URL = svn_stringbuf_dup (entry->url, pool);

  /* Do RA interaction here to figure out what is out of date with
     respect to the repository.  All RA errors are non-fatal!! */

  /* Get the RA library that handles URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  if (svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool) != NULL)
    return SVN_NO_ERROR;

  /* Open a repository session to the URL. */
  SVN_ERR (svn_client__get_ra_callbacks (&ra_callbacks, &cb_baton,
                                         auth_baton, anchor, TRUE,
                                         TRUE, pool));
  if (ra_lib->open (&session, URL, ra_callbacks, cb_baton, pool) != NULL)
    return SVN_NO_ERROR;


  /* Tell RA to drive a status-editor;  this will fill in the
     repos_status_ fields and repos_rev fields in each status struct. */

  SVN_ERR (svn_wc_get_status_editor (&status_editor, &edit_baton,
                                     path, descend, statushash, pool));
  if (ra_lib->do_status (session,
                         &reporter, &report_baton,
                         target, descend,
                         status_editor, edit_baton) == NULL)
    {
      /* Drive the reporter structure, describing the revisions within
         PATH.  When we call reporter->finish_report, the
         status_editor will be driven by svn_repos_dir_delta. */
      SVN_ERR (svn_wc_crawl_revisions (path, reporter, report_baton, 
                                       FALSE, /* ignore unversioned stuff */
                                       FALSE, /* don't restore missing files */
                                       descend,
                                       pool));
    }

  /* We're done with the RA session. */
  (void) ra_lib->close (session);

  return SVN_NO_ERROR;
}





/*** Public Interface. ***/


svn_error_t *
svn_client_status (apr_hash_t **statushash,
                   svn_stringbuf_t *path,
                   svn_client_auth_baton_t *auth_baton,
                   svn_boolean_t descend,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   apr_pool_t *pool)
{
  apr_hash_t *hash = apr_hash_make (pool);

  /* Ask the wc to give us a list of svn_wc_status_t structures.
     These structures contain nothing but information found in the
     working copy.

     Pass the GET_ALL and DESCEND flags;  this working copy function
     understands these flags too, and will return the correct set of
     structures.  */
  SVN_ERR (svn_wc_statuses (hash, path, descend, get_all, pool));


  /* If the caller wants us to contact the repository also... */
  if (update)    
    /* Add "dry-run" update information to our existing structures.
       (Pass the DESCEND flag here, since we may want to ignore update
       info that is below PATH.)  */
    SVN_ERR (add_update_info_to_status_hash (hash, path,
                                             auth_baton, descend, pool));


  *statushash = hash;

  return SVN_NO_ERROR;
}









/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
