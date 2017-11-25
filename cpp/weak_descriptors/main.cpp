/**
 * Preliminary C++ implementation of binary search tree using LLX/SCX.
 * 
 * Copyright (C) 2014 Trevor Brown
 * This preliminary implementation is CONFIDENTIAL and may not be distributed.
 */

typedef int test_type;

#include <cmath>
#include <bitset>
#include <fstream>
#include <sstream>
#include <sched.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <ctime>
#include <set>
#include <typeinfo>
#include <pthread.h>
#include <atomic>
#include <chrono>
#include <time.h>
#include <random.h>
#include "debugcounters.h"
#include "globals.h"
#include "globals_extern.h"
#include <binding.h>
#include <papi_util_impl.h>
#include <memusage.h>

#ifndef EXPERIMENT_FN
#define EXPERIMENT_FN trial
#endif

#define eassert(x, y) if ((x) != (y)) { cout<<"ERROR: "<<#x<<" != "<<#y<<" :: "<<#x<<"="<<x<<" "<<#y<<"="<<y<<endl; exit(-1); }

#define MEASURE_TIME_TO_PREFILL

#define DEFAULT_SUSPECTED_SIGNAL SIGQUIT
static Random rngs[MAX_TID_POW2*PREFETCH_SIZE_WORDS]; // create per-thread random number generators (padded to avoid false sharing)

//extern __thread long long maxAllocatedBytes;

#if defined(BST)
#include "bst/bst_impl.h"
#elif defined(BST_THROWAWAY)
#include "bst_throwaway/bst_impl.h"
#elif defined(ABTREE)
#include "abtree/abtree_impl.h"
#elif defined(ABTREE_THROWAWAY) || defined(DUMMY_NOOP) || defined(DUMMY_ALLOC)
#include "abtree_throwaway/abtree_impl.h"
#elif defined(BSLACK_THROWAWAY)
#include "bslack_throwaway/bslack_impl.h"
#elif defined(BSLACK_REUSE)
#include "bslack_reuse/bslack_impl.h"
#else
#error "Failed to define a data structure"
#endif

#include <record_manager.h>

using namespace std;

// variables used in the concurrent test
chrono::time_point<chrono::high_resolution_clock> startTime;
chrono::time_point<chrono::high_resolution_clock> endTime;
long elapsedMillis;
long elapsedMillisNapping;

bool start = false;
bool done = false;
atomic_int running; // number of threads that are running
debugCounter * keysum; // key sum hashes for all threads (including for prefilling)
debugCounter * prefillSize;

// create a binary search tree with an allocator that uses a new form of epoch based memory reclamation
const test_type NO_KEY = -1;
const test_type NO_VALUE = -1;
const int RETRY = -2;
void *__tree;

const test_type NEG_INFTY = -1;
const test_type POS_INFTY = 2000000000;

#ifdef MEASURE_TIME_TO_PREFILL
const long long PREFILL_INTERVAL_MILLIS = 100;
#else
const long long PREFILL_INTERVAL_MILLIS = 200;
#endif
volatile long long prefillIntervalElapsedMillis;
//volatile long long memoryFootprintEnd = 0;
//volatile long long memoryFootprintPeak = 0;
//volatile long long memoryFootprintPrefill = 0;
//volatile long long memoryFootprintPrefillPeak = 0;

#if defined(BST) || defined(BST_THROWAWAY) || defined(BST_REUSE_PMARK)
#define DS_DECLARATION bst<test_type, test_type, less<test_type>, MemMgmt>
#elif defined(ABTREE) || defined(ABTREE_REUSE_PMARK) || defined(ABTREE_THROWAWAY) || defined(DUMMY_NOOP) || defined(DUMMY_ALLOC)
#define ABTREE_NODE_DEGREE 16
#define ABTREE_NODE_MIN_DEGREE 6
#define DS_DECLARATION abtree<ABTREE_NODE_DEGREE, test_type, less<test_type>, MemMgmt>
#elif defined(BSLACK_THROWAWAY) || defined(BSLACK_REUSE)
#define BSLACK_DEGREE 16
#define DS_DECLARATION bslack<BSLACK_DEGREE, test_type, less<test_type>, MemMgmt>
#elif defined(LAZYLIST)
#define DS_DECLARATION lazylist<test_type, test_type, MemMgmt>
#else
#error "Failed to define a data structure"
#endif

#define STR(x) XSTR(x)
#define XSTR(x) #x

#ifndef RQ_FUNC
#define RQ_FUNC rangeQuery
#endif

#ifndef INSERT_FUNC
#define INSERT_FUNC insert
#endif

#ifndef ERASE_FUNC
#define ERASE_FUNC erase
#endif

#ifndef RQ_FUNC
#define RQ_FUNC rangeQuery
#endif

#ifndef FIND_FUNC
#define FIND_FUNC find
#endif

#ifndef OPS_BETWEEN_TIME_CHECKS
#define OPS_BETWEEN_TIME_CHECKS 500
#endif

#if defined(ABTREE) || defined(ABTREE_REUSE_PMARK) || defined(ABTREE_THROWAWAY) || defined(BSLACK_THROWAWAY) || defined(BSLACK_REUSE) || defined(DUMMY_NOOP) || defined(DUMMY_ALLOC)
#define VALUE ((void*) (int64_t) key)
#define KEY keys[0]
#else
#define VALUE key
#define KEY key
#endif

