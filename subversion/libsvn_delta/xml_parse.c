/*
 * xml_parse.c:  parse an Subversion "tree-delta" XML stream
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */

/* ==================================================================== */


/*
  This file implements a few public interfaces :

     svn_make_xml_parser()   -- create a custom svn/expat parser
     svn_free_xml_parser()   -- free it
 
     svn_xml_parsebytes()    -- push some svn xml stream at the parser
     svn_xml_auto_parse()    -- automated `pull interface' wrapper

  As the parser receives xml, calls are made into an svn_delta_edit_fns_t.

  See the bottom of this file for these routines.
*/



#include <stdio.h>
#include <string.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_xml.h"
#include "apr_strings.h"
#include "xmlparse.h"
#include "delta.h"


/* We must keep this map IN SYNC with the enumerated type
   svn_delta__XML_t in delta.h!  

   It allows us to do rapid strcmp() comparisons.  We terminate with
   NULL so that we have the ability to loop over the array easily. */
static const char * const svn_delta__tagmap[] =
{
  "tree-delta",
  "add",
  "delete",
  "replace",
  "file",
  "dir",
  "text-delta",
  "prop-delta",
  "set",
  NULL
};





/* The way to officially bail out of expat. 
   
   Store ERROR in DIGGER and set all expat callbacks to NULL. (To
   understand why this works, see svn_delta_parse(). ) */
static void
signal_expat_bailout (svn_error_t *error, svn_xml__digger_t *digger)
{
  /* This will cause the current XML_Parse() call to finish quickly! */
  XML_SetElementHandler (digger->expat_parser, NULL, NULL);
  XML_SetCharacterDataHandler (digger->expat_parser, NULL);

  /* Once outside of XML_Parse(), the existence of this field will
     cause svn_delta_parse()'s main read-loop to return error. */
  digger->validation_error = error;
}



/* Return an informative error message about invalid XML.
   (Set DESTROY_P to indicate an unexpected closure tag) */
static svn_error_t *
xml_validation_error (apr_pool_t *pool,
                      const char *name,
                      svn_boolean_t destroy_p)
{
  char *msg;

  if (destroy_p)
    msg = apr_psprintf (pool,
                        "XML validation error: got unexpected </%s>",
                        name);

  else
    msg = apr_psprintf (pool,
                        "XML validation error: got unexpected <%s>",
                        name);

  return svn_create_error (SVN_ERR_MALFORMED_XML, 0, msg, NULL, pool);
}


/* Set up FRAME's ancestry information to the degree that it is not
   already set.  Information is derived by walking backwards up from
   FRAME and examining parents, so it is important that frame has
   _already_ been linked into the digger's stack. */
static void
maybe_derive_ancestry (svn_xml__stackframe_t *frame,
                       apr_pool_t *pool)
{
  if ((frame->tag != svn_delta__XML_dir) 
      && (frame->tag != svn_delta__XML_file))
    {
      /* This is not the kind of frame that needs ancestry information. */
      return;
    }
  else if (frame->ancestor_path
           && (frame->ancestor_version >= 0))
    {
      /* It is the kind of frame that needs ancestry information, but
         all its ancestry information is already set. */
      return;
    }
  else
    {
      svn_xml__stackframe_t *p = frame->previous;
      svn_string_t *this_name = NULL;

      while (p)
        {
          /* Since we're walking up from youngest, we catch and hang
             onto the name attribute before seeing any ancestry. */
          if ((! this_name) && p->name)
            this_name = p->name;

          if (p->ancestor_path && (! frame->ancestor_path))
            {
              /* Why are we setting derived_ancestry_path according to
               * the nearest previous ancestor_path, instead of
               * nearest previous name?
               *
               * I'm glad you asked.
               *
               * It's because ancestry needs to be an absolute
               * path in existing repository version.  There's no
               * guarantee that the `name' fields we've seen so far
               * are actually in the repository, and even if they
               * were, there's no guarantee that the first frame with
               * a name represents a name at the _top_ of the
               * repository.  Following ancestry solves these
               * problems.
               *
               * kff todo: sleep on above reasoning.
               *
               * Remember that if any of the directories in the
               * chain has changed its name, then we wouldn't be
               * here anyway, because the delta should have set
               * ancestry attributes explicitly for everything
               * under that change.
               */

              frame->ancestor_path
                = svn_string_dup (p->ancestor_path, pool);
              svn_path_add_component (frame->ancestor_path, this_name,
                                      SVN_PATH_REPOS_STYLE, pool);
            }

          /* If ancestor_version not set, and see it here, then set it. */
          if ((frame->ancestor_version < 0) && (p->ancestor_version >= 0))
            frame->ancestor_version = p->ancestor_version;

          /* If we have all the ancestry information we need, then
             stop the search. */
          if ((frame->ancestor_version >= 0) && frame->ancestor_path)
            break;

          /* Else, keep searching. */
          p = p->previous;
        }

      /* That's it.  We don't check that ancestry was actually found.
         It's not this function's job to determine if an ancestor is
         necessary, only to find and set one if available. */
    }
}


/* A validation note.  

   The strategy for validating our XML stream is simple:

   1. When we find a new "open" tag, make sure it logically follows
   the previous tag.  This is handled in do_stack_append().

   2. When we find a "close" tag, make sure the newest item on the
   stack is of the identical type.  This is handled by
   do_stack_remove().

   When these functions find invalid XML, they call signal_expat_bailout().

*/


