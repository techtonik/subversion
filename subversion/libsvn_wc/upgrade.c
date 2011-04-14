/*
 * upgrade.c:  routines for upgrading a working copy
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

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"

#include "wc.h"
#include "adm_files.h"
#include "entries.h"
#include "wc_db.h"
#include "tree_conflicts.h"
#include "wc-queries.h"  /* for STMT_*  */
#include "workqueue.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
#include "private/svn_sqlite.h"
#include "private/svn_token.h"

/* WC-1.0 administrative area extensions */
#define SVN_WC__BASE_EXT      ".svn-base" /* for text and prop bases */
#define SVN_WC__WORK_EXT      ".svn-work" /* for working propfiles */
#define SVN_WC__REVERT_EXT    ".svn-revert" /* for reverting a replaced
                                               file */

/* Old locations for storing "wcprops" (aka "dav cache").  */
#define WCPROPS_SUBDIR_FOR_FILES "wcprops"
#define WCPROPS_FNAME_FOR_DIR "dir-wcprops"
#define WCPROPS_ALL_DATA "all-wcprops"

/* Old property locations. */
#define PROPS_SUBDIR "props"
#define PROP_BASE_SUBDIR "prop-base"
#define PROP_BASE_FOR_DIR "dir-prop-base"
#define PROP_REVERT_FOR_DIR "dir-prop-revert"
#define PROP_WORKING_FOR_DIR "dir-props"

/* Old textbase location. */
#define TEXT_BASE_SUBDIR "text-base"

#define TEMP_DIR "tmp"

/* Old data files that we no longer need/use.  */
#define ADM_README "README.txt"
#define ADM_EMPTY_FILE "empty-file"
#define ADM_LOG "log"
#define ADM_LOCK "lock"

/* New pristine location */
#define PRISTINE_STORAGE_RELPATH "pristine"
#define SDB_FILE  "wc.db"


/* Read the properties from the file at PROPFILE_ABSPATH, returning them
   as a hash in *PROPS. If the propfile is NOT present, then NULL will
   be returned in *PROPS.  */
static svn_error_t *
read_propfile(apr_hash_t **props,
              const char *propfile_abspath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_stream_t *stream;
  apr_finfo_t finfo;

  err = svn_io_stat(&finfo, propfile_abspath, APR_FINFO_SIZE, scratch_pool);

  if (err
      && (APR_STATUS_IS_ENOENT(err->apr_err)
          || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err)))
    {
      svn_error_clear(err);

      /* The propfile was not there. Signal with a NULL.  */
      *props = NULL;
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  /* A 0-bytes file signals an empty property list.
     (mostly used for revert-props) */
  if (finfo.size == 0)
    {
      *props = apr_hash_make(result_pool);
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_stream_open_readonly(&stream, propfile_abspath,
                                   scratch_pool, scratch_pool));

  /* ### does this function need to be smarter? will we see zero-length
     ### files? see props.c::load_props(). there may be more work here.
     ### need a historic analysis of 1.x property storage. what will we
     ### actually run into?  */

  /* ### loggy_write_properties() and immediate_install_props() write
     ### zero-length files for "no props", so we should be a bit smarter
     ### in here.  */

  /* ### should we be forgiving in here? I say "no". if we can't be sure,
     ### then we could effectively corrupt the local working copy.  */

  *props = apr_hash_make(result_pool);
  SVN_ERR(svn_hash_read2(*props, stream, SVN_HASH_TERMINATOR, result_pool));

  return svn_error_return(svn_stream_close(stream));
}


/* Read one proplist (allocated from RESULT_POOL) from STREAM, and place it
   into ALL_WCPROPS at NAME.  */
static svn_error_t *
read_one_proplist(apr_hash_t *all_wcprops,
                  const char *name,
                  svn_stream_t *stream,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  apr_hash_t *proplist;

  proplist = apr_hash_make(result_pool);
  SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, result_pool));
  apr_hash_set(all_wcprops, name, APR_HASH_KEY_STRING, proplist);

  return SVN_NO_ERROR;
}


/* Read the wcprops from all the files in the admin area of DIR_ABSPATH,
   returning them in *ALL_WCPROPS. Results are allocated in RESULT_POOL,
   and temporary allocations are performed in SCRATCH_POOL.  */
static svn_error_t *
read_many_wcprops(apr_hash_t **all_wcprops,
                  const char *dir_abspath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  const char *propfile_abspath;
  apr_hash_t *wcprops;
  apr_hash_t *dirents;
  const char *props_dir_abspath;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  *all_wcprops = apr_hash_make(result_pool);

  /* First, look at dir-wcprops. */
  propfile_abspath = svn_wc__adm_child(dir_abspath, WCPROPS_FNAME_FOR_DIR,
                                       scratch_pool);
  SVN_ERR(read_propfile(&wcprops, propfile_abspath, result_pool, iterpool));
  if (wcprops != NULL)
    apr_hash_set(*all_wcprops, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING,
                 wcprops);

  props_dir_abspath = svn_wc__adm_child(dir_abspath, WCPROPS_SUBDIR_FOR_FILES,
                                        scratch_pool);

  /* Now walk the wcprops directory. */
  SVN_ERR(svn_io_get_dirents3(&dirents, props_dir_abspath, TRUE,
                              scratch_pool, scratch_pool));

  for (hi = apr_hash_first(scratch_pool, dirents);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);

      svn_pool_clear(iterpool);

      propfile_abspath = svn_dirent_join(props_dir_abspath, name, iterpool);

      SVN_ERR(read_propfile(&wcprops, propfile_abspath,
                            result_pool, iterpool));
      SVN_ERR_ASSERT(wcprops != NULL);
      apr_hash_set(*all_wcprops,
                   apr_pstrdup(result_pool, name), APR_HASH_KEY_STRING,
                   wcprops);
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


/* For wcprops stored in a single file in this working copy, read that
   file and return it in *ALL_WCPROPS, allocated in RESULT_POOL.   Use
   SCRATCH_POOL for temporary allocations. */
static svn_error_t *
read_wcprops(apr_hash_t **all_wcprops,
             const char *dir_abspath,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;
  svn_error_t *err;

  *all_wcprops = apr_hash_make(result_pool);

  err = svn_wc__open_adm_stream(&stream, dir_abspath,
                                WCPROPS_ALL_DATA,
                                scratch_pool, scratch_pool);

  /* A non-existent file means there are no props. */
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  /* Read the proplist for THIS_DIR. */
  SVN_ERR(read_one_proplist(*all_wcprops, SVN_WC_ENTRY_THIS_DIR, stream,
                            result_pool, scratch_pool));

  /* And now, the children. */
  while (1729)
    {
      svn_stringbuf_t *line;
      svn_boolean_t eof;

      SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, result_pool));
      if (eof)
        {
          if (line->len > 0)
            return svn_error_createf
              (SVN_ERR_WC_CORRUPT, NULL,
               _("Missing end of line in wcprops file for '%s'"),
               svn_dirent_local_style(dir_abspath, scratch_pool));
          break;
        }
      SVN_ERR(read_one_proplist(*all_wcprops, line->data, stream,
                                result_pool, scratch_pool));
    }

  return svn_error_return(svn_stream_close(stream));
}