// note: INSERT success checks use "== NO_VALUE" so that prefilling can tell that a new KEY has been inserted
#if defined(BST) || defined(BST_THROWAWAY) || defined(BST_REUSE_PMARK) || defined(ABTREE) || defined(ABTREE_REUSE_PMARK) || defined(ABTREE_THROWAWAY) || defined(BSLACK_THROWAWAY) || defined(BSLACK_REUSE)
#define INSERT_AND_CHECK_SUCCESS tree->INSERT_FUNC(tid, key, VALUE) == tree->NO_VALUE
#define DELETE_AND_CHECK_SUCCESS tree->ERASE_FUNC(tid, key).second
#define FIND_AND_CHECK_SUCCESS tree->FIND_FUNC(tid, key)
#define RQ_AND_CHECK_SUCCESS(rqcnt) rqcnt = tree->RQ_FUNC(tid, key, key+RQSIZE, rqResults)
#define RQ_GARBAGE(rqcnt) rqResults[0]->KEY + rqResults[rqcnt-1]->KEY
#define INIT_THREAD(tid) tree->initThread(tid)
#define DEINIT_THREAD(tid) tree->deinitThread(tid)
#define PRCU_INIT 
#define PRCU_REGISTER(tid)
#define PRCU_UNREGISTER
#define CLEAR_COUNTERS tree->clearCounters()
#elif defined(DUMMY_NOOP)
#define INSERT_AND_CHECK_SUCCESS false
#define DELETE_AND_CHECK_SUCCESS false
#define FIND_AND_CHECK_SUCCESS false
#define RQ_AND_CHECK_SUCCESS(rqcnt) false
#define RQ_GARBAGE(rqcnt) rqResults[0]->KEY + rqResults[rqcnt-1]->KEY
#define INIT_THREAD(tid) tree->initThread(tid)
#define DEINIT_THREAD(tid) 
#define PRCU_INIT 
#define PRCU_REGISTER(tid)
#define PRCU_UNREGISTER
#define CLEAR_COUNTERS tree->clearCounters()
#elif defined(DUMMY_ALLOC)
#define INSERT_AND_CHECK_SUCCESS ({test_type *___x = (test_type *)malloc(sizeof(test_type)); *___x = key; garbage += *___x; free(___x); false;})
#define DELETE_AND_CHECK_SUCCESS INSERT_AND_CHECK_SUCCESS
#define FIND_AND_CHECK_SUCCESS false
#define RQ_AND_CHECK_SUCCESS(rqcnt) false
#define RQ_GARBAGE(rqcnt) rqResults[0]->KEY + rqResults[rqcnt-1]->KEY
#define INIT_THREAD(tid) tree->initThread(tid)
#define DEINIT_THREAD(tid) 
#define PRCU_INIT 
#define PRCU_REGISTER(tid)
#define PRCU_UNREGISTER
#define CLEAR_COUNTERS tree->clearCounters()
#elif defined(LAZYLIST)
#define INSERT_AND_CHECK_SUCCESS tree->INSERT_FUNC(tid, key, VALUE) == tree->NO_VALUE
#define DELETE_AND_CHECK_SUCCESS tree->ERASE_FUNC(tid, key) != tree->NO_VALUE
#define FIND_AND_CHECK_SUCCESS tree->FIND_FUNC(tid, key)
#define RQ_AND_CHECK_SUCCESS(rqcnt) rqcnt = tree->RQ_FUNC(tid, key, key+RQSIZE, rqResults)
#define RQ_GARBAGE(rqcnt) rqResults[0] + rqResults[rqcnt-1]
#define INIT_THREAD(tid) tree->initThread(tid)
#define DEINIT_THREAD(tid) 
#define PRCU_INIT 
#define PRCU_REGISTER(tid)
#define PRCU_UNREGISTER
#define CLEAR_COUNTERS tree->clearCounters()
#endif

template <class MemMgmt>
void *thread_prefill(void *_id) {
    int tid = *((int*) _id);
    binding_bindThread(tid, LOGICAL_PROCESSORS);
    PRCU_REGISTER(tid);
    test_type garbage = 0;
    Random *rng = &rngs[tid*PREFETCH_SIZE_WORDS];
    DS_DECLARATION * tree = (DS_DECLARATION *) __tree;

    double insProbability = (INS > 0 ? 100*INS/(INS+DEL) : 50.);
    
//    INIT_THREAD(tid);
    running.fetch_add(1);
    __sync_synchronize();
    while (!start) { __sync_synchronize(); TRACE COUTATOMICTID("waiting to start"<<endl); } // wait to start
    int cnt = 0;
    chrono::time_point<chrono::high_resolution_clock> __endTime = startTime;
    while (!done) {
        if (((++cnt) % OPS_BETWEEN_TIME_CHECKS) == 0) {
            __endTime = chrono::high_resolution_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(__endTime-startTime).count() >= PREFILL_INTERVAL_MILLIS) {
                done = true;
                __sync_synchronize();
                break;
            }
        }
        
        VERBOSE if (cnt&&((cnt % 1000000) == 0)) COUTATOMICTID("op# "<<cnt<<endl);
        int key = rng->nextNatural(MAXKEY);
        double op = rng->nextNatural(100000000) / 1000000.;
        if (op < insProbability) {
            if (INSERT_AND_CHECK_SUCCESS) {
                keysum->add(tid, key);
                prefillSize->add(tid, 1);
                tree->debugGetCounters()->insertSuccess->inc(tid);
            }
        } else {
            if (DELETE_AND_CHECK_SUCCESS) {
                keysum->add(tid, -key);
                prefillSize->add(tid, -1);
                tree->debugGetCounters()->eraseSuccess->inc(tid);
            }
        }
    }

    __sync_bool_compare_and_swap(&prefillIntervalElapsedMillis, 0, chrono::duration_cast<chrono::milliseconds>(__endTime-startTime).count());
    
//    COUTATOMICTID("prefill thread maxAllocatedBytes="<<maxAllocatedBytes<<" currentAllocatedBytes="<<currentAllocatedBytes<<endl);
//    __sync_fetch_and_add(&memoryFootprintPrefill, currentAllocatedBytes);
//    __sync_fetch_and_add(&memoryFootprintPrefillPeak, maxAllocatedBytes);
    running.fetch_add(-1);
    PRCU_UNREGISTER;
    return NULL;
}

