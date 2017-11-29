/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   eer_prcu_imlp.h
 * Author: Maya Arbel-Raviv
 *
 * Created on August 20, 2017, 6:49 PM
 */

#ifndef EER_PRCU_IMPL_H
#define EER_PRCU_IMPL_H

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "urcu.h"
#include "tsc.h"

namespace urcu {

static int threads;
volatile char padding0[256];
static rcu_node *urcu_table;
volatile char padding1[256];

static __thread long *times = NULL;
static __thread int i;

void init(const int num_threads){
        rcu_node *result = (rcu_node *) malloc(sizeof(rcu_node) * num_threads);
        int i;
        threads = num_threads;
        for (i = 0; i < threads; i++) {
                rcu_node *nnode = result+i;
                //nnode = (rcu_node *) malloc(sizeof(rcu_node));
                nnode->time = 1;
                nnode->val1 = 0;
                nnode->val2 = 0;
                //result[i] = nnode;
        }
        urcu_table = result;
        printf("initializing URCU finished, node_size: %zd\n", sizeof (rcu_node));
        return;
}

//void
//prcu_set_hash(predicate_hash hash, hash_info sent_info){}
//
//int
//prcu_get_size(){
//        return 1;			//no buckets
//}

void deinit(const int numThreads) {
    //for (int i=0;i<numThreads;++i) {
    //    free(urcu_table[i]);
    //}
    free(urcu_table);
}

void registerThread(int id){
        times = (long *) malloc(sizeof(long) * threads);
        if (times == NULL) {
                printf("malloc failed\n");
                exit(1);
        }
        i = id;
}

void unregisterThread(){
        free(times);
        times = NULL;
}

void readLock(uint64_t val1, uint64_t val2){
        assert(urcu_table[i] != NULL);
        assert(val1 <= val2);
        urcu_table[i].val1 = val1;
        urcu_table[i].val2 = val2;
//        urcu_table[i].time = 0;
//        urcu_table[i].time = read_tsc()<<1;
#ifndef EERPRCU_DISABLE_RDTSC_TTAS
        __sync_lock_test_and_set(&urcu_table[i].time, read_tsc() << 1);
#endif
}

static inline void
set_bit(int nr, volatile unsigned long *addr){
        asm("btsl %1,%0": "+m"(*addr): "Ir"(nr));
}

void readUnlock(){
        assert(urcu_table[i] != NULL);
        set_bit(0, &urcu_table[i].time);
}

// used to require also int min, range_predicate pred_next, predicate_info pred_info
void synchronize(predicate pred, predicate_info pred_info)
{
        /* fence to order against previous writes by updater, and the
         * subsequent loads (TSC and others).
         */
        asm volatile("mfence":::"memory");

        /* this PRCU variant uses regular predicates */
        uint64_t now = read_tsc() << 1;
        int i;
        for (i = 0; i < threads; i++) {
                unsigned long t = urcu_table[i].time;
                SOFTWARE_BARRIER;
                uint64_t val1 = urcu_table[i].val1;
                uint64_t val2 = urcu_table[i].val2;
                SOFTWARE_BARRIER;
                // make sure we read the two values atomically
                // if not, the thread left the read-side critical section
                unsigned long t1 = urcu_table[i].time;
                if (t1 != t || t & 1 || t1 & 1) continue; 
                assert(val1 <= val2);
                if (!pred(pred_info, val1, val2))
                        continue;
                while (1) {
                        t = urcu_table[i].time;
                        if (t & 1 || t > now) {
                                break;
                        }
                }
        }
}

}

#endif /* EER_PRCU_IMPL_H */

