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
 * Converted into a class and implemented as a 3-path algorithm by Trevor Brown
 * (Experimental implementation that uses 3-paths w/HTM to accelerate RCU.)
 */

#ifndef CITRUS_IMPL_H
#define CITRUS_IMPL_H

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <utility>
#include "citrus.h"
#include "../globals_extern.h"
#include "../common/rtm.h"
#include "../debugcounters.h"
using namespace std;

struct node_t {
    int key;
    struct node_t *child[2] __attribute__((aligned(16)));
    volatile int lock;
    bool marked;
    int tag[2];
    int value;
};

struct predicate_info_t {
    int min_key;
    int max_key;
    int max;
};

// max key in range is global so that pred_hash can be a static non-member func
// (so it can be passed to prcu_set_hash in the citrustree constructor)
static int max_key_in_range;

bool pred(predicate_info info, int value) {
    return (info->min_key < value && info->max_key >= value);

}

int pred_hash(hash_info info, int key) {
    int num_buckets = prcu_get_size();
    assert(num_buckets > 0);
    if (num_buckets > max_key_in_range) {
        return key; //put each value in it's own bucket
    }
    int result;
    int num_elemets_per_bucket = max_key_in_range / num_buckets;
    assert(num_elemets_per_bucket > 0);
    int overflow = max_key_in_range - (num_buckets * num_elemets_per_bucket);
    if (overflow == 0) {
        result = (key / num_elemets_per_bucket);
    } else {
        //The first |overflow| buckets should have an extra key
        int threshold = overflow * (num_elemets_per_bucket + 1);
        if (key < threshold) {
            result = (key / (num_elemets_per_bucket + 1));
        } else {
            result = overflow + ((key - (threshold)) / num_elemets_per_bucket);
        }
    }
    assert(result < num_buckets);
    return result;
}

int pred_next(predicate_info info, int curr_bucket) {
    int max_bucket = pred_hash(NULL, info->max_key);
    if (curr_bucket < max_bucket) {
        return curr_bucket + 1;
    } else {
        return -1;
    }
}

template <class RecManager>
nodeptr citrustree<RecManager>::newNode(const int tid, int key, int value) {
    nodeptr nnode = recordmgr->template allocate<node_t>(tid);
    //nodeptr nnode = (nodeptr) malloc(sizeof (struct node_t));
    if (nnode == NULL) {
        printf("out of memory\n");
        exit(1);
    }
    nnode->key = key;
    nnode->marked = false;
    nnode->child[0] = NULL;
    nnode->child[1] = NULL;
    nnode->tag[0] = 0;
    nnode->tag[1] = 0;
    nnode->value = value;
//    if (pthread_mutex_init(&(nnode->lock), NULL) != 0) {
//        printf("\n mutex init failed\n");
//    }
    nnode->lock = false;
    return nnode;
}

template <class RecManager>
citrustree<RecManager>::citrustree(int max_key, const int numProcesses)
        : counters(new debugCounters(numProcesses))
        , recordmgr(new RecManager(numProcesses, SIGQUIT)) {
    const int tid = 0;
    initThread(tid);
    // finish initializing RCU
    if (max_key < 0) {
        max_key_in_range = infinity;
    } else {
        max_key_in_range = max_key + 1;
    }
    prcu_set_hash(pred_hash, NULL);
    root = newNode(tid, infinity, 0);
    root->child[0] = newNode(tid, infinity, 0);
    lock = 0;
}

template <class RecManager>
int citrustree<RecManager>::contains(const int tid, int key) {
    recordmgr->leaveQuiescentState(tid);
    prcu_enter(key);
    nodeptr curr = root->child[0];
    int ckey = curr->key;
    while (curr != NULL && ckey != key) {
        if (ckey > key)
            curr = curr->child[0];
        if (ckey < key)
            curr = curr->child[1];
        if (curr != NULL)
            ckey = curr->key;
    }
    prcu_exit(key);
    if (curr == NULL) {
        recordmgr->enterQuiescentState(tid);
        return -1;
    }
    int result = curr->value;
    recordmgr->enterQuiescentState(tid);
    return result;
}

template <class RecManager>
int citrustree<RecManager>::contains_tle(const int tid, int key) {
    recordmgr->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, counters->pathSuccess, counters->pathFail, counters->htmAbort);

    nodeptr curr = root->child[0];
    int ckey = curr->key;
    while (curr != NULL && ckey != key) {
        if (ckey > key)
            curr = curr->child[0];
        if (ckey < key)
            curr = curr->child[1];
        if (curr != NULL)
            ckey = curr->key;
    }
    if (curr == NULL) {
        scope.end();
        recordmgr->enterQuiescentState(tid);
        return -1;
    }
    int result = curr->value;
    scope.end();
    recordmgr->enterQuiescentState(tid);
    return result;
}