/* If FRAME represents an <add> or <replace> command, check if the
   "name" attribute conflicts with an preexisting dirent name in the
   parent (tree-delta) frame.  If so, return error.  If not, store the
   dirent name in parent's "namespace" hash. 

   Assumes that FRAME has not yet been appended to DIGGER->STACK.
*/
static svn_error_t *
check_dirent_namespace (svn_xml__digger_t *digger,
                        svn_xml__stackframe_t *frame)
{
  apr_hash_t *namespace = NULL;
  void *dirent_exists = NULL;

  /* Sanity: check frame's type.  If we're not looking at directory
     entries, just leave. */
  if ((frame->tag != svn_delta__XML_add)
       && (frame->tag != svn_delta__XML_replace))
    return SVN_NO_ERROR;

  namespace= digger->stack->namespace;
  
  if (namespace == NULL)
    return 
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0, 
       "check_dirent_namespace: parent frame has no namespace hash.",
       NULL, digger->pool);

  if ((frame->name == NULL) || (svn_string_isempty (frame->name)))
    return
      svn_create_error
      (SVN_ERR_MALFORMED_XML, 0,
       "check_dirent_namespace: <add> or <replace> has no `name' attribute.",
       NULL, digger->pool);
  
  /* Is "name" in the namespace already? */
  dirent_exists = apr_hash_get (namespace,
                                frame->name->data,
                                frame->name->len);
  if (dirent_exists)
    return 
      svn_create_errorf
      (SVN_ERR_MALFORMED_XML, 0, NULL, digger->pool,
       "check_dirent_namespace: non-unique dirent name '%s'",
       frame->name->data);

  else /* dirent_exists == NULL, so this is a unique dirent name */
    apr_hash_set (namespace, frame->name->data, frame->name->len, (void *) 1);

  return SVN_NO_ERROR;
}




/* Decide if it's valid XML to append NEW_FRAME to DIGGER's stack.  If
   so, append the frame and inherit the parent's baton.  If not,
   return a validity error. (TAGNAME is used for error message.) */
static svn_error_t *
do_stack_append (svn_xml__digger_t *digger, 
                 svn_xml__stackframe_t *new_frame,
                 const char *tagname)
{
  svn_error_t *err;
  apr_pool_t *pool = digger->pool;
  svn_xml__stackframe_t *youngest_frame = digger->stack;

  if (youngest_frame == NULL)
    {
      /* The stack is empty, this is our first frame. */

      /* Make sure that it's indeed a tree-delta. */
      if (new_frame->tag != svn_delta__XML_treedelta)
        return xml_validation_error (pool, tagname, FALSE);

      digger->stack = new_frame;
      /* kff todo: parent_baton cannot be side effected, so why do we
         have it?  It will always be digger->dir_baton. */
      new_frame->baton = digger->dir_baton;
    }
  else   /* We already have a context, so check validity. */
    {
      /* <tree-delta> must follow <dir> */
      if ((new_frame->tag == svn_delta__XML_treedelta)
          && (youngest_frame->tag != svn_delta__XML_dir))
        return xml_validation_error (pool, tagname, FALSE);
      
      /* <add>, <replace> must follow <tree-delta> */
      else if ( ((new_frame->tag == svn_delta__XML_add)
                 || (new_frame->tag == svn_delta__XML_replace))
                && (youngest_frame->tag != svn_delta__XML_treedelta) )
        return xml_validation_error (pool, tagname, FALSE);

      /* <delete> must follow either <tree-delta> or <prop-delta> */
      else if ( (new_frame->tag == svn_delta__XML_delete)
                && (youngest_frame->tag != svn_delta__XML_treedelta)
                && (youngest_frame->tag != svn_delta__XML_propdelta) )
        return xml_validation_error (pool, tagname, FALSE);
      
      /* <file>, <dir> must follow either <add> or <replace> */
      else if ((new_frame->tag == svn_delta__XML_file)
               || (new_frame->tag == svn_delta__XML_dir))
        {
          if ((youngest_frame->tag != svn_delta__XML_add)
              && (youngest_frame->tag != svn_delta__XML_replace))
            return xml_validation_error (digger->pool, tagname, FALSE);
        }
      
      /* <prop-delta> must follow one of <add>, <replace> (if talking
         about a directory entry's properties) or must follow one of
         <file>, <dir> */
      else if ((new_frame->tag == svn_delta__XML_propdelta)
               && (youngest_frame->tag != svn_delta__XML_add)
               && (youngest_frame->tag != svn_delta__XML_replace)
               && (youngest_frame->tag != svn_delta__XML_file)
               && (youngest_frame->tag != svn_delta__XML_dir))
        return xml_validation_error (pool, tagname, FALSE);
      
      /* <text-delta> must follow <file> */
      else if ((new_frame->tag == svn_delta__XML_textdelta)
               && (youngest_frame->tag != svn_delta__XML_file))
        return xml_validation_error (pool, tagname, FALSE);

      /* <set> must follow <prop-delta> */
      else if ((new_frame->tag == svn_delta__XML_textdelta)
               && (youngest_frame->tag != svn_delta__XML_file))
        return xml_validation_error (pool, tagname, FALSE);

      /* ancestry information can only appear as <file> or <dir> attrs */
      else if ((new_frame->ancestor_path
                || (new_frame->ancestor_version >= 0))
               && (new_frame->tag != svn_delta__XML_file)
               && (new_frame->tag != svn_delta__XML_dir))
        return xml_validation_error (pool, tagname, FALSE);

      /* Final check: if this is an <add> or <replace>, make sure the
         "name" attribute is unique within the parent <tree-delta>. */

      err = check_dirent_namespace (digger, new_frame);
      if (err)
        return err;

      /* The XML is valid.  Do the append.  */
      youngest_frame->next = new_frame;

      /* Inherit parent's baton. */
      new_frame->baton = youngest_frame->baton; 

      /* Change digger's field accordingly. */
      digger->stack = new_frame;
    }
  
  /* Link backwards, too. */
  new_frame->previous = youngest_frame;

  /* Set up any unset ancestry information. */
  maybe_derive_ancestry (new_frame, pool);

  return SVN_NO_ERROR;
}




