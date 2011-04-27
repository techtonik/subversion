 * Copyright (c) 2000-2007, 2009 CollabNet.  All rights reserved.
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
  const char *from_path;
  apr_array_header_t *merge_revarray;
      const void *key;
      void *val;
      apr_hash_this(hi, &key, NULL, &val);
      from_path = key;
      merge_revarray = val;

      const void *key;
      void *val;
      apr_hash_this(hi, &key, NULL, &val);
      from_path = key;
      merge_revarray = val;

   passed to svn_client_diff4, which is probably stdout. */
  if (relative_to_dir)
      /* Possibly adjust the path shown in the output (see issue #2723). */
      const char *child_path = svn_path_is_child(relative_to_dir, path, pool);
      if (child_path)
        path = child_path;
      else if (!svn_path_compare_paths(relative_to_dir, path))
        path = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(path, relative_to_dir);
                                svn_path_local_style(path, pool),
      const char *header_fmt;
        header_fmt = _("Added: %s%s");
        header_fmt = _("Deleted: %s%s");
        header_fmt = _("Modified: %s%s");
      SVN_ERR(file_printf_from_utf8(file, encoding, header_fmt,
      /* For now, we have a rather simple heuristic: if this is an
         "svn:" property, then assume the value is UTF-8 and must
         therefore be converted before printing.  Otherwise, just
         print whatever's there and hope for the best. */
        svn_boolean_t val_is_utf8 = svn_prop_is_svn_prop(propchange->name);

        if (original_value != NULL)
          {
            if (val_is_utf8)
              {
                SVN_ERR(file_printf_from_utf8
                        (file, encoding,
                         "   - %s" APR_EOL_STR, original_value->data));
              }
            else
              {
                /* ### todo: check for error? */
                apr_file_printf
                  (file, "   - %s" APR_EOL_STR, original_value->data);
              }
          }

        if (propchange->value != NULL)
          {
            if (val_is_utf8)
              {
                SVN_ERR(file_printf_from_utf8
                        (file, encoding, "   + %s" APR_EOL_STR,
                         propchange->value->data));
              }
            else
              {
                /* ### todo: check for error? */
                apr_file_printf(file, "   + %s" APR_EOL_STR,
                                propchange->value->data);
              }
          }
  /* ### todo [issue #1533]: Use file_printf_from_utf8() to convert this
     to native encoding, at least conditionally?  Or is it better to
     have under_string always output the same eol, so programs can
     find it consistently?  Also, what about checking for error? */
  apr_file_printf(file, APR_EOL_STR);


     svn_client_diff4, either may be SVN_INVALID_REVNUM.  We need these
     because some of the svn_wc_diff_callbacks3_t don't get revision
};
/* Generate a label for the diff output for file PATH at revision REVNUM.
   If REVNUM is invalid then it is assumed to be the current working
   copy.  Assumes the paths are already in the desired style (local
   vs internal).  Allocate the label in POOL. */
static const char *
diff_label(const char *path,
           svn_revnum_t revnum,
           apr_pool_t *pool)
{
  const char *label;
  if (revnum != SVN_INVALID_REVNUM)
    label = apr_psprintf(pool, _("%s\t(revision %ld)"), path, revnum);
  else
    label = apr_psprintf(pool, _("%s\t(working copy)"), path);
  return label;
}
/* An svn_wc_diff_callbacks3_t function.  Used for both file and directory
   property diffs. */
diff_props_changed(svn_wc_adm_access_t *adm_access,
                   void *diff_baton)
    SVN_ERR(display_prop_diffs(props, original_props, path,
                               diff_cmd_baton->header_encoding,
                               diff_cmd_baton->outfile,
                               diff_cmd_baton->relative_to_dir,
                               subpool));
  apr_size_t len;
  /* ### Holy cow.  Due to anchor/target weirdness, we can't
     simply join diff_cmd_baton->orig_path_1 with path, ditto for
     orig_path_2.  That will work when they're directory URLs, but
     not for file URLs.  Nor can we just use anchor1 and anchor2
     from do_diff(), at least not without some more logic here.
     What a nightmare.

     For now, to distinguish the two paths, we'll just put the
     unique portions of the original targets in parentheses after
     the received path, with ellipses for handwaving.  This makes
     the labels a bit clumsy, but at least distinctive.  Better
     solutions are possible, they'll just take more thought. */

  path1 = diff_cmd_baton->orig_path_1;
  path2 = diff_cmd_baton->orig_path_2;
  len = strlen(svn_path_get_longest_ancestor(path1, path2, subpool));
  path1 = path1 + len;
  path2 = path2 + len;

  /* ### Should diff labels print paths in local style?  Is there
     already a standard for this?  In any case, this code depends on
     a particular style, so not calling svn_path_local_style() on the
     paths below.*/
  if (path1[0] == '\0')
    path1 = apr_psprintf(subpool, "%s", path);
  else if (path1[0] == '/')
    path1 = apr_psprintf(subpool, "%s\t(...%s)", path, path1);
  else
    path1 = apr_psprintf(subpool, "%s\t(.../%s)", path, path1);

  if (path2[0] == '\0')
    path2 = apr_psprintf(subpool, "%s", path);
  else if (path2[0] == '/')
    path2 = apr_psprintf(subpool, "%s\t(...%s)", path, path2);
  else
    path2 = apr_psprintf(subpool, "%s\t(.../%s)", path, path2);
  if (diff_cmd_baton->relative_to_dir)
    {
      /* Possibly adjust the paths shown in the output (see issue #2723). */
      const char *child_path = svn_path_is_child(rel_to_dir, path, subpool);

      if (child_path)
        path = child_path;
      else if (!svn_path_compare_paths(rel_to_dir, path))
        path = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(path, rel_to_dir);

      child_path = svn_path_is_child(rel_to_dir, path1, subpool);

      if (child_path)
        path1 = child_path;
      else if (!svn_path_compare_paths(rel_to_dir, path1))
        path1 = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(path1, rel_to_dir);

      child_path = svn_path_is_child(rel_to_dir, path2, subpool);

      if (child_path)
        path2 = child_path;
      else if (!svn_path_compare_paths(rel_to_dir, path2))
        path2 = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(path2, rel_to_dir);
    }
      SVN_ERR(svn_io_run_diff(".",
                              diff_cmd_baton->options.for_external.argv,
                              diff_cmd_baton->options.for_external.argc,
                              label1, label2,
                              tmpfile1, tmpfile2,
                              &exitcode, diff_cmd_baton->outfile, errfile,
                              diff_cmd_baton->diff_cmd, subpool));
      if (svn_diff_contains_diffs(diff) || diff_cmd_baton->force_empty)
          SVN_ERR(svn_diff_file_output_unified3
                  (os, diff, tmpfile1, tmpfile2, label1, label2,
                   diff_cmd_baton->header_encoding, rel_to_dir,
                   diff_cmd_baton->options.for_internal->show_c_function,
                   subpool));
/* An svn_wc_diff_callbacks3_t function. */
diff_file_changed(svn_wc_adm_access_t *adm_access,
                  void *diff_baton)
                                 mimetype1, mimetype2, diff_baton));
    SVN_ERR(diff_props_changed(adm_access, prop_state, tree_conflicted,
                               path, prop_changes,
                               original_props, diff_baton));