bool validate(nodeptr prev, int tag, nodeptr curr, int direction) {
    bool result;
    if (curr == NULL) {
        result = (!(prev->marked) && (prev->child[direction] == curr)
                    && (prev->tag[direction] == tag));
    } else {
        result = (!(prev->marked) && !(curr->marked)
                    && prev->child[direction] == curr);
    }
    return result;
}










#define THREE_PATH_BEGIN(info) \
    info.path = (MAX_FAST_HTM_RETRIES >= 0 ? PATH_FAST_HTM : MAX_SLOW_HTM_RETRIES >= 0 ? PATH_SLOW_HTM : PATH_FALLBACK); \
    int attempts = 0; \
    bool finished = 0; \
    for (;;) { \
        info.numberOfNodesAllocated = 0; \
        info.lastAbort = 0;

#define THREE_PATH_END(info, finished, countersSucc, countersFail, countersAbort) \
        ++attempts; \
        if (finished) { \
            if ((info.path == PATH_FALLBACK) && (MAX_FAST_HTM_RETRIES >= 0 || MAX_SLOW_HTM_RETRIES >= 0)) { \
                __sync_fetch_and_add(&numFallback, -1); \
            } \
            counters->pathFail[info.path]->add(tid, attempts-1); \
            counters->pathSuccess[info.path]->inc(tid); \
            break; /*return finished.second;*/ \
        } \
        switch (info.path) { \
            case PATH_FAST_HTM: \
                /* check if we should change paths */ \
                if (attempts > MAX_FAST_HTM_RETRIES) { \
                    counters->pathFail[info.path]->add(tid, attempts); \
                    attempts = 0; \
                    if (MAX_SLOW_HTM_RETRIES < 0) { \
                        info.path = PATH_FALLBACK; \
                        __sync_fetch_and_add(&numFallback, 1); \
                    } else { \
                        info.path = PATH_SLOW_HTM; \
                    } \
                /* MOVE TO THE MIDDLE PATH IMMEDIATELY IF SOMEONE IS ON THE FALLBACK PATH */ \
                } else if ((info.lastAbort >> 24) == ABORT_PROCESS_ON_FALLBACK && MAX_SLOW_HTM_RETRIES >= 0) { \
                    attempts = 0; \
                    info.path = PATH_SLOW_HTM; \
                /* if there is no middle path, wait for the fallback path to be empty */ \
                } else if (MAX_SLOW_HTM_RETRIES < 0) { \
                    while (numFallback > 0) { __asm__ __volatile__("pause;"); } \
                } \
                break; \
            case PATH_SLOW_HTM: \
                /* check if we should change paths */ \
                if (attempts > MAX_SLOW_HTM_RETRIES) { \
                    counters->pathFail[info.path]->add(tid, attempts); \
                    attempts = 0; \
                    info.path = PATH_FALLBACK; \
                    __sync_fetch_and_add(&numFallback, 1); \
                } \
                break; \
            case PATH_FALLBACK: { \
                /** BEGIN DEBUG **/ \
                const int MAX_ATTEMPTS = 1000000; \
                if (attempts == MAX_ATTEMPTS) { cout<<"ERROR: more than "<<MAX_ATTEMPTS<<" attempts on fallback"<<endl; TRACE_ON; } \
                if (attempts > 2*MAX_ATTEMPTS) { cout<<"ERROR: more than "<<(2*MAX_ATTEMPTS)<<" attempts on fallback"<<endl; exit(-1); } \
                /** END DEBUG **/ \
                } \
                break; \
            default: \
                cout<<"reached impossible switch case"<<endl; \
                exit(-1); \
                break; \
        } \
    }







#define SEARCH \
        prev = root;\
        curr = root->child[0];\
        direction = 0;\
        ckey = curr->key;\
        while (curr != NULL && ckey != key) {\
            prev = curr;\
            if (ckey > key) {\
                curr = curr->child[0];\
                direction = 0;\
            }\
            if (ckey < key) {\
                curr = curr->child[1];\
                direction = 1;\
            }\
            if (curr != NULL)\
                ckey = curr->key;\
        }