/* Decide if an xml closure TAGNAME is valid, by examining the
   youngest frame in DIGGER's stack. */
static svn_error_t *
do_stack_check_remove (svn_xml__digger_t *digger, const char *tagname)
{
  apr_pool_t *pool = digger->pool;
  svn_xml__stackframe_t *youngest_frame = digger->stack;

  if (youngest_frame == NULL)
    return xml_validation_error (pool, tagname, TRUE);

  /* Validity check: Make sure the kind of object we're removing (due
     to an XML TAGNAME closure) actually agrees with the type of frame
     at the top of the stack.  This also filters out bogus values of
     TAGNAME. */
  if (strcmp (tagname, svn_delta__tagmap[youngest_frame->tag]))
    return xml_validation_error (pool, tagname, TRUE);
        
  return SVN_NO_ERROR;
}



/* Utility:  set FRAME's tag field to an svn_delta__XML_t, according to NAME */
static svn_error_t *
set_tag_type (svn_xml__stackframe_t *frame,
              const char *name,
              svn_xml__digger_t *digger)
{
  int tag;
  
  for (tag = 0; svn_delta__tagmap[tag]; tag++)
    if (! strcmp (name, svn_delta__tagmap[tag]))
      {
        frame->tag = tag;
        return SVN_NO_ERROR;
      }
  
  return xml_validation_error (digger->pool, name, TRUE);
}




/* Called when we get a <dir> tag preceeded by either a <new> or
   <replace> tag; calls the appropriate callback inside
   DIGGER->EDITOR, depending on the value of REPLACE_P. */
static svn_error_t *
do_directory_callback (svn_xml__digger_t *digger, 
                       svn_xml__stackframe_t *youngest_frame,
                       const char **atts,
                       svn_boolean_t replace_p)
{
  svn_error_t *err;
  const char *ancestor, *ver;
  svn_string_t *dir_name = NULL;

  /* Only proceed if the editor callback exists. */
  if (replace_p && (! digger->editor->replace_directory))
    return SVN_NO_ERROR;
  if ((! replace_p) && (! digger->editor->add_directory))
    return SVN_NO_ERROR;

  /* Retrieve the "name" field from the previous <new> or <replace> tag */
  dir_name = youngest_frame->previous->name;
  if (dir_name == NULL)
    return
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0,
       "do_directory_callback: <dir>'s parent tag has no 'name' field.",
       NULL, digger->pool);
                             
  /* Search through ATTS, looking for any "ancestor" or "ver"
     attributes of the current <dir> tag. */
  ancestor = svn_xml_get_attr_value ("ancestor", atts);
  if (ancestor)
    youngest_frame->ancestor_path = svn_string_create (ancestor, digger->pool);
  ver = svn_xml_get_attr_value ("ver", atts);
  if (ver)
    youngest_frame->ancestor_version = atoi (ver);

  /* Call our editor's callback. */
  if (replace_p)
    err = (* (digger->editor->replace_directory)) 
      (dir_name,
       digger->edit_baton,
       youngest_frame->baton,
       youngest_frame->ancestor_path,
       youngest_frame->ancestor_version,
       &(youngest_frame->baton));
  else
    err = (* (digger->editor->add_directory)) 
      (dir_name,
       digger->edit_baton,
       youngest_frame->baton,
       youngest_frame->ancestor_path,
       youngest_frame->ancestor_version,
       &(youngest_frame->baton));

  if (err) 
    return err;

  /* Store CHILD_BATON in the digger, too, for safekeeping. */
  digger->dir_baton = youngest_frame->baton;

  return SVN_NO_ERROR;
}



/* Called when we find a <delete> tag after a <tree-delta> tag. */
static svn_error_t *
do_delete_dirent (svn_xml__digger_t *digger, 
                  svn_xml__stackframe_t *youngest_frame)
{
  svn_string_t *dirent_name = NULL;
  svn_error_t *err;

  /* Only proceed if the editor callback exists. */
  if (! (digger->editor->delete))
    return SVN_NO_ERROR;
  
  /* Retrieve the "name" field from the current <delete> tag */
  dirent_name = youngest_frame->name;
  if (dirent_name == NULL)
    return 
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0,
       "do_delete_dirent: <delete> tag has no 'name' field.",
       NULL, digger->pool);

  /* Call our editor's callback */
  err = (* (digger->editor->delete)) (dirent_name, 
                                      digger->edit_baton,
                                      youngest_frame->baton);
  if (err)
    return err;

  return SVN_NO_ERROR;
}