/* If the versioned child (which should be a directory) exists on disk as
   an actual directory, then add it to the array of subdirs.  */
static svn_error_t *
maybe_add_subdir(apr_array_header_t *subdirs,
                 const char *dir_abspath,
                 const char *child_name,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  const char *child_abspath = svn_dirent_join(dir_abspath, child_name,
                                              scratch_pool);
  svn_node_kind_t kind;

  SVN_ERR(svn_io_check_path(child_abspath, &kind, scratch_pool));
  if (kind == svn_node_dir)
    {
      APR_ARRAY_PUSH(subdirs, const char *) = apr_pstrdup(result_pool,
                                                          child_abspath);
    }

  return SVN_NO_ERROR;
}


/* Return in CHILDREN, the list of all 1.6 versioned subdirectories
   which also exist on disk as directories.  */
static svn_error_t *
get_versioned_subdirs(apr_array_header_t **children,
                      const char *dir_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;

  *children = apr_array_make(result_pool, 10, sizeof(const char *));

  SVN_ERR(svn_wc__read_entries_old(&entries, dir_abspath,
                                   scratch_pool, iterpool));
  for (hi = apr_hash_first(scratch_pool, entries);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);

      /* skip "this dir"  */
      if (*name == '\0')
        continue;

      svn_pool_clear(iterpool);

      SVN_ERR(maybe_add_subdir(*children, dir_abspath, name,
                               result_pool, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Return in CHILDREN the names of all versioned *files* in SDB that
   are children of PARENT_RELPATH.  These files' existence on disk is
   not tested.

   This set of children is intended for property upgrades.
   Subdirectory's properties exist in the subdirs.

   Note that this uses just the SDB to locate children, which means
   that the children must have been upgraded to wc-ng format. */
static svn_error_t *
get_versioned_files(const apr_array_header_t **children,
                    const char *parent_relpath,
                    svn_sqlite__db_t *sdb,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  apr_array_header_t *child_names;
  svn_boolean_t have_row;

  /* ### just select 'file' children. do we need 'symlink' in the future?  */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_ALL_FILES));
  SVN_ERR(svn_sqlite__bindf(stmt, "s", parent_relpath));

  /* ### 10 is based on Subversion's average of 8.5 files per versioned
     ### directory in its repository. maybe use a different value? or
     ### count rows first?  */
  child_names = apr_array_make(result_pool, 10, sizeof(const char *));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *local_relpath = svn_sqlite__column_text(stmt, 0,
                                                          result_pool);

      APR_ARRAY_PUSH(child_names, const char *)
        = svn_relpath_basename(local_relpath, result_pool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  *children = child_names;

  return svn_error_return(svn_sqlite__reset(stmt));
}


/* Return the path of the old-school administrative lock file
   associated with LOCAL_DIR_ABSPATH, allocated from RESULT_POOL. */
static const char *
build_lockfile_path(const char *local_dir_abspath,
                    apr_pool_t *result_pool)
{
  return svn_dirent_join_many(result_pool,
                              local_dir_abspath,
                              svn_wc_get_adm_dir(result_pool),
                              ADM_LOCK,
                              NULL);
}


/* Create a physical lock file in the admin directory for ABSPATH.  */
static svn_error_t *
create_physical_lock(const char *abspath, apr_pool_t *scratch_pool)
{
  const char *lock_abspath = build_lockfile_path(abspath, scratch_pool);
  svn_error_t *err;
  apr_file_t *file;

  err = svn_io_file_open(&file, lock_abspath,
                         APR_WRITE | APR_CREATE | APR_EXCL,
                         APR_OS_DEFAULT,
                         scratch_pool);

  if (err && APR_STATUS_IS_EEXIST(err->apr_err))
    {
      /* Congratulations, we just stole a physical lock from somebody */
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  return svn_error_return(err);
}


/* Wipe out all the obsolete files/dirs from the administrative area.  */
static void
wipe_obsolete_files(const char *wcroot_abspath, apr_pool_t *scratch_pool)
{
  /* Zap unused files.  */
  svn_error_clear(svn_io_remove_file2(
                    svn_wc__adm_child(wcroot_abspath,
                                      SVN_WC__ADM_FORMAT,
                                      scratch_pool),
                    TRUE, scratch_pool));
  svn_error_clear(svn_io_remove_file2(
                    svn_wc__adm_child(wcroot_abspath,
                                      SVN_WC__ADM_ENTRIES,
                                      scratch_pool),
                    TRUE, scratch_pool));
  svn_error_clear(svn_io_remove_file2(
                    svn_wc__adm_child(wcroot_abspath,
                                      ADM_EMPTY_FILE,
                                      scratch_pool),
                    TRUE, scratch_pool));
  svn_error_clear(svn_io_remove_file2(
                    svn_wc__adm_child(wcroot_abspath,
                                      ADM_README,
                                      scratch_pool),
                    TRUE, scratch_pool));

  /* For formats <= SVN_WC__WCPROPS_MANY_FILES_VERSION, we toss the wcprops
     for the directory itself, and then all the wcprops for the files.  */
  svn_error_clear(svn_io_remove_file2(
                    svn_wc__adm_child(wcroot_abspath,
                                      WCPROPS_FNAME_FOR_DIR,
                                      scratch_pool),
                    TRUE, scratch_pool));
  svn_error_clear(svn_io_remove_dir2(
                    svn_wc__adm_child(wcroot_abspath,
                                      WCPROPS_SUBDIR_FOR_FILES,
                                      scratch_pool),
                    FALSE, NULL, NULL, scratch_pool));

  /* And for later formats, they are aggregated into one file.  */
  svn_error_clear(svn_io_remove_file2(
                    svn_wc__adm_child(wcroot_abspath,
                                      WCPROPS_ALL_DATA,
                                      scratch_pool),
                    TRUE, scratch_pool));

  /* Remove the old text-base directory and the old text-base files. */
  svn_error_clear(svn_io_remove_dir2(
                    svn_wc__adm_child(wcroot_abspath,
                                      TEXT_BASE_SUBDIR,
                                      scratch_pool),
                    FALSE, NULL, NULL, scratch_pool));

  /* Remove the old properties files... whole directories at a time.  */
  svn_error_clear(svn_io_remove_dir2(
                    svn_wc__adm_child(wcroot_abspath,
                                      PROPS_SUBDIR,
                                      scratch_pool),
                    FALSE, NULL, NULL, scratch_pool));
  svn_error_clear(svn_io_remove_dir2(
                    svn_wc__adm_child(wcroot_abspath,
                                      PROP_BASE_SUBDIR,
                                      scratch_pool),
                    FALSE, NULL, NULL, scratch_pool));
  svn_error_clear(svn_io_remove_file2(
                     svn_wc__adm_child(wcroot_abspath,
                                       PROP_WORKING_FOR_DIR,
                                       scratch_pool),
                     TRUE, scratch_pool));
  svn_error_clear(svn_io_remove_file2(
                     svn_wc__adm_child(wcroot_abspath,
                                      PROP_BASE_FOR_DIR,
                                      scratch_pool),
                     TRUE, scratch_pool));
  svn_error_clear(svn_io_remove_file2(
                     svn_wc__adm_child(wcroot_abspath,
                                      PROP_REVERT_FOR_DIR,
                                      scratch_pool),
                     TRUE, scratch_pool));

#if 0
  /* ### this checks for a write-lock, and we are not (always) taking out
     ### a write lock in all callers.  */
  SVN_ERR(svn_wc__adm_cleanup_tmp_area(db, wcroot_abspath, iterpool));
#endif

  /* Remove the old-style lock file LAST.  */
  svn_error_clear(svn_io_remove_file2(
                    build_lockfile_path(wcroot_abspath, scratch_pool),
                    TRUE, scratch_pool));
}

svn_error_t *
svn_wc__wipe_postupgrade(const char *dir_abspath,
                         svn_boolean_t whole_admin,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *subdirs;
  svn_error_t *err;
  int i;

  if (cancel_func)
    SVN_ERR((*cancel_func)(cancel_baton));

  err = get_versioned_subdirs(&subdirs, dir_abspath, scratch_pool, iterpool);
  if (err)
    {
      if (APR_STATUS_IS_ENOENT(err->apr_err))
        {
          /* An unversioned dir is obstructing a versioned dir */
          svn_error_clear(err);
          err = NULL;
        }
      svn_pool_destroy(iterpool);
      return err;
    }
  for (i = 0; i < subdirs->nelts; ++i)
    {
      const char *child_abspath = APR_ARRAY_IDX(subdirs, i, const char *);

      svn_pool_clear(iterpool);
      SVN_ERR(svn_wc__wipe_postupgrade(child_abspath, TRUE,
                                       cancel_func, cancel_baton, iterpool));
    }

  /* ### Should we really be ignoring errors here? */
  if (whole_admin)
    svn_error_clear(svn_io_remove_dir2(svn_wc__adm_child(dir_abspath, "",
                                                         iterpool),
                                       TRUE, NULL, NULL, iterpool));
  else
    wipe_obsolete_files(dir_abspath, scratch_pool);

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Ensure that ENTRY has its REPOS and UUID fields set. These will be
   used to establish the REPOSITORY row in the new database, and then
   used within the upgraded entries as they are written into the database.

   If one or both are not available, then it attempts to retrieve this
   information from REPOS_CACHE. And if that fails from REPOS_INFO_FUNC,
   passing REPOS_INFO_BATON.
   Returns a user understandable error using LOCAL_ABSPATH if the
   information cannot be obtained.  */
static svn_error_t *
ensure_repos_info(svn_wc_entry_t *entry,
                  const char *local_abspath,
                  svn_wc_upgrade_get_repos_info_t repos_info_func,
                  void *repos_info_baton,
                  apr_hash_t *repos_cache,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  /* Easy exit.  */
  if (entry->repos != NULL && entry->uuid != NULL)
    return SVN_NO_ERROR;

  if ((entry->repos == NULL || entry->uuid == NULL)
      && entry->url)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, repos_cache);
           hi; hi = apr_hash_next(hi))
        {
          if (svn_uri_is_child(svn__apr_hash_index_key(hi), entry->url,
                               scratch_pool))
            {
              if (!entry->repos)
                entry->repos = svn__apr_hash_index_key(hi);

              if (!entry->uuid)
                entry->uuid = svn__apr_hash_index_val(hi);

              return SVN_NO_ERROR;
            }
        }
    }

  if (entry->repos == NULL && repos_info_func == NULL)
    return svn_error_createf(
        SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
        _("Working copy '%s' can't be upgraded because the repository root is "
          "not available and can't be retrieved"),
        svn_dirent_local_style(local_abspath, scratch_pool));

  if (entry->uuid == NULL && repos_info_func == NULL)
    return svn_error_createf(
        SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
        _("Working copy '%s' can't be upgraded because the repository uuid is "
          "not available and can't be retrieved"),
        svn_dirent_local_style(local_abspath, scratch_pool));

   if (entry->url == NULL)
     return svn_error_createf(
        SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
        _("Working copy '%s' can't be upgraded because it doesn't have a url"),
        svn_dirent_local_style(local_abspath, scratch_pool));

   return svn_error_return((*repos_info_func)(&entry->repos, &entry->uuid,
                                              repos_info_baton,
                                              entry->url,
                                              result_pool, scratch_pool));
}


/*
 * Read tree conflict descriptions from @a conflict_data.  Set @a *conflicts
 * to a hash of pointers to svn_wc_conflict_description2_t objects indexed by
 * svn_wc_conflict_description2_t.local_abspath, all newly allocated in @a
 * pool.  @a dir_path is the path to the working copy directory whose conflicts
 * are being read.  The conflicts read are the tree conflicts on the immediate
 * child nodes of @a dir_path.  Do all allocations in @a pool.
 *
 * Note: There were some concerns about this function:
 *
 * ### this is BAD. the CONFLICTS structure should not be dependent upon
 * ### DIR_PATH. each conflict should be labeled with an entry name, not
 * ### a whole path. (and a path which happens to vary based upon invocation
 * ### of the user client and these APIs)
 *
 * those assumptions were baked into former versions of the data model, so
 * they have to stick around here.  But they have been removed from the
 * New Way. */
static svn_error_t *
read_tree_conflicts(apr_hash_t **conflicts,
                    const char *conflict_data,
                    const char *dir_path,
                    apr_pool_t *pool)
{
  const svn_skel_t *skel;
  apr_pool_t *iterpool;

  *conflicts = apr_hash_make(pool);

  if (conflict_data == NULL)
    return SVN_NO_ERROR;

  skel = svn_skel__parse(conflict_data, strlen(conflict_data), pool);
  if (skel == NULL)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Error parsing tree conflict skel"));

  iterpool = svn_pool_create(pool);
  for (skel = skel->children; skel != NULL; skel = skel->next)
    {
      const svn_wc_conflict_description2_t *conflict;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_wc__deserialize_conflict(&conflict, skel, dir_path,
                                           pool, iterpool));
      if (conflict != NULL)
        apr_hash_set(*conflicts, svn_dirent_basename(conflict->local_abspath,
                                                     pool),
                     APR_HASH_KEY_STRING, conflict);
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
migrate_single_tree_conflict_data(svn_sqlite__db_t *sdb,
                                  const char *tree_conflict_data,
                                  apr_int64_t wc_id,
                                  const char *local_relpath,
                                  apr_pool_t *scratch_pool)
{
  apr_hash_t *conflicts;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  SVN_ERR(read_tree_conflicts(&conflicts, tree_conflict_data, local_relpath,
                              scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, conflicts);
       hi;
       hi = apr_hash_next(hi))
    {
      const svn_wc_conflict_description2_t *conflict =
          svn__apr_hash_index_val(hi);
      const char *conflict_relpath;
      const char *conflict_data;
      svn_sqlite__stmt_t *stmt;
      svn_boolean_t have_row;
      svn_skel_t *skel;

      svn_pool_clear(iterpool);

      conflict_relpath = svn_dirent_join(local_relpath,
                                         svn_dirent_basename(
                                           conflict->local_abspath, iterpool),
                                         iterpool);

      SVN_ERR(svn_wc__serialize_conflict(&skel, conflict, iterpool, iterpool));
      conflict_data = svn_skel__unparse(skel, iterpool)->data;

      /* See if we need to update or insert an ACTUAL node. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_ACTUAL_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, conflict_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      SVN_ERR(svn_sqlite__reset(stmt));

      if (have_row)
        {
          /* There is an existing ACTUAL row, so just update it. */
          SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                            STMT_UPDATE_ACTUAL_CONFLICT_DATA));
        }
      else
        {
          /* We need to insert an ACTUAL row with the tree conflict data. */
          SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                            STMT_INSERT_ACTUAL_CONFLICT_DATA));
        }

      SVN_ERR(svn_sqlite__bindf(stmt, "iss", wc_id, conflict_relpath,
                                conflict_data));
      if (!have_row)
        SVN_ERR(svn_sqlite__bind_text(stmt, 4, local_relpath));

      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
