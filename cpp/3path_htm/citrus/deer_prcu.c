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

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "prcu.h"
#include "tsc.h"

typedef struct rcu_node_t {
        volatile long unsigned int time;
        char p[184];
} rcu_node;

#ifndef RCU_TABLE_SIZE
#define RCU_TABLE_SIZE	16
#endif

static int threads;
static int matrix_size = RCU_TABLE_SIZE;
static rcu_node ***prcu_table;
static predicate_hash hash_func;
static hash_info info;


void
prcu_init(int num_threads)
{
        rcu_node ***result =
                (rcu_node ***) malloc(sizeof(rcu_node) * num_threads);
        if (result == NULL) {
                printf("malloc failed\n");
                exit(1);
        }
        int i, j;
        rcu_node *nnode;
        threads = num_threads;
        for (i = 0; i < threads; i++) {
                result[i] = (rcu_node **) malloc(sizeof(rcu_node) * matrix_size);
                if (result[i] == NULL) {
                        printf("malloc failed\n");
                        exit(1);
                }
                for (j = 0; j < matrix_size; j++) {
                        nnode = (rcu_node *) malloc(sizeof(rcu_node));
                        if (nnode == NULL) {
                                printf("malloc failed\n");
                                exit(1);
                        }
                        nnode->time = 1;
                        result[i][j] = nnode;
                }
        }
        prcu_table = result;
        printf("node size is %zd\n", sizeof(struct rcu_node_t));
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
        return matrix_size;
}

static __thread long *times = NULL;
static __thread int i;

void
prcu_register(int id)
{
        times = (long *) malloc(sizeof(long) * threads);
        if (times == NULL) {
                printf("malloc failed\n");
                exit(1);
        }
        i = id;
}

void
prcu_unregister()
{
        free(times);
}

void
prcu_enter(int value)
{
        int j = hash_func(info, value);
        assert(j > -1);
        assert(j < matrix_size);
        assert(prcu_table[i][j] != NULL);
        __sync_lock_test_and_set(&prcu_table[i][j]->time, read_tsc() << 1);
}

static inline void
set_bit(int nr, volatile unsigned long *addr)
{
        asm("btsl %1,%0": "+m"(*addr): "Ir"(nr));
}

void
prcu_exit(int value)
{
        int j = hash_func(info, value);
        assert(j > -1);
        assert(j < matrix_size);
        assert(prcu_table[i][j] != NULL);
        set_bit(0, &prcu_table[i][j]->time);
}

static inline void
wait(int j, uint64_t now)
{
        int i;
        for (i = 0; i < threads; i++) {
                while (1) {
                        unsigned long t = prcu_table[i][j]->time;
                        if (t & 1 || t > now) {
                                break;
                        }
                }
        }
}

void
prcu_wait_for_readers(predicate pred, int min_bucket,
                      range_predicate pred_next, predicate_info pred_info)
{
        /* fence to order against previous writes by updater, and the
         * subsequent loads (TSC and others).
         */
        asm volatile("mfence":::"memory");

        /* this PRCU variant uses range predicates */
        uint64_t now = read_tsc() << 1;
        int bucket = min_bucket;
        while (bucket >= 0) {
                wait(bucket, now);
                bucket = pred_next(pred_info, bucket);
        }
}