/* Called when we get <file> after a <new> or <replace>. */
static svn_error_t *
do_file_callback (svn_xml__digger_t *digger,
                  svn_xml__stackframe_t *youngest_frame,
                  const char **atts,
                  svn_boolean_t replace_p)
{
  svn_error_t *err;
  const char *ancestor, *ver;
  svn_string_t *filename = NULL;

  /* Only proceed if the editor callback exists. */
  if (replace_p && (! digger->editor->replace_file))
    return SVN_NO_ERROR;
  if ((! replace_p) && (! digger->editor->add_file))
    return SVN_NO_ERROR;

  /* Retrieve the "name" field from the previous <new> or <replace> tag */
  filename = youngest_frame->previous->name;
  if (filename == NULL)
    return 
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0,
       "do_file_callback: <file>'s parent tag has no 'name' field.",
       NULL, digger->pool);
                             
  /* Search through ATTS, looking for any "ancestor" or "ver"
     attributes of the current <dir> tag. */
  ancestor = svn_xml_get_attr_value ("ancestor", atts);
  if (ancestor)
    youngest_frame->ancestor_path = svn_string_create (ancestor, digger->pool);
  ver = svn_xml_get_attr_value ("ver", atts);
  if (ver)
    youngest_frame->ancestor_version = atoi (ver);

  /* Call our editor's callback, and get back a window handler & baton. */
  if (replace_p)
    err = (* (digger->editor->replace_file)) 
      (filename,
       digger->edit_baton,
       youngest_frame->baton,
       youngest_frame->ancestor_path,
       youngest_frame->ancestor_version,
       &(youngest_frame->file_baton));
  else
    err = (* (digger->editor->add_file)) 
      (filename,
       digger->edit_baton,
       youngest_frame->baton,
       youngest_frame->ancestor_path,
       youngest_frame->ancestor_version,
       &(youngest_frame->file_baton));
  
  if (err)
    return err;

  /* Store FILE_BATON in the digger, too, for safekeeping. */
  digger->file_baton = youngest_frame->file_baton;

  
  return SVN_NO_ERROR;
}




/* Called when we get a </dir> tag */
static svn_error_t *
do_close_directory (svn_xml__digger_t *digger)
{
  svn_error_t *err;

  /* Only proceed if the editor callback exists. */
  if (! (digger->editor->close_directory))
    return SVN_NO_ERROR;

  /* Nothing to do but caller the editor's callback, methinks. */
  err = (* (digger->editor->close_directory)) (digger->edit_baton,
                                                digger->stack->baton);
  if (err)
    return err;

  /* Drop the current directory baton */
  digger->dir_baton = NULL;

  return SVN_NO_ERROR;
}


/* Called when we get a </file> tag */
static svn_error_t *
do_close_file (svn_xml__digger_t *digger)
{
  svn_error_t *err;

  /* Only proceed further if the editor callback exists. */
  if (! (digger->editor->close_file))
    return SVN_NO_ERROR;

  /* Call the editor's callback. */
  err = (* (digger->editor->close_file)) (digger->edit_baton,
                                           digger->stack->file_baton);
  if (err)
    return err;

  /* Drop the current parsers! */
  digger->vcdiff_parser = NULL;

  /* Drop the current file baton */
  digger->file_baton = NULL;

  return SVN_NO_ERROR;
}



/* When we find a new text-delta, a editor callback returns to us a
   vcdiff-window-consumption routine that we use to create a unique
   vcdiff parser.  (The vcdiff parser knows how to "push" windows of
   vcdata to the consumption routine.)  */
static svn_error_t *
do_begin_textdelta (svn_xml__digger_t *digger)
{
  svn_error_t *err;
  svn_txdelta_window_handler_t *window_consumer = NULL;
  void *consumer_baton = NULL;

  if (digger->editor->apply_textdelta == NULL)
    return SVN_NO_ERROR;

  /* Get a window consumer & baton! */
  err = (* (digger->editor->apply_textdelta)) (digger->edit_baton,
                                               digger->dir_baton,
                                               digger->file_baton,
                                               &window_consumer,
                                               &consumer_baton);

  /* Now create a vcdiff parser based on the consumer/baton we got. */  
  digger->vcdiff_parser = svn_make_vcdiff_parser (window_consumer,
                                                  consumer_baton,
                                                  digger->pool);
  return SVN_NO_ERROR;
}