migrate_tree_conflict_data(svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Iterate over each node which has a set of tree conflicts, then insert
     all of them into the new schema.  */

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_SELECT_OLD_TREE_CONFLICT));

  /* Get all the existing tree conflict data. */
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      apr_int64_t wc_id;
      const char *local_relpath;
      const char *tree_conflict_data;

      svn_pool_clear(iterpool);

      wc_id = svn_sqlite__column_int64(stmt, 0);
      local_relpath = svn_sqlite__column_text(stmt, 1, iterpool);
      tree_conflict_data = svn_sqlite__column_text(stmt, 2, iterpool);

      SVN_ERR(migrate_single_tree_conflict_data(sdb, tree_conflict_data,
                                                wc_id, local_relpath,
                                                iterpool));

      /* We don't need to do anything but step over the previously
         prepared statement. */
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  /* Erase all the old tree conflict data.  */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_ERASE_OLD_CONFLICTS));
  SVN_ERR(svn_sqlite__step_done(stmt));

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


struct bump_baton {
  const char *wcroot_abspath;
};

/* Migrate the properties for one node (LOCAL_ABSPATH).  */
static svn_error_t *
migrate_node_props(const char *dir_abspath,
                   const char *new_wcroot_abspath,
                   const char *name,
                   svn_sqlite__db_t *sdb,
                   int original_format,
                   apr_pool_t *scratch_pool)
{
  const char *base_abspath;  /* old name. nowadays: "pristine"  */
  const char *revert_abspath;  /* old name. nowadays: "BASE"  */
  const char *working_abspath;  /* old name. nowadays: "ACTUAL"  */
  apr_hash_t *base_props;
  apr_hash_t *revert_props;
  apr_hash_t *working_props;
  const char *old_wcroot_abspath
    = svn_dirent_get_longest_ancestor(dir_abspath, new_wcroot_abspath,
                                      scratch_pool);
  const char *dir_relpath = svn_dirent_skip_ancestor(old_wcroot_abspath,
                                                     dir_abspath);

  if (*name == '\0')
    {
      base_abspath = svn_wc__adm_child(dir_abspath,
                                       PROP_BASE_FOR_DIR, scratch_pool);
      revert_abspath = svn_wc__adm_child(dir_abspath,
                                         PROP_REVERT_FOR_DIR, scratch_pool);
      working_abspath = svn_wc__adm_child(dir_abspath,
                                          PROP_WORKING_FOR_DIR, scratch_pool);
    }
  else
    {
      const char *basedir_abspath;
      const char *propsdir_abspath;

      propsdir_abspath = svn_wc__adm_child(dir_abspath, PROPS_SUBDIR,
                                           scratch_pool);
      basedir_abspath = svn_wc__adm_child(dir_abspath, PROP_BASE_SUBDIR,
                                          scratch_pool);

      base_abspath = svn_dirent_join(basedir_abspath,
                                     apr_pstrcat(scratch_pool,
                                                 name,
                                                 SVN_WC__BASE_EXT,
                                                 (char *)NULL),
                                     scratch_pool);

      revert_abspath = svn_dirent_join(basedir_abspath,
                                       apr_pstrcat(scratch_pool,
                                                   name,
                                                   SVN_WC__REVERT_EXT,
                                                   (char *)NULL),
                                       scratch_pool);

      working_abspath = svn_dirent_join(propsdir_abspath,
                                        apr_pstrcat(scratch_pool,
                                                    name,
                                                    SVN_WC__WORK_EXT,
                                                    (char *)NULL),
                                        scratch_pool);
    }

  SVN_ERR(read_propfile(&base_props, base_abspath,
                        scratch_pool, scratch_pool));
  SVN_ERR(read_propfile(&revert_props, revert_abspath,
                        scratch_pool, scratch_pool));
  SVN_ERR(read_propfile(&working_props, working_abspath,
                        scratch_pool, scratch_pool));

  return svn_error_return(svn_wc__db_upgrade_apply_props(
                            sdb, new_wcroot_abspath,
                            svn_relpath_join(dir_relpath, name, scratch_pool),
                            base_props, revert_props, working_props,
                            original_format,
                            scratch_pool));
}


