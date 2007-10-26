/*
 * diff_memory.c :  routines for doing diffs on in-memory data
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#define WANT_MEMFUNC
#define WANT_STRFUNC
#include <apr.h>
#include <apr_want.h>
#include <apr_tables.h>

#include "svn_diff.h"
#include "svn_pools.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_utf.h"
#include "diff.h"
#include "svn_private_config.h"

typedef struct source_tokens_t
{
  /* A token simply is an svn_string_t pointing to
     the data of the in-memory data source, containing
     the raw token text, with length stored in the string */
  /*###TODO: Note we currently don't support normalization. */
  apr_array_header_t *tokens;

  /* Next token to be consumed */
  apr_size_t next_token;

  /* The source, containing the in-memory data to be diffed */
  svn_string_t *source;

  /* The last token ends with a newline character (sequence) */
  svn_boolean_t ends_without_eol;
} source_tokens_t;

typedef struct diff_mem_baton_t
{
  /* The tokens for each of the sources */
  source_tokens_t sources[4];

  /* Normalization buffer; we only ever compare 2 tokens at the same time */
  char *normalization_buf[2];

  /* Options for normalized comparison of the data sources */
  const svn_diff_file_options_t *normalization_options;
} diff_mem_baton_t;


static int
datasource_to_index(svn_diff_datasource_e datasource)
{
  switch (datasource)
    {
    case svn_diff_datasource_original:
      return 0;

    case svn_diff_datasource_modified:
      return 1;

    case svn_diff_datasource_latest:
      return 2;

    case svn_diff_datasource_ancestor:
      return 3;
    }

  return -1;
}


/* Implements svn_diff_fns_t::datasource_open */
static svn_error_t *
datasource_open(void *baton, svn_diff_datasource_e datasource)
{
  /* Do nothing: everything is already there and initialized to 0 */
  return SVN_NO_ERROR;
}


/* Implements svn_diff_fns_t::datasource_close */
static svn_error_t *
datasource_close(void *baton, svn_diff_datasource_e datasource)
{
  /* Do nothing.  The compare_token function needs previous datasources
   * to stay available until all datasources are processed.
   */

  return SVN_NO_ERROR;
}


/* Implements svn_diff_fns_t::datasource_get_next_token */
static svn_error_t *
datasource_get_next_token(apr_uint32_t *hash, void **token, void *baton,
                          svn_diff_datasource_e datasource)
{
  diff_mem_baton_t *mem_baton = baton;
  source_tokens_t *src = &(mem_baton->sources[datasource_to_index(datasource)]);

  if (src->tokens->nelts > src->next_token)
    {
      /* There are actually tokens to be returned */
      char *buf = mem_baton->normalization_buf[0];
      svn_string_t *tok = (*token)
        = APR_ARRAY_IDX(src->tokens, src->next_token, svn_string_t *);
      apr_off_t len = tok->len;
      svn_diff__normalize_state_t state
        = svn_diff__normalize_state_normal;

      svn_diff__normalize_buffer(&buf, &len, &state, tok->data,
                                 mem_baton->normalization_options);
      *hash = svn_diff__adler32(0, buf, len);
      src->next_token++;
    }
  else
    *token = NULL;

  return SVN_NO_ERROR;
}

/* Implements svn_diff_fns_t::token_compare */
static svn_error_t *
token_compare(void *baton, void *token1, void *token2, int *result)
{
  /* Implement the same behaviour as diff_file.c:token_compare(),
     but be simpler, because we know we'll have all data in memory */
  diff_mem_baton_t *btn = baton;
  svn_string_t *t1 = token1;
  svn_string_t *t2 = token2;
  char *buf1 = btn->normalization_buf[0];
  char *buf2 = btn->normalization_buf[1];
  apr_off_t len1 = t1->len;
  apr_off_t len2 = t2->len;
  svn_diff__normalize_state_t state = svn_diff__normalize_state_normal;

  svn_diff__normalize_buffer(&buf1, &len1, &state, t1->data,
                             btn->normalization_options);
  state = svn_diff__normalize_state_normal;
  svn_diff__normalize_buffer(&buf2, &len2, &state, t2->data,
                             btn->normalization_options);

  if (len1 != len2)
    *result = (len1 < len2) ? -1 : 1;
  else
    *result = (len1 == 0) ? 0 : memcmp(buf1, buf2, len1);

  return SVN_NO_ERROR;
}