/* When we find a new <prop-delta> */
static svn_error_t *
do_begin_propdelta (svn_xml__digger_t *digger)
{
  svn_xml__stackframe_t *youngest_frame;

  /* First, allocate a new propdelta object in our digger (if there's
     already one there, we lose the pointer to it, which is fine.) */
  digger->current_propdelta = 
    (svn_propdelta_t *) apr_palloc (digger->pool, 
                                    sizeof(svn_propdelta_t));

  digger->current_propdelta->name  = svn_string_create ("", digger->pool);
  digger->current_propdelta->value = svn_string_create ("", digger->pool);

  /* Now figure out our context.  Is this a propdelta on a file, dir,
     or dirent? */
  youngest_frame = digger->stack;
  if (!youngest_frame->previous)
    return 
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0,
       "do_begin_propdelta: <prop-delta> tag has no parent context",
       NULL, digger->pool);
  
  switch (youngest_frame->previous->tag)
    {
    case svn_delta__XML_file:
      {
        digger->current_propdelta->kind = svn_propdelta_file;
        /* Get the name of the file, too. */
        if (youngest_frame->previous->previous)
          digger->current_propdelta->entity_name = 
            svn_string_dup (youngest_frame->previous->previous->name,
                            digger->pool);
        break;
      }
    case svn_delta__XML_dir:
      {
        digger->current_propdelta->kind = svn_propdelta_dir;
        /* Get the name of the dir, too. */
        if (youngest_frame->previous->previous)
          digger->current_propdelta->entity_name = 
            svn_string_dup (youngest_frame->previous->previous->name,
                            digger->pool);
        break;
      }
    case svn_delta__XML_add:
      {
        digger->current_propdelta->kind = svn_propdelta_dirent;
        /* Get the name of the dirent, too. */
        digger->current_propdelta->entity_name = 
            svn_string_dup (youngest_frame->previous->name,
                            digger->pool);
        break;
      }
    case svn_delta__XML_replace:
      {
        digger->current_propdelta->kind = svn_propdelta_dirent;
        /* Get the name of the dirent, too. */
        digger->current_propdelta->entity_name = 
            svn_string_dup (youngest_frame->previous->name,
                            digger->pool);
        break;
      }
    default:
      return 
        svn_create_error 
        (SVN_ERR_MALFORMED_XML, 0,
         "do_begin_propdelta: <prop-delta> tag has unknown context!",
         NULL, digger->pool);
    }
   

  return SVN_NO_ERROR;
}



/* When we get a <set>, add the "name" field to our propdelta in-progress */
static svn_error_t *
do_begin_setprop (svn_xml__digger_t *digger,
                  svn_xml__stackframe_t *youngest_frame)
{
  if (digger->current_propdelta)
    digger->current_propdelta->name =
      svn_string_dup (youngest_frame->name, digger->pool);
  
  return SVN_NO_ERROR;
}




/* Called when we find a <delete> tag after a <prop-delta> tag. */
static svn_error_t *
do_delete_prop (svn_xml__digger_t *digger, 
                svn_xml__stackframe_t *youngest_frame)
{
  svn_string_t *dir_name = NULL;
        
  if (! digger->current_propdelta)
    return SVN_NO_ERROR;

  /* Retrieve the "name" field from the current <delete> tag */
  dir_name = youngest_frame->name;
  if (dir_name == NULL)
    return 
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0,
       "do_delete_prop: <delete> tag has no 'name' field.",
       NULL, digger->pool);
  
  /* Finish filling out current propdelta. */
  digger->current_propdelta->name =
    svn_string_dup (dir_name, digger->pool);


  return SVN_NO_ERROR;
}


/* When we get a </set>, or when we get the implicit closure at the
   end of <delete />, we send off the prop-delta to the appropriate
   editor callback.  Then blank the current prop-delta's name and
   value. */
static svn_error_t *
do_prop_delta_callback (svn_xml__digger_t *digger)
{
  svn_string_t *value_string;
  svn_error_t *err = SVN_NO_ERROR;

  if (! digger->current_propdelta)
    return SVN_NO_ERROR;

  if (svn_string_isempty(digger->current_propdelta->value))
    value_string = NULL;
  else
    value_string = digger->current_propdelta->value;

  switch (digger->current_propdelta->kind)
    {
    case svn_propdelta_file:
      {
        if (digger->editor->change_file_prop)
          err = (*(digger->editor->change_file_prop)) 
            (digger->edit_baton, digger->dir_baton, digger->file_baton,
             digger->current_propdelta->name,
             value_string);
        break;
      }
    case svn_propdelta_dir:
      {
        if (digger->editor->change_dir_prop)
          err = (*(digger->editor->change_dir_prop)) 
            (digger->edit_baton, digger->dir_baton,
             digger->current_propdelta->name,
             value_string);
        break;
      }
    case svn_propdelta_dirent:
      {
        if (digger->editor->change_dirent_prop)
          err = (*(digger->editor->change_dirent_prop)) 
            (digger->edit_baton, digger->dir_baton,
             digger->current_propdelta->entity_name,
             digger->current_propdelta->name,
             value_string);
        break;
      }
    default:
      {
        return svn_create_error 
          (SVN_ERR_MALFORMED_XML, 0,
           "do_prop_delta_callback: unknown digger->current_propdelta->kind",
           NULL, digger->pool);
      }
    }

  if (err)
    return err;

  /* Now that the change has been sent, clear its NAME and VALUE
     fields -- but not the KIND field, because more changes may be
     coming inside this <prop-delta> ! */
  svn_string_setempty(digger->current_propdelta->name);
  svn_string_setempty(digger->current_propdelta->value);

  return SVN_NO_ERROR;
}