/* */
static svn_error_t *
migrate_props(const char *dir_abspath,
              const char *new_wcroot_abspath,
              svn_sqlite__db_t *sdb,
              int original_format,
              apr_pool_t *scratch_pool)
{
  /* General logic here: iterate over all the immediate children of the root
     (since we aren't yet in a centralized system), and for any properties that
     exist, map them as follows:

     if (revert props exist):
       revert  -> BASE
       base    -> WORKING
       working -> ACTUAL
     else if (prop pristine is working [as defined in props.c] ):
       base    -> WORKING
       working -> ACTUAL
     else:
       base    -> BASE
       working -> ACTUAL

     ### the middle "test" should simply look for a WORKING_NODE row

     Note that it is legal for "working" props to be missing. That implies
     no local changes to the properties.
  */
  const apr_array_header_t *children;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  const char *old_wcroot_abspath
    = svn_dirent_get_longest_ancestor(dir_abspath, new_wcroot_abspath,
                                      scratch_pool);
  const char *dir_relpath = svn_dirent_skip_ancestor(old_wcroot_abspath,
                                                     dir_abspath);
  int i;

  /* Migrate the props for "this dir".  */
  SVN_ERR(migrate_node_props(dir_abspath, new_wcroot_abspath, "", sdb,
                             original_format, iterpool));

  /* Iterate over all the files in this SDB.  */
  SVN_ERR(get_versioned_files(&children, dir_relpath, sdb, scratch_pool,
                              iterpool));
  for (i = 0; i < children->nelts; i++)
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);

      svn_pool_clear(iterpool);

      SVN_ERR(migrate_node_props(dir_abspath, new_wcroot_abspath,
                                 name, sdb, original_format, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* If STR ends with SUFFIX and is longer than SUFFIX, return the part of
 * STR that comes before SUFFIX; else return NULL. */
static char *
remove_suffix(const char *str, const char *suffix, apr_pool_t *result_pool)
{
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);

  if (str_len > suffix_len
      && strcmp(str + str_len - suffix_len, suffix) == 0)
    {
      return apr_pstrmemdup(result_pool, str, str_len - suffix_len);
    }

  return NULL;
}

/* Copy all the text-base files from the administrative area of WC directory
   DIR_ABSPATH into the pristine store of SDB which is located in directory
   NEW_WCROOT_ABSPATH.

   Set *TEXT_BASES_INFO to a new hash, allocated in RESULT_POOL, that maps
   (const char *) name of the versioned file to (svn_wc__text_base_info_t *)
   information about the pristine text. */
static svn_error_t *
migrate_text_bases(apr_hash_t **text_bases_info,
                   const char *dir_abspath,
                   const char *new_wcroot_abspath,
                   svn_sqlite__db_t *sdb,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  apr_hash_t *dirents;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;
  const char *text_base_dir = svn_wc__adm_child(dir_abspath,
                                                TEXT_BASE_SUBDIR,
                                                scratch_pool);

  *text_bases_info = apr_hash_make(result_pool);

  /* Iterate over the text-base files */
  SVN_ERR(svn_io_get_dirents3(&dirents, text_base_dir, TRUE,
                              scratch_pool, scratch_pool));
  for (hi = apr_hash_first(scratch_pool, dirents); hi;
       hi = apr_hash_next(hi))
    {
      const char *text_base_basename = svn__apr_hash_index_key(hi);
      svn_checksum_t *md5_checksum;
      svn_checksum_t *sha1_checksum;

      svn_pool_clear(iterpool);

      /* Calculate its checksums and copy it to the pristine store */
      {
        const char *pristine_path;
        const char *text_base_path;
        svn_sqlite__stmt_t *stmt;
        apr_finfo_t finfo;

        text_base_path = svn_dirent_join(text_base_dir, text_base_basename,
                                         iterpool);

        /* ### This code could be a bit smarter: we could chain checksum
               streams instead of reading the file twice; we could check to
               see if a pristine row exists before attempting to insert one;
               we could check and see if a pristine file exists before
               attempting to copy a new one over it.

               However, I think simplicity is the big win here, especially since
               this is code that runs exactly once on a user's machine: when
               doing the upgrade.  If you disagree, feel free to add the
               complexity. :)  */

        /* Gather the two checksums. */
        SVN_ERR(svn_io_file_checksum2(&md5_checksum, text_base_path,
                                      svn_checksum_md5, iterpool));
        SVN_ERR(svn_io_file_checksum2(&sha1_checksum, text_base_path,
                                      svn_checksum_sha1, iterpool));

        SVN_ERR(svn_io_stat(&finfo, text_base_path, APR_FINFO_SIZE, iterpool));

        /* Insert a row into the pristine table. */
        SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                          STMT_INSERT_OR_IGNORE_PRISTINE));
        SVN_ERR(svn_sqlite__bind_checksum(stmt, 1, sha1_checksum, iterpool));
        SVN_ERR(svn_sqlite__bind_checksum(stmt, 2, md5_checksum, iterpool));
        SVN_ERR(svn_sqlite__bind_int64(stmt, 3, finfo.size));
        SVN_ERR(svn_sqlite__insert(NULL, stmt));

        SVN_ERR(svn_wc__db_pristine_get_future_path(&pristine_path,
                                                    new_wcroot_abspath,
                                                    sha1_checksum,
                                                    iterpool, iterpool));

        /* Ensure any sharding directories exist. */
        SVN_ERR(svn_wc__ensure_directory(svn_dirent_dirname(pristine_path,
                                                            iterpool),
                                         iterpool));

        /* Copy, rather than move, so that the upgrade can be restarted.
           It could be moved if upgrades scanned for files in the
           pristine directory as well as the text-base directory. */
        SVN_ERR(svn_io_copy_file(text_base_path, pristine_path, TRUE,
                                 iterpool));
      }

      /* Add the checksums for this text-base to *TEXT_BASES_INFO. */
      {
        const char *versioned_file_name;
        svn_boolean_t is_revert_base;
        svn_wc__text_base_info_t *info;
        svn_wc__text_base_file_info_t *file_info;

        /* Determine the versioned file name and whether this is a normal base
         * or a revert base. */
        versioned_file_name = remove_suffix(text_base_basename,
                                            SVN_WC__REVERT_EXT, result_pool);
        if (versioned_file_name)
          {
            is_revert_base = TRUE;
          }
        else
          {
            versioned_file_name = remove_suffix(text_base_basename,
                                                SVN_WC__BASE_EXT, result_pool);
            is_revert_base = FALSE;
          }

        /* Create a new info struct for this versioned file, or fill in the
         * existing one if this is the second text-base we've found for it. */
        info = apr_hash_get(*text_bases_info, versioned_file_name,
                            APR_HASH_KEY_STRING);
        if (info == NULL)
          info = apr_pcalloc(result_pool, sizeof (*info));
        file_info = (is_revert_base ? &info->revert_base : &info->normal_base);

        file_info->sha1_checksum = svn_checksum_dup(sha1_checksum, result_pool);
        file_info->md5_checksum = svn_checksum_dup(md5_checksum, result_pool);
        apr_hash_set(*text_bases_info, versioned_file_name, APR_HASH_KEY_STRING,
                     info);
      }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
bump_to_20(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_sqlite__exec_statements(sdb, STMT_CREATE_NODES));
  SVN_ERR(svn_sqlite__exec_statements(sdb, STMT_UPGRADE_TO_20));
  return SVN_NO_ERROR;
}

