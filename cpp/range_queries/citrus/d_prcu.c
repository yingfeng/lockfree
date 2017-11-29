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

/*
 * D-PRCU
 *
 * Basic idea: readers maintain a reference counter in each bucket
 * (which is shared by all readers).  Synchronize waits for the
 * reference counter to reach 0 (an overapproximation, but spares
 * us from tracking when each reader enters its critical section).
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include "prcu.h"
#include "tsc.h"

typedef struct rcu_node_t {
        int which;
        volatile long count[2];
        pthread_mutex_t lock;
        char p[128];
} rcu_node;

#ifndef RCU_TABLE_SIZE
#define RCU_TABLE_SIZE	1023
#endif

static int threads;
static int table_size = RCU_TABLE_SIZE;
static rcu_node *prcu_table;
static predicate_hash hash_func;
static hash_info info;


void
prcu_init(int num_threads)
{

        rcu_node *result = (rcu_node *) malloc(sizeof(rcu_node) * (table_size));
        if (result == NULL) {
                printf("malloc failed\n");
                exit(1);
        }
        int i, j;
        threads = num_threads;
        for (i = 0; i < table_size; i++) {
                rcu_node *nnode = &result[i];
                nnode->count[0] = nnode->count[1] = 0;
                nnode->which = 0;
                pthread_mutex_init(&nnode->lock, NULL);
        }
        prcu_table = result;
        return;
}

void
prcu_set_hash(predicate_hash hash, hash_info sent_info)
{
        hash_func = hash;
        info = sent_info;
}

int
prcu_get_size()
{
        return table_size;
}

static __thread int i;

void
prcu_register(int id)
{
        i = id;
}

void
prcu_unregister()
{
}

static __thread int which;

void
prcu_enter(int value)
{
        int j = hash_func(info, value);
        assert(j > -1);
        assert(j < table_size);
        which = prcu_table[j].which;
        __sync_fetch_and_add(&prcu_table[j].count[which], 1);
}

void
prcu_exit(int value)
{
        int j = hash_func(info, value);
        assert(j > -1);
        assert(j < table_size);
        __sync_fetch_and_add(&prcu_table[j].count[which], -1);
}

static inline void
wait(rcu_node * node)
{
        /* If both counts are 0, no thread is in an rcu read critical section,
         * and we can return.  Since we expect this to be the case usually,
         * try waiting a while for this to happen, before going to slow path.
         */
        int i;
        for (i = 0; i < 10000; i++) {
                if (node->count[0] == 0 && node->count[1] == 0)
                        return;
        }

        /* Slow path: We can't just wait for a counter to reach 0, since
         * someone may always be inside the read critical section.  So force
         * reads to drain by changing the "which" gate twice.  We need to
         * change it twice because readers may be active with both values
         * of "which" (i.e., if a reader observes which==1 and then stalls
         * before incrementing counter[1].
         */
        pthread_mutex_lock(&node->lock);

        int which = node->which;

        node->which = !which;
        asm volatile("":::"memory");
        while (node->count[which] != 0)
                continue;

        node->which = which;
        asm volatile("":::"memory");
        while (node->count[!which] != 0)
                continue;

        pthread_mutex_unlock(&node->lock);
}

void
prcu_wait_for_readers(predicate pred, int min_bucket,
                      range_predicate pred_next, predicate_info pred_info)
{
        /* fence to order against previous writes by updater, and the
         * subsequent loads.
         */
        asm volatile("mfence":::"memory");

        /* this PRCU variant uses range predicates */
        int bucket = min_bucket;
        while (bucket >= 0) {
                wait(&prcu_table[bucket]);
                bucket = pred_next(pred_info, bucket);
        }
}
