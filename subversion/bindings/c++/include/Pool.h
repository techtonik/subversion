/**
 * @copyright
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
 * @endcopyright
 *
 * @file Pool.h
 * @brief Interface of the class Pool
 */

#ifndef POOL_H
#define POOL_H

#include "svn_pools.h"

namespace SVN
{

  /**
   * This class manages one APR pool.  Objects of this class may be allocated
   * on the stack, ensuring the pool is destroyed when the function 
   * completes.
   */
  class Pool
  {

    private:
      /** The pool request pool.  */
      apr_pool_t *m_pool;

      /**
       * We declare the assignment operator private here, so that the compiler
       * won't inadvertently use them for us.  The default copy constructor just
       * copies all the data members, which would create two pointers to the
       * same pool, one of which would get destroyed while the other thought it
       * was still valid...and BOOM!  Hence the private declaration.
       */
      Pool &operator=(Pool &that);

    public:
      Pool();

      // Explicit to avoid being called as an assignment operator.
      explicit Pool(Pool &parent);
      ~Pool();

      void *alloc(apr_size_t sz);
      void registerCleanup(apr_status_t (*cleanup_func)(void *), void *baton);
      apr_pool_t *pool() const;
      void clear() const;
  };

// The following one-line functions are best inlined by the compiler, and
// need to be implemented in the header file for that to happen.

inline apr_pool_t *
Pool::pool() const
{
  return m_pool;
}

inline void
Pool::clear() const
{
  svn_pool_clear(m_pool);
}

}


#endif // POOL_H
