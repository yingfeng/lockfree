/**
 * Copyright 2015
 * Maya Arbel (mayaarl [at] cs [dot] technion [dot] ac [dot] il).
 * Adam Morrison (mad [at] cs [dot] technion [dot] ac [dot] il).
 *
 * This file is part of Predicate RCU.
 *
 * Predicate RCU is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors Maya Arbel and Adam Morrison
 */

#ifndef _PRCU_H_
#define _PRCU_H_

#include <stdbool.h>

typedef struct predicate_info_t *predicate_info;
typedef struct hash_info_t *hash_info;
typedef bool (*predicate)(predicate_info, int);
typedef int (*range_predicate)(predicate_info, int);
typedef int (*predicate_hash)(hash_info, int);

#if !defined(EXTERNAL_RCU)

void prcu_init(int threads);
void prcu_set_hash(predicate_hash hash, hash_info info);
int prcu_get_size();
void prcu_enter(int value);
void prcu_exit(int value);
void prcu_register(int id);
void prcu_unregister();
void prcu_wait_for_readers(predicate pred, int min,
                           range_predicate pred_next,
                           predicate_info pred_info);

/* Note that prcu_wait_for_readers interface differs from the interface in the paper,
 * It requires both type of predicates, but each implementation uses only one of them.
 * Range predicates are defined differently than the paper, allowing users to optimize waiting.
 * Users should use their knowledge on the hash function to avoid waiting on a bucket multiple times.
 * Range predicate receives a min_bucket value (as opposed to min_value and max_value in the paper),
 * and a next function that allows iterating over buckets (as opposed to iterating over values).
 * The next function should use the PRCU matrix size provided by prcu_get_size function.
 * Buckets are integer values from [0,.. ,prcu_get_size-1].
 */
#else

#include <urcu.h>

static inline void
prcu_init(int num_threads)
{
        rcu_init();
}

static inline void
prcu_set_hash(predicate_hash hash, hash_info sent_info)
{
}

static inline int
prcu_get_size()
{
        return 1;
}


static inline void
prcu_register(int id)
{
        rcu_register_thread();
}

static inline void
prcu_unregister(void)
{
        rcu_unregister_thread();
}

static inline void
prcu_enter(int value)
{
        rcu_read_lock();
}

static inline void
prcu_exit(int value)
{
        rcu_read_unlock();
}

static inline void
prcu_wait_for_readers(predicate pred, int min,
                      range_predicate pred_next, predicate_info pred_info)
{
        synchronize_rcu();
}


#endif	/* EXTERNAL RCU */

#endif	/*_PRCU_H_*/