static svn_error_t *
bump_to_21(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_sqlite__exec_statements(sdb, STMT_UPGRADE_TO_21));
  SVN_ERR(migrate_tree_conflict_data(sdb, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
bump_to_22(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_sqlite__exec_statements(sdb, STMT_UPGRADE_TO_22));
  return SVN_NO_ERROR;
}

static svn_error_t *
bump_to_23(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  const char *wcroot_abspath = ((struct bump_baton *)baton)->wcroot_abspath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_HAS_WORKING_NODES));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));
  if (have_row)
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("The working copy at '%s' is format 22 with "
                               "WORKING nodes; use a format 22 client to "
                               "diff/revert before using this client"),
                             wcroot_abspath);

  SVN_ERR(svn_sqlite__exec_statements(sdb, STMT_UPGRADE_TO_23));
  return SVN_NO_ERROR;
}

static svn_error_t *
bump_to_24(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_sqlite__exec_statements(sdb, STMT_UPGRADE_TO_24));
  SVN_ERR(svn_sqlite__exec_statements(sdb, STMT_CREATE_NODES_TRIGGERS));
  return SVN_NO_ERROR;
}

static svn_error_t *
bump_to_25(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_sqlite__exec_statements(sdb, STMT_UPGRADE_TO_25));
  return SVN_NO_ERROR;
}