template <class MemMgmt>
void prefill(DS_DECLARATION * tree) {
    chrono::time_point<chrono::high_resolution_clock> prefillStartTime = chrono::high_resolution_clock::now();

    const double PREFILL_THRESHOLD = 0.05;
#ifdef MEASURE_TIME_TO_PREFILL
    const int MAX_ATTEMPTS = 1000;
#else
    const int MAX_ATTEMPTS = 500;
#endif
    const double expectedFullness = (INS+DEL ? INS / (double)(INS+DEL) : 0.5); // percent full in expectation
    const int expectedSize = (int)(MAXKEY * expectedFullness);

    long long totalThreadsPrefillElapsedMillis = 0;
    
    int sz = 0;
    int attempts;
    for (attempts=0;attempts<MAX_ATTEMPTS;++attempts) {
        // create threads
        pthread_t *threads = new pthread_t[TOTAL_THREADS];
        int *ids = new int[TOTAL_THREADS];
        for (int i=0;i<TOTAL_THREADS;++i) {
            ids[i] = i;
            rngs[i*PREFETCH_SIZE_WORDS].setSeed(rand());
            INIT_THREAD(i);
        }

        PRCU_INIT;

        // start all threads
        for (int i=0;i<TOTAL_THREADS;++i) {
            if (pthread_create(&threads[i], NULL, thread_prefill<MemMgmt>, &ids[i])) {
                cerr<<"ERROR: could not create thread"<<endl;
                exit(-1);
            }
        }

        TRACE COUTATOMIC("main thread: waiting for threads to START prefilling running="<<running.load()<<endl);
        while (running.load() < TOTAL_THREADS) {}
        TRACE COUTATOMIC("main thread: starting prefilling timer..."<<endl);
        startTime = chrono::high_resolution_clock::now();
        
        prefillIntervalElapsedMillis = 0;
        __sync_synchronize();
        start = true;
        
        /**
         * START INFINITE LOOP DETECTION CODE
         */
        // amount of time for main thread to wait for children threads
        timespec tsExpected;
        tsExpected.tv_sec = 0;
        tsExpected.tv_nsec = PREFILL_INTERVAL_MILLIS * ((__syscall_slong_t) 1000000);
        // short nap
        timespec tsNap;
        tsNap.tv_sec = 0;
        tsNap.tv_nsec = 10000000; // 10ms

        nanosleep(&tsExpected, NULL);
        done = true;
        __sync_synchronize();

        const long MAX_NAPPING_MILLIS = 500;
        elapsedMillis = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - startTime).count();
        elapsedMillisNapping = 0;
        while (running.load() > 0 && elapsedMillisNapping < MAX_NAPPING_MILLIS) {
            nanosleep(&tsNap, NULL);
            elapsedMillisNapping = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - startTime).count() - elapsedMillis;
        }
        if (running.load() > 0) {
            COUTATOMIC("Validation FAILURE: "<<running.load()<<" non-responsive thread(s) [during prefill]"<<endl);
            exit(-1);
        }
        /**
         * END INFINITE LOOP DETECTION CODE
         */
        
//        TRACE COUTATOMIC("main thread: waiting for threads to STOP prefilling running="<<running.load()<<endl);
//        while (running.load() > 0) {}
//
//        for (int i=0;i<TOTAL_THREADS;++i) {
//            if (pthread_join(threads[i], NULL)) {
//                cerr<<"ERROR: could not join prefilling thread"<<endl;
//                exit(-1);
//            }
//        }
        
        delete[] threads;
        delete[] ids;

        start = false;
        done = false;

//        tree->debugPrintToFile("tree_", 0, "", 0, ".out");
//        exit(-1);
        
        sz = prefillSize->getTotal();
        //sz = tree->getSize();
        int absdiff = (sz < expectedSize ? expectedSize - sz : sz - expectedSize);
        if (absdiff < expectedSize*PREFILL_THRESHOLD) {
            break;
        }
//        if ((attempts%5)==0) {
//            COUTATOMIC("main thread: prefilling sz="<<sz<<" expectedSize="<<expectedSize<<" keysum="<<keysum->getTotal()<<" treekeysum="<<tree->debugKeySum()<<" treesize="<<tree->getSize()<<endl);
//        }
        
        totalThreadsPrefillElapsedMillis += prefillIntervalElapsedMillis;
    }
    
    if (attempts >= MAX_ATTEMPTS) {
        cerr<<"ERROR: could not prefill to expected size "<<expectedSize<<". reached size "<<sz<<" after "<<attempts<<" attempts"<<endl;
        exit(-1);
    }
    
    chrono::time_point<chrono::high_resolution_clock> prefillEndTime = chrono::high_resolution_clock::now();
    auto elapsed = chrono::duration_cast<chrono::milliseconds>(prefillEndTime-prefillStartTime).count();

    debugCounters * const counters = tree->debugGetCounters();
    const long totalSuccUpdates = counters->insertSuccess->getTotal()+counters->eraseSuccess->getTotal();
    COUTATOMIC("finished prefilling to size "<<sz<<" for expected size "<<expectedSize<<" keysum="<<keysum->getTotal()<<" treekeysum="<<tree->debugKeySum()<<" treesize="<<tree->getSize()<<", performing "<<totalSuccUpdates<<" successful updates in "<<(totalThreadsPrefillElapsedMillis/1000.) /*(elapsed/1000.)*/<<" seconds (total time "<<(elapsed/1000.)<<"s)"<<endl);
    CLEAR_COUNTERS;
}

template <class MemMgmt>
void *thread_ops(void *_id) {
    int tid = *((int*) _id);
    binding_bindThread(tid, LOGICAL_PROCESSORS);
    PRCU_REGISTER(tid);
    test_type garbage = 0;
    Random *rng = &rngs[tid*PREFETCH_SIZE_WORDS];
    DS_DECLARATION * tree = (DS_DECLARATION *) __tree;

#if defined(BST) || defined(BST_THROWAWAY) || defined(BST_REUSE_PMARK)
    Node<test_type, test_type> const ** rqResults = new Node<test_type, test_type> const *[RQSIZE];
#elif defined(ABTREE) || defined(ABTREE_REUSE_PMARK) || defined(ABTREE_THROWAWAY) || defined(DUMMY_NOOP) || defined(DUMMY_ALLOC)
    abtree_Node<ABTREE_NODE_DEGREE, test_type> const ** rqResults = new abtree_Node<ABTREE_NODE_DEGREE, test_type> const *[RQSIZE];
#elif defined(BSLACK_THROWAWAY) || defined(BSLACK_REUSE)
    bslack_Node<BSLACK_DEGREE, test_type> const ** rqResults = new bslack_Node<BSLACK_DEGREE, test_type> const *[RQSIZE];
#elif defined(LAZYLIST)
    int * rqResults = new int[RQSIZE];
#endif
    
    INIT_THREAD(tid);
    papi_create_eventset(tid);
    running.fetch_add(1);
    __sync_synchronize();
    while (!start) { __sync_synchronize(); TRACE COUTATOMICTID("waiting to start"<<endl); } // wait to start
    papi_start_counters(tid);
    
    for (int i=0;i<OPS_PER_THREAD;++i) {
        int key = rng->nextNatural(MAXKEY);
        double op = rng->nextNatural(100000000) / 1000000.;
        if (op < INS) {
            if (INSERT_AND_CHECK_SUCCESS) {
                keysum->add(tid, key);
            }
            tree->debugGetCounters()->insertSuccess->inc(tid);
        } else if (op < INS+DEL) {
            if (DELETE_AND_CHECK_SUCCESS) {
                keysum->add(tid, -key);
            }
            tree->debugGetCounters()->eraseSuccess->inc(tid);
        } else if (op < INS+DEL+RQ) {
            int rqcnt;
            if (RQ_AND_CHECK_SUCCESS(rqcnt)) { // prevent rqResults and count from being optimized out
                garbage += RQ_GARBAGE(rqcnt);
            }
            tree->debugGetCounters()->rqSuccess->inc(tid);
        } else {
            FIND_AND_CHECK_SUCCESS;
            tree->debugGetCounters()->findSuccess->inc(tid);
        }
    }

    papi_stop_counters(tid);
    running.fetch_add(-1);
    VERBOSE COUTATOMICTID("termination"<<" garbage="<<garbage<<endl);
    PRCU_UNREGISTER;
    DEINIT_THREAD(tid);
    return NULL;
}