/* An svn_wc_diff_callbacks3_t function. */
diff_file_added(svn_wc_adm_access_t *adm_access,
                void *diff_baton)
  SVN_ERR(diff_file_changed(adm_access, content_state, prop_state,
                            tree_conflicted, path,
                            tmpfile1, tmpfile2,
                            rev1, rev2,
                            mimetype1, mimetype2,
                            prop_changes, original_props, diff_baton));
/* An svn_wc_diff_callbacks3_t function. */
diff_file_deleted_with_diff(svn_wc_adm_access_t *adm_access,
                            void *diff_baton)
  return diff_file_changed(adm_access, state, NULL, tree_conflicted, path,
                           tmpfile1, tmpfile2,
                           diff_cmd_baton->revnum1, diff_cmd_baton->revnum2,
                           mimetype1, mimetype2,
                           apr_array_make(diff_cmd_baton->pool, 1,
                                          sizeof(svn_prop_t)),
                           apr_hash_make(diff_cmd_baton->pool), diff_baton);
/* An svn_wc_diff_callbacks3_t function. */
diff_file_deleted_no_diff(svn_wc_adm_access_t *adm_access,
                          void *diff_baton)
/* An svn_wc_diff_callbacks3_t function. */
diff_dir_added(svn_wc_adm_access_t *adm_access,
               void *diff_baton)
  if (state)
    *state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;
/* An svn_wc_diff_callbacks3_t function. */
diff_dir_deleted(svn_wc_adm_access_t *adm_access,
                 void *diff_baton)
  if (state)
    *state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;
