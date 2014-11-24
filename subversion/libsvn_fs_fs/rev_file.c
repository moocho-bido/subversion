/* rev_file.c --- revision file and index access functions
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include "rev_file.h"
#include "fs_fs.h"
#include "low_level.h"
#include "util.h"

#include "../libsvn_fs/fs-loader.h"

#include "private/svn_io_private.h"
#include "svn_private_config.h"

/* Initialize the *FILE structure for REVISION in filesystem FS.  Set its
 * pool member to the provided POOL. */
static void
init_revision_file(svn_fs_fs__revision_file_t *file,
                   svn_fs_t *fs,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  file->is_packed = svn_fs_fs__is_packed_rev(fs, revision);
  file->start_revision = svn_fs_fs__packed_base_rev(fs, revision);

  file->file = NULL;
  file->stream = NULL;
  file->pool = pool;
}

/* Baton type for set_read_only() */
typedef struct set_read_only_baton_t
{
  /* File to set to read-only. */
  const char *file_path;

  /* Scratch pool sufficient life time.
   * Ideally the pool that we registered the cleanup on. */
  apr_pool_t *pool;
} set_read_only_baton_t;

/* APR pool cleanup callback taking a set_read_only_baton_t baton and then
 * (trying to) set the specified file to r/o mode. */
static apr_status_t
set_read_only(void *baton)
{
  set_read_only_baton_t *ro_baton = baton;
  apr_status_t status = APR_SUCCESS;
  svn_error_t *err;

  err = svn_io_set_file_read_only(ro_baton->file_path, TRUE, ro_baton->pool);
  if (err)
    {
      status = err->apr_err;
      svn_error_clear(err);
    }

  return status;
}

/* Core implementation of svn_fs_fs__open_pack_or_rev_file working on an
 * existing, initialized FILE structure.
 */
static svn_error_t *
open_pack_or_rev_file(svn_fs_fs__revision_file_t *file,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_error_t *err;
  svn_boolean_t retry = FALSE;

  do
    {
      const char *path = svn_fs_fs__path_rev_absolute(fs, rev, scratch_pool);
      apr_file_t *apr_file;

      /* open the revision file in buffered r/o mode */
      err = svn_io_file_open(&apr_file, path, APR_READ | APR_BUFFERED,
                             APR_OS_DEFAULT, result_pool);

      if (!err)
        {
          file->file = apr_file;
          file->stream = svn_stream_from_aprfile2(apr_file, TRUE,
                                                  result_pool);
          file->is_packed = svn_fs_fs__is_packed_rev(fs, rev);

          return SVN_NO_ERROR;
        }

      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        {
          if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
            {
              /* Could not open the file. This may happen if the
               * file once existed but got packed later. */
              svn_error_clear(err);

              /* if that was our 2nd attempt, leave it at that. */
              if (retry)
                return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                         _("No such revision %ld"), rev);

              /* We failed for the first time. Refresh cache & retry. */
              SVN_ERR(svn_fs_fs__update_min_unpacked_rev(fs, scratch_pool));
              file->start_revision = svn_fs_fs__packed_base_rev(fs, rev);

              retry = TRUE;
            }
          else
            {
              svn_error_clear(err);
              return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                       _("No such revision %ld"), rev);
            }
        }
      else
        {
          retry = FALSE;
        }
    }
  while (retry);

  return svn_error_trace(err);
}

svn_error_t *
svn_fs_fs__open_pack_or_rev_file(svn_fs_fs__revision_file_t **file,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  *file = apr_palloc(result_pool, sizeof(**file));
  init_revision_file(*file, fs, rev, result_pool);

  return svn_error_trace(open_pack_or_rev_file(*file, fs, rev, result_pool,
                                               scratch_pool));
}

svn_error_t *
svn_fs_fs__open_proto_rev_file(svn_fs_fs__revision_file_t **file,
                               svn_fs_t *fs,
                               const svn_fs_fs__id_part_t *txn_id,
                               apr_pool_t* result_pool,
                               apr_pool_t *scratch_pool)
{
  apr_file_t *apr_file;
  SVN_ERR(svn_io_file_open(&apr_file,
                           svn_fs_fs__path_txn_proto_rev(fs, txn_id,
                                                         scratch_pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                           result_pool));

  *file = apr_pcalloc(result_pool, sizeof(**file));
  (*file)->file = apr_file;
  (*file)->is_packed = FALSE;
  (*file)->start_revision = SVN_INVALID_REVNUM;
  (*file)->stream = svn_stream_from_aprfile2(apr_file, TRUE, result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__close_revision_file(svn_fs_fs__revision_file_t *file)
{
  if (file->stream)
    SVN_ERR(svn_stream_close(file->stream));
  if (file->file)
    SVN_ERR(svn_io_file_close(file->file, file->pool));

  file->file = NULL;
  file->stream = NULL;

  return SVN_NO_ERROR;
}