template <class RecManager>
bool citrustree<RecManager>::insert_fast(const int tid, wrapper_info *info, int key, int value, bool * const inserted) {
    nodeptr prev;
    nodeptr curr;
    int direction;
    int ckey;
    int tag;
    
    int attempts = MAX_FAST_HTM_RETRIES;
TXN1: (0);
    recordmgr->leaveQuiescentState(tid);
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        if (numFallback > 0) { XABORT(ABORT_PROCESS_ON_FALLBACK); }
        SEARCH;
//        /** DEBUG */ tag = prev->tag[direction];
        if (curr != NULL) {
#ifndef INSERT_REPLACE
#else
            curr->value = value;
#endif
            XEND();
            recordmgr->enterQuiescentState(tid);
            *inserted = false;
            return true;
        }
//        /** DEBUG */ if (prev->tag[direction] != tag) { XABORT(ABORT_UPDATE_FAILED); }
        nodeptr nnode = newNode(tid, key, value);
        prev->child[direction] = nnode;
        XEND();
        recordmgr->enterQuiescentState(tid);
        *inserted = true;
        return true;
    } else { // aborted
        info->lastAbort = status;
        recordmgr->enterQuiescentState(tid);
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
//        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        *inserted = false;
        return false;
    }
}

template <class RecManager>
bool citrustree<RecManager>::insert_middle(const int tid, wrapper_info *info, int key, int value, bool * const inserted) {
    nodeptr prev;
    nodeptr curr;
    int direction;
    int ckey;
    int tag;
    
TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    recordmgr->leaveQuiescentState(tid);
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
//            prcu_enter(key);
        SEARCH;
        tag = prev->tag[direction];
#ifdef PRCU_EXIT_OUTSIDE_TXN
        XEND();
    } else { // aborted
        info->lastAbort = status;
        recordmgr->enterQuiescentState(tid);
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
        *inserted = false;
        return false;
    }

    prcu_exit(key);

    status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
#else
//            prcu_exit(key);
#endif
        if (curr != NULL) {
#ifndef INSERT_REPLACE
            XEND();
            recordmgr->enterQuiescentState(tid);
            *inserted = false;
            return true;
#else
            if (readLock(&(prev->lock))) { XABORT(ABORT_UPDATE_FAILED); }
            if (validate(prev, tag, curr, direction)) {
                if (readLock(&(curr->lock))) { XABORT(ABORT_UPDATE_FAILED); }
                curr->marked = true;
                nodeptr nnode = newNode(tid, key, value);
                nnode->child[0] = curr->child[0];
                nnode->child[1] = curr->child[1];
                prev->child[direction] = nnode;
                XEND();
                recordmgr->enterQuiescentState(tid);
                *inserted = false;
                return true;
            }
            XEND();
            recordmgr->enterQuiescentState(tid);
            *inserted = false;
            return false;
#endif
        }
        if (readLock(&(prev->lock))) { XABORT(ABORT_UPDATE_FAILED); }
        if (validate(prev, tag, curr, direction)) {
            nodeptr nnode = newNode(tid, key, value);
            prev->child[direction] = nnode;
//                releaseLock(&(prev->lock));
            XEND();
            recordmgr->enterQuiescentState(tid);
            *inserted = true;
            return true;
        }
        XEND();
        recordmgr->enterQuiescentState(tid);
        *inserted = false;
        return false;
    } else {
        // aborted
        recordmgr->enterQuiescentState(tid);
//        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        *inserted = false;
        return false;
    }
}

template <class RecManager>
bool citrustree<RecManager>::insert_fallback(const int tid, wrapper_info *info, int key, int value, bool * const inserted) {
    nodeptr prev;
    nodeptr curr;
    int direction;
    int ckey;
    int tag;

    recordmgr->leaveQuiescentState(tid);
    prcu_enter(key);
    SEARCH;
    tag = prev->tag[direction];
    prcu_exit(key);
    if (curr != NULL) {
#ifndef INSERT_REPLACE
        recordmgr->enterQuiescentState(tid);
        *inserted = false;
        return true;
#else
        acquireLock(&(prev->lock));
        if (validate(prev, tag, curr, direction)) {
            acquireLock(&(curr->lock));
            curr->marked = true;
            nodeptr nnode = newNode(tid, key, value);
            nnode->child[0] = curr->child[0];
            nnode->child[1] = curr->child[1];
            prev->child[direction] = nnode;

            releaseLock(&(curr->lock));
            releaseLock(&(prev->lock));
            recordmgr->enterQuiescentState(tid);
            *inserted = false;
            return true;
        }
        releaseLock(&(prev->lock));
        recordmgr->enterQuiescentState(tid);
        *inserted = false;
        return false;
#endif
    }

    acquireLock(&(prev->lock));
    if (validate(prev, tag, curr, direction)) {
        nodeptr nnode = newNode(tid, key, value);
        prev->child[direction] = nnode;

        releaseLock(&(prev->lock));
        recordmgr->enterQuiescentState(tid);
        *inserted = true;
        return true;
    } else {
        releaseLock(&(prev->lock));
        recordmgr->enterQuiescentState(tid);
        *inserted = false;
        return false;
    }
}