long long abs(long long x) {
    return (x < 0 ? -x : x);
}

template <class MemMgmt>
void *thread_timed(void *_id) {
    int tid = *((int*) _id);
    binding_bindThread(tid, LOGICAL_PROCESSORS);
    PRCU_REGISTER(tid);
    test_type garbage = 0;
    Random *rng = &rngs[tid*PREFETCH_SIZE_WORDS];
    DS_DECLARATION * tree = (DS_DECLARATION *) __tree;

#if defined(BST) || defined(BST_THROWAWAY) || defined(BST_REUSE_PMARK)
    Node<test_type, test_type> const ** rqResults = new Node<test_type, test_type> const *[RQSIZE];
#elif defined(ABTREE) || defined(ABTREE_REUSE_PMARK) || defined(ABTREE_THROWAWAY) || defined(DUMMY_NOOP) || defined(DUMMY_ALLOC)
    abtree_Node<ABTREE_NODE_DEGREE, test_type> const ** rqResults = new abtree_Node<ABTREE_NODE_DEGREE, test_type> const *[RQSIZE];
#elif defined(BSLACK_THROWAWAY) || defined(BSLACK_REUSE)
    bslack_Node<BSLACK_DEGREE, test_type> const ** rqResults = new bslack_Node<BSLACK_DEGREE, test_type> const *[RQSIZE];
#elif defined(LAZYLIST)
    int * rqResults = new int[RQSIZE];
#endif
    
    INIT_THREAD(tid);
    papi_create_eventset(tid);
    running.fetch_add(1);
    __sync_synchronize();
    while (!start) { __sync_synchronize(); TRACE COUTATOMICTID("waiting to start"<<endl); } // wait to start
    papi_start_counters(tid);
    int cnt = 0;
    while (!done) {
        if (((++cnt) % OPS_BETWEEN_TIME_CHECKS) == 0) {
            chrono::time_point<chrono::high_resolution_clock> __endTime = chrono::high_resolution_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(__endTime-startTime).count() >= abs(MILLIS_TO_RUN)) {
                done = true;
                __sync_synchronize();
                break;
            }
        }
        
        VERBOSE if (cnt&&((cnt % 1000000) == 0)) COUTATOMICTID("op# "<<cnt<<endl);
        int key = rng->nextNatural(MAXKEY);
        double op = rng->nextNatural(100000000) / 1000000.;
        if (op < INS) {
            if (INSERT_AND_CHECK_SUCCESS) {
                keysum->add(tid, key);
            }
            tree->debugGetCounters()->insertSuccess->inc(tid);
        } else if (op < INS+DEL) {
            if (DELETE_AND_CHECK_SUCCESS) {
                keysum->add(tid, -key);
            }
            tree->debugGetCounters()->eraseSuccess->inc(tid);
        } else if (op < INS+DEL+RQ) {
            int rqcnt;
            if (RQ_AND_CHECK_SUCCESS(rqcnt)) { // prevent rqResults and count from being optimized out
                garbage += RQ_GARBAGE(rqcnt);
            }
            tree->debugGetCounters()->rqSuccess->inc(tid);
        } else {
            FIND_AND_CHECK_SUCCESS;
            tree->debugGetCounters()->findSuccess->inc(tid);
        }
    }
    papi_stop_counters(tid);
//    COUTATOMICTID("timed thread maxAllocatedBytes="<<maxAllocatedBytes<<" currentAllocatedBytes="<<currentAllocatedBytes<<endl);
//    __sync_fetch_and_add(&memoryFootprintPeak, maxAllocatedBytes);
//    __sync_fetch_and_add(&memoryFootprintEnd, currentAllocatedBytes);
    running.fetch_add(-1);
    VERBOSE COUTATOMICTID("termination"<<" garbage="<<garbage<<endl);
    PRCU_UNREGISTER;
    DEINIT_THREAD(tid);
    return NULL;
}

