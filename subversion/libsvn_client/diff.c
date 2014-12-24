#include "private/svn_diff_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_io_private.h"
#define DIFF_REVNUM_NONEXISTENT ((svn_revnum_t) -100)
/* Calculate the repository relative path of DIFF_RELPATH, using
 * SESSION_RELPATH and WC_CTX, and return the result in *REPOS_RELPATH.
 * ORIG_TARGET is the related original target passed to the diff command,
 * ANCHOR is the local path where the diff editor is anchored.
make_repos_relpath(const char **repos_relpath,
                   const char *diff_relpath,
                   const char *orig_target,
                   const char *session_relpath,
                   svn_wc_context_t *wc_ctx,
                   const char *anchor,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
  if (! session_relpath
      || (anchor && !svn_path_is_url(orig_target)))
      svn_error_t *err;
      SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                      svn_dirent_join(anchor, diff_relpath,
                                                      scratch_pool),
                                      scratch_pool));

      err = svn_wc__node_get_repos_info(NULL, repos_relpath, NULL, NULL,
                                        wc_ctx, local_abspath,
                                        result_pool, scratch_pool);

      if (!session_relpath
          || ! err
          || (err && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND))
        {
           return svn_error_trace(err);
        }
      /* The path represents a local working copy path, but does not
         exist. Fall through to calculate an in-repository location
         based on the ra session */
      /* ### Maybe we should use the nearest existing ancestor instead? */
      svn_error_clear(err);
  *repos_relpath = svn_relpath_join(session_relpath, diff_relpath,
                                    result_pool);
/* Adjust *INDEX_PATH, *ORIG_PATH_1 and *ORIG_PATH_2, representing the changed
 * node and the two original targets passed to the diff command, to handle the
 * directory the diff target should be considered relative to.
 * ANCHOR is the local path where the diff editor is anchored. The resulting
 * values are allocated in RESULT_POOL and temporary allocations are performed
 * in SCRATCH_POOL. */
adjust_paths_for_diff_labels(const char **index_path,
                             const char *anchor,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
  const char *new_path = *index_path;
  if (anchor)
    new_path = svn_dirent_join(anchor, new_path, result_pool);
                                                   result_pool);
      else if (! strcmp(relative_to_dir, new_path))
    }
  {
    apr_size_t len;
    svn_boolean_t is_url1;
    svn_boolean_t is_url2;
    /* ### Holy cow.  Due to anchor/target weirdness, we can't
       simply join dwi->orig_path_1 with path, ditto for
       orig_path_2.  That will work when they're directory URLs, but
       not for file URLs.  Nor can we just use anchor1 and anchor2
       from do_diff(), at least not without some more logic here.
       What a nightmare.

       For now, to distinguish the two paths, we'll just put the
       unique portions of the original targets in parentheses after
       the received path, with ellipses for handwaving.  This makes
       the labels a bit clumsy, but at least distinctive.  Better
       solutions are possible, they'll just take more thought. */

    /* ### BH: We can now just construct the repos_relpath, etc. as the
           anchor is available. See also make_repos_relpath() */

    is_url1 = svn_path_is_url(new_path1);
    is_url2 = svn_path_is_url(new_path2);

    if (is_url1 && is_url2)
      len = strlen(svn_uri_get_longest_ancestor(new_path1, new_path2,
                                                scratch_pool));
    else if (!is_url1 && !is_url2)
      len = strlen(svn_dirent_get_longest_ancestor(new_path1, new_path2,
                                                   scratch_pool));
    else
      len = 0; /* Path and URL */

    new_path1 += len;
    new_path2 += len;
  }
  /* ### Should diff labels print paths in local style?  Is there
     already a standard for this?  In any case, this code depends on
     a particular style, so not calling svn_dirent_local_style() on the
     paths below.*/
  if (new_path[0] == '\0')
    new_path = ".";
  if (new_path1[0] == '\0')
    new_path1 = new_path;
  else if (svn_path_is_url(new_path1))
    new_path1 = apr_psprintf(result_pool, "%s\t(%s)", new_path, new_path1);
  else if (new_path1[0] == '/')
    new_path1 = apr_psprintf(result_pool, "%s\t(...%s)", new_path, new_path1);
  else
    new_path1 = apr_psprintf(result_pool, "%s\t(.../%s)", new_path, new_path1);

  if (new_path2[0] == '\0')
    new_path2 = new_path;
  else if (svn_path_is_url(new_path2))
    new_path2 = apr_psprintf(result_pool, "%s\t(%s)", new_path, new_path2);
  else if (new_path2[0] == '/')
    new_path2 = apr_psprintf(result_pool, "%s\t(...%s)", new_path, new_path2);
  else
    new_path2 = apr_psprintf(result_pool, "%s\t(.../%s)", new_path, new_path2);

  *index_path = new_path;
  if (revnum >= 0)
  else if (revnum == DIFF_REVNUM_NONEXISTENT)
    label = apr_psprintf(pool, _("%s\t(nonexistent)"), path);
  else /* SVN_INVALID_REVNUM */
                             const char *copyfrom_path,
                             svn_revnum_t copyfrom_rev,
                             const char *path,
  if (copyfrom_rev != SVN_INVALID_REVNUM)
    SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                        "copy from %s@%ld%s", copyfrom_path,
                                        copyfrom_rev, APR_EOL_STR));
  else
    SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                        "copy from %s%s", copyfrom_path,
                                        APR_EOL_STR));
 * revisions being diffed. COPYFROM_PATH and COPYFROM_REV indicate where the
 * diffed item was copied from.
                      svn_revnum_t copyfrom_rev,
                                           copyfrom_path, copyfrom_rev,
                                           repos_relpath2,
   to OUTSTREAM.   Of course, OUTSTREAM will probably be whatever was
   passed to svn_client_diff6(), which is probably stdout.
   ANCHOR is the local path where the diff editor is anchored. */
                   const char *diff_relpath,
                   const char *anchor,
                   svn_stream_t *outstream,
                   const char *ra_session_relpath,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *scratch_pool)
  const char *repos_relpath1 = NULL;
  const char *repos_relpath2 = NULL;
  const char *index_path = diff_relpath;
  const char *adjusted_path1 = orig_path1;
  const char *adjusted_path2 = orig_path2;
      SVN_ERR(make_repos_relpath(&repos_relpath1, diff_relpath, orig_path1,
                                 ra_session_relpath, wc_ctx, anchor,
                                 scratch_pool, scratch_pool));
      SVN_ERR(make_repos_relpath(&repos_relpath2, diff_relpath, orig_path2,
                                 ra_session_relpath, wc_ctx, anchor,
                                 scratch_pool, scratch_pool));
  SVN_ERR(adjust_paths_for_diff_labels(&index_path, &adjusted_path1,
                                       &adjusted_path2,
                                       relative_to_dir, anchor,
                                       scratch_pool, scratch_pool));
      label1 = diff_label(adjusted_path1, rev1, scratch_pool);
      label2 = diff_label(adjusted_path2, rev2, scratch_pool);
      SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, scratch_pool,
                                          "Index: %s" APR_EOL_STR
                                          SVN_DIFF__EQUAL_STRING APR_EOL_STR,
                                          index_path));
        SVN_ERR(print_git_diff_header(outstream, &label1, &label2,
                                      svn_diff_op_modified,
                                      repos_relpath1, repos_relpath2,
                                      rev1, rev2, NULL,
                                      SVN_INVALID_REVNUM,
                                      encoding, scratch_pool));

      /* --- label1
       * +++ label2 */
      SVN_ERR(svn_diff__unidiff_write_header(
        outstream, encoding, label1, label2, scratch_pool));
  SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, scratch_pool,
                                      _("%sProperty changes on: %s%s"),
                                      APR_EOL_STR,
                                      use_git_diff_format
                                            ? repos_relpath1
                                            : index_path,
                                      APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, scratch_pool,
                                      SVN_DIFF__UNDER_STRING APR_EOL_STR));
  SVN_ERR(svn_diff__display_prop_diffs(
            outstream, encoding, propchanges, original_props,
            TRUE /* pretty_print_mergeinfo */,
            -1 /* context_size */,
            cancel_func, cancel_baton, scratch_pool));