template <class RecManager>
bool citrustree<RecManager>::insert_tle(const int tid, int key, int value) {
    recordmgr->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, counters->pathSuccess, counters->pathFail, counters->htmAbort);
    
    nodeptr prev;
    nodeptr curr;
    int direction;
    int ckey;
    int tag;
    
    SEARCH;
    if (curr != NULL) {
#ifndef INSERT_REPLACE
#else
        curr->value = value;
#endif
        scope.end();
        recordmgr->enterQuiescentState(tid);
        return false;
    }
    nodeptr nnode = newNode(tid, key, value);
    prev->child[direction] = nnode;
    scope.end();
    recordmgr->enterQuiescentState(tid);
    return true;
}

template <class RecManager>
bool citrustree<RecManager>::insert(const int tid, int key, int value) {
    bool retval = false;
    bool inserted = false;
    wrapper_info info;
    THREE_PATH_BEGIN(info);
    if (info.path == PATH_FAST_HTM) {
        retval = insert_fast(tid, &info, key, value, &inserted);
    } else if (info.path == PATH_SLOW_HTM) {
        retval = insert_middle(tid, &info, key, value, &inserted);
    } else if (info.path == PATH_FALLBACK) {
        retval = insert_fallback(tid, &info, key, value, &inserted);
    }
    THREE_PATH_END(info, retval, counters->pathSuccess, counters->pathFail, counters->htmAbort);
    return inserted;
}

template<class RecManager>
bool citrustree<RecManager>::erase_fast(const int tid, wrapper_info *info, int key, bool * const erased) {
    nodeptr prev;
    nodeptr curr;
    int direction;
    int ckey;
    
    nodeptr prevSucc;
    nodeptr succ;
    nodeptr next;

TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    recordmgr->leaveQuiescentState(tid);
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        if (numFallback > 0) { XABORT(ABORT_PROCESS_ON_FALLBACK); }
        SEARCH;
        if (curr == NULL) {
            XEND(); // note: this line was accidentally omitted in the previous, complex interleaved 3path code
            recordmgr->enterQuiescentState(tid);
            *erased = false;
            return true;
        }
        for (int edir=0;edir<2;++edir) {
            if (curr->child[edir] == NULL) {
                prev->child[direction] = curr->child[1-edir];
//                /** DEBUG */ if (prev->child[direction] == NULL) { prev->tag[direction]++; }
                XEND();
                recordmgr->enterQuiescentState(tid);
                recordmgr->retire(tid, curr);
                *erased = true;
                return true;
            }
        }
        prevSucc = curr;
        succ = curr->child[1];
        next = succ->child[0];
        while (next != NULL) {
            prevSucc = succ;
            succ = next;
            next = next->child[0];
        }
        int succDirection = 1;
        if (prevSucc != curr) {
            succDirection = 0;
        }

        curr->key = succ->key;
        curr->value = succ->value;
        curr->tag[0] = 0;
        curr->tag[1] = 0;

        if (prevSucc == curr) {
            curr->child[1] = succ->child[1];
//            /** DEBUG */ if (curr->child[1] == NULL) { curr->tag[1]++; }
        } else {
            prevSucc->child[0] = succ->child[1];
//            /** DEBUG */ if (prevSucc->child[0] == NULL) { prevSucc->tag[0]++; }
        }
        XEND();
        recordmgr->enterQuiescentState(tid);
        recordmgr->retire(tid, succ);
        *erased = true;
        return true;
    } else { // aborted
        info->lastAbort = status;
        recordmgr->enterQuiescentState(tid);
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
//        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        *erased = false;
        return false;
    }
}

template<class RecManager>
bool citrustree<RecManager>::erase_middle(const int tid, wrapper_info *info, int key, bool * const erased) {
    nodeptr prev;
    nodeptr curr;
    int direction;
    int ckey;
    
    nodeptr prevSucc;
    nodeptr succ;
    nodeptr next;
    nodeptr nnode;
    
    int min_bucket;
    struct predicate_info_t pred_info;

TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    recordmgr->leaveQuiescentState(tid);
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
//            prcu_enter(key);
        SEARCH;
#ifdef PRCU_EXIT_OUTSIDE_TXN
        XEND();
    } else { // aborted
        info->lastAbort = status;
        recordmgr->enterQuiescentState(tid);
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
        *erased = false;
        return false;
    }

    prcu_exit(key);
    if (curr == NULL) {
        recordmgr->enterQuiescentState(tid);
        *erased = false;
        return true;
    }

    status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        /* note: if-statement added for compatibility with fast path
         * (needed since fast path modified keys in addition to replacing nodes) */
        if (curr->key != key) { XABORT(ABORT_UPDATE_FAILED); }