template <class MemMgmt>
void *thread_rq(void *_id) {
    int tid = *((int*) _id);
    binding_bindThread(tid, LOGICAL_PROCESSORS);
    PRCU_REGISTER(tid);
    test_type garbage = 0;
    Random *rng = &rngs[tid*PREFETCH_SIZE_WORDS];
    DS_DECLARATION * tree = (DS_DECLARATION *) __tree;

#if defined(BST) || defined(BST_THROWAWAY) || defined(BST_REUSE_PMARK)
    Node<test_type, test_type> const ** rqResults = new Node<test_type, test_type> const *[RQSIZE];
#elif defined(ABTREE) || defined(ABTREE_REUSE_PMARK) || defined(ABTREE_THROWAWAY) || defined(DUMMY_NOOP) || defined(DUMMY_ALLOC)
    abtree_Node<ABTREE_NODE_DEGREE, test_type> const ** rqResults = new abtree_Node<ABTREE_NODE_DEGREE, test_type> const *[RQSIZE];
#elif defined(BSLACK_THROWAWAY) || defined(BSLACK_REUSE)
    bslack_Node<BSLACK_DEGREE, test_type> const ** rqResults = new bslack_Node<BSLACK_DEGREE, test_type> const *[RQSIZE];
#elif defined(LAZYLIST)
    int * rqResults = new int[RQSIZE];
#else
#error "Failed to define a data structure"
#endif
    
    int sqrt_rqsize = sqrt(RQSIZE);

    INIT_THREAD(tid);
    running.fetch_add(1);
    __sync_synchronize();
    while (!start) { __sync_synchronize(); TRACE COUTATOMICTID("waiting to start"<<endl); } // wait to start
    int cnt = 0;
    while (!done) {
        if (((++cnt) % OPS_BETWEEN_TIME_CHECKS) == 0) {
            chrono::time_point<chrono::high_resolution_clock> __endTime = chrono::high_resolution_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(__endTime-startTime).count() >= MILLIS_TO_RUN) {
                done = true;
                __sync_synchronize();
                break;
            }
        }
        
        VERBOSE if (cnt&&((cnt % 1000000) == 0)) COUTATOMICTID("op# "<<cnt<<endl);
        int key = rng->nextNatural(MAXKEY);
        int sz = rng->nextNatural(sqrt_rqsize); sz *= sz;

        int rqcnt;
        if (RQ_AND_CHECK_SUCCESS(rqcnt)) { // prevent rqResults and count from being optimized out
            garbage += RQ_GARBAGE(rqcnt);
        }
        tree->debugGetCounters()->rqSuccess->inc(tid);
    }
    running.fetch_add(-1);
    VERBOSE COUTATOMICTID("termination"<<" garbage="<<garbage<<endl);
    PRCU_UNREGISTER;
    DEINIT_THREAD(tid);
    return NULL;
}

template <class MemMgmt>
void printOutput();

template <class MemMgmt>
void trial() {
#if defined(BST) || defined(BST_THROWAWAY) || defined(BST_REUSE_PMARK)
    __tree = (void*) new DS_DECLARATION(NO_KEY, NO_VALUE, RETRY, TOTAL_THREADS, DEFAULT_SUSPECTED_SIGNAL);
#elif defined(ABTREE) || defined(ABTREE_REUSE_PMARK) || defined(ABTREE_THROWAWAY) || defined(DUMMY_NOOP) || defined(DUMMY_ALLOC)
    __tree = (void*) new DS_DECLARATION(TOTAL_THREADS, DEFAULT_SUSPECTED_SIGNAL, NO_KEY, ABTREE_NODE_MIN_DEGREE);
#elif defined(BSLACK_THROWAWAY) || defined(BSLACK_REUSE) 
    __tree = (void*) new DS_DECLARATION(TOTAL_THREADS, BSLACK_DEGREE, NO_KEY, DEFAULT_SUSPECTED_SIGNAL);
#elif defined(LAZYLIST)
    __tree = (void*) new DS_DECLARATION(TOTAL_THREADS, NO_KEY, NO_VALUE);
#else
#error "Failed to define a data structure"
#endif

    // get random number generator seeded with time
    // we use this rng to seed per-thread rng's that use a different algorithm
    srand(time(NULL));

    // create threads
    pthread_t *threads[TOTAL_THREADS];
    int ids[TOTAL_THREADS];
    for (int i=0;i<TOTAL_THREADS;++i) {
        threads[i] = new pthread_t;
        ids[i] = i;
        rngs[i*PREFETCH_SIZE_WORDS].setSeed(rand());
    }

    PRCU_INIT;
    papi_init_program(TOTAL_THREADS);
    
//    memoryFootprintBefore = getCurrentRSS();
    
    if (PREFILL) prefill((DS_DECLARATION *) __tree);

    // amount of time for main thread to wait for children threads
    timespec tsExpected;
    tsExpected.tv_sec = MILLIS_TO_RUN / 1000;
    tsExpected.tv_nsec = (MILLIS_TO_RUN % 1000) * ((__syscall_slong_t) 1000000);
    // short nap
    timespec tsNap;
    tsNap.tv_sec = 0;
    tsNap.tv_nsec = 10000000; // 10ms

    // start all threads
    for (int i=0;i<TOTAL_THREADS;++i) {
        if (pthread_create(threads[i], NULL,
                    (OPS_PER_THREAD > 0) ? thread_ops<MemMgmt>
                    : (i < WORK_THREADS
                       ? thread_timed<MemMgmt>
                       : thread_rq<MemMgmt>), &ids[i])) {
            cerr<<"ERROR: could not create thread"<<endl;
            exit(-1);
        }
    }

    while (running.load() < TOTAL_THREADS) {
        TRACE COUTATOMIC("main thread: waiting for threads to START running="<<running.load()<<endl);
    } // wait for all threads to be ready
    COUTATOMIC("main thread: starting timer..."<<endl);
    startTime = chrono::high_resolution_clock::now();
    __sync_synchronize();
    start = true;

    // if MILLIS_TO_RUN is negative, then we are interested in measuring what happens when you cut off threads mid-operation.
    if (MILLIS_TO_RUN < 0) {
        tsExpected.tv_sec = (-MILLIS_TO_RUN) / 1000;
        tsExpected.tv_nsec = ((-MILLIS_TO_RUN) % 1000) * ((__syscall_slong_t) 1000000);
        nanosleep(&tsExpected, NULL);
        COUTATOMIC("Forcefully killing threads after "<<((-MILLIS_TO_RUN)/1000.)<<"s since MILLIS_TO_RUN is negative."<<endl);
        for (int i=0;i<TOTAL_THREADS;++i) {
            pthread_cancel(*(threads[i]));
        }
        done = true; // AFTER canceling, because we want a hard kill on threads, so they can't finish their current operations.
//        printOutput<MemMgmt>();
//        exit(0);
        
    } else {
        // pthread_join is replaced with sleeping, and kill threads if they run too long
        // method: sleep for the desired time + a small epsilon,
        //      then check "running.load()" to see if we're done.
        //      if not, loop and sleep in small increments for up to 1s,
        //      and exit(-1) if running doesn't hit 0.

        if (MILLIS_TO_RUN > 0) {
            nanosleep(&tsExpected, NULL);
            done = true;
        }

        const long MAX_NAPPING_MILLIS = (MILLIS_TO_RUN > 0 ? 200 : 30000);
        elapsedMillis = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - startTime).count();
        elapsedMillisNapping = 0;
        while (running.load() > 0 && elapsedMillisNapping < MAX_NAPPING_MILLIS) {
            nanosleep(&tsNap, NULL);
            elapsedMillisNapping = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - startTime).count() - elapsedMillis;
        }
        if (running.load() > 0) {
            COUTATOMIC("Validation FAILURE: "<<running.load()<<" non-terminating thread(s) [did we exhaust physical memory and experience excessive slowdown due to swap mem?]"<<endl);
            for (int i=0;i<TOTAL_THREADS;++i) {
                pthread_cancel(*(threads[i]));
            }
#if defined(REBALANCING_WEIGHT_ONLY) || defined(REBALANCING_NONE) //|| defined(NO_VALIDATION)
            cout<<"NOTICE: skipping structural validation."<<endl;
#else
            DS_DECLARATION * tree = (DS_DECLARATION *) __tree;
            if (tree->validate(0, false)) {
                cout<<"Structural validation OK"<<endl;
            } else {
                cout<<"Structural validation FAILURE."<<endl;
                exit(-1);
            }
#endif
            exit(-1);
        }
    }

    // join all threads
    for (int i=0;i<TOTAL_THREADS;++i) {
        COUTATOMIC("joining thread "<<i<<endl);
        if (pthread_join(*(threads[i]), NULL)) {
            cerr<<"ERROR: could not join thread"<<endl;
            exit(-1);
        }
    }

    COUTATOMIC(((elapsedMillis+elapsedMillisNapping)/1000.)<<"s"<<endl);