/* State provided by the diff drivers; used by the diff writer */
typedef struct diff_driver_info_t
{
  /* The anchor to prefix before wc paths */
  const char *anchor;

   /* Relative path of ra session from repos_root_url */
  const char *session_relpath;

  /* The original targets passed to the diff command.  We may need
     these to construct distinctive diff labels when comparing the
     same relative path in the same revision, under different anchors
     (for example, when comparing a trunk against a branch). */
  const char *orig_path_1;
  const char *orig_path_2;
} diff_driver_info_t;
/* Diff writer state */
typedef struct diff_writer_info_t
{
  svn_stream_t *outstream;
  svn_stream_t *errstream;
  /* Whether property differences are ignored. */
  svn_boolean_t ignore_properties;

  /* Whether to show only property changes. */
  svn_boolean_t properties_only;

  /* Whether addition of a file is summarized versus showing a full diff. */
  svn_boolean_t no_diff_added;
  /* Whether deletion of a file is summarized versus showing a full diff. */
  svn_boolean_t no_diff_deleted;
  /* Whether to ignore copyfrom information when showing adds */
  svn_boolean_t show_copies_as_adds;
  /* Empty files for creating diffs or NULL if not used yet */
  const char *empty_file;
  svn_wc_context_t *wc_ctx;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
  struct diff_driver_info_t ddi;
} diff_writer_info_t;
diff_props_changed(const char *diff_relpath,
                   svn_revnum_t rev1,
                   svn_revnum_t rev2,
                   svn_boolean_t show_diff_header,
                   diff_writer_info_t *dwi,

  /* If property differences are ignored, there's nothing to do. */
  if (dwi->ignore_properties)
    return SVN_NO_ERROR;
      /* We're using the revnums from the dwi since there's
      SVN_ERR(display_prop_diffs(props, original_props,
                                 diff_relpath,
                                 dwi->ddi.anchor,
                                 dwi->ddi.orig_path_1,
                                 dwi->ddi.orig_path_2,
                                 rev1,
                                 rev2,
                                 dwi->header_encoding,
                                 dwi->outstream,
                                 dwi->relative_to_dir,
                                 dwi->use_git_diff_format,
                                 dwi->ddi.session_relpath,
                                 dwi->cancel_func,
                                 dwi->cancel_baton,
                                 dwi->wc_ctx,
/* Show differences between TMPFILE1 and TMPFILE2. DIFF_RELPATH, REV1, and
   REV2 are used in the headers to indicate the file and revisions.  If either
   MIMETYPE1 or MIMETYPE2 indicate binary content, don't show a diff,
   but instead print a warning message.
   If FORCE_DIFF is TRUE, always write a diff, even for empty diffs.
   Set *WROTE_HEADER to TRUE if a diff header was written */
diff_content_changed(svn_boolean_t *wrote_header,
                     const char *diff_relpath,
                     svn_boolean_t force_diff,
                     svn_revnum_t copyfrom_rev,
                     diff_writer_info_t *dwi,
                     apr_pool_t *scratch_pool)
  const char *rel_to_dir = dwi->relative_to_dir;
  svn_stream_t *outstream = dwi->outstream;
  const char *index_path = diff_relpath;
  const char *path1 = dwi->ddi.orig_path_1;
  const char *path2 = dwi->ddi.orig_path_2;
  /* If only property differences are shown, there's nothing to do. */
  if (dwi->properties_only)
    return SVN_NO_ERROR;
  SVN_ERR(adjust_paths_for_diff_labels(&index_path, &path1, &path2,
                                       rel_to_dir, dwi->ddi.anchor,
                                       scratch_pool, scratch_pool));
  label1 = diff_label(path1, rev1, scratch_pool);
  label2 = diff_label(path2, rev2, scratch_pool);
  if (! dwi->force_binary && (mt1_binary || mt2_binary))
      SVN_ERR(svn_stream_printf_from_utf8(outstream,
               dwi->header_encoding, scratch_pool,
               "Index: %s" APR_EOL_STR
               SVN_DIFF__EQUAL_STRING APR_EOL_STR,
               index_path));
      if (dwi->use_git_diff_format)
          svn_stream_t *left_stream;
          svn_stream_t *right_stream;
          const char *repos_relpath1;
          const char *repos_relpath2;

          SVN_ERR(make_repos_relpath(&repos_relpath1, diff_relpath,
                                      dwi->ddi.orig_path_1,
                                      dwi->ddi.session_relpath,
                                      dwi->wc_ctx,
                                      dwi->ddi.anchor,
                                      scratch_pool, scratch_pool));
          SVN_ERR(make_repos_relpath(&repos_relpath2, diff_relpath,
                                      dwi->ddi.orig_path_2,
                                      dwi->ddi.session_relpath,
                                      dwi->wc_ctx,
                                      dwi->ddi.anchor,
                                      scratch_pool, scratch_pool));
          SVN_ERR(print_git_diff_header(outstream, &label1, &label2,
                                        operation,
                                        repos_relpath1, repos_relpath2,
                                        rev1, rev2,
                                        copyfrom_path,
                                        copyfrom_rev,
                                        dwi->header_encoding,
                                        scratch_pool));

          SVN_ERR(svn_stream_open_readonly(&left_stream, tmpfile1,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_stream_open_readonly(&right_stream, tmpfile2,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_diff_output_binary(outstream,
                                         left_stream, right_stream,
                                         dwi->cancel_func, dwi->cancel_baton,
                                         scratch_pool));
        }
      else
        {
          SVN_ERR(svn_stream_printf_from_utf8(outstream,
                   dwi->header_encoding, scratch_pool,
                   _("Cannot display: file marked as a binary type.%s"),
                   APR_EOL_STR));

          if (mt1_binary && !mt2_binary)
            SVN_ERR(svn_stream_printf_from_utf8(outstream,
                     dwi->header_encoding, scratch_pool,
                     "svn:mime-type = %s" APR_EOL_STR, mimetype1));
          else if (mt2_binary && !mt1_binary)
            SVN_ERR(svn_stream_printf_from_utf8(outstream,
                     dwi->header_encoding, scratch_pool,
                     "svn:mime-type = %s" APR_EOL_STR, mimetype2));
          else if (mt1_binary && mt2_binary)
            {
              if (strcmp(mimetype1, mimetype2) == 0)
                SVN_ERR(svn_stream_printf_from_utf8(outstream,
                         dwi->header_encoding, scratch_pool,
                         "svn:mime-type = %s" APR_EOL_STR,
                         mimetype1));
              else
                SVN_ERR(svn_stream_printf_from_utf8(outstream,
                         dwi->header_encoding, scratch_pool,
                         "svn:mime-type = (%s, %s)" APR_EOL_STR,
                         mimetype1, mimetype2));
            }
  if (dwi->diff_cmd)
      svn_stream_t *errstream = dwi->errstream;
      apr_file_t *outfile;
      apr_file_t *errfile;
      const char *outfilename;
      const char *errfilename;
      svn_stream_t *stream;
      int exitcode;

      SVN_ERR(svn_stream_printf_from_utf8(outstream,
               dwi->header_encoding, scratch_pool,
               "Index: %s" APR_EOL_STR
               SVN_DIFF__EQUAL_STRING APR_EOL_STR,
               index_path));
      /* We deal in streams, but svn_io_run_diff2() deals in file handles,
         so we may need to make temporary files and then copy the contents
         to our stream. */
      outfile = svn_stream__aprfile(outstream);
      if (outfile)
        outfilename = NULL;
      else
        SVN_ERR(svn_io_open_unique_file3(&outfile, &outfilename, NULL,
                                         svn_io_file_del_on_pool_cleanup,
                                         scratch_pool, scratch_pool));

      errfile = svn_stream__aprfile(errstream);
      if (errfile)
        errfilename = NULL;
      else
        SVN_ERR(svn_io_open_unique_file3(&errfile, &errfilename, NULL,
                                         svn_io_file_del_on_pool_cleanup,
                                         scratch_pool, scratch_pool));

                               dwi->options.for_external.argv,
                               dwi->options.for_external.argc,
                               &exitcode, outfile, errfile,
                               dwi->diff_cmd, scratch_pool));

      /* Now, open and copy our files to our output streams. */
      if (outfilename)
        {
          SVN_ERR(svn_io_file_close(outfile, scratch_pool));
          SVN_ERR(svn_stream_open_readonly(&stream, outfilename,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_stream_copy3(stream, svn_stream_disown(outstream,
                                                             scratch_pool),
                                   NULL, NULL, scratch_pool));
        }
      if (errfilename)
        {
          SVN_ERR(svn_io_file_close(errfile, scratch_pool));
          SVN_ERR(svn_stream_open_readonly(&stream, errfilename,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_stream_copy3(stream, svn_stream_disown(errstream,
                                                             scratch_pool),
                                   NULL, NULL, scratch_pool));
        }
      /* If we have printed a diff for this path, mark it as visited. */
      if (exitcode == 1)
        *wrote_header = TRUE;
                                   dwi->options.for_internal,
                                   scratch_pool));
      if (force_diff
          || dwi->use_git_diff_format
          || svn_diff_contains_diffs(diff))
          SVN_ERR(svn_stream_printf_from_utf8(outstream,
                   dwi->header_encoding, scratch_pool,
                   "Index: %s" APR_EOL_STR
                   SVN_DIFF__EQUAL_STRING APR_EOL_STR,
                   index_path));
          if (dwi->use_git_diff_format)
              const char *repos_relpath1;
              const char *repos_relpath2;
              SVN_ERR(make_repos_relpath(&repos_relpath1, diff_relpath,
                                         dwi->ddi.orig_path_1,
                                         dwi->ddi.session_relpath,
                                         dwi->wc_ctx,
                                         dwi->ddi.anchor,
                                         scratch_pool, scratch_pool));
              SVN_ERR(make_repos_relpath(&repos_relpath2, diff_relpath,
                                         dwi->ddi.orig_path_2,
                                         dwi->ddi.session_relpath,
                                         dwi->wc_ctx,
                                         dwi->ddi.anchor,
                                         scratch_pool, scratch_pool));
              SVN_ERR(print_git_diff_header(outstream, &label1, &label2,
                                            operation,
                                            repos_relpath1, repos_relpath2,
                                            rev1, rev2,
                                            copyfrom_rev,
                                            dwi->header_encoding,
                                            scratch_pool));
          if (force_diff || svn_diff_contains_diffs(diff))
            SVN_ERR(svn_diff_file_output_unified4(outstream, diff,
                     tmpfile1, tmpfile2, label1, label2,
                     dwi->header_encoding, rel_to_dir,
                     dwi->options.for_internal->show_c_function,
                     dwi->options.for_internal->context_size,
                     dwi->cancel_func, dwi->cancel_baton,
                     scratch_pool));

          /* If we have printed a diff for this path, mark it as visited. */
          if (dwi->use_git_diff_format || svn_diff_contains_diffs(diff))
            *wrote_header = TRUE;
/* An svn_diff_tree_processor_t callback. */
diff_file_changed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  const char *left_file,
                  const char *right_file,
                  /*const*/ apr_hash_t *left_props,
                  /*const*/ apr_hash_t *right_props,
                  svn_boolean_t file_modified,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
  diff_writer_info_t *dwi = processor->baton;
  svn_boolean_t wrote_header = FALSE;

  if (file_modified)
    SVN_ERR(diff_content_changed(&wrote_header, relpath,
                                 left_file, right_file,
                                 left_source->revision,
                                 right_source->revision,
                                 svn_prop_get_value(left_props,
                                                    SVN_PROP_MIME_TYPE),
                                 svn_prop_get_value(right_props,
                                                    SVN_PROP_MIME_TYPE),
                                 svn_diff_op_modified, FALSE,
                                 NULL,
                                 SVN_INVALID_REVNUM, dwi,
                                 scratch_pool));
    SVN_ERR(diff_props_changed(relpath,
                               left_source->revision,
                               right_source->revision, prop_changes,
                               left_props, !wrote_header,
                               dwi, scratch_pool));
/* An svn_diff_tree_processor_t callback. */
diff_file_added(const char *relpath,
                const svn_diff_source_t *copyfrom_source,
                const svn_diff_source_t *right_source,
                const char *copyfrom_file,
                const char *right_file,
                /*const*/ apr_hash_t *copyfrom_props,
                /*const*/ apr_hash_t *right_props,
                void *file_baton,
                const struct svn_diff_tree_processor_t *processor,
  diff_writer_info_t *dwi = processor->baton;
  svn_boolean_t wrote_header = FALSE;
  const char *left_file;
  apr_hash_t *left_props;
  apr_array_header_t *prop_changes;

  /* During repos->wc diff of a copy revision numbers obtained
   * from the working copy are always SVN_INVALID_REVNUM. */
  if (copyfrom_source && !dwi->show_copies_as_adds)
    {
      left_file = copyfrom_file;
      left_props = copyfrom_props ? copyfrom_props : apr_hash_make(scratch_pool);
    }
  else
    {
      if (!dwi->empty_file)
        SVN_ERR(svn_io_open_unique_file3(NULL, &dwi->empty_file,
                                         NULL, svn_io_file_del_on_pool_cleanup,
                                         dwi->pool, scratch_pool));

      left_file = dwi->empty_file;
      left_props = apr_hash_make(scratch_pool);

      copyfrom_source = NULL;
      copyfrom_file = NULL;
    }

  SVN_ERR(svn_prop_diffs(&prop_changes, right_props, left_props, scratch_pool));

  if (dwi->no_diff_added)
    {
      const char *index_path = relpath;

      if (dwi->ddi.anchor)
        index_path = svn_dirent_join(dwi->ddi.anchor, relpath,
                                     scratch_pool);

      SVN_ERR(svn_stream_printf_from_utf8(dwi->outstream,
                dwi->header_encoding, scratch_pool,
                "Index: %s (added)" APR_EOL_STR
                SVN_DIFF__EQUAL_STRING APR_EOL_STR,
                index_path));
      wrote_header = TRUE;
    }
  else if (copyfrom_source && right_file)
    SVN_ERR(diff_content_changed(&wrote_header, relpath,
                                 left_file, right_file,
                                 copyfrom_source->revision,
                                 right_source->revision,
                                 svn_prop_get_value(left_props,
                                                    SVN_PROP_MIME_TYPE),
                                 svn_prop_get_value(right_props,
                                                    SVN_PROP_MIME_TYPE),
                                 svn_diff_op_copied,
                                 TRUE /* force diff output */,
                                 copyfrom_source->repos_relpath,
                                 copyfrom_source->revision,
                                 dwi, scratch_pool));
  else if (right_file)
    SVN_ERR(diff_content_changed(&wrote_header, relpath,
                                 left_file, right_file,
                                 DIFF_REVNUM_NONEXISTENT,
                                 right_source->revision,
                                 svn_prop_get_value(left_props,
                                                    SVN_PROP_MIME_TYPE),
                                 svn_prop_get_value(right_props,
                                                    SVN_PROP_MIME_TYPE),
                                 svn_diff_op_added,
                                 TRUE /* force diff output */,
                                 NULL, SVN_INVALID_REVNUM,
                                 dwi, scratch_pool));

    SVN_ERR(diff_props_changed(relpath,
                               copyfrom_source ? copyfrom_source->revision
                                               : DIFF_REVNUM_NONEXISTENT,
                               right_source->revision,
                               prop_changes,
                               left_props, ! wrote_header,
                               dwi, scratch_pool));
/* An svn_diff_tree_processor_t callback. */
diff_file_deleted(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const char *left_file,
                  /*const*/ apr_hash_t *left_props,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
  diff_writer_info_t *dwi = processor->baton;

  if (dwi->no_diff_deleted)
    {
      const char *index_path = relpath;
      if (dwi->ddi.anchor)
        index_path = svn_dirent_join(dwi->ddi.anchor, relpath,
                                     scratch_pool);
      SVN_ERR(svn_stream_printf_from_utf8(dwi->outstream,
                dwi->header_encoding, scratch_pool,
                "Index: %s (deleted)" APR_EOL_STR
                SVN_DIFF__EQUAL_STRING APR_EOL_STR,
                index_path));
    }
  else
    {
      svn_boolean_t wrote_header = FALSE;

      if (!dwi->empty_file)
        SVN_ERR(svn_io_open_unique_file3(NULL, &dwi->empty_file,
                                         NULL, svn_io_file_del_on_pool_cleanup,
                                         dwi->pool, scratch_pool));

      if (left_file)
        SVN_ERR(diff_content_changed(&wrote_header, relpath,
                                     left_file, dwi->empty_file,
                                     left_source->revision,
                                     DIFF_REVNUM_NONEXISTENT,
                                     svn_prop_get_value(left_props,
                                                        SVN_PROP_MIME_TYPE),
                                     NULL,
                                     svn_diff_op_deleted, FALSE,
                                     NULL, SVN_INVALID_REVNUM,
                                     dwi,
                                     scratch_pool));

      if (left_props && apr_hash_count(left_props))
        {
          apr_array_header_t *prop_changes;
          SVN_ERR(svn_prop_diffs(&prop_changes, apr_hash_make(scratch_pool),
                                 left_props, scratch_pool));
          SVN_ERR(diff_props_changed(relpath,
                                     left_source->revision,
                                     DIFF_REVNUM_NONEXISTENT,
                                     prop_changes,
                                     left_props, ! wrote_header,
                                     dwi, scratch_pool));
        }
    }
diff_dir_changed(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 /*const*/ apr_hash_t *left_props,
                 /*const*/ apr_hash_t *right_props,
                 const apr_array_header_t *prop_changes,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
  diff_writer_info_t *dwi = processor->baton;
  SVN_ERR(diff_props_changed(relpath,
                             left_source->revision,
                             right_source->revision,
                             prop_changes,
                             left_props,
                             TRUE /* show_diff_header */,
                             dwi,
                             scratch_pool));
  return SVN_NO_ERROR;
/* An svn_diff_tree_processor_t callback. */
diff_dir_added(const char *relpath,
               const svn_diff_source_t *copyfrom_source,
               const svn_diff_source_t *right_source,
               /*const*/ apr_hash_t *copyfrom_props,
               /*const*/ apr_hash_t *right_props,
               void *dir_baton,
               const struct svn_diff_tree_processor_t *processor,
  diff_writer_info_t *dwi = processor->baton;
  apr_hash_t *left_props;
  apr_array_header_t *prop_changes;
  if (dwi->no_diff_added)
    return SVN_NO_ERROR;
  if (copyfrom_source && !dwi->show_copies_as_adds)
    {
      left_props = copyfrom_props ? copyfrom_props
                                  : apr_hash_make(scratch_pool);
    }
  else
    {
      left_props = apr_hash_make(scratch_pool);
      copyfrom_source = NULL;
    }

  SVN_ERR(svn_prop_diffs(&prop_changes, right_props, left_props,
                         scratch_pool));

  return svn_error_trace(diff_props_changed(relpath,
                                            copyfrom_source ? copyfrom_source->revision
                                                            : DIFF_REVNUM_NONEXISTENT,
                                            right_source->revision,
                                            prop_changes,
                                            left_props,
                                            TRUE /* show_diff_header */,
                                            dwi,
                                            scratch_pool));
/* An svn_diff_tree_processor_t callback. */
diff_dir_deleted(const char *relpath,
                 const svn_diff_source_t *left_source,
                 /*const*/ apr_hash_t *left_props,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
  diff_writer_info_t *dwi = processor->baton;
  apr_array_header_t *prop_changes;
  if (dwi->no_diff_deleted)
    return SVN_NO_ERROR;
  SVN_ERR(svn_prop_diffs(&prop_changes, apr_hash_make(scratch_pool),
                         left_props, scratch_pool));
  SVN_ERR(diff_props_changed(relpath,
                             left_source->revision,
                             DIFF_REVNUM_NONEXISTENT,
                             prop_changes,
                             left_props,
                             TRUE /* show_diff_header */,
                             dwi,
                             scratch_pool));
      1. path is not a URL and start_revision != end_revision
      2. path is not a URL and start_revision == end_revision
      3. path is a URL and start_revision != end_revision
      4. path is a URL and start_revision == end_revision
      5. path is not a URL and no revisions given
   other.  When path is a URL there is no working copy. Thus
/** Check if paths PATH_OR_URL1 and PATH_OR_URL2 are urls and if the
 * revisions REVISION1 and REVISION2 are local. If PEG_REVISION is not
 * unspecified, ensure that at least one of the two revisions is not
 * BASE or WORKING.
 * If PATH_OR_URL1 can only be found in the repository, set *IS_REPOS1
 * to TRUE. If PATH_OR_URL2 can only be found in the repository, set
 * *IS_REPOS2 to TRUE. */
            const char *path_or_url1,
            const char *path_or_url2,
  /* Revisions can be said to be local or remote.
   * BASE and WORKING are local revisions.  */
  if (peg_revision->kind != svn_opt_revision_unspecified &&
      is_local_rev1 && is_local_rev2)
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("At least one revision must be something other "
                              "than BASE or WORKING when diffing a URL"));
  /* Working copy paths with non-local revisions get turned into
     URLs.  We don't do that here, though.  We simply record that it
     needs to be done, which is information that helps us choose our
     diff helper function.  */
  *is_repos1 = ! is_local_rev1 || svn_path_is_url(path_or_url1);
  *is_repos2 = ! is_local_rev2 || svn_path_is_url(path_or_url2);
/** Prepare a repos repos diff between PATH_OR_URL1 and
 * PATH_OR_URL2@PEG_REVISION, in the revision range REVISION1:REVISION2.
                         const char *path_or_url1,
                         const char *path_or_url2,
  const char *local_abspath1 = NULL;
  const char *local_abspath2 = NULL;
  const char *repos_root_url;
  const char *wri_abspath = NULL;
  svn_client__pathrev_t *resolved1;
  svn_client__pathrev_t *resolved2 = NULL;
  enum svn_opt_revision_kind peg_kind = peg_revision->kind;

  if (!svn_path_is_url(path_or_url2))
    {
      SVN_ERR(svn_dirent_get_absolute(&local_abspath2, path_or_url2, pool));
      SVN_ERR(svn_wc__node_get_url(url2, ctx->wc_ctx, local_abspath2,
                                   pool, pool));
      wri_abspath = local_abspath2;
    }
    *url2 = apr_pstrdup(pool, path_or_url2);
  if (!svn_path_is_url(path_or_url1))
    {
      SVN_ERR(svn_dirent_get_absolute(&local_abspath1, path_or_url1, pool));
      wri_abspath = local_abspath1;
    }

  SVN_ERR(svn_client_open_ra_session2(ra_session, *url2, wri_abspath,
                                      ctx, pool, pool));
  if (peg_kind != svn_opt_revision_unspecified
      || path_or_url1 == path_or_url2
      || local_abspath2)
      svn_error_t *err;

      err = svn_client__resolve_rev_and_url(&resolved2,
                                            *ra_session, path_or_url2,
                                            peg_revision, revision2,
                                            ctx, pool);
      if (err)
          if (err->apr_err != SVN_ERR_CLIENT_UNRELATED_RESOURCES
              && err->apr_err != SVN_ERR_FS_NOT_FOUND)
            return svn_error_trace(err);

          svn_error_clear(err);
          resolved2 = NULL;
    }
  else
    resolved2 = NULL;

  if (peg_kind != svn_opt_revision_unspecified
      || path_or_url1 == path_or_url2
      || local_abspath1)
    {
      svn_error_t *err;

      err = svn_client__resolve_rev_and_url(&resolved1,
                                            *ra_session, path_or_url1,
                                            peg_revision, revision1,
                                            ctx, pool);
      if (err)
          if (err->apr_err != SVN_ERR_CLIENT_UNRELATED_RESOURCES
              && err->apr_err != SVN_ERR_FS_NOT_FOUND)
            return svn_error_trace(err);

          svn_error_clear(err);
          resolved1 = NULL;
    }
  else
    resolved1 = NULL;

  if (resolved1)
    {
      *url1 = resolved1->url;
      *rev1 = resolved1->rev;
    }
  else
    {
      /* It would be nice if we could just return an error when resolving a
         location fails... But in many such cases we prefer diffing against
         an not existing location to show adds od removes (see issue #4153) */

      if (resolved2
          && (peg_kind != svn_opt_revision_unspecified
              || path_or_url1 == path_or_url2))
        *url1 = resolved2->url;
      else if (! local_abspath1)
        *url1 = path_or_url1;
      else
        SVN_ERR(svn_wc__node_get_url(url1, ctx->wc_ctx, local_abspath1,
                                     pool, pool));

      SVN_ERR(svn_client__get_revision_number(rev1, NULL, ctx->wc_ctx,
                                              local_abspath1 /* may be NULL */,
                                              *ra_session, revision1, pool));
    }
  if (resolved2)
    {
      *url2 = resolved2->url;
      *rev2 = resolved2->rev;
    }
  else
    {
      /* It would be nice if we could just return an error when resolving a
         location fails... But in many such cases we prefer diffing against
         an not existing location to show adds od removes (see issue #4153) */

      if (resolved1
          && (peg_kind != svn_opt_revision_unspecified
              || path_or_url1 == path_or_url2))
        *url2 = resolved1->url;
      /* else keep url2 */

      SVN_ERR(svn_client__get_revision_number(rev2, NULL, ctx->wc_ctx,
                                              local_abspath2 /* may be NULL */,
                                              *ra_session, revision2, pool));
  SVN_ERR(svn_ra_reparent(*ra_session, *url2, pool));
                                 _("Diff targets '%s' and '%s' were not found "
  SVN_ERR(svn_ra_get_repos_root2(*ra_session, &repos_root_url, pool));

  /* If none of the targets is the repository root open the parent directory
     to allow describing replacement of the target itself */
  if (strcmp(*url1, repos_root_url) != 0
      && strcmp(*url2, repos_root_url) != 0)
      svn_node_kind_t ignored_kind;
      svn_error_t *err;



      /* We might not have the necessary rights to read the root now.
         (It is ok to pass a revision here where the node doesn't exist) */
      err = svn_ra_check_path(*ra_session, "", *rev1, &ignored_kind, pool);

      if (err && (err->apr_err == SVN_ERR_RA_DAV_FORBIDDEN
                  || err->apr_err == SVN_ERR_RA_NOT_AUTHORIZED))
        {
          svn_error_clear(err);

          /* Ok, lets undo the reparent...

             We can't report replacements this way, but at least we can
             report changes on the descendants */

          *anchor1 = svn_path_url_add_component2(*anchor1, *target1, pool);
          *anchor2 = svn_path_url_add_component2(*anchor2, *target2, pool);
          *target1 = "";
          *target2 = "";

          SVN_ERR(svn_ra_reparent(*ra_session, *anchor1, pool));
        }
      else
        SVN_ERR(err);
   This function is really svn_client_diff6().  If you read the public
   API description for svn_client_diff6(), it sounds quite Grand.  It
   Since Subversion 1.8 we also have a variant of svn_wc_diff called
   svn_client__arbitrary_nodes_diff, that allows handling WORKING-WORKING
   comparisons between nodes in the working copy.

   pigeonholed into one of these use-cases, we currently bail with a
   friendly apology.
   Perhaps someday a brave soul will truly make svn_client_diff6()
                          _("Sorry, svn_client_diff6 was called in a way "
   All other options are the same as those passed to svn_client_diff6(). */
diff_wc_wc(const char **root_relpath,
           svn_boolean_t *root_is_dir,
           struct diff_driver_info_t *ddi,
           const char *path1,
           const svn_diff_tree_processor_t *diff_processor,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
  SVN_ERR(svn_dirent_get_absolute(&abspath1, path1, scratch_pool));
    return unsupported_diff_error(
       svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                        _("Only diffs between a path's text-base "
                          "and its working files are supported at this time"
                          )));
  if (ddi)
      svn_node_kind_t kind;
      SVN_ERR(svn_wc_read_kind2(&kind, ctx->wc_ctx, abspath1,
                              TRUE, FALSE, scratch_pool));
      if (kind != svn_node_dir)
        ddi->anchor = svn_dirent_dirname(path1, scratch_pool);
        ddi->anchor = path1;
  SVN_ERR(svn_wc__diff7(root_relpath, root_is_dir,
                        ctx->wc_ctx, abspath1, depth,
                        ignore_ancestry, changelists,
                        diff_processor,
                        ctx->cancel_func, ctx->cancel_baton,
                        result_pool, scratch_pool));
   PATH_OR_URL1 and PATH_OR_URL2 may be either URLs or the working copy paths.
   If PEG_REVISION is specified, PATH_OR_URL2 is the path at the peg revision,
   history from PATH_OR_URL2.
   All other options are the same as those passed to svn_client_diff6(). */
diff_repos_repos(const char **root_relpath,
                 svn_boolean_t *root_is_dir,
                 struct diff_driver_info_t *ddi,
                 const char *path_or_url1,
                 const char *path_or_url2,
                 svn_boolean_t text_deltas,
                 const svn_diff_tree_processor_t *diff_processor,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
  SVN_ERR(diff_prepare_repos_repos(&url1, &url2, &rev1, &rev2,
                                   ctx, path_or_url1, path_or_url2,
                                   scratch_pool));
  /* Set up the repos_diff editor on BASE_PATH, if available.
     Otherwise, we just use "". */
  if (ddi)
      /* Get actual URLs. */
      ddi->orig_path_1 = url1;
      ddi->orig_path_2 = url2;

      /* This should be moved to the diff writer
         - path_or_url are provided by the caller
         - target1 is available as *root_relpath
         - (kind1 != svn_node_dir || kind2 != svn_node_dir) = !*root_is_dir */

      if (!svn_path_is_url(path_or_url2))
        ddi->anchor = path_or_url2;
      else if (!svn_path_is_url(path_or_url1))
        ddi->anchor = path_or_url1;
      else
        ddi->anchor = NULL;

      if (*target1 && ddi->anchor
          && (kind1 != svn_node_dir || kind2 != svn_node_dir))
        ddi->anchor = svn_dirent_dirname(ddi->anchor, result_pool);
  /* The repository can bring in a new working copy, but not delete
     everything. Luckily our new diff handler can just be reversed. */
  if (kind2 == svn_node_none)
    {
      const char *str_tmp;
      svn_revnum_t rev_tmp;
      str_tmp = url2;
      url2 = url1;
      url1 = str_tmp;
      rev_tmp = rev2;
      rev2 = rev1;
      rev1 = rev_tmp;
      str_tmp = anchor2;
      anchor2 = anchor1;
      anchor1 = str_tmp;
      str_tmp = target2;
      target2 = target1;
      target1 = str_tmp;
      diff_processor = svn_diff__tree_processor_reverse_create(diff_processor,
                                                               NULL,
                                                               scratch_pool);
  /* Filter the first path component using a filter processor, until we fixed
     the diff processing to handle this directly */
  if (root_relpath)
    *root_relpath = apr_pstrdup(result_pool, target1);
  else if ((kind1 != svn_node_file && kind2 != svn_node_file)
           && target1[0] != '\0')
      diff_processor = svn_diff__tree_processor_filter_create(
                                        diff_processor, target1, scratch_pool);
    }
  /* Now, we open an extra RA session to the correct anchor
     location for URL1.  This is used during the editor calls to fetch file
     contents.  */
  SVN_ERR(svn_ra_dup_session(&extra_ra_session, ra_session, anchor1,
                             scratch_pool, scratch_pool));
  if (ddi)
      const char *repos_root_url;
      const char *session_url;
      SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url,
                                      scratch_pool));
      SVN_ERR(svn_ra_get_session_url(ra_session, &session_url,
                                      scratch_pool));
      ddi->session_relpath = svn_uri_skip_ancestor(repos_root_url,
                                                    session_url,
                                                    result_pool);
  SVN_ERR(svn_client__get_diff_editor2(
                &diff_editor, &diff_edit_baton,
                extra_ra_session, depth,
                rev1,
                text_deltas,
                diff_processor,
                ctx->cancel_func, ctx->cancel_baton,
                scratch_pool));
  /* We want to switch our txn into URL2 */
  SVN_ERR(svn_ra_do_diff3(ra_session, &reporter, &reporter_baton,
                          rev2, target1,
                          depth, ignore_ancestry, text_deltas,
                          url2, diff_editor, diff_edit_baton, scratch_pool));
  /* Drive the reporter; do the diff. */
  SVN_ERR(reporter->set_path(reporter_baton, "", rev1,
                             svn_depth_infinity,
                             FALSE, NULL,
                             scratch_pool));
  return svn_error_trace(
                  reporter->finish_report(reporter_baton, scratch_pool));
   PATH_OR_URL1 may be either a URL or a working copy path.  PATH2 is a
   If PEG_REVISION is specified, then PATH_OR_URL1 is the path in the peg
   All other options are the same as those passed to svn_client_diff6(). */
diff_repos_wc(const char **root_relpath,
              svn_boolean_t *root_is_dir,
              struct diff_driver_info_t *ddi,
              const char *path_or_url1,
              const svn_diff_tree_processor_t *diff_processor,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
  const char *anchor, *anchor_url, *target;
  const char *abspath_or_url1;
  svn_boolean_t is_copy;
  svn_revnum_t cf_revision;
  const char *cf_repos_relpath;
  const char *cf_repos_root_url;
  svn_depth_t cf_depth;
  const char *copy_root_abspath;
  const char *target_url;
  svn_client__pathrev_t *loc1;
  if (!svn_path_is_url(path_or_url1))
    {
      SVN_ERR(svn_dirent_get_absolute(&abspath_or_url1, path_or_url1,
                                      scratch_pool));
    }
    {
      abspath_or_url1 = path_or_url1;
    }
  SVN_ERR(svn_dirent_get_absolute(&abspath2, path2, scratch_pool));
  /* Check if our diff target is a copied node. */
  SVN_ERR(svn_wc__node_get_origin(&is_copy,
                                  &cf_revision,
                                  &cf_repos_relpath,
                                  &cf_repos_root_url,
                                  NULL, &cf_depth,
                                  &copy_root_abspath,
                                  ctx->wc_ctx, abspath2,
                                  FALSE, scratch_pool, scratch_pool));
  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &loc1,
                                            path_or_url1, abspath2,
                                            peg_revision, revision1,
                                            ctx, scratch_pool));
  if (revision2->kind == svn_opt_revision_base || !is_copy)
    {
      /* Convert path_or_url1 to a URL to feed to do_diff. */
      SVN_ERR(svn_wc_get_actual_target2(&anchor, &target, ctx->wc_ctx, path2,
                                        scratch_pool, scratch_pool));
      /* Handle the ugly case where target is ".." */
      if (*target && !svn_path_is_single_path_component(target))
        {
          anchor = svn_dirent_join(anchor, target, scratch_pool);
          target = "";
        }

      if (root_relpath)
        *root_relpath = apr_pstrdup(result_pool, target);
      if (root_is_dir)
        *root_is_dir = (*target == '\0');

      /* Fetch the URL of the anchor directory. */
      SVN_ERR(svn_dirent_get_absolute(&anchor_abspath, anchor, scratch_pool));
      SVN_ERR(svn_wc__node_get_url(&anchor_url, ctx->wc_ctx, anchor_abspath,
                                   scratch_pool, scratch_pool));
      SVN_ERR_ASSERT(anchor_url != NULL);

      target_url = NULL;
    }
  else /* is_copy && revision2->kind == svn_opt_revision_base */
#if 0
      svn_node_kind_t kind;
#endif
      /* ### Ugly hack ahead ###
       *
       * We're diffing a locally copied/moved node.
       * Describe the copy source to the reporter instead of the copy itself.
       * Doing the latter would generate a single add_directory() call to the
       * diff editor which results in an unexpected diff (the copy would
       * be shown as deleted).
       *
       * ### But if we will receive any real changes from the repositor we
       * will most likely fail to apply them as the wc diff editor assumes
       * that we have the data to which the change applies in BASE...
       */

      target_url = svn_path_url_add_component2(cf_repos_root_url,
                                               cf_repos_relpath,
                                               scratch_pool);

#if 0
      /*SVN_ERR(svn_wc_read_kind2(&kind, ctx->wc_ctx, abspath2, FALSE, FALSE,
                                scratch_pool));

      if (kind != svn_node_dir
          || strcmp(copy_root_abspath, abspath2) != 0) */
#endif
          /* We are looking at a subdirectory of the repository,
             We can describe the parent directory as the anchor..

             ### This 'appears to work', but that is really dumb luck
             ### for the simple cases in the test suite */
          anchor_abspath = svn_dirent_dirname(abspath2, scratch_pool);
          anchor_url = svn_path_url_add_component2(cf_repos_root_url,
                                                   svn_relpath_dirname(
                                                            cf_repos_relpath,
                                                            scratch_pool),
                                                   scratch_pool);
          target = svn_dirent_basename(abspath2, NULL);
          anchor = svn_dirent_dirname(path2, scratch_pool);
#if 0
          /* This code, while ok can't be enabled without causing test
           * failures. The repository will send some changes against
           * BASE for nodes that don't have BASE...
           */
          anchor_abspath = abspath2;
          anchor_url = svn_path_url_add_component2(cf_repos_root_url,
                                                   cf_repos_relpath,
                                                   scratch_pool);
          anchor = path2;
          target = "";
#endif
  SVN_ERR(svn_ra_reparent(ra_session, anchor_url, scratch_pool));

  if (ddi)
      const char *repos_root_url;
      ddi->anchor = anchor;
      if (!reverse)
        {
          ddi->orig_path_1 = apr_pstrdup(result_pool, loc1->url);
          ddi->orig_path_2 =
            svn_path_url_add_component2(anchor_url, target, result_pool);
        }
      else
        {
          ddi->orig_path_1 =
            svn_path_url_add_component2(anchor_url, target, result_pool);
          ddi->orig_path_2 = apr_pstrdup(result_pool, loc1->url);
        }
      SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url,
                                      scratch_pool));

      ddi->session_relpath = svn_uri_skip_ancestor(repos_root_url,
                                                   anchor_url,
                                                   result_pool);
  if (reverse)
    diff_processor = svn_diff__tree_processor_reverse_create(
                              diff_processor, NULL, scratch_pool);

  /* Use the diff editor to generate the diff. */
                                SVN_RA_CAPABILITY_DEPTH, scratch_pool));
  SVN_ERR(svn_wc__get_diff_editor(&diff_editor, &diff_edit_baton,
                                  diff_processor,
                                  scratch_pool, scratch_pool));


  if (is_copy && revision2->kind != svn_opt_revision_base)
    {
      /* Tell the RA layer we want a delta to change our txn to URL1 */
      SVN_ERR(svn_ra_do_diff3(ra_session,
                              &reporter, &reporter_baton,
                              loc1->rev,
                              target,
                              diff_depth,
                              ignore_ancestry,
                              TRUE,  /* text_deltas */
                              loc1->url,
                              diff_editor, diff_edit_baton,
                              scratch_pool));

      /* Report the copy source. */
      if (cf_depth == svn_depth_unknown)
        cf_depth = svn_depth_infinity;

      /* Reporting the in-wc revision as r0, makes the repository send
         everything as added, which avoids using BASE for pristine information,
         which is not there (or unrelated) for a copy */

      SVN_ERR(reporter->set_path(reporter_baton, "",
                                 ignore_ancestry ? 0 : cf_revision,
                                 cf_depth, FALSE, NULL, scratch_pool));

      if (*target)
        SVN_ERR(reporter->link_path(reporter_baton, target,
                                    target_url,
                                    ignore_ancestry ? 0 : cf_revision,
                                    cf_depth, FALSE, NULL, scratch_pool));

      /* Finish the report to generate the diff. */
      SVN_ERR(reporter->finish_report(reporter_baton, scratch_pool));
    }
  else
    {
      /* Tell the RA layer we want a delta to change our txn to URL1 */
      SVN_ERR(svn_ra_do_diff3(ra_session,
                              &reporter, &reporter_baton,
                              loc1->rev,
                              target,
                              diff_depth,
                              ignore_ancestry,
                              TRUE,  /* text_deltas */
                              loc1->url,
                              diff_editor, diff_edit_baton,
                              scratch_pool));

      /* Create a txn mirror of path2;  the diff editor will print
         diffs in reverse.  :-)  */
      SVN_ERR(svn_wc_crawl_revisions5(ctx->wc_ctx, abspath2,
                                      reporter, reporter_baton,
                                      FALSE, depth, TRUE,
                                      (! server_supports_depth),
                                      FALSE,
                                      ctx->cancel_func, ctx->cancel_baton,
                                      NULL, NULL, /* notification is N/A */
                                      scratch_pool));
    }
/* This is basically just the guts of svn_client_diff[_summarize][_peg]6(). */
do_diff(const char **root_relpath,
        svn_boolean_t *root_is_dir,
        diff_driver_info_t *ddi,
        const char *path_or_url1,
        const char *path_or_url2,
        svn_boolean_t text_deltas,
        const svn_diff_tree_processor_t *diff_processor,
        svn_client_ctx_t *ctx,
        apr_pool_t *result_pool,
        apr_pool_t *scratch_pool)
  SVN_ERR(check_paths(&is_repos1, &is_repos2, path_or_url1, path_or_url2,
          /* ### Ignores 'show_copies_as_adds'. */
          SVN_ERR(diff_repos_repos(root_relpath, root_is_dir,
                                   ddi,
                                   path_or_url1, path_or_url2,
                                   revision1, revision2,
                                   text_deltas,
                                   diff_processor, ctx,
                                   result_pool, scratch_pool));
      else /* path_or_url2 is a working copy path */
          SVN_ERR(diff_repos_wc(root_relpath, root_is_dir, ddi,
                                path_or_url1, revision1, peg_revision,
                                path_or_url2, revision2, FALSE, depth,
                                ignore_ancestry, changelists,
                                diff_processor, ctx,
                                result_pool, scratch_pool));
  else /* path_or_url1 is a working copy path */
          SVN_ERR(diff_repos_wc(root_relpath, root_is_dir, ddi,
                                path_or_url2, revision2, peg_revision,
                                path_or_url1, revision1, TRUE, depth,
                                ignore_ancestry, changelists,
                                diff_processor, ctx,
                                result_pool, scratch_pool));
      else /* path_or_url2 is a working copy path */
          if (revision1->kind == svn_opt_revision_working
              && revision2->kind == svn_opt_revision_working)
            {
              const char *abspath1;
              const char *abspath2;

              SVN_ERR(svn_dirent_get_absolute(&abspath1, path_or_url1,
                                              scratch_pool));
              SVN_ERR(svn_dirent_get_absolute(&abspath2, path_or_url2, 
                                              scratch_pool));

              /* ### What about ddi? */

              SVN_ERR(svn_client__arbitrary_nodes_diff(root_relpath, root_is_dir,
                                                       abspath1, abspath2,
                                                       depth,
                                                       diff_processor,
                                                       ctx,
                                                       result_pool, scratch_pool));
            }
          else
            {
              SVN_ERR(diff_wc_wc(root_relpath, root_is_dir, ddi,
                                 path_or_url1, revision1,
                                 path_or_url2, revision2,
                                 depth, ignore_ancestry, changelists,
                                 diff_processor, ctx,
                                 result_pool, scratch_pool));
            }
/* Initialize DWI.diff_cmd and DWI.options,
 * Allocate the fields in RESULT_POOL, which should be at least as long-lived
 * as the pool DWI itself is allocated in.
create_diff_writer_info(diff_writer_info_t *dwi,
                        const apr_array_header_t *options,
                        apr_hash_t *config, apr_pool_t *result_pool)
      svn_config_t *cfg = svn_hash_gets(config, SVN_CONFIG_CATEGORY_CONFIG);
            options = svn_cstring_split(diff_extensions, " \t\n\r", TRUE,
                                        result_pool);
    options = apr_array_make(result_pool, 0, sizeof(const char *));
    SVN_ERR(svn_path_cstring_to_utf8(&dwi->diff_cmd, diff_cmd,
                                     result_pool));
    dwi->diff_cmd = NULL;
  if (dwi->diff_cmd)
          argv = apr_palloc(result_pool, argc * sizeof(char *));
                      APR_ARRAY_IDX(options, i, const char *), result_pool));
      dwi->options.for_external.argv = argv;
      dwi->options.for_external.argc = argc;
      dwi->options.for_internal = svn_diff_file_options_create(result_pool);
      SVN_ERR(svn_diff_file_options_parse(dwi->options.for_internal,
                                          options, result_pool));
svn_client_diff6(const apr_array_header_t *options,
                 const char *path_or_url1,
                 const char *path_or_url2,
                 svn_boolean_t no_diff_added,
                 svn_boolean_t ignore_properties,
                 svn_boolean_t properties_only,
                 svn_stream_t *outstream,
                 svn_stream_t *errstream,
  diff_writer_info_t dwi = { 0 };
  svn_opt_revision_t peg_revision;
  const svn_diff_tree_processor_t *diff_processor;
  svn_diff_tree_processor_t *processor;

  if (ignore_properties && properties_only)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Cannot ignore properties and show only "
                              "properties at the same time"));
  dwi.ddi.orig_path_1 = path_or_url1;
  dwi.ddi.orig_path_2 = path_or_url2;

  SVN_ERR(create_diff_writer_info(&dwi, options,
                                  ctx->config, pool));
  dwi.pool = pool;
  dwi.outstream = outstream;
  dwi.errstream = errstream;
  dwi.header_encoding = header_encoding;

  dwi.force_binary = ignore_content_type;
  dwi.ignore_properties = ignore_properties;
  dwi.properties_only = properties_only;
  dwi.relative_to_dir = relative_to_dir;
  dwi.use_git_diff_format = use_git_diff_format;
  dwi.no_diff_added = no_diff_added;
  dwi.no_diff_deleted = no_diff_deleted;
  dwi.show_copies_as_adds = show_copies_as_adds;

  dwi.cancel_func = ctx->cancel_func;
  dwi.cancel_baton = ctx->cancel_baton;

  dwi.wc_ctx = ctx->wc_ctx;
  dwi.ddi.session_relpath = NULL;
  dwi.ddi.anchor = NULL;

  processor = svn_diff__tree_processor_create(&dwi, pool);

  processor->dir_added = diff_dir_added;
  processor->dir_changed = diff_dir_changed;
  processor->dir_deleted = diff_dir_deleted;

  processor->file_added = diff_file_added;
  processor->file_changed = diff_file_changed;
  processor->file_deleted = diff_file_deleted;

  diff_processor = processor;

  /* --show-copies-as-adds and --git imply --notice-ancestry */
  if (show_copies_as_adds || use_git_diff_format)
    ignore_ancestry = FALSE;

  return svn_error_trace(do_diff(NULL, NULL, &dwi.ddi,
                                 path_or_url1, path_or_url2,
                                 revision1, revision2, &peg_revision,
                                 depth, ignore_ancestry, changelists,
                                 TRUE /* text_deltas */,
                                 diff_processor, ctx, pool, pool));
svn_client_diff_peg6(const apr_array_header_t *options,
                     const char *path_or_url,
                     svn_boolean_t no_diff_added,
                     svn_boolean_t ignore_properties,
                     svn_boolean_t properties_only,
                     svn_stream_t *outstream,
                     svn_stream_t *errstream,
  diff_writer_info_t dwi = { 0 };
  const svn_diff_tree_processor_t *diff_processor;
  svn_diff_tree_processor_t *processor;

  if (ignore_properties && properties_only)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Cannot ignore properties and show only "
                              "properties at the same time"));
  dwi.ddi.orig_path_1 = path_or_url;
  dwi.ddi.orig_path_2 = path_or_url;

  SVN_ERR(create_diff_writer_info(&dwi, options,
                                  ctx->config, pool));
  dwi.pool = pool;
  dwi.outstream = outstream;
  dwi.errstream = errstream;
  dwi.header_encoding = header_encoding;

  dwi.force_binary = ignore_content_type;
  dwi.ignore_properties = ignore_properties;
  dwi.properties_only = properties_only;
  dwi.relative_to_dir = relative_to_dir;
  dwi.use_git_diff_format = use_git_diff_format;
  dwi.no_diff_added = no_diff_added;
  dwi.no_diff_deleted = no_diff_deleted;
  dwi.show_copies_as_adds = show_copies_as_adds;

  dwi.cancel_func = ctx->cancel_func;
  dwi.cancel_baton = ctx->cancel_baton;

  dwi.wc_ctx = ctx->wc_ctx;
  dwi.ddi.session_relpath = NULL;
  dwi.ddi.anchor = NULL;

  processor = svn_diff__tree_processor_create(&dwi, pool);

  processor->dir_added = diff_dir_added;
  processor->dir_changed = diff_dir_changed;
  processor->dir_deleted = diff_dir_deleted;

  processor->file_added = diff_file_added;
  processor->file_changed = diff_file_changed;
  processor->file_deleted = diff_file_deleted;

  diff_processor = processor;

  /* --show-copies-as-adds and --git imply --notice-ancestry */
  if (show_copies_as_adds || use_git_diff_format)
    ignore_ancestry = FALSE;

  return svn_error_trace(do_diff(NULL, NULL, &dwi.ddi,
                                 path_or_url, path_or_url,
                                 start_revision, end_revision, peg_revision,
                                 depth, ignore_ancestry, changelists,
                                 TRUE /* text_deltas */,
                                 diff_processor, ctx, pool, pool));
svn_client_diff_summarize2(const char *path_or_url1,
                           const char *path_or_url2,
  const svn_diff_tree_processor_t *diff_processor;
  const char **p_root_relpath;

  /* We will never do a pegged diff from here. */
  SVN_ERR(svn_client__get_diff_summarize_callbacks(
                     &diff_processor, &p_root_relpath,
                     summarize_func, summarize_baton,
                     path_or_url1, pool, pool));

  return svn_error_trace(do_diff(p_root_relpath, NULL, NULL,
                                 path_or_url1, path_or_url2,
                                 revision1, revision2, &peg_revision,
                                 depth, ignore_ancestry, changelists,
                                 FALSE /* text_deltas */,
                                 diff_processor, ctx, pool, pool));
svn_client_diff_summarize_peg2(const char *path_or_url,
  const svn_diff_tree_processor_t *diff_processor;
  const char **p_root_relpath;

  SVN_ERR(svn_client__get_diff_summarize_callbacks(
                     &diff_processor, &p_root_relpath,
                     summarize_func, summarize_baton,
                     path_or_url, pool, pool));

  return svn_error_trace(do_diff(p_root_relpath, NULL, NULL,
                                 path_or_url, path_or_url,
                                 start_revision, end_revision, peg_revision,
                                 depth, ignore_ancestry, changelists,
                                 FALSE /* text_deltas */,
                                 diff_processor, ctx, pool, pool));