#else
//            prcu_exit(key);
        if (curr == NULL) {
            XEND();
            recordmgr->enterQuiescentState(tid);
            *erased = false;
            return true;
        }
#endif
        if (readLock(&(prev->lock))) { XABORT(ABORT_UPDATE_FAILED); }
        if (readLock(&(curr->lock))) { XABORT(ABORT_UPDATE_FAILED); }
        if (!validate(prev, 0, curr, direction)) {
            XEND();
            recordmgr->enterQuiescentState(tid);
            *erased = false;
            return false;
        }
        if (curr->child[0] == NULL) {
            curr->marked = true;
            prev->child[direction] = curr->child[1];
            if (prev->child[direction] == NULL) {
                prev->tag[direction]++;
            }
            XEND();
            recordmgr->enterQuiescentState(tid);
            recordmgr->retire(tid, curr);
            *erased = true;
            return true;
        }
        if (curr->child[1] == NULL) {
            curr->marked = true;
            prev->child[direction] = curr->child[0];
            if (prev->child[direction] == NULL) {
                prev->tag[direction]++;
            }
            XEND();
            recordmgr->enterQuiescentState(tid);
            recordmgr->retire(tid, curr);
            *erased = true;
            return true;
        }
        prevSucc = curr;
        succ = curr->child[1];
        next = succ->child[0];
        while (next != NULL) {
            prevSucc = succ;
            succ = next;
            next = next->child[0];
        }
        int succDirection = 1;
        if (prevSucc != curr) {
            if (readLock(&(prevSucc->lock))) { XABORT(ABORT_UPDATE_FAILED); }
            succDirection = 0;
        }
        if (readLock(&(succ->lock))) { XABORT(ABORT_UPDATE_FAILED); }
        if (validate(prevSucc, 0, succ, succDirection) && validate(succ, succ->tag[0], NULL, 0)) {
            curr->marked = true;
            nnode = newNode(tid, succ->key, succ->value);
            nnode->child[0] = curr->child[0];
            nnode->child[1] = curr->child[1];
            if (readLock(&(nnode->lock))) { XABORT(ABORT_UPDATE_FAILED); }
            prev->child[direction] = nnode;
//                pred_info.min_key = key;
//                pred_info.max_key = succ->key;
//                pred_info.max = max_key_in_range;
//                min_bucket = pred_hash(NULL, key);
//                int max_bucket = pred_hash(NULL, succ->key);
//                assert(min_bucket <= max_bucket);
//                prcu_wait_for_readers(pred, min_bucket, pred_next, &pred_info);
            succ->marked = true;
            if (prevSucc == curr) {
                nnode->child[1] = succ->child[1];
                if (nnode->child[1] == NULL) {
                    nnode->tag[1]++;
                }
            } else {
                prevSucc->child[0] = succ->child[1];
                if (prevSucc->child[0] == NULL) {
                    prevSucc->tag[0]++;
                }
            }
            XEND();
            recordmgr->enterQuiescentState(tid);
            recordmgr->retire(tid, succ);
            recordmgr->retire(tid, curr);
            *erased = true;
            return true;
        }
        XEND();
        recordmgr->enterQuiescentState(tid);
        *erased = false;
        return false;
    } else { // aborted
        info->lastAbort = status;
        recordmgr->enterQuiescentState(tid);
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
//        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        *erased = false;
        return false;
    }
}