/*-----------------------------------------------------------------------*/
/* Callback:  called whenever expat finds a new "open" tag.

   NAME contains the name of the tag.
   ATTS is a dumb list of tag attributes;  a list of name/value pairs, all
   null-terminated cstrings, and ending with an extra final NULL.
*/  
static void
xml_handle_start (void *userData, const char *name, const char **atts)
{
  svn_error_t *err;
  const char *value;

  /* Resurrect our digger structure */
  svn_xml__digger_t *my_digger = (svn_xml__digger_t *) userData;

  /* Create new stackframe */
  svn_xml__stackframe_t *new_frame
    = apr_pcalloc (my_digger->pool, sizeof (svn_xml__stackframe_t));

  /* Initialize the ancestor version to a recognizably invalid value. */
  new_frame->ancestor_version = -1;

  /* Set the tag field */
  err = set_tag_type (new_frame, name, my_digger);
  if (err)
    {
      /* Uh-oh, unrecognized tag, bail out. */
      signal_expat_bailout (err, my_digger);
      return;
    }

  /* Set "name" field in frame, if there's any such attribute in ATTS */
  value = svn_xml_get_attr_value ("name", atts);
  if (value)
    new_frame->name = svn_string_create (value, my_digger->pool);
  
  /* Set ancestor path in frame, if there's any such attribute in ATTS */
  value = svn_xml_get_attr_value ("ancestor", atts);
  if (value)
    new_frame->ancestor_path = svn_string_create (value, my_digger->pool);
  
  /* Set ancestor version in frame, if there's any such attribute in ATTS */
  value = svn_xml_get_attr_value ("ver", atts);
  if (value)
    new_frame->ancestor_version = atoi (value);

  /* If this frame represents a tree-delta, initialize an internal
     hashtable to hold dirent names. */
  if (new_frame->tag == svn_delta__XML_treedelta)
    new_frame->namespace = apr_make_hash (my_digger->pool);
  
  /*  Append new frame to stack, validating in the process. 
      If successful, new frame will automatically inherit parent's baton. */
  err = do_stack_append (my_digger, new_frame, name);
  if (err) {
    /* Uh-oh, invalid XML, bail out. */
    signal_expat_bailout (err, my_digger);
    return;
  }

  /* Now look for special events that the uber-caller (of
     svn_delta_parse()) might want to know about.  */

  /* EVENT:  Are we adding a new directory?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_delta__XML_add) 
        && (new_frame->tag == svn_delta__XML_dir))
      {
        err = do_directory_callback (my_digger, new_frame, atts, FALSE);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }

  /* EVENT:  Are we replacing a directory?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_delta__XML_replace) 
        && (new_frame->tag == svn_delta__XML_dir))
      {
        err = do_directory_callback (my_digger, new_frame, atts, TRUE);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }  

  /* EVENT:  Are we deleting a directory entry?  */
  if (new_frame->previous)
    if ( (new_frame->tag == svn_delta__XML_delete) &&
         (new_frame->previous->tag == svn_delta__XML_treedelta) )
      {
        err = do_delete_dirent (my_digger, new_frame);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }

  /* EVENT:  Are we adding a new file?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_delta__XML_add) 
        && (new_frame->tag == svn_delta__XML_file))
      {
        err = do_file_callback (my_digger, new_frame, atts, FALSE);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }

  /* EVENT:  Are we replacing a file?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_delta__XML_replace) 
        && (new_frame->tag == svn_delta__XML_file))
      {
        err = do_file_callback (my_digger, new_frame, atts, TRUE);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }

  /* EVENT:  Are we starting a new text-delta?  */
  if (new_frame->tag == svn_delta__XML_textdelta) 
    {
      err = do_begin_textdelta (my_digger);
      if (err)
        signal_expat_bailout (err, my_digger);
      return;
    }

  /* EVENT:  Are we starting a new prop-delta?  */
  if (new_frame->tag == svn_delta__XML_propdelta) 
    {
      err = do_begin_propdelta (my_digger);
      if (err)
        signal_expat_bailout (err, my_digger);
      return;
    }

  /* EVENT:  Are we setting a proeprty?  */
  if (new_frame->tag == svn_delta__XML_set) 
    {
      err = do_begin_setprop (my_digger, new_frame);
      if (err)
        signal_expat_bailout (err, my_digger);
      return;
    }

  /* EVENT:  Are we deleting a property?  */
  if (new_frame->previous)
    if ( (new_frame->tag == svn_delta__XML_delete) &&
         (new_frame->previous->tag == svn_delta__XML_propdelta) )
      {
        err = do_delete_prop (my_digger, new_frame);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }

  /* This is a void expat callback, don't return anything. */
}



/*  Callback:  called whenever we find a close tag (close paren) */