static svn_error_t *
bump_to_26(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_sqlite__exec_statements(sdb, STMT_UPGRADE_TO_26));
  return SVN_NO_ERROR;
}

static svn_error_t *
bump_to_27(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  const char *wcroot_abspath = ((struct bump_baton *)baton)->wcroot_abspath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_HAS_ACTUAL_NODES_CONFLICTS));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));
  if (have_row)
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("The working copy at '%s' is format 26 with "
                               "conflicts; use a format 26 client to resolve "
                               "before using this client"),
                             wcroot_abspath);
  SVN_ERR(svn_sqlite__exec_statements(sdb, STMT_UPGRADE_TO_27));
  return SVN_NO_ERROR;
}


struct upgrade_data_t {
  svn_sqlite__db_t *sdb;
  const char *root_abspath;
  apr_int64_t repos_id;
  apr_int64_t wc_id;
};

/* Upgrade the working copy directory represented by DB/DIR_ABSPATH
   from OLD_FORMAT to the wc-ng format (SVN_WC__WC_NG_VERSION)'.

   Pass REPOS_INFO_FUNC, REPOS_INFO_BATON and REPOS_CACHE to
   ensure_repos_info. Add the found repository root and UUID to
   REPOS_CACHE if it doesn't have a cached entry for this
   repository.

   DATA->SDB will be null if this is the root directory, in which case
   the db must be created and *DATA filled in, otherwise *DATA refer
   to the single root db.

   Uses SCRATCH_POOL for all temporary allocation.  */
static svn_error_t *
upgrade_to_wcng(void **dir_baton,
                void *parent_baton,
                svn_wc__db_t *db,
                const char *dir_abspath,
                int old_format,
                svn_wc_upgrade_get_repos_info_t repos_info_func,
                void *repos_info_baton,
                apr_hash_t *repos_cache,
                struct upgrade_data_t *data,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  const char *logfile_path = svn_wc__adm_child(dir_abspath, ADM_LOG,
                                               scratch_pool);
  svn_node_kind_t logfile_on_disk;
  apr_hash_t *entries;
  svn_wc_entry_t *this_dir;
  const char *old_wcroot_abspath, *dir_relpath;
  apr_hash_t *text_bases_info;

  /* Don't try to mess with the WC if there are old log files left. */

  /* Is the (first) log file present?  */
  SVN_ERR(svn_io_check_path(logfile_path, &logfile_on_disk, scratch_pool));
  if (logfile_on_disk == svn_node_file)
    return svn_error_create(SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
                            _("Cannot upgrade with existing logs; please "
                              "run 'svn cleanup' with Subversion 1.6"));

  /* Lock this working copy directory, or steal an existing lock. Do this
     BEFORE we read the entries. We don't want another process to modify the
     entries after we've read them into memory.  */
  SVN_ERR(create_physical_lock(dir_abspath, scratch_pool));

  /* What's going on here?
   *
   * We're attempting to upgrade an older working copy to the new wc-ng format.
   * The semantics and storage mechanisms between the two are vastly different,
   * so it's going to be a bit painful.  Here's a plan for the operation:
   *
   * 1) Read the old 'entries' using the old-format reader.
   *
   * 2) Create the new DB if it hasn't already been created.
   *
   * 3) Use our compatibility code for writing entries to fill out the (new)
   *    DB state.  Use the remembered checksums, since an entry has only the
   *    MD5 not the SHA1 checksum, and in the case of a revert-base doesn't
   *    even have that.
   *
   * 4) Convert wcprop to the wc-ng format
   *
   * 5) Migrate regular properties to the WC-NG DB.
   */

  /***** ENTRIES - READ *****/
  SVN_ERR(svn_wc__read_entries_old(&entries, dir_abspath,
                                   scratch_pool, scratch_pool));

  this_dir = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);
  SVN_ERR(ensure_repos_info(this_dir, dir_abspath,
                            repos_info_func, repos_info_baton,
                            repos_cache,
                            scratch_pool, scratch_pool));

  /* Cache repos UUID pairs for when a subdir doesn't have this information */
  if (!apr_hash_get(repos_cache, this_dir->repos, APR_HASH_KEY_STRING))
    {
      apr_pool_t *hash_pool = apr_hash_pool_get(repos_cache);

      apr_hash_set(repos_cache,
                   apr_pstrdup(hash_pool, this_dir->repos),
                   APR_HASH_KEY_STRING,
                   apr_pstrdup(hash_pool, this_dir->uuid));
    }

  /* Create the new DB if it hasn't already been created. */
  if (!data->sdb)
    {
      const char *root_adm_abspath;

      /* In root wc construst path to temporary root wc/.svn/tmp/wcng/.svn */

      data->root_abspath = svn_dirent_join(svn_wc__adm_child(dir_abspath, "tmp",
                                                             scratch_pool),
                                           "wcng", result_pool);
      root_adm_abspath = svn_wc__adm_child(data->root_abspath, "",
                                           scratch_pool);
      SVN_ERR(svn_io_remove_dir2(root_adm_abspath, TRUE, NULL, NULL,
                                 scratch_pool));
      SVN_ERR(svn_wc__ensure_directory(root_adm_abspath, scratch_pool));

      /* Create an empty sqlite database for this directory. */
      SVN_ERR(svn_wc__db_upgrade_begin(&data->sdb,
                                       &data->repos_id, &data->wc_id,
                                       data->root_abspath,
                                       this_dir->repos, this_dir->uuid,
                                       result_pool, scratch_pool));

      /* Migrate the entries over to the new database.
         ### We need to think about atomicity here.

         entries_write_new() writes in current format rather than
         f12. Thus, this function bumps a working copy all the way to
         current.  */
      SVN_ERR(svn_wc__db_wclock_obtain(db, data->root_abspath, 0, FALSE,
                                       scratch_pool));
    }

  old_wcroot_abspath = svn_dirent_get_longest_ancestor(dir_abspath,
                                                       data->root_abspath,
                                                       scratch_pool);
  dir_relpath = svn_dirent_skip_ancestor(old_wcroot_abspath, dir_abspath);

  /***** TEXT BASES *****/
  SVN_ERR(migrate_text_bases(&text_bases_info, dir_abspath, data->root_abspath,
                             data->sdb, scratch_pool, scratch_pool));

  /***** ENTRIES - WRITE *****/
  SVN_ERR(svn_wc__write_upgraded_entries(dir_baton, parent_baton, db, data->sdb,
                                         data->repos_id, data->wc_id,
                                         dir_abspath, data->root_abspath,
                                         entries, text_bases_info,
                                         result_pool, scratch_pool));

  /***** WC PROPS *****/
  /* If we don't know precisely where the wcprops are, ignore them.  */
  if (old_format != SVN_WC__WCPROPS_LOST)
    {
      apr_hash_t *all_wcprops;

      if (old_format <= SVN_WC__WCPROPS_MANY_FILES_VERSION)
        SVN_ERR(read_many_wcprops(&all_wcprops, dir_abspath,
                                  scratch_pool, scratch_pool));
      else
        SVN_ERR(read_wcprops(&all_wcprops, dir_abspath,
                             scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__db_upgrade_apply_dav_cache(data->sdb, dir_relpath,
                                                 all_wcprops, scratch_pool));
    }

  /* Upgrade all the properties (including "this dir").

     Note: this must come AFTER the entries have been migrated into the
     database. The upgrade process needs the children in BASE_NODE and
     WORKING_NODE, and to examine the resultant WORKING state.  */
  SVN_ERR(migrate_props(dir_abspath, data->root_abspath, data->sdb, old_format,
                        scratch_pool));

  return SVN_NO_ERROR;
}