template<class RecManager>
bool citrustree<RecManager>::erase_fallback(const int tid, wrapper_info *info, int key, bool * const erased) {
    nodeptr prev;
    nodeptr curr;
    int direction;
    int ckey;
    
    nodeptr prevSucc;
    nodeptr succ;
    nodeptr next;
    nodeptr nnode;
    
    int min_bucket;
    struct predicate_info_t pred_info;

    recordmgr->leaveQuiescentState(tid);
    prcu_enter(key);
    SEARCH;
    prcu_exit(key);
    if (curr == NULL) {
        recordmgr->enterQuiescentState(tid);
        *erased = false;
        return true;
    }
    acquireLock(&(prev->lock));
    acquireLock(&(curr->lock));
    if (!validate(prev, 0, curr, direction)) {
        releaseLock(&(prev->lock));
        releaseLock(&(curr->lock));
        recordmgr->enterQuiescentState(tid);
        *erased = false;
        return false;
    }
    if (curr->child[0] == NULL) {
        curr->marked = true;
        prev->child[direction] = curr->child[1];
        if (prev->child[direction] == NULL) {
            prev->tag[direction]++;
        }
        releaseLock(&(prev->lock));
        releaseLock(&(curr->lock));
        recordmgr->enterQuiescentState(tid);
        recordmgr->retire(tid, curr);
        *erased = true;
        return true;
    }
    if (curr->child[1] == NULL) {
        curr->marked = true;
        prev->child[direction] = curr->child[0];
        if (prev->child[direction] == NULL) {
            prev->tag[direction]++;
        }
        releaseLock(&(prev->lock));
        releaseLock(&(curr->lock));
        recordmgr->enterQuiescentState(tid);
        recordmgr->retire(tid, curr);
        *erased = true;
        return true;
    }
    prevSucc = curr;
    succ = curr->child[1];
    next = succ->child[0];
    while (next != NULL) {
        prevSucc = succ;
        succ = next;
        next = next->child[0];
    }
    int succDirection = 1;
    if (prevSucc != curr) {
        acquireLock(&(prevSucc->lock));
        succDirection = 0;
    }
    acquireLock(&(succ->lock));
    if (validate(prevSucc, 0, succ, succDirection) && validate(succ, succ->tag[0], NULL, 0)) {
        curr->marked = true;
        nnode = newNode(tid, succ->key, succ->value);
        nnode->child[0] = curr->child[0];
        nnode->child[1] = curr->child[1];
        acquireLock(&(nnode->lock));
        prev->child[direction] = nnode;
        struct predicate_info_t pred_info = {.min_key = key, .max_key =
            succ->key, .max = max_key_in_range};
        int min_bucket = pred_hash(NULL, key);
        int max_bucket = pred_hash(NULL, succ->key);
        assert(min_bucket <= max_bucket);
        prcu_wait_for_readers(pred, min_bucket, pred_next, &pred_info);

        succ->marked = true;
        if (prevSucc == curr) {
            nnode->child[1] = succ->child[1];
            if (nnode->child[1] == NULL) {
                nnode->tag[1]++;
            }
        } else {
            prevSucc->child[0] = succ->child[1];
            if (prevSucc->child[0] == NULL) {
                prevSucc->tag[0]++;
            }
        }
        releaseLock(&(prev->lock));
        releaseLock(&(nnode->lock));
        releaseLock(&(curr->lock));
        if (prevSucc != curr)
            releaseLock(&(prevSucc->lock));
        releaseLock(&(succ->lock));
        recordmgr->enterQuiescentState(tid);
        recordmgr->retire(tid, succ);
        recordmgr->retire(tid, curr);
        *erased = true;
        return true;
    }
    releaseLock(&(prev->lock));
    releaseLock(&(curr->lock));
    if (prevSucc != curr)
        releaseLock(&(prevSucc->lock));
    releaseLock(&(succ->lock));
    recordmgr->enterQuiescentState(tid);
    *erased = false;
    return false;
}

template <class RecManager>
bool citrustree<RecManager>::erase_tle(const int tid, int key) {
    recordmgr->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, counters->pathSuccess, counters->pathFail, counters->htmAbort);
        
    nodeptr prev;
    nodeptr curr;
    int direction;
    int ckey;
    
    nodeptr prevSucc;
    nodeptr succ;
    nodeptr next;
    nodeptr nnode;
    
    SEARCH;
    if (curr == NULL) {
        scope.end();
        recordmgr->enterQuiescentState(tid);
        return false;
    }
    if (curr->child[0] == NULL) {
        prev->child[direction] = curr->child[1];
        scope.end();
        recordmgr->enterQuiescentState(tid);
        return true;
    }
    if (curr->child[1] == NULL) {
        prev->child[direction] = curr->child[0];
        scope.end();
        recordmgr->enterQuiescentState(tid);
        return true;
    }
    prevSucc = curr;
    succ = curr->child[1];
    next = succ->child[0];
    while (next != NULL) {
        prevSucc = succ;
        succ = next;
        next = next->child[0];
    }
    int succDirection = 1;
    if (prevSucc != curr) {
        succDirection = 0;
    }

    curr->key = succ->key;
    curr->value = succ->value;

    if (prevSucc == curr) {
        curr->child[1] = succ->child[1];
    } else {
        prevSucc->child[0] = succ->child[1];
    }
    scope.end();
    recordmgr->enterQuiescentState(tid);
    return true;
}