/* Implements svn_diff_fns_t::token_discard */
static void
token_discard(void *baton, void *token)
{
  /* No-op, we have no use for discarded tokens... */
}


/* Implements svn_diff_fns_t::token_discard_all */
static void
token_discard_all(void *baton)
{
  /* Do nothing.
     Note that in the file case, this function discards all
     tokens allocated, but we're geared toward small in-memory diffs.
     Meaning that there's no special pool to clear.
  */
}


static const svn_diff_fns_t svn_diff__mem_vtable =
{
  datasource_open,
  datasource_close,
  datasource_get_next_token,
  token_compare,
  token_discard,
  token_discard_all
};

/* Fill SRC with the diff tokens (e.g. lines).

   TEXT is assumed to live long enough for the tokens to
   stay valid during their lifetime: no data is copied,
   instead, svn_string_t's are allocated pointing straight
   into TEXT.
*/
static void
fill_source_tokens(source_tokens_t *src,
                   const svn_string_t *text,
                   apr_pool_t *pool)
{
  const char *curp;
  const char *endp;
  const char *startp;

  src->tokens = apr_array_make(pool, 0, sizeof(svn_string_t *));
  src->next_token = 0;
  src->source = (svn_string_t *)text;

  for (startp = curp = text->data, endp = curp + text->len;
       curp != endp; curp++)
    {
      if (curp != endp && *curp == '\r' && *(curp + 1) == '\n')
        curp++;

      if (*curp == '\r' || *curp == '\n')
        {
          APR_ARRAY_PUSH(src->tokens, svn_string_t *) =
            svn_string_ncreate(startp, curp - startp + 1, pool);

          startp = curp + 1;
        }
    }

  /* If there's anything remaining (ie last line doesn't have a newline) */
  if (startp != endp)
    {
      APR_ARRAY_PUSH(src->tokens, svn_string_t *) =
        svn_string_ncreate(startp, endp - startp, pool);
      src->ends_without_eol = TRUE;
    }
  else
    src->ends_without_eol = FALSE;
}


static void
alloc_normalization_bufs(diff_mem_baton_t *btn,
                         int sources,
                         apr_pool_t *pool)
{
  apr_size_t max_len = 0;
  apr_off_t idx;
  int i;

  for (i = 0; i < sources; i++)
    {
      apr_array_header_t *tokens = btn->sources[i].tokens;
      if (tokens->nelts > 0)
        for (idx = 0; idx < tokens->nelts; idx++)
          {
            apr_size_t token_len
              = APR_ARRAY_IDX(tokens, idx, svn_string_t *)->len;
            max_len = (max_len < token_len) ? token_len : max_len;
          }
    }

  btn->normalization_buf[0] = apr_palloc(pool, max_len);
  btn->normalization_buf[1] = apr_palloc(pool, max_len);
}