/* Return a string indicating the released version (or versions) of
 * Subversion that used WC format number WC_FORMAT, or some other
 * suitable string if no released version used WC_FORMAT.
 *
 * ### It's not ideal to encode this sort of knowledge in this low-level
 * library.  On the other hand, it doesn't need to be updated often and
 * should be easily found when it does need to be updated.  */
static const char *
version_string_from_format(int wc_format)
{
  switch (wc_format)
    {
      case 4: return "<=1.3";
      case 8: return "1.4";
      case 9: return "1.5";
      case 10: return "1.6";
    }
  return _("(unreleased development version)");
}

svn_error_t *
svn_wc__upgrade_sdb(int *result_format,
                    const char *wcroot_abspath,
                    svn_sqlite__db_t *sdb,
                    int start_format,
                    apr_pool_t *scratch_pool)
{
  struct bump_baton bb = { wcroot_abspath };

  if (start_format < SVN_WC__WC_NG_VERSION /* 12 */)
    return svn_error_createf(SVN_ERR_WC_UPGRADE_REQUIRED, NULL,
                             _("Working copy '%s' is too old (format %d, "
                               "created by Subversion %s)"),
                             svn_dirent_local_style(wcroot_abspath,
                                                    scratch_pool),
                             start_format,
                             version_string_from_format(start_format));

  /* Early WCNG formats no longer supported. */
  if (start_format < 19)
    return svn_error_createf(SVN_ERR_WC_UPGRADE_REQUIRED, NULL,
                             _("Working copy '%s' is an old development "
                               "version (format %d); to upgrade it, "
                               "use a format 18 client, then "
                               "use 'tools/dev/wc-ng/bump-to-19.py', then "
                               "use the current client"),
                             svn_dirent_local_style(wcroot_abspath,
                                                    scratch_pool),
                             start_format);

  /* ### need lock-out. only one upgrade at a time. note that other code
     ### cannot use this un-upgraded database until we finish the upgrade.  */

  /* Note: none of these have "break" statements; the fall-through is
     intentional. */
  switch (start_format)
    {
      case 19:
        SVN_ERR(svn_sqlite__with_transaction(sdb, bump_to_20, &bb,
                                             scratch_pool));
        *result_format = 20;
        /* FALLTHROUGH  */

      case 20:
        SVN_ERR(svn_sqlite__with_transaction(sdb, bump_to_21, &bb,
                                             scratch_pool));
        *result_format = 21;
        /* FALLTHROUGH  */

      case 21:
        SVN_ERR(svn_sqlite__with_transaction(sdb, bump_to_22, &bb,
                                             scratch_pool));
        *result_format = 22;
        /* FALLTHROUGH  */

      case 22:
        SVN_ERR(svn_sqlite__with_transaction(sdb, bump_to_23, &bb,
                                             scratch_pool));
        *result_format = 23;
        /* FALLTHROUGH  */

      case 23:
        SVN_ERR(svn_sqlite__with_transaction(sdb, bump_to_24, &bb,
                                             scratch_pool));
        *result_format = 24;
        /* FALLTHROUGH  */

      case 24:
        SVN_ERR(svn_sqlite__with_transaction(sdb, bump_to_25, &bb,
                                             scratch_pool));
        *result_format = 25;
        /* FALLTHROUGH  */

      case 25:
        SVN_ERR(svn_sqlite__with_transaction(sdb, bump_to_26, &bb,
                                             scratch_pool));
        *result_format = 26;
        /* FALLTHROUGH  */

      case 26:
        SVN_ERR(svn_sqlite__with_transaction(sdb, bump_to_27, &bb,
                                             scratch_pool));
        *result_format = 27;
        /* FALLTHROUGH  */

      /* ### future bumps go here.  */
#if 0
      case XXX-1:
        /* Revamp the recording of tree conflicts.  */
        SVN_ERR(svn_sqlite__with_transaction(sdb, bump_to_XXX, &bb,
                                             scratch_pool));
        *result_format = XXX;
        /* FALLTHROUGH  */
#endif
    }

#ifdef SVN_DEBUG
  if (*result_format != start_format)
    {
      int schema_version;
      SVN_ERR(svn_sqlite__read_schema_version(&schema_version, sdb, scratch_pool));

      /* If this assertion fails the schema isn't updated correctly */
      SVN_ERR_ASSERT(schema_version == *result_format);
    }
#endif

  /* Zap anything that might be remaining or escaped our notice.  */
  wipe_obsolete_files(wcroot_abspath, scratch_pool);

  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
upgrade_working_copy(void *parent_baton,
                     svn_wc__db_t *db,
                     const char *dir_abspath,
                     svn_wc_upgrade_get_repos_info_t repos_info_func,
                     void *repos_info_baton,
                     apr_hash_t *repos_cache,
                     struct upgrade_data_t *data,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     svn_wc_notify_func2_t notify_func,
                     void *notify_baton,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  void *dir_baton;
  int old_format;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *subdirs;
  svn_error_t *err;
  int i;

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  SVN_ERR(svn_wc__db_temp_get_format(&old_format, db, dir_abspath,
                                     iterpool));

  if (old_format >= SVN_WC__WC_NG_VERSION)
    {
      if (notify_func)
        notify_func(notify_baton,
                    svn_wc_create_notify(dir_abspath, svn_wc_notify_skip,
                                         iterpool),
                iterpool);
      svn_pool_destroy(iterpool);
      return SVN_NO_ERROR;
    }

  err = get_versioned_subdirs(&subdirs, dir_abspath, scratch_pool, iterpool);
  if (err)
    {
      if (APR_STATUS_IS_ENOENT(err->apr_err))
        {
          /* An unversioned dir is obstructing a versioned dir */
          svn_error_clear(err);
          err = NULL;
          if (notify_func)
            notify_func(notify_baton,
                        svn_wc_create_notify(dir_abspath, svn_wc_notify_skip,
                                             iterpool),
                        iterpool);
        }
      svn_pool_destroy(iterpool);
      return err;
    }


  SVN_ERR(upgrade_to_wcng(&dir_baton, parent_baton, db, dir_abspath, old_format,
                          repos_info_func, repos_info_baton,
                          repos_cache, data, scratch_pool, iterpool));

  if (notify_func)
    notify_func(notify_baton,
                svn_wc_create_notify(dir_abspath, svn_wc_notify_upgraded_path,
                                     iterpool),
                iterpool);

  for (i = 0; i < subdirs->nelts; ++i)
    {
      const char *child_abspath = APR_ARRAY_IDX(subdirs, i, const char *);

      svn_pool_clear(iterpool);

      SVN_ERR(upgrade_working_copy(dir_baton, db, child_abspath,
                                   repos_info_func, repos_info_baton,
                                   repos_cache, data,
                                   cancel_func, cancel_baton,
                                   notify_func, notify_baton,
                                   iterpool, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Return TRUE if LOCAL_ABSPATH is a pre-1.7 working copy root, FALSE
   otherwise. */
static svn_boolean_t
is_old_wcroot(const char *local_abspath,
              apr_pool_t *scratch_pool)
{
  apr_hash_t *entries;
  const char *parent_abspath, *name;
  svn_wc_entry_t *entry;
  svn_error_t *err = svn_wc__read_entries_old(&entries, local_abspath,
                                              scratch_pool, scratch_pool);
  if (err)
    {
      svn_error_clear(err);
      return FALSE;
    }

  svn_dirent_split(&parent_abspath, &name, local_abspath, scratch_pool);

  err = svn_wc__read_entries_old(&entries, parent_abspath,
                                 scratch_pool, scratch_pool);
  if (err)
    {
      svn_error_clear(err);
      return TRUE;
    }

  entry = apr_hash_get(entries, name, APR_HASH_KEY_STRING);
  if (!entry
      || entry->absent
      || (entry->deleted && entry->schedule != svn_wc_schedule_add))
    {
      return TRUE;
    }

  return FALSE;
}

svn_error_t *
svn_wc_upgrade(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_wc_upgrade_get_repos_info_t repos_info_func,
               void *repos_info_baton,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db;
  struct upgrade_data_t data = { NULL };
  svn_skel_t *work_item, *work_items = NULL;
  const char *pristine_from, *pristine_to, *db_from, *db_to;

  if (!is_old_wcroot(local_abspath, scratch_pool))
    return svn_error_createf(
      SVN_ERR_WC_INVALID_OP_ON_CWD, NULL,
      _("Cannot upgrade '%s' as it is not a pre-1.7 working copy root"),
      svn_dirent_local_style(local_abspath, scratch_pool));

  /* Given a pre-wcng root some/wc we create a temporary wcng in
     some/wc/.svn/tmp/wcng/wc.db and copy the metadata from one to the
     other, then the temporary wc.db file gets moved into the original
     root.  Until the wc.db file is moved the original working copy
     remains a pre-wcng and 'cleanup' with an old client will remove
     the partial upgrade.  Moving the wc.db file creates a wcng, and
     'cleanup' with a new client will complete any outstanding
     upgrade. */

  SVN_ERR(svn_wc__db_open(&db,
                          NULL /* ### config */, FALSE, FALSE,
                          scratch_pool, scratch_pool));

  /* Upgrade the pre-wcng into a wcng in a temporary location. */
  SVN_ERR(upgrade_working_copy(NULL, db, local_abspath,
                               repos_info_func, repos_info_baton,
                               apr_hash_make(scratch_pool), &data,
                               cancel_func, cancel_baton,
                               notify_func, notify_baton,
                               scratch_pool, scratch_pool));

  /* A workqueue item to move the pristine dir into place */
  pristine_from = svn_wc__adm_child(data.root_abspath, PRISTINE_STORAGE_RELPATH,
                                    scratch_pool);
  pristine_to = svn_wc__adm_child(local_abspath, PRISTINE_STORAGE_RELPATH,
                                  scratch_pool);
  SVN_ERR(svn_wc__ensure_directory(pristine_from, scratch_pool));
  SVN_ERR(svn_wc__wq_build_file_move(&work_item, db,
                                     pristine_from, pristine_to,
                                     scratch_pool, scratch_pool));
  work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);

  /* A workqueue item to remove pre-wcng metadata */
  SVN_ERR(svn_wc__wq_build_postupgrade(&work_item, scratch_pool));
  work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);
  SVN_ERR(svn_wc__db_wq_add(db, data.root_abspath, work_items, scratch_pool));

  SVN_ERR(svn_wc__db_wclock_release(db, data.root_abspath, scratch_pool));
  SVN_ERR(svn_sqlite__close(data.sdb));
  SVN_ERR(svn_wc__db_close(db));

  /* Renaming the db file is what makes the pre-wcng into a wcng */
  db_from = svn_wc__adm_child(data.root_abspath, SDB_FILE, scratch_pool);
  db_to = svn_wc__adm_child(local_abspath, SDB_FILE, scratch_pool);
  SVN_ERR(svn_io_file_rename(db_from, db_to, scratch_pool));

  /* Now we have a working wcng, tidy up the droppings */
  SVN_ERR(svn_wc__db_open(&db, NULL /* ### config */, FALSE, FALSE,
                          scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__wq_run(db, local_abspath, cancel_func, cancel_baton,
                         scratch_pool));
  SVN_ERR(svn_wc__db_close(db));

  /* Should we have the workqueue remove this empty dir? */
  SVN_ERR(svn_io_remove_dir2(data.root_abspath, FALSE, NULL, NULL,
                             scratch_pool));

  return SVN_NO_ERROR;
}