//    memoryFootprintAfter = getCurrentRSS();
//    
//    struct rusage rusage;
//    getrusage( RUSAGE_CHILDREN, &rusage );
//    memoryFootprintPeak = (size_t)(rusage.ru_maxrss * 1024L);
//    
//    //memoryFootprintPeak = getPeakRSS();
    
    for (int i=0;i<TOTAL_THREADS;++i) {
        delete threads[i];
    }
}

#ifdef BST
    //void printInterruptedAddresses(ostream& os) {
    //    // print addresses where threads were interrupted by signals
    //    // that performed siglongjmp
    //    for (int tid=0;tid<TOTAL_THREADS;++tid) {
    //        const int sz = threadAddrIndices[tid*PREFETCH_SIZE_WORDS];
    //        for (int ix=0;ix<sz;++ix) {
    //            os<<hex<<threadAddr[tid*MAX_THREAD_ADDR + ix]<<endl;
    //        }
    //    }
    //}

    void sighandler(int signum) {
        printf("Process %d got signal %d\n", getpid(), signum);

    #ifdef POSIX_SYSTEM
        printf("comparing signum=%d with SIGUSR1=%d\n", signum, SIGUSR1);
        if (signum == SIGUSR1) {
            TRACE_TOGGLE;
            TRACE { COUTATOMIC("TRACING IS ON"<<endl); } else { COUTATOMIC("TRACING IS OFF"<<endl); }
        } else {
            // tell all threads to terminate
            done = true;
            __sync_synchronize();

            // wait up to 10 seconds for all threads to terminate
            auto t1 = chrono::high_resolution_clock::now();
            while (running.load() > 0) {
                auto t2 = chrono::high_resolution_clock::now();
                auto diffSecondsFloor = chrono::duration_cast<chrono::milliseconds>(t2-t1).count();
                if (diffSecondsFloor > 3000) {
                    COUTATOMIC("WARNING: threads did not terminate after "<<diffSecondsFloor<<"ms; forcefully exiting; tree render may not be consistent... running="<<running.load()<<endl);
                    break;
                }
            }

            // print a tree file that we can later render an image from
            bst<test_type, test_type, less<test_type>, void> *tree =
            ((bst<test_type, test_type, less<test_type>, void> *) __tree);
            tree->debugPrintToFile("tree", 0, "", 0, "");
            //tree->debugPrintToFileWeight("tree", 0, "", 0, "weight");

            fstream fs ("addr", fstream::out);
    //        printInterruptedAddresses(fs);
            fs.close();

            // handle the original signal to get a core dump / termination as necessary
            if (signum == SIGSEGV || signum == SIGABRT) {
                signal(signum, SIG_DFL);
                kill(getpid(), signum);
            } else {
                printf("exiting...\n");
                exit(-1);
            }
        }
    #endif
    }
#elif defined(ABTREE) || defined(ABTREE_REUSE_PMARK) || defined(BST_THROWAWAY) || defined(BST_REUSE_PMARK) || defined(ABTREE_THROWAWAY) || defined(DUMMY_NOOP) || defined(DUMMY_ALLOC) || defined(BSLACK_THROWAWAY) || defined(BSLACK_REUSE)
    void sighandler(int signum) {
        printf("Process %d got signal %d\n", getpid(), signum);

    #ifdef POSIX_SYSTEM
        printf("comparing signum=%d with SIGUSR1=%d\n", signum, SIGUSR1);
        if (signum == SIGUSR1) {
            TRACE_TOGGLE;
            TRACE { COUTATOMIC("TRACING IS ON"<<endl); } else { COUTATOMIC("TRACING IS OFF"<<endl); }
        } else {
            // tell all threads to terminate
            done = true;
            __sync_synchronize();

            // wait up to 10 seconds for all threads to terminate
            auto t1 = chrono::high_resolution_clock::now();
            while (running.load() > 0) {
                auto t2 = chrono::high_resolution_clock::now();
                auto diffSecondsFloor = chrono::duration_cast<chrono::milliseconds>(t2-t1).count();
                if (diffSecondsFloor > 3000) {
                    COUTATOMIC("WARNING: threads did not terminate after "<<diffSecondsFloor<<"ms; forcefully exiting; tree render may not be consistent... running="<<running.load()<<endl);
                    break;
                }
            }

//            // print a tree file that we can later render an image from
//            bst<test_type, test_type, less<test_type>, void> *tree =
//            ((bst<test_type, test_type, less<test_type>, void> *) __tree);
//            tree->debugPrintToFile("tree", 0, "", 0, "");
//            //tree->debugPrintToFileWeight("tree", 0, "", 0, "weight");
//
//            fstream fs ("addr", fstream::out);
//    //        printInterruptedAddresses(fs);
//            fs.close();

            // handle the original signal to get a core dump / termination as necessary
            if (signum == SIGSEGV || signum == SIGABRT) {
                signal(signum, SIG_DFL);
                kill(getpid(), signum);
            } else {
                printf("exiting...\n");
                exit(-1);
            }
        }
    #endif
    }