/* An svn_wc_diff_callbacks3_t function. */
diff_dir_opened(svn_wc_adm_access_t *adm_access,
                void *diff_baton)
  if (tree_conflicted)
    *tree_conflicted = FALSE;
/* An svn_wc_diff_callbacks3_t function. */
diff_dir_closed(svn_wc_adm_access_t *adm_access,
                void *diff_baton)
  if (contentstate)
    *contentstate = svn_wc_notify_state_unknown;
  if (propstate)
    *propstate = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;
/* Helper function: given a working-copy PATH, return its associated
   url in *URL, allocated in POOL.  If PATH is *already* a URL, that's
   fine, just set *URL = PATH. */
               const char *path,
               apr_pool_t *pool)
  svn_wc_adm_access_t *adm_access;  /* ### FIXME local */
  const svn_wc_entry_t *entry;

  if (svn_path_is_url(path))
      *url = path;
  /* ### This may not be a good idea, see issue 880 */
  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path, FALSE,
                                 0, NULL, NULL, pool));
  SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, FALSE, pool));
  SVN_ERR(svn_wc_adm_close2(adm_access, pool));

  if (entry->url)
    *url = apr_pstrdup(pool, entry->url);
  else
    *url = apr_pstrdup(pool, entry->copyfrom_url);
/** Helper structure: for passing around the diff parameters */
struct diff_parameters
{
  /* First input path */
  const char *path1;

  /* Revision of first input path */
  const svn_opt_revision_t *revision1;

  /* Second input path */
  const char *path2;

  /* Revision of second input path */
  const svn_opt_revision_t *revision2;

  /* Peg revision */
  const svn_opt_revision_t *peg_revision;

  /* Desired depth */
  svn_depth_t depth;

  /* Ignore ancestry */
  svn_boolean_t ignore_ancestry;

  /* Ignore deleted */
  svn_boolean_t no_diff_deleted;

  /* Changelists of interest */
  const apr_array_header_t *changelists;
};

/** Helper structure: filled by check_paths() */
struct diff_paths
{
  /* path1 can only be found in the repository? */
  svn_boolean_t is_repos1;

  /* path2 can only be found in the repository? */
  svn_boolean_t is_repos2;
};


/** Check if paths are urls and if the revisions are local, and, for
    pegged revisions, ensure that at least one revision is non-local.
    Fills the PATHS structure. */
check_paths(const struct diff_parameters *params,
            struct diff_paths *paths)
  if ((params->revision1->kind == svn_opt_revision_unspecified)
      || (params->revision2->kind == svn_opt_revision_unspecified))
    ((params->revision1->kind == svn_opt_revision_base)
     || (params->revision1->kind == svn_opt_revision_working));
    ((params->revision2->kind == svn_opt_revision_base)
     || (params->revision2->kind == svn_opt_revision_working));
  if (params->peg_revision->kind != svn_opt_revision_unspecified)
      paths->is_repos1 = ! is_local_rev1;
      paths->is_repos2 = ! is_local_rev2;
      paths->is_repos1 = ! is_local_rev1 || svn_path_is_url(params->path1);
      paths->is_repos2 = ! is_local_rev2 || svn_path_is_url(params->path2);
/** Helper structure filled by diff_prepare_repos_repos */
struct diff_repos_repos_t
{
  /* URL created from path1 */
  const char *url1;

  /* URL created from path2 */
  const char *url2;

  /* The BASE_PATH for the diff */
  const char *base_path;

  /* url1 and url2 are the same */
  svn_boolean_t same_urls;

  /* Revision of url1 */
  svn_revnum_t rev1;

  /* Revision of url2 */
  svn_revnum_t rev2;

  /* Anchor based on url1 */
  const char *anchor1;

  /* Anchor based on url2 */
  const char *anchor2;

  /* Target based on url1 */
  const char *target1;

  /* Target based on url2 */
  const char *target2;

  /* RA session pointing at anchor1. */
  svn_ra_session_t *ra_session;
};

/** Helper function: prepare a repos repos diff. Fills DRR
 * structure. */