svn_error_t *
svn_diff_mem_string_diff(svn_diff_t **diff,
                         const svn_string_t *original,
                         const svn_string_t *modified,
                         const svn_diff_file_options_t *options,
                         apr_pool_t *pool)
{
  diff_mem_baton_t baton;

  fill_source_tokens(&(baton.sources[0]), original, pool);
  fill_source_tokens(&(baton.sources[1]), modified, pool);
  alloc_normalization_bufs(&baton, 2, pool);

  baton.normalization_options = options;

  SVN_ERR(svn_diff_diff(diff, &baton, &svn_diff__mem_vtable, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_mem_string_diff3(svn_diff_t **diff,
                          const svn_string_t *original,
                          const svn_string_t *modified,
                          const svn_string_t *latest,
                          const svn_diff_file_options_t *options,
                          apr_pool_t *pool)
{
  diff_mem_baton_t baton;

  fill_source_tokens(&(baton.sources[0]), original, pool);
  fill_source_tokens(&(baton.sources[1]), modified, pool);
  fill_source_tokens(&(baton.sources[2]), latest, pool);
  alloc_normalization_bufs(&baton, 3, pool);

  baton.normalization_options = options;

  SVN_ERR(svn_diff_diff3(diff, &baton, &svn_diff__mem_vtable, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_diff_mem_string_diff4(svn_diff_t **diff,
                          const svn_string_t *original,
                          const svn_string_t *modified,
                          const svn_string_t *latest,
                          const svn_string_t *ancestor,
                          const svn_diff_file_options_t *options,
                          apr_pool_t *pool)
{
  diff_mem_baton_t baton;

  fill_source_tokens(&(baton.sources[0]), original, pool);
  fill_source_tokens(&(baton.sources[1]), modified, pool);
  fill_source_tokens(&(baton.sources[2]), latest, pool);
  fill_source_tokens(&(baton.sources[3]), ancestor, pool);
  alloc_normalization_bufs(&baton, 4, pool);

  baton.normalization_options = options;

  SVN_ERR(svn_diff_diff4(diff, &baton, &svn_diff__mem_vtable, pool));

  return SVN_NO_ERROR;
}


typedef enum unified_output_e
{
  unified_output_context = 0,
  unified_output_delete,
  unified_output_insert
} unified_output_e;

/* Baton for generating unified diffs */
typedef struct unified_output_baton_t
{
  svn_stream_t *output_stream;
  const char *header_encoding;
  source_tokens_t sources[2]; /* 0 == original; 1 == modified */
  apr_size_t next_token; /* next token in original source */

  /* Cached markers, in header_encoding,
     indexed using unified_output_e */
  const char *prefix_str[3];

  svn_stringbuf_t *hunk;    /* in-progress hunk data */
  apr_off_t hunk_length[2]; /* 0 == original; 1 == modified */
  apr_off_t hunk_start[2];  /* 0 == original; 1 == modified */

  /* Pool for allocation of temporary memory in the callbacks
     Should be cleared on entry of each iteration of a callback */
  apr_pool_t *pool;
} output_baton_t;


/* Append tokens (lines) FIRST up to PAST_LAST
   from token-source index TOKENS with change-type TYPE
   to the current hunk.
*/
static svn_error_t *
output_unified_token_range(output_baton_t *btn,
                           int tokens,
                           unified_output_e type,
                           apr_off_t first,
                           apr_off_t past_last)
{
  source_tokens_t *source = &btn->sources[tokens];
  apr_off_t idx;

  past_last = (past_last > source->tokens->nelts)
    ? source->tokens->nelts : past_last;

  if (tokens == 0)
    /* We get context from the original source, don't expect
       to be asked to output a block which starts before
       what we already have written. */
    first = (first < btn->next_token) ? btn->next_token : first;

  if (first >= past_last)
    return SVN_NO_ERROR;

  /* Do the loop with prefix and token */
  for (idx = first; idx < past_last; idx++)
    {
      svn_string_t *token
        = APR_ARRAY_IDX(source->tokens, idx, svn_string_t *);
      svn_stringbuf_appendcstr(btn->hunk, btn->prefix_str[type]);
      svn_stringbuf_appendbytes(btn->hunk, token->data, token->len);

      if (type == unified_output_context)
        {
          btn->hunk_length[0]++;
          btn->hunk_length[1]++;
        }
      else if (type == unified_output_delete)
        btn->hunk_length[0]++;
      else
        btn->hunk_length[1]++;

    }
  if (past_last == source->tokens->nelts && source->ends_without_eol)
    {
      const char *out_str;
      SVN_ERR(svn_utf_cstring_from_utf8_ex2
              (&out_str,
               /* The string below is intentionally not marked for translation:
                  it's vital to correct operation of the diff(1)/patch(1)
                  program pair. */
               APR_EOL_STR "\\ No newline at end of file" APR_EOL_STR,
               btn->header_encoding, btn->pool));
      svn_stringbuf_appendcstr(btn->hunk, out_str);
    }

  if (tokens == 0)
    btn->next_token = past_last;

  return SVN_NO_ERROR;
}

/* Flush the hunk currently built up in BATON
   into the baton's output_stream */
static svn_error_t *
output_unified_flush_hunk(output_baton_t *baton)
{
  apr_off_t target_token;
  apr_size_t hunk_len;

  if (svn_stringbuf_isempty(baton->hunk))
    return SVN_NO_ERROR;

  svn_pool_clear(baton->pool);

  /* Write the trailing context */
  target_token = baton->hunk_start[0] + baton->hunk_length[0]
    + SVN_DIFF__UNIFIED_CONTEXT_SIZE;
  SVN_ERR(output_unified_token_range(baton, 0 /*original*/,
                                     unified_output_context,
                                     baton->next_token, target_token));

  /* Write the hunk header */
  if (baton->hunk_length[0] > 0)
    /* Convert our 0-based line numbers into unidiff 1-based numbers */
    baton->hunk_start[0]++;
  SVN_ERR(svn_stream_printf_from_utf8
          (baton->output_stream, baton->header_encoding,
           baton->pool,
           /* Hunk length 1 is implied, don't show the
              length field if we have a hunk that long */
           (baton->hunk_length[0] == 1)
           ? ("@@ -%" APR_OFF_T_FMT)
           : ("@@ -%" APR_OFF_T_FMT ",%" APR_OFF_T_FMT),
           baton->hunk_start[0], baton->hunk_length[0]));

  if (baton->hunk_length[1] > 0)
    /* Convert our 0-based line numbers into unidiff 1-based numbers */
    baton->hunk_start[1]++;
  SVN_ERR(svn_stream_printf_from_utf8
          (baton->output_stream, baton->header_encoding,
           baton->pool,
           /* Hunk length 1 is implied, don't show the
              length field if we have a hunk that long */
           (baton->hunk_length[1] == 1)
           ? (" +%" APR_OFF_T_FMT " @@" APR_EOL_STR)
           : (" +%" APR_OFF_T_FMT ",%" APR_OFF_T_FMT " @@" APR_EOL_STR),
           baton->hunk_start[1], baton->hunk_length[1]));

  hunk_len = baton->hunk->len;
  SVN_ERR(svn_stream_write(baton->output_stream,
                           baton->hunk->data, &hunk_len));

  baton->hunk_length[0] = baton->hunk_length[1] = 0;
  svn_stringbuf_setempty(baton->hunk);

  return SVN_NO_ERROR;
}

/* Implements svn_diff_output_fns_t::output_diff_modified */
static svn_error_t *
output_unified_diff_modified(void *baton,
                             apr_off_t original_start,
                             apr_off_t original_length,
                             apr_off_t modified_start,
                             apr_off_t modified_length,
                             apr_off_t latest_start,
                             apr_off_t latest_length)
{
  output_baton_t *btn = baton;
  apr_off_t targ_orig, targ_mod;

  targ_orig = original_start - SVN_DIFF__UNIFIED_CONTEXT_SIZE;
  targ_orig = (targ_orig < 0) ? 0 : targ_orig;
  targ_mod = modified_start;

  if (btn->next_token + SVN_DIFF__UNIFIED_CONTEXT_SIZE < targ_orig)
    SVN_ERR(output_unified_flush_hunk(btn));

  if (btn->hunk_length[0] == 0
      && btn->hunk_length[1] == 0)
    {
      btn->hunk_start[0] = targ_orig;
      btn->hunk_start[1] = targ_mod + targ_orig - original_start;
    }

  SVN_ERR(output_unified_token_range(btn, 0/*original*/,
                                     unified_output_context,
                                     targ_orig, original_start));
  SVN_ERR(output_unified_token_range(btn, 0/*original*/,
                                     unified_output_delete,
                                     original_start,
                                     original_start + original_length));
  SVN_ERR(output_unified_token_range(btn, 1/*modified*/, unified_output_insert,
                                     modified_start,
                                     modified_start + modified_length));

  return SVN_NO_ERROR;
}

static const svn_diff_output_fns_t mem_output_unified_vtable =
{
  NULL, /* output_common */
  output_unified_diff_modified,
  NULL, /* output_diff_latest */
  NULL, /* output_diff_common */
  NULL  /* output_conflict */
};


svn_error_t *
svn_diff_mem_string_output_unified(svn_stream_t *output_stream,
                                   svn_diff_t *diff,
                                   const char *original_header,
                                   const char *modified_header,
                                   const char *header_encoding,
                                   const svn_string_t *original,
                                   const svn_string_t *modified,
                                   apr_pool_t *pool)
{

  if (svn_diff_contains_diffs(diff))
    {
      output_baton_t baton;

      memset(&baton, 0, sizeof(baton));
      baton.output_stream = output_stream;
      baton.pool = svn_pool_create(pool);
      baton.header_encoding = header_encoding;
      baton.hunk = svn_stringbuf_create("", pool);

      SVN_ERR(svn_utf_cstring_from_utf8_ex2
              (&(baton.prefix_str[unified_output_context]), " ",
               header_encoding, pool));
      SVN_ERR(svn_utf_cstring_from_utf8_ex2
              (&(baton.prefix_str[unified_output_delete]), "-",
               header_encoding, pool));
      SVN_ERR(svn_utf_cstring_from_utf8_ex2
              (&(baton.prefix_str[unified_output_insert]), "+",
               header_encoding, pool));

      fill_source_tokens(&baton.sources[0], original, pool);
      fill_source_tokens(&baton.sources[1], modified, pool);

      SVN_ERR(svn_stream_printf_from_utf8
              (output_stream, header_encoding, pool,
               "--- %s" APR_EOL_STR
               "+++ %s" APR_EOL_STR,
               original_header, modified_header));

      SVN_ERR(svn_diff_output(diff, &baton,
                              &mem_output_unified_vtable));
      SVN_ERR(output_unified_flush_hunk(&baton));

      svn_pool_destroy(baton.pool);
    }

  return SVN_NO_ERROR;
}



/* diff3 merge output */

typedef struct merge_output_baton_t
{
  svn_stream_t *output_stream;

  /* Tokenized source text */
  source_tokens_t sources[3];
  apr_off_t next_token;

  /* Markers for marking conflicted sections */
  const char *markers[4]; /* 0 = original, 1 = modified,
                             2 = separator, 3 = latest (end) */

  svn_boolean_t display_original_in_conflict;
  svn_boolean_t display_resolved_conflicts;
} merge_output_baton_t;


static svn_error_t *
output_merge_token_range(merge_output_baton_t *btn,
                         int idx, apr_off_t first,
                         apr_off_t length)
{
  apr_array_header_t *tokens = btn->sources[idx].tokens;

  for (; length > 0; length--, first++)
    {
      svn_string_t *token = APR_ARRAY_IDX(tokens, first, svn_string_t *);
      apr_size_t len = token->len;

      SVN_ERR(svn_stream_write(btn->output_stream, token->data, &len));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
output_merge_marker(merge_output_baton_t *btn, int idx)
{
  apr_size_t len = strlen(btn->markers[idx]);
  return svn_stream_write(btn->output_stream, btn->markers[idx], &len);
}

static svn_error_t *
output_common_modified(void *baton,
                       apr_off_t original_start, apr_off_t original_length,
                       apr_off_t modified_start, apr_off_t modified_length,
                       apr_off_t latest_start, apr_off_t latest_length)
{
  return output_merge_token_range(baton, 1/*modified*/,
                                  modified_start, modified_length);
}

static svn_error_t *
output_latest(void *baton,
              apr_off_t original_start, apr_off_t original_length,
              apr_off_t modified_start, apr_off_t modified_length,
              apr_off_t latest_start, apr_off_t latest_length)
{
  return output_merge_token_range(baton, 2/*latest*/,
                                  latest_start, latest_length);
}

static svn_error_t *
output_conflict(void *baton,
                apr_off_t original_start, apr_off_t original_length,
                apr_off_t modified_start, apr_off_t modified_length,
                apr_off_t latest_start, apr_off_t latest_length,
                svn_diff_t *diff);

static const svn_diff_output_fns_t merge_output_vtable =
{
  output_common_modified, /* common */
  output_common_modified, /* modified */
  output_latest,
  output_common_modified, /* output_diff_common */
  output_conflict
};

static svn_error_t *
output_conflict(void *baton,
                apr_off_t original_start, apr_off_t original_length,
                apr_off_t modified_start, apr_off_t modified_length,
                apr_off_t latest_start, apr_off_t latest_length,
                svn_diff_t *diff)
{
  merge_output_baton_t *btn = baton;

  if (diff && btn->display_resolved_conflicts)
    return svn_diff_output(diff, baton, &merge_output_vtable);

  SVN_ERR(output_merge_marker(btn, 1/*modified*/));
  SVN_ERR(output_merge_token_range(btn, 1/*modified*/,
                                   modified_start, modified_length));

  if (btn->display_original_in_conflict)
    {
      SVN_ERR(output_merge_marker(btn, 0/*original*/));
      SVN_ERR(output_merge_token_range(btn, 0/*modified*/,
                                       original_start, original_length));
    }

  SVN_ERR(output_merge_marker(btn, 2/*separator*/));
  SVN_ERR(output_merge_token_range(btn, 2/*latest*/,
                                   latest_start, latest_length));
  SVN_ERR(output_merge_marker(btn, 3/*latest (end)*/));

  return SVN_NO_ERROR;
}

/* TOKEN is the first token in the modified file.
   Return its line-ending, if any. */
static const char *
detect_eol(svn_string_t *token)
{
  const char *curp;

  if (token->len == 0)
    return NULL;

  curp = token->data + token->len - 1;
  if (*curp == '\r')
    return "\r";
  else if (*curp != '\n')
    return NULL;
  else
    {
      if (token->len == 1
          || *(--curp) != '\r')
        return "\n";
      else
        return "\r\n";
    }
}

svn_error_t *
svn_diff_mem_string_output_merge(svn_stream_t *output_stream,
                                 svn_diff_t *diff,
                                 const svn_string_t *original,
                                 const svn_string_t *modified,
                                 const svn_string_t *latest,
                                 const char *conflict_original,
                                 const char *conflict_modified,
                                 const char *conflict_latest,
                                 const char *conflict_separator,
                                 svn_boolean_t display_original_in_conflict,
                                 svn_boolean_t display_resolved_conflicts,
                                 apr_pool_t *pool)
{
  merge_output_baton_t btn;
  const char *eol;

  memset(&btn, 0, sizeof(btn));
  btn.output_stream = output_stream;

  fill_source_tokens(&(btn.sources[0]), original, pool);
  fill_source_tokens(&(btn.sources[1]), modified, pool);
  fill_source_tokens(&(btn.sources[2]), latest, pool);

  btn.display_original_in_conflict = display_original_in_conflict;
  btn.display_resolved_conflicts = display_resolved_conflicts;

  if (btn.sources[1].tokens->nelts > 0)
    {
      eol = detect_eol(APR_ARRAY_IDX(btn.sources[1].tokens, 0, svn_string_t *));
      if (!eol)
        eol = APR_EOL_STR;  /* use the platform default */
    }
  else
    eol = APR_EOL_STR;  /* use the platform default */

  SVN_ERR(svn_utf_cstring_from_utf8
          (&btn.markers[1],
           apr_psprintf(pool, "%s%s",
                        conflict_modified
                        ? conflict_modified : "<<<<<<< (modified)",
                        eol),
           pool));
  SVN_ERR(svn_utf_cstring_from_utf8
          (&btn.markers[0],
           apr_psprintf(pool, "%s%s",
                        conflict_original
                        ? conflict_original : "||||||| (original)",
                        eol),
           pool));
  SVN_ERR(svn_utf_cstring_from_utf8
          (&btn.markers[2],
           apr_psprintf(pool, "%s%s",
                        conflict_separator
                        ? conflict_separator : "=======",
                        eol),
           pool));
  SVN_ERR(svn_utf_cstring_from_utf8
          (&btn.markers[3],
           apr_psprintf(pool, "%s%s",
                        conflict_latest
                        ? conflict_latest : ">>>>>>> (latest)",
                        eol),
           pool));

  SVN_ERR(svn_diff_output(diff, &btn, &merge_output_vtable));

  return SVN_NO_ERROR;
}