template <class RecManager>
bool citrustree<RecManager>::erase(const int tid, int key) {
    bool retval = false;
    bool erased = false;
    wrapper_info info;
    THREE_PATH_BEGIN(info);
    if (info.path == PATH_FAST_HTM) {
        retval = erase_fast(tid, &info, key, &erased);
    } else if (info.path == PATH_SLOW_HTM) {
        retval = erase_middle(tid, &info, key, &erased);
    } else if (info.path == PATH_FALLBACK) {
        retval = erase_fallback(tid, &info, key, &erased);
    }
    THREE_PATH_END(info, retval, counters->pathSuccess, counters->pathFail, counters->htmAbort);
    return erased;
}

template <class RecManager>
bool citrustree<RecManager>::rq_fast(const int tid, wrapper_info *info, int lo, int hi, int *results, int * const cnt) {
    nodeptr curr;
    int direction;
    int ckey;
    
    const int MAX_STACK_SIZE = 1<<8;
    nodeptr dfsstack[MAX_STACK_SIZE];
    int si = 0; // stack index (top of stack)
    int ri = 0; // results index (size of results)

TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    recordmgr->leaveQuiescentState(tid);
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        //if (numFallback > 0) { XABORT(ABORT_PROCESS_ON_FALLBACK); }
        dfsstack[si++] = root;
        while (si > 0) {
            curr = dfsstack[--si];
            int key = curr->key;
            if (lo < key && curr->child[0]) {
                dfsstack[si++] = curr->child[0];
            }
            if (hi > key && curr->child[1]) {
                dfsstack[si++] = curr->child[1];
            }
            if (lo <= key && key <= hi) {
                results[ri++] = key;
            }
        }
        XEND();
        recordmgr->enterQuiescentState(tid);
        *cnt = ri;
        return true;
    } else { // aborted
        info->lastAbort = status;
        recordmgr->enterQuiescentState(tid);
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
//        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        *cnt = 0;
        return false;
    }
}

template <class RecManager>
bool citrustree<RecManager>::rq_middle(const int tid, wrapper_info *info, int lo, int hi, int *results, int * const cnt) {
    nodeptr curr;
    int direction;
    int ckey;
    
    const int MAX_STACK_SIZE = 1<<8;
    nodeptr dfsstack[MAX_STACK_SIZE];
    int si = 0; // stack index (top of stack)
    int ri = 0; // results index (size of results)

TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    recordmgr->leaveQuiescentState(tid);
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        dfsstack[si++] = root;
        while (si > 0) {
            curr = dfsstack[--si];
//            if (readLock(&curr->lock)) { XABORT(ABORT_LOCK_HELD); }
            int key = curr->key;
            if (lo < key && curr->child[0]) {
                dfsstack[si++] = curr->child[0];
            }
            if (hi > key && curr->child[1]) {
                dfsstack[si++] = curr->child[1];
            }
            if (lo <= key && key <= hi) {
                results[ri++] = key;
            }
        }
        XEND();
        recordmgr->enterQuiescentState(tid);
        *cnt = ri;
        return true;
    } else { // aborted
        info->lastAbort = status;
        recordmgr->enterQuiescentState(tid);
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
//        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        *cnt = 0;
        return false;
    }
}