diff_prepare_repos_repos(const struct diff_parameters *params,
                         struct diff_repos_repos_t *drr,
  svn_ra_session_t *ra_session;
  SVN_ERR(convert_to_url(&drr->url1, params->path1, pool));
  SVN_ERR(convert_to_url(&drr->url2, params->path2, pool));
  drr->same_urls = (strcmp(drr->url1, drr->url2) == 0);
  drr->base_path = NULL;
  if (drr->url1 != params->path1)
    drr->base_path = params->path1;
  if (drr->url2 != params->path2)
    drr->base_path = params->path2;

  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, drr->url2,
                                               NULL, NULL, NULL, FALSE,
  if (params->peg_revision->kind != svn_opt_revision_unspecified)
      SVN_ERR(svn_client__repos_locations(&drr->url1, &start_ignore,
                                          &drr->url2, &end_ignore,
                                          ra_session,
                                          params->path2,
                                          params->peg_revision,
                                          params->revision1,
                                          params->revision2,
      /* Reparent the session, since drr->url2 might have changed as a result
      SVN_ERR(svn_ra_reparent(ra_session, drr->url2, pool));
  SVN_ERR(svn_client__get_revision_number
          (&drr->rev2, NULL, ra_session, params->revision2,
           (params->path2 == drr->url2) ? NULL : params->path2, pool));
  SVN_ERR(svn_ra_check_path(ra_session, "", drr->rev2, &kind2, pool));
       drr->url2, drr->rev2);
  SVN_ERR(svn_ra_reparent(ra_session, drr->url1, pool));
  SVN_ERR(svn_client__get_revision_number
          (&drr->rev1, NULL, ra_session, params->revision1,
           (params->path1 == drr->url1) ? NULL : params->path1, pool));
  SVN_ERR(svn_ra_check_path(ra_session, "", drr->rev1, &kind1, pool));
       drr->url1, drr->rev1);
  drr->anchor1 = drr->url1;
  drr->anchor2 = drr->url2;
  drr->target1 = "";
  drr->target2 = "";
      svn_path_split(drr->url1, &drr->anchor1, &drr->target1, pool);
      drr->target1 = svn_path_uri_decode(drr->target1, pool);
      svn_path_split(drr->url2, &drr->anchor2, &drr->target2, pool);
      drr->target2 = svn_path_uri_decode(drr->target2, pool);
      if (drr->base_path)
        drr->base_path = svn_path_dirname(drr->base_path, pool);
      SVN_ERR(svn_ra_reparent(ra_session, drr->anchor1, pool));
  drr->ra_session = ra_session;
   This function is really svn_client_diff4().  If you read the public
   API description for svn_client_diff4(), it sounds quite Grand.  It
   Perhaps someday a brave soul will truly make svn_client_diff4
                          _("Sorry, svn_client_diff4 was called in a way "
   All other options are the same as those passed to svn_client_diff4(). */
           const svn_wc_diff_callbacks3_t *callbacks,
  svn_wc_adm_access_t *adm_access, *target_access;
  const char *target;
  int levels_to_lock = SVN_WC__LEVELS_TO_LOCK_FROM_DEPTH(depth);
  SVN_ERR(svn_wc_adm_open_anchor(&adm_access, &target_access, &target,
                                 path1, FALSE, levels_to_lock,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));
  /* Resolve named revisions to real numbers. */
  SVN_ERR(svn_client__get_revision_number
          (&callback_baton->revnum1, NULL, NULL, revision1, path1, pool));
  SVN_ERR(svn_wc_diff5(adm_access, target, callbacks, callback_baton,
                       depth, ignore_ancestry, changelists, pool));
  return svn_wc_adm_close2(adm_access, pool);
   DIFF_PARAM.PATH1 and DIFF_PARAM.PATH2 may be either URLs or the working
   copy paths. DIFF_PARAM.REVISION1 and DIFF_PARAM.REVISION2 are their
   respective revisions. If DIFF_PARAM.PEG_REVISION is specified,
   DIFF_PARAM.PATH2 is the path at the peg revision, and the actual two
   paths compared are determined by following copy history from PATH2.
   All other options are the same as those passed to svn_client_diff4(). */
diff_repos_repos(const struct diff_parameters *diff_param,
                 const svn_wc_diff_callbacks3_t *callbacks,
  void *report_baton;
  struct diff_repos_repos_t drr;
  SVN_ERR(diff_prepare_repos_repos(diff_param, &drr, ctx, pool));
  callback_baton->orig_path_1 = drr.url1;
  callback_baton->orig_path_2 = drr.url2;
  callback_baton->revnum1 = drr.rev1;
  callback_baton->revnum2 = drr.rev2;
  SVN_ERR(svn_client__open_ra_session_internal
          (&extra_ra_session, drr.anchor1, NULL, NULL, NULL, FALSE, TRUE, ctx,
           pool));
  SVN_ERR(svn_client__get_diff_editor
          (drr.base_path ? drr.base_path : "",
           NULL, callbacks, callback_baton, diff_param->depth,
           FALSE /* doesn't matter for diff */, extra_ra_session, drr.rev1,
           NULL /* no notify_func */, NULL /* no notify_baton */,
           ctx->cancel_func, ctx->cancel_baton,
           &diff_editor, &diff_edit_baton, pool));
          (drr.ra_session, &reporter, &report_baton, drr.rev2, drr.target1,
           diff_param->depth, diff_param->ignore_ancestry, TRUE,
           drr.url2, diff_editor, diff_edit_baton, pool));
  SVN_ERR(reporter->set_path(report_baton, "", drr.rev1,
  return reporter->finish_report(report_baton, pool);
   All other options are the same as those passed to svn_client_diff4(). */
              const svn_wc_diff_callbacks3_t *callbacks,
  svn_wc_adm_access_t *adm_access, *dir_access;
  const svn_wc_entry_t *entry;
  void *report_baton;
  int levels_to_lock = SVN_WC__LEVELS_TO_LOCK_FROM_DEPTH(depth);
  SVN_ERR(convert_to_url(&url1, path1, pool));
  SVN_ERR(svn_wc_adm_open_anchor(&adm_access, &dir_access, &target,
                                 path2, FALSE, levels_to_lock,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));
  anchor = svn_wc_adm_access_path(adm_access);
  SVN_ERR(svn_wc__entry_versioned(&entry, anchor, adm_access, FALSE, pool));
  if (! entry->url)
                             svn_path_local_style(anchor, pool));
  anchor_url = apr_pstrdup(pool, entry->url);

          callback_baton->orig_path_2 = svn_path_join(anchor_url, target, pool);
          callback_baton->orig_path_1 = svn_path_join(anchor_url, target, pool);
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, anchor_url,
                                               NULL, NULL, NULL, FALSE, TRUE,
  SVN_ERR(svn_wc_get_diff_editor5(adm_access, target,
                                  callbacks, callback_baton,
                                  changelists,
                                  &diff_editor, &diff_edit_baton,
                                  pool));
  SVN_ERR(svn_client__get_revision_number
          (&rev, NULL, ra_session, revision1,
           (path1 == url1) ? NULL : path1, pool));
                          &reporter, &report_baton,
                          target ? svn_path_uri_decode(target, pool) : NULL,
                          depth,
  SVN_ERR(svn_ra_has_capability(ra_session, &server_supports_depth,
                                SVN_RA_CAPABILITY_DEPTH, pool));

  SVN_ERR(svn_wc_crawl_revisions4(path2, dir_access,
                                  reporter, report_baton,
                                  NULL, pool));
  return svn_wc_adm_close2(adm_access, pool);
/* This is basically just the guts of svn_client_diff[_peg]3(). */
do_diff(const struct diff_parameters *diff_param,
        const svn_wc_diff_callbacks3_t *callbacks,
  struct diff_paths diff_paths;
  SVN_ERR(check_paths(diff_param, &diff_paths));
  if (diff_paths.is_repos1)
      if (diff_paths.is_repos2)
          return diff_repos_repos(diff_param, callbacks, callback_baton,
                                  ctx, pool);
          return diff_repos_wc(diff_param->path1, diff_param->revision1,
                               diff_param->peg_revision,
                               diff_param->path2, diff_param->revision2,
                               FALSE, diff_param->depth,
                               diff_param->ignore_ancestry,
                               diff_param->changelists,
                               callbacks, callback_baton, ctx, pool);
      if (diff_paths.is_repos2)
          return diff_repos_wc(diff_param->path2, diff_param->revision2,
                               diff_param->peg_revision,
                               diff_param->path1, diff_param->revision1,
                               TRUE, diff_param->depth,
                               diff_param->ignore_ancestry,
                               diff_param->changelists,
                               callbacks, callback_baton, ctx, pool);
          return diff_wc_wc(diff_param->path1, diff_param->revision1,
                            diff_param->path2, diff_param->revision2,
                            diff_param->depth,
                            diff_param->ignore_ancestry,
                            diff_param->changelists,
                            callbacks, callback_baton, ctx, pool);
diff_summarize_repos_repos(const struct diff_parameters *diff_param,
                           svn_client_diff_summarize_func_t summarize_func,
  void *report_baton;
  struct diff_repos_repos_t drr;
  SVN_ERR(diff_prepare_repos_repos(diff_param, &drr, ctx, pool));
  SVN_ERR(svn_client__open_ra_session_internal
          (&extra_ra_session, drr.anchor1, NULL, NULL, NULL, FALSE, TRUE,
           ctx, pool));
          (drr.target2, summarize_func,
           summarize_baton, extra_ra_session, drr.rev1, ctx->cancel_func,
          (drr.ra_session, &reporter, &report_baton, drr.rev2, drr.target1,
           diff_param->depth, diff_param->ignore_ancestry,
           FALSE /* do not create text delta */, drr.url2, diff_editor,
  SVN_ERR(reporter->set_path(report_baton, "", drr.rev1,
  return reporter->finish_report(report_baton, pool);
do_diff_summarize(const struct diff_parameters *diff_param,
                  svn_client_diff_summarize_func_t summarize_func,
  struct diff_paths diff_paths;
  SVN_ERR(check_paths(diff_param, &diff_paths));

  if (diff_paths.is_repos1 && diff_paths.is_repos2)
    return diff_summarize_repos_repos(diff_param, summarize_func,
                                      summarize_baton, ctx, pool);
 * according to OPTIONS and CONFIG.  CONFIG may be null.
  /* See if there is a command. */
  diff_cmd_baton->diff_cmd = diff_cmd;
            argv[i] = APR_ARRAY_IDX(options, i, const char *);
svn_client_diff4(const apr_array_header_t *options,
  struct diff_parameters diff_params;

  struct diff_cmd_baton diff_cmd_baton;
  svn_wc_diff_callbacks3_t diff_callbacks;
  /* fill diff_param */
  diff_params.path1 = path1;
  diff_params.revision1 = revision1;
  diff_params.path2 = path2;
  diff_params.revision2 = revision2;
  diff_params.peg_revision = &peg_revision;
  diff_params.depth = depth;
  diff_params.ignore_ancestry = ignore_ancestry;
  diff_params.no_diff_deleted = no_diff_deleted;
  diff_params.changelists = changelists;

  diff_callbacks.dir_props_changed = diff_props_changed;

  return do_diff(&diff_params, &diff_callbacks, &diff_cmd_baton, ctx, pool);
svn_client_diff_peg4(const apr_array_header_t *options,
  struct diff_parameters diff_params;

  struct diff_cmd_baton diff_cmd_baton;
  svn_wc_diff_callbacks3_t diff_callbacks;

  if (svn_path_is_url(path) &&
        (start_revision->kind == svn_opt_revision_base
         || end_revision->kind == svn_opt_revision_base) )
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("Revision type requires a working copy "
                              "path, not a URL"));

  /* fill diff_param */
  diff_params.path1 = path;
  diff_params.revision1 = start_revision;
  diff_params.path2 = path;
  diff_params.revision2 = end_revision;
  diff_params.peg_revision = peg_revision;
  diff_params.depth = depth;
  diff_params.ignore_ancestry = ignore_ancestry;
  diff_params.no_diff_deleted = no_diff_deleted;
  diff_params.changelists = changelists;
  diff_callbacks.dir_props_changed = diff_props_changed;

  return do_diff(&diff_params, &diff_callbacks, &diff_cmd_baton, ctx, pool);
  struct diff_parameters diff_params;

  /* fill diff_param */
  diff_params.path1 = path1;
  diff_params.revision1 = revision1;
  diff_params.path2 = path2;
  diff_params.revision2 = revision2;
  diff_params.peg_revision = &peg_revision;
  diff_params.depth = depth;
  diff_params.ignore_ancestry = ignore_ancestry;
  diff_params.no_diff_deleted = FALSE;
  diff_params.changelists = changelists;

  return do_diff_summarize(&diff_params, summarize_func, summarize_baton,
                           ctx, pool);
  struct diff_parameters diff_params;

  /* fill diff_param */
  diff_params.path1 = path;
  diff_params.revision1 = start_revision;
  diff_params.path2 = path;
  diff_params.revision2 = end_revision;
  diff_params.peg_revision = peg_revision;
  diff_params.depth = depth;
  diff_params.ignore_ancestry = ignore_ancestry;
  diff_params.no_diff_deleted = FALSE;
  diff_params.changelists = changelists;

  return do_diff_summarize(&diff_params, summarize_func, summarize_baton,
                           ctx, pool);