static void 
xml_handle_end (void *userData, const char *name)
{
  svn_error_t *err;

  /* Resurrect our digger structure */
  svn_xml__digger_t *digger = (svn_xml__digger_t *) userData;
  svn_xml__stackframe_t *youngest_frame = digger->stack;

  /* Validity check: is it going to be ok to remove the youngest frame
      in our stack?  */
  err = do_stack_check_remove (digger, name);
  if (err) {
    /* Uh-oh, invalid XML, bail out */
    signal_expat_bailout (err, digger);
    return;
  }
  
  /* Now look for special events that the uber-caller (of
     svn_delta_parse()) might want to know about.  */

  /* EVENT:  When we get a </dir> pass back the dir_baton. */
  if (strcmp (name, "dir") == 0)
    {
      err = do_close_directory (digger);
      if (err)
        signal_expat_bailout (err, digger);
    }      

  /* EVENT: when we get a </file>, drop our digger's parsers. */
  if (strcmp (name, "file") == 0)
    {
      err = do_close_file (digger);
      if (err)
        signal_expat_bailout (err, digger);
    }

  /* EVENT: when we get a </text-delta>, let the vcdiff parser know! */
  if (strcmp (name, "text-delta") == 0)
    {
      if (digger->vcdiff_parser)
        {     
          /* (length = 0) implies that we're done parsing vcdiff stream.
             Let the parser flush its buffer, clean up, whatever it wants
             to do. */
          err = svn_vcdiff_parse (digger->vcdiff_parser, NULL, 0);
          if (err)
            signal_expat_bailout (err, digger);
        }
    }


  /* EVENT: when we get a </set>, send off the prop-delta. */
  if (strcmp (name, "set") == 0)
    {
      err = do_prop_delta_callback (digger);
      if (err)
        signal_expat_bailout (err, digger);
    }

  /* EVENT: when we get a prop-delta </delete>, send it off. */
  if (digger->stack->previous)
    if ( (strcmp (name, "delete") == 0)
         && (digger->stack->previous->tag == svn_delta__XML_propdelta) )
      {
        err = do_prop_delta_callback (digger);
        if (err)
          signal_expat_bailout (err, digger);
      }


  /* After checking for above events, do the stackframe removal. */

  /* Lose the pointer to the youngest frame. */
  if (youngest_frame->previous) {
    digger->stack = youngest_frame->previous;
    digger->stack->next = NULL;
  }
  else
    digger->stack = NULL;  /* we just removed the last frame */
  

  /* This is a void expat callback, don't return anything. */
}




/* Callback: called whenever expat finds data _between_ an open/close
   tagpair. */
static void 
xml_handle_data (void *userData, const char *data, int len)
{
  apr_size_t length = (apr_size_t) len;

  /* Resurrect digger structure */
  svn_xml__digger_t *digger = (svn_xml__digger_t *) userData;

  /* Figure out the context of this callback.  If we're currently
     inside a <text-delta> or <prop-delta>, that's great.  If not,
     then we've got some erroneous data flying around our XML, and we
     should return an error. */

  svn_xml__stackframe_t *youngest_frame = digger->stack;

  if (youngest_frame == NULL) {
    svn_error_t *err = svn_create_error (SVN_ERR_MALFORMED_XML, 0,
                                         "xml_handle_data: no XML context!",
                                         NULL, digger->pool);
    signal_expat_bailout (err, digger);
    return;
  }

  if (youngest_frame->tag == svn_delta__XML_textdelta)
    {
      svn_error_t *err;
      
      /* Check that we have a vcdiff parser to deal with this data. */
      if (! digger->vcdiff_parser)
        return;

      /* Pass the data to our current vcdiff parser.  When the vcdiff
         parser has received enough bytes to make a "window", it
         pushes the window to the uber-caller's own window-consumer
         routine. */
      err = svn_vcdiff_parse (digger->vcdiff_parser, data, length);
      if (err)
        {
          signal_expat_bailout
            (svn_quick_wrap_error
             (err, "xml_handle_data: vcdiff parser choked."),
             digger);
          return;
        }                          
    }

  else if (youngest_frame->tag == svn_delta__XML_set)
    {
      /* We're about to receive some amount of "value" data for a
         prop-delta `set' command.  (The "name" data is already stored
         in the current stackframe's "name" field, since expat gave us
         the whole thing as an XML attribute.) 

         So just append the new data to the current_propdelta's
         "value" buffer.  Easy.
      */

      if (digger->current_propdelta)
        svn_string_appendbytes (digger->current_propdelta->value,
                                data, length,
                                digger->pool);
    }

  else
    {
      /* The data must be outside the bounds of a <text-delta> or a
      <prop-delta> -- so we ignore it.  (It's probably whitespace --
      expat sends us whitespace frequently.) */
    }

  /* This is a void expat callback, don't return anything. */
}


/*------------------------------------------------------------------*/
/* Public interfaces (see svn_delta.h)  */


/* Given a precreated svn_delta_edit_fns_t EDITOR, return a custom xml
   PARSER that will call into it (and feed EDIT_BATON to its
   callbacks.)  Additionally, this XML parser will use BASE_PATH and
   BASE_VERSION as default "context variables" when computing ancestry
   within a tree-delta. */
