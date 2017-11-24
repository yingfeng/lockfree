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
 */

#ifndef _DICTIONARY_H_
#define _DICTIONARY_H_

#include <stdbool.h>
#include <utility>
#include "prcu.h"
#include "../debugcounters.h"
#include "../globals_extern.h"
#include "../tle.h"
#include <signal.h>
using namespace std;

//#define NO_TXNS
#ifdef NO_TXNS
    #define XBEGIN() _XBEGIN_STARTED; /*__sync_synchronize();*/
    #define XEND() /*__sync_synchronize();*/
    #define XABORT(_status) /*status = (_status); __sync_synchronize(); goto aborthere;*/
    #define XTEST() false
#endif

//#define INSERT_REPLACE

#define infinity 2147483647

typedef struct node_t *nodeptr;
struct predicate_info_t;

typedef struct wrapper_info_t {
    int lastAbort;
    int path;
    int numberOfNodesAllocated;
    int numberOfNodesRetired;
} wrapper_info;

template <class RecManager>
class citrustree {
private:
    RecManager * const recordmgr;
    volatile int lock; // used for TLE
    
    nodeptr root;
    volatile int numFallback; // number of processes on the fallback path
    
    debugCounters * const counters;
    
    nodeptr newNode(const int tid, int key, int value);
    bool insert_fast(const int tid, wrapper_info *info, int key, int value, bool * const erased);
    bool insert_middle(const int tid, wrapper_info *info, int key, int value, bool * const erased);
    bool insert_fallback(const int tid, wrapper_info *info, int key, int value, bool * const erased);
    bool erase_fast(const int tid, wrapper_info *info, int key, bool * const erased);
    bool erase_middle(const int tid, wrapper_info *info, int key, bool * const erased);
    bool erase_fallback(const int tid, wrapper_info *info, int key, bool * const erased);
    bool rq_fast(const int tid, wrapper_info *info, int lo, int hi, int *results, int * const cnt);
    bool rq_middle(const int tid, wrapper_info *info, int lo, int hi, int *results, int * const cnt);
    bool rq_fallback(const int tid, wrapper_info *info, int lo, int hi, int *results, int * const cnt);
    long long debugKeySum(nodeptr root);

public:
    citrustree(int max_key, int numProcesses);	// if the max key is not known, provide a negative number
    int contains(const int tid, int key);
    int contains_tle(const int tid, int key);
    bool insert(const int tid, int key, int value);
    bool insert_tle(const int tid, int key, int value);
    bool erase(const int tid, int key);
    bool erase_tle(const int tid, int key);
    int rangeQuery(const int tid, int lo, int hi, int *results);
    int rangeQuery_tle(const int tid, int lo, int hi, int *results);
    
    debugCounters * debugGetCounters() { return counters; }
    long long debugKeySum();
    void clearCounters() { counters->clear(); }
    
    /**
     * This function must be called once by each thread that will
     * invoke any functions on this class.
     * 
     * It must be okay that we do this with the main thread and later with another thread!!!
     */
    void initThread(const int tid) {
        recordmgr->initThread(tid);
    }
};

#endif