#endif

string percent(int numer, int denom) {
    if (numer == 0 || denom == 0) return "0%";
//    if (numer == 0 || denom == 0) return "0.0%";
    double x = numer / (double) denom;
    stringstream ss;
    int xpercent = (int)(x*100+0.5);
    ss<<xpercent<<"%";
    return ss.str();
}

template <class MemMgmt>
void printOutput() {
    cout<<"PRODUCING OUTPUT"<<endl;
    DS_DECLARATION * tree = (DS_DECLARATION *) __tree;

    long long threadsKeySum = keysum->getTotal();
    long long treeKeySum = tree->debugKeySum();
    if (threadsKeySum == treeKeySum) {
        cout<<"Validation OK: threadsKeySum = "<<threadsKeySum<<" treeKeySum="<<treeKeySum<<endl;
    } else {
        cout<<"Validation FAILURE: threadsKeySum = "<<threadsKeySum<<" treeKeySum="<<treeKeySum<<endl;
#if defined(ABTREE) || defined(ABTREE_REUSE_PMARK) || defined(ABTREE_THROWAWAY)|| defined(BST) || defined(BST_THROWAWAY) || defined(BST_REUSE_PMARK)
        tree->debugPrintToFile("tree_", 0, "", 0, ".out");
#endif
        exit(-1);
    }
    
#if defined(REBALANCING_WEIGHT_ONLY) || defined(REBALANCING_NONE) //|| defined(NO_VALIDATION)
    cout<<"NOTICE: skipping structural validation."<<endl;
#else
    if (tree->validate(threadsKeySum, true)) {
        cout<<"Structural validation OK"<<endl;
    } else {
        cout<<"Structural validation FAILURE."<<endl;
        exit(-1);
    }
#endif

    debugCounters * const counters = tree->debugGetCounters();
    int totalOps = 0, totalSuccessful = 0, totalChanges = 0;
    int totalPathOps[NUMBER_OF_PATHS];
    for (int path=0;path<NUMBER_OF_PATHS;++path) {
        totalSuccessful += counters->pathSuccess[path]->getTotal();
        totalChanges += counters->updateChange[path]->getTotal();
        totalPathOps[path] = counters->pathSuccess[path]->getTotal() + counters->pathFail[path]->getTotal();
        totalOps += totalPathOps[path];
    }
    
    COUTATOMIC(endl);

    long long rqSuccessTotal = counters->rqSuccess->getTotal();
    long long rqFailTotal = counters->rqFail->getTotal();
    long long findSuccessTotal = counters->findSuccess->getTotal();
    long long findFailTotal = counters->findFail->getTotal();
    if (counters->insertSuccess->getTotal()) COUTATOMIC("total insert succ             : "<<counters->insertSuccess->getTotal()<<endl);
    if (counters->insertFail->getTotal()) COUTATOMIC("total insert retry            : "<<counters->insertFail->getTotal()<<endl);
    if (counters->eraseSuccess->getTotal()) COUTATOMIC("total erase succ              : "<<counters->eraseSuccess->getTotal()<<endl);
    if (counters->eraseFail->getTotal()) COUTATOMIC("total erase retry             : "<<counters->eraseFail->getTotal()<<endl);
    if (findSuccessTotal) COUTATOMIC("total find succ               : "<<findSuccessTotal<<endl);
    if (findFailTotal) COUTATOMIC("total find retry              : "<<findFailTotal<<endl);
    if (rqSuccessTotal) COUTATOMIC("total rq succ                 : "<<rqSuccessTotal<<endl);
    if (rqFailTotal) COUTATOMIC("total rq fail                 : "<<rqFailTotal<<endl);
    const long totalSuccUpdates = counters->insertSuccess->getTotal()+counters->eraseSuccess->getTotal();
    const long totalSuccAll = totalSuccUpdates + rqSuccessTotal + findSuccessTotal;
    const long throughput = (long) (totalSuccUpdates / ((elapsedMillis+elapsedMillisNapping)/1000.));
    const long throughputAll = (long) (totalSuccAll / ((elapsedMillis+elapsedMillisNapping)/1000.));
    COUTATOMIC("total succ updates            : "<<totalSuccUpdates<<endl);
    COUTATOMIC("total succ                    : "<<totalSuccAll<<endl);
    COUTATOMIC("throughput (succ updates/sec) : "<<throughput<<endl);
    COUTATOMIC("    incl. queries             : "<<throughputAll<<endl);
    COUTATOMIC("elapsed milliseconds          : "<<(elapsedMillis+elapsedMillisNapping)<<endl);
    COUTATOMIC(endl);
    
#if defined(BST) || defined(BST_THROWAWAY) || defined(BST_REUSE_PMARK) || defined(ABTREE) || defined(ABTREE_REUSE_PMARK) || defined(ABTREE_THROWAWAY) || defined(BSLACK_THROWAWAY) || defined(BSLACK_REUSE)
    tree->debugGetRecMgr()->printStatus();
    COUTATOMIC("tree        : "<<tree->getSizeString()<<endl);
#endif

    COUTATOMIC(endl);
    papi_print_counters(totalSuccAll);
    
#if defined(BST) || defined(BST_THROWAWAY) || defined(BST_REUSE_PMARK) || defined(ABTREE) || defined(ABTREE_REUSE_PMARK) || defined(ABTREE_THROWAWAY)
    if (PRINT_TREE) {
        tree->debugPrintToFile("tree_", 0, "", 0, ".out");
    }
#endif
    
    // free tree
    delete tree;
    
//    COUTATOMIC("memory footprint after prefilling  : "<<memoryFootprintPrefill<<endl);
//    COUTATOMIC("memory footprint at the end        : "<<memoryFootprintEnd<<endl);
//    COUTATOMIC("memory footprint peak in prefill   : "<<memoryFootprintPrefillPeak<<endl);
//    COUTATOMIC("memory footprint peak overall      : "<<(memoryFootprintPeak+memoryFootprintPrefillPeak)<<endl);
        
    VERBOSE COUTATOMIC("main thread: garbage#=");
    VERBOSE COUTATOMIC(counters->garbage->getTotal()<<endl);
}