svn_error_t *
svn_make_xml_parser (svn_xml_parser_t **parser,
                     const svn_delta_edit_fns_t *editor,
                     svn_string_t *base_path, 
                     svn_vernum_t base_version,
                     void *edit_baton,
                     apr_pool_t *pool)
{
  svn_error_t *err;

  svn_xml_parser_t *xmlparser;
  XML_Parser expat_parser;
  svn_xml__digger_t *digger;

  apr_pool_t *main_subpool;
  void *rootdir_baton = NULL;

  /* Create a subpool to contain *everything*  */
  main_subpool = apr_make_sub_pool (pool, NULL);

  /* Fetch the rootdir_baton by calling into the editor */
  if (editor->replace_root) 
    {
      err = (* (editor->replace_root)) (base_path, base_version,
                                        edit_baton, &rootdir_baton);
      if (err)
        return
          svn_quick_wrap_error (err,
                                "svn_make_xml_parser: replace_root failed.");
    }
      

  /* Create a new digger structure and fill it out*/
  digger = apr_pcalloc (main_subpool, sizeof (svn_xml__digger_t));

  digger->pool             = main_subpool;
  digger->stack            = NULL;
  digger->editor           = editor;
  digger->base_path        = base_path;
  digger->base_version     = base_version;
  digger->edit_baton       = edit_baton;
  digger->rootdir_baton    = rootdir_baton;
  digger->dir_baton        = rootdir_baton;
  digger->validation_error = SVN_NO_ERROR;
  digger->vcdiff_parser    = NULL;

  /* Create an expat parser */
  expat_parser = XML_ParserCreate (NULL);

  /* All expat callbacks should receive the digger structure. */
  XML_SetUserData (expat_parser, digger);

  /* Register our local callbacks with the expat parser */
  XML_SetElementHandler (expat_parser,
                         xml_handle_start,
                         xml_handle_end); 
  XML_SetCharacterDataHandler (expat_parser, xml_handle_data);

  /* Store the parser in the digger too, so that our expat callbacks
     can magically set themselves to NULL in the case of an error. */
  digger->expat_parser = expat_parser;

  /* Create a new subversion xml parser and put everything inside it. */
  xmlparser = apr_pcalloc (main_subpool, sizeof (svn_xml_parser_t));
  
  xmlparser->my_pool      = main_subpool;
  xmlparser->expat_parser = expat_parser;
  xmlparser->digger       = digger;

  /* Return goodness. */
  *parser = xmlparser;
  return SVN_NO_ERROR;
}



/* Destroy an svn_xml_parser_t when finished with it. */
void 
svn_free_xml_parser (svn_xml_parser_t *parser)
{
  apr_destroy_pool (parser->my_pool);
}



/* Parse LEN bytes of xml data in BUFFER at SVN_XML_PARSER.  As xml is
   parsed, editor callbacks will be executed.  If this is the final
   parser "push", ISFINAL must be set to true (so that both expat and
   local cleanup can occur.)  */
svn_error_t *
svn_xml_parsebytes (const char *buffer, apr_size_t len, int isFinal, 
                    svn_xml_parser_t *svn_xml_parser)
{
  svn_error_t *err;
  XML_Parser expat_parser = svn_xml_parser->expat_parser;
  apr_pool_t *pool = svn_xml_parser->digger->pool;

  /* Parse the chunk of stream. */
  if (! XML_Parse (expat_parser, buffer, len, isFinal))
    {
      /* Uh oh, expat *itself* choked somehow! */
      err = svn_create_error
        (SVN_ERR_MALFORMED_XML, 0,
         apr_psprintf (pool, "%s at line %d",
                       XML_ErrorString (XML_GetErrorCode (expat_parser)),
                       XML_GetCurrentLineNumber (expat_parser)),
         NULL, pool);

      /* Kill the expat parser and return its error */
      XML_ParserFree (expat_parser);      
      return err;
    }

  if (isFinal && svn_xml_parser->digger->editor->close_edit)
    {
      err = (* (svn_xml_parser->digger->editor->close_edit))
        (svn_xml_parser->digger->edit_baton,
         svn_xml_parser->digger->rootdir_baton);

      if (err)
        return err;        
    }
  
  /* After parsing our chunk, check to see if any editor callback did
     a signal_expat_bailout() */
  if (svn_xml_parser->digger->validation_error)
      return svn_xml_parser->digger->validation_error;

  return SVN_NO_ERROR;
}



/* Reads an XML stream from SOURCE_FN using expat internally,
  validating the XML as it goes (according to Subversion's own
  tree-delta DTD).  Whenever an interesting event happens, it calls a
  caller-specified callback routine from EDITOR.  

  Once called, it retains control and "pulls" data from SOURCE_FN
  until either the stream runs out or it encounters an error. */
svn_error_t *
svn_xml_auto_parse (svn_read_fn_t *source_fn,
                    void *source_baton,
                    const svn_delta_edit_fns_t *editor,
                    svn_string_t *base_path,
                    svn_vernum_t base_version,
                    void *edit_baton,
                    apr_pool_t *pool)
{
  char buf[BUFSIZ];
  apr_size_t len;
  int done;
  svn_error_t *err;
  svn_xml_parser_t *xmlparser;

  /* Create a custom Subversion XML parser */
  err =  svn_make_xml_parser (&xmlparser,
                              editor,
                              base_path,
                              base_version,
                              edit_baton,
                              pool);
  if (err)
    return err;

  /* Repeatedly pull data from SOURCE_FN and feed it to the parser,
     until there's no more data (or we get an error). */
  
  do {
    /* Read BUFSIZ bytes into buf using the supplied read function. */
    len = BUFSIZ;
    err = (*(source_fn)) (source_baton, buf, &len, pool);
    if (err)
      return svn_quick_wrap_error (err, "svn_delta_parse: can't read source");

    /* How many bytes were actually read into buf?  According to the
       definition of an svn_read_fn_t, we should keep reading
       until the reader function says that 0 bytes were read. */
    done = (len == 0);

    /* Push these bytes at the parser */
    err = svn_xml_parsebytes (buf, len, done, xmlparser);
    if (err)
      return err;

  } while (! done);

  svn_free_xml_parser (xmlparser);
  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
