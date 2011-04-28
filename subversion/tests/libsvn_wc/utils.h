/* utils.h --- wc/client test utilities
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

#ifndef SVN_TEST_UTILS_H
#define SVN_TEST_UTILS_H

#include <apr_pools.h>
#include "svn_error.h"
#include "../svn_test.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*-------------------------------------------------------------------*/

/** Helper routines for creating repositories and WCs. **/


#define REPOSITORIES_WORK_DIR "svn-test-work/repositories"
#define WCS_WORK_DIR "svn-test-work/working-copies"


/* The "sandbox" is a work space including a working copy and a repository.
 * Functions are provided for easy manipulation of the WC.  Paths given to
 * these functions can be relative to the WC root as stored in the sandbox
 * object, or can be absolute paths. */

/* An object holding the state of a test sand-box. */
typedef struct svn_test__sandbox_t
{
  /* The WC context object. */
  svn_wc_context_t *wc_ctx;
  /* The repository URL. */
  const char *repos_url;
  /* The absolute local path of the WC root. */
  const char *wc_abspath;
  /* A pool that can be used for all allocations. */
  apr_pool_t *pool;
} svn_test__sandbox_t;


/* Create an empty repository and WC for the test TEST_NAME.  Fill in
 * *SANDBOX with all the details.
 *
 * Create the repository and WC in subdirectories called
 * REPOSITORIES_WORK_DIR/TEST_NAME and WCS_WORK_DIR/TEST_NAME respectively,
 * within the current working directory.
 *
 * Register the repo and WC to be cleaned up when the test suite exits. */
svn_error_t *
svn_test__sandbox_create(svn_test__sandbox_t *sandbox,
                         const char *test_name,
                         const svn_test_opts_t *opts,
                         apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_TEST_UTILS_H */