#define PRINTI(name) { cout<<(#name)<<"="<<name<<endl; }
#define PRINTS(name) { cout<<(#name)<<"="<<name<<endl; }

template <class Reclaim, class Alloc, class Pool>
void performExperiment() {
#if defined(BST) || defined(BST_REUSE_PMARK)
    typedef record_manager<Reclaim, Alloc, Pool, Node<test_type, test_type> > MemMgmt;
#elif defined(BST_THROWAWAY)
    typedef record_manager<Reclaim, Alloc, Pool, Node<test_type, test_type>, SCXRecord<test_type, test_type> > MemMgmt;
#elif defined(ABTREE) || defined(ABTREE_REUSE_PMARK)
    typedef record_manager<Reclaim, Alloc, Pool, abtree_Node<ABTREE_NODE_DEGREE, test_type> > MemMgmt;
#elif defined(ABTREE_THROWAWAY) || defined(DUMMY_NOOP) || defined(DUMMY_ALLOC)
    typedef record_manager<Reclaim, Alloc, Pool, abtree_Node<ABTREE_NODE_DEGREE, test_type>, abtree_SCXRecord<ABTREE_NODE_DEGREE, test_type> > MemMgmt;
#elif defined(BSLACK_THROWAWAY) || defined(BSLACK_REUSE)
    typedef record_manager<Reclaim, Alloc, Pool, bslack_Node<BSLACK_DEGREE, test_type>, bslack_SCXRecord<BSLACK_DEGREE, test_type> > MemMgmt;
#elif defined(LAZYLIST)
    typedef record_manager<Reclaim, Alloc, Pool, node_t<test_type, test_type> > MemMgmt;
#endif
    EXPERIMENT_FN<MemMgmt>();
    printOutput<MemMgmt>();
}

template <class Reclaim, class Alloc>
void performExperiment() {
    // determine the correct pool class
    
    if (strcmp(POOL_TYPE, "perthread_and_shared") == 0) {
        performExperiment<Reclaim, Alloc, pool_perthread_and_shared<test_type> >();
    } else if (strcmp(POOL_TYPE, "none") == 0) {
        performExperiment<Reclaim, Alloc, pool_none<test_type> >();
    } else {
        cout<<"bad pool type"<<endl;
        exit(1);
    }
}

template <class Reclaim>
void performExperiment() {
    // determine the correct allocator class
    
/*    if (strcmp(ALLOC_TYPE, "once") == 0) {
        performExperiment<Reclaim, allocator_once<test_type> >();
    } else*/ if (strcmp(ALLOC_TYPE, "bump") == 0) {
        performExperiment<Reclaim, allocator_bump<test_type> >();
    } else if (strcmp(ALLOC_TYPE, "new") == 0) {
        performExperiment<Reclaim, allocator_new<test_type> >();
    } else {
        cout<<"bad allocator type"<<endl;
        exit(1);
    }
}

void performExperiment() {
    // determine the correct reclaim class
    
    if (strcmp(RECLAIM_TYPE, "none") == 0) {
        performExperiment<reclaimer_none<test_type> >();
    } else if (strcmp(RECLAIM_TYPE, "debra") == 0) {
        performExperiment<reclaimer_debra<test_type> >();
#ifdef USE_RECLAIMER_RCU
    } else if (strcmp(RECLAIM_TYPE, "rcu") == 0) {
        performExperiment<reclaimer_rcu<test_type> >();
#endif
    } else {
        cout<<"bad reclaimer type"<<endl;
        exit(1);
    }
}

int main(int argc, char** argv) {
    signal(SIGUSR1, sighandler);

    PREFILL = false;
    PRINT_TREE = false; // unused
    NO_THREADS = false; // unused
    OPS_PER_THREAD = 0; // unused
    MILLIS_TO_RUN = 0;
    MAX_FAST_HTM_RETRIES = 10; // unused
    MAX_SLOW_HTM_RETRIES = -1; // unused
    RQ_THREADS = 0;
    WORK_THREADS = 1;
    RQSIZE = 0;
    RQ = 0;
    INS = 0;
    DEL = 0;
    MAXKEY = 100000;
    
    for (int i=1;i<argc;++i) {
        if (strcmp(argv[i], "-i") == 0) {
            INS = atof(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0) {
            DEL = atof(argv[++i]);
        } else if (strcmp(argv[i], "-rq") == 0) {
            RQ = atof(argv[++i]);
        } else if (strcmp(argv[i], "-rqsize") == 0) {
            RQSIZE = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-k") == 0) {
            MAXKEY = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-nrq") == 0) {
            RQ_THREADS = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-nwork") == 0) {
            WORK_THREADS = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-mr") == 0) {
            RECLAIM_TYPE = argv[++i];
        } else if (strcmp(argv[i], "-ma") == 0) {
            ALLOC_TYPE = argv[++i];
        } else if (strcmp(argv[i], "-mp") == 0) {
            POOL_TYPE = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0) {
            MILLIS_TO_RUN = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-p") == 0) {
            PREFILL = true;
        } else if (strcmp(argv[i], "-bind") == 0) {
            binding_parseCustom(string(argv[++i]));
        } else {
            cout<<"bad argument "<<argv[i]<<endl;
            exit(1);
        }
    }
    TOTAL_THREADS = WORK_THREADS + RQ_THREADS;
    
    binding_configurePolicy(TOTAL_THREADS, LOGICAL_PROCESSORS);

    PRINTS(STR(FIND_FUNC));
    PRINTS(STR(INSERT_FUNC));
    PRINTS(STR(ERASE_FUNC));
    PRINTS(STR(RQ_FUNC));
    PRINTS(STR(EXPERIMENT_FN));
    PRINTI(PREFILL);
    PRINTI(MILLIS_TO_RUN);
    PRINTI(INS);
    PRINTI(DEL);
    PRINTI(MAXKEY);
    PRINTI(WORK_THREADS);
    PRINTI(RQ_THREADS);
    PRINTS(RECLAIM_TYPE);
    PRINTS(ALLOC_TYPE);
    PRINTS(POOL_TYPE);
#ifdef WIDTH1_SEQ
    PRINTI(WIDTH1_SEQ);
#endif

    keysum = new debugCounter(MAX_TID_POW2);
    prefillSize = new debugCounter(MAX_TID_POW2);
    
    performExperiment();
    
    delete keysum;
    delete prefillSize;
    return 0;
}