template <class RecManager>
bool citrustree<RecManager>::rq_fallback(const int tid, wrapper_info *info, int lo, int hi, int *results, int * const cnt) {
    nodeptr curr;
    
    const int MAX_STACK_SIZE = 1<<8;
    nodeptr dfsstack[MAX_STACK_SIZE];
    int si; // stack index (top of stack)
    int ri; // results index (size of results)
    bool same; // were this scan's results the same as the previous scan's?

//    const int MAX_LOCKSET_SIZE = 1<<16;
//    nodeptr lockset[MAX_LOCKSET_SIZE];
//    int li = 0; // lockset index (size of lockset)
//    bool succ = true;

    bool first = true;
    int attempts = 10;
    bool in_rcu;
retry:
    recordmgr->leaveQuiescentState(tid);
    prcu_enter(lo); // this needs to end before i take a lock!!!
    in_rcu = true;
    si = 0;
    ri = 0;
    same = true;
    dfsstack[si++] = root;
    while (si > 0) {
        curr = dfsstack[--si];
////        acquireLock(&curr->lock);
////        lockset[li++] = curr;
        int key = curr->key;
        if (lo < key && curr->child[0]) {
            dfsstack[si++] = curr->child[0];
        }
        if (hi > key && curr->child[1]) {
            dfsstack[si++] = curr->child[1];
        }
        if (lo <= key && key <= hi) {
//            if (in_rcu) { prcu_exit(lo); in_rcu = false; }
//            acquireLock(&curr->lock);
//            lockset[li++] = curr;
//            if (curr->marked) { succ = false; break; }
            if (first || results[ri] != key) {
                results[ri] = key;
                same = false;
            }
            ri++;
        }
    }
    first = false;
    if (in_rcu) { prcu_exit(lo); in_rcu = false; }
    // check if last 2 snapshots match
    if (!same && --attempts > 0) {
        recordmgr->enterQuiescentState(tid);
        recordmgr->leaveQuiescentState(tid);
        goto retry;
    }
    if (attempts > 0) {
        recordmgr->enterQuiescentState(tid);
        *cnt = ri;
        return true;
    } else {
        // fall back to locking after exhausting our limit on scan attempts
        const int MAX_LOCKSET_SIZE = 1<<16;
        nodeptr lockset[MAX_LOCKSET_SIZE];
        int li = 0; // lockset index (size of lockset)
        
        prcu_enter(lo); // this needs to end before we take a lock!
        in_rcu = true;
        si = 0;
        ri = 0;
        dfsstack[si++] = root;
        while (si > 0) {
            curr = dfsstack[--si];
            int key = curr->key;
            if (lo < key && curr->child[0]) {
                dfsstack[si++] = curr->child[0];
            }
            if (hi > key && curr->child[1]) {
                dfsstack[si++] = curr->child[1];
            }
            if (lo <= key && key <= hi) {
                if (in_rcu) { prcu_exit(lo); in_rcu = false; }
                acquireLock(&curr->lock);
                lockset[li++] = curr;
                results[ri++] = key;
            }
        }
        if (in_rcu) { prcu_exit(lo); in_rcu = false; }
        for (int i=0;i<li;++i) {
            releaseLock(&lockset[i]->lock);
        }
        recordmgr->enterQuiescentState(tid);
        *cnt = ri;
        return true;
    }

//    for (int i=0;i<li;++i) {
//        releaseLock(&lockset[i]->lock);
//    }

//    return make_pair(succ, ri);
}

template <class RecManager>
int citrustree<RecManager>::rangeQuery_tle(const int tid, int lo, int hi, int *results) {
    recordmgr->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, counters->pathSuccess, counters->pathFail, counters->htmAbort);
    
    nodeptr curr;
    int direction;
    int ckey;
    
    const int MAX_STACK_SIZE = 1<<8;
    nodeptr dfsstack[MAX_STACK_SIZE];
    int si = 0; // stack index (top of stack)
    int ri = 0; // results index (size of results)

    dfsstack[si++] = root;
    while (si > 0) {
        curr = dfsstack[--si];
        int key = curr->key;
        if (lo < key && curr->child[0]) {
            dfsstack[si++] = curr->child[0];
        }
        if (hi > key && curr->child[1]) {
            dfsstack[si++] = curr->child[1];
        }
        if (lo <= key && key <= hi) {
            results[ri++] = key;
        }
    }
    recordmgr->enterQuiescentState(tid);
    return ri;
}

template <class RecManager>
int citrustree<RecManager>::rangeQuery(const int tid, int lo, int hi, int *results) {
//    const int RQ_SIZE_THRESHOLD = 0;
    bool retval = false;
    int cnt = 0;
    wrapper_info info;
    THREE_PATH_BEGIN(info);
//    if (hi-lo+1 > RQ_SIZE_THRESHOLD) info.path = PATH_FALLBACK;
    if (info.path == PATH_FAST_HTM) {
        retval = rq_fast(tid, &info, lo, hi, results, &cnt);
    } else if (info.path == PATH_SLOW_HTM) {
        retval = rq_middle(tid, &info, lo, hi, results, &cnt);
    } else if (info.path == PATH_FALLBACK) {
        retval = rq_fallback(tid, &info, lo, hi, results, &cnt);
    }
    THREE_PATH_END(info, retval, counters->pathSuccess, counters->pathFail, counters->htmAbort);
    return cnt;
}

template <class RecManager>
long long citrustree<RecManager>::debugKeySum(nodeptr root) {
    if (root == NULL) return 0;
    return root->key + debugKeySum(root->child[0]) + debugKeySum(root->child[1]);
}

template <class RecManager>
long long citrustree<RecManager>::debugKeySum() {
    return debugKeySum(root->child[0]->child[0]);
}

#endif