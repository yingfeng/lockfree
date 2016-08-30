/**
 * C++ implementation of lock-free chromatic tree using LLX/SCX and DEBRA(+).
 * 
 * Copyright (C) 2016 Trevor Brown
 * Contact (tabrown [at] cs [dot] toronto [dot edu]) with any questions or comments.
 *
 * This program is free software: you can redistribute it and/or modify
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
 */

#ifndef CHROMATIC_H
#define	CHROMATIC_H

#include <string>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>
#include <unistd.h>
#include <sys/types.h>
#include <stdexcept>
#include <bitset>

#include "globals.h"
#include "random.h"
#include "scxrecord.h"
#include "node.h"

#include <csignal>
#include <setjmp.h>
#include "recordmgr/record_manager.h"
#include "recordmgr/recovery_manager.h"

using namespace std;

/**
 * Class containing the information necessary to decide whether a Node has
 * been retired. This information is used by the hazard pointer scheme
 * to determine whether a hazard pointer can safely be acquired.
 * If a node u points to a node v, and u is not marked, then u is in the tree,
 * and so is v. However, if u is marked, then u might have been removed from
 * the tree, and, hence, so might v. See the paper for more information about
 * how we respond to marked nodes, and about the complications that arise when
 * hazard pointers are used.
 */
class Chromatic_retired_info {
public:
    void * obj;
    atomic_uintptr_t * ptrToObj;
    atomic_bool * nodeContainingPtrToObjIsMarked;
    Chromatic_retired_info(
            void *_obj,
            atomic_uintptr_t *_ptrToObj,
            atomic_bool * _nodeContainingPtrToObjIsMarked)
            : obj(_obj),
              ptrToObj(_ptrToObj),
              nodeContainingPtrToObjIsMarked(_nodeContainingPtrToObjIsMarked) {}
    Chromatic_retired_info() {}
};

template <class K, class V, class Compare, class MasterRecordMgr>
class Chromatic {
private:
    Compare cmp;
    
    /**
     * Memory management
     */
    
    MasterRecordMgr * const recordmgr;

    // allocatedSCXRecord[tid*PREFETCH_SIZE_WORDS] = an allocated scx record
    //     ready for thread tid to use for its next SCX.
    //     the first such scx record for each thread tid is allocated in
    //     initThread(tid). subsequently, each scx record for thread tid is
    //     allocated by the implementation of SCX (in a quiescent state).
    // this is kind of like a local thread pool, but much simpler (so we can
    // take and use an scx record in one atomic step that we can do in a
    // non-quiescent state). this is useful because not every operation
    // needs to create an scx record, and we can avoid allocating scx records
    // for operations that don't need them by holding onto the last allocated
    // scx record, here, until it's needed by one of thread tid's operations.
    SCXRecord<K,V> **allocatedSCXRecord;
    #define REPLACE_ALLOCATED_SCXRECORD(tid) GET_ALLOCATED_SCXRECORD_PTR(tid) = allocateSCXRecord(tid)
    #define GET_ALLOCATED_SCXRECORD_PTR(tid) allocatedSCXRecord[tid*PREFETCH_SIZE_WORDS]

    // similarly, allocatedNodes[tid*PREFETCH_SIZE_WORDS+i] = an allocated node
    //     for i = 0..MAX_NODES-2
    Node<K,V> **allocatedNodes;
    #define REPLACE_ALLOCATED_NODE(tid, i) GET_ALLOCATED_NODE_PTR(tid, i) = allocateNode(tid)
    #define GET_ALLOCATED_NODE_PTR(tid, i) allocatedNodes[tid*(PREFETCH_SIZE_WORDS+MAX_NODES-1)+i]

    bool reclaimMemoryAfterSCX(
            const int tid,
            const int operationType,
            Node<K,V> **nodes,
            SCXRecord<K,V> * const * const scxRecordsSeen,
            const int state);
    
    // functions for DEBRA and DEBRA+
    inline bool tryRetireSCXRecord(const int tid, SCXRecord<K,V> * const scx, Node<K,V> * const node);
    inline void unblockCrashRecoverySignal(); // DEBRA+
    bool recoverAnyAttemptedSCX(const int tid, const int location); // DEBRA+

    /**
     * Implementation of LLX and SCX
     */
    
    /**
     * this is what LLX returns when it is performed on a leaf.
     * the important qualities of this value are:
     *      - it is not NULL
     *      - it cannot be equal to any pointer to an scx record
     */
    #define LLX_RETURN_IS_LEAF ((void*) 1)
    #define LLX llx
    #define SCXAndEnterQuiescentState scx

    int help(const int tid, SCXRecord<K,V> *scx, bool helpingOther);
    bool scx(
            const int tid,
            const int operationType,
            Node<K,V> **nodes,
            void **llxResults,
            atomic_uintptr_t *field, // pointer to a "field pointer" that will be changed
            Node<K,V> *newNode);
    void* llx(
            const int tid,
            Node<K,V> *node,
            Node<K,V> **retLeft,
            Node<K,V> **retRight);

    /**
     * Chromatic tree implementation
     */
    
    const int N; // number of violations to allow on a search path before we fix everything on it
    Node<K,V> *root;        // actually const
    SCXRecord<K,V> *dummy;  // actually const
    
    bool updateInsert(const int, const K& key, const V& val, const bool onlyIfAbsent, V *result, bool *shouldRebalance); // last 2 args are output args
    bool updateErase(const int, const K& key, V *result, bool *shouldRebalance); // last 2 args are output args
    bool updateRebalancingStep(const int tid, const K& key);
    int computeSize(Node<K,V>* node);
    
    // rotations
    void fixAllToKey(const int tid, const K& k);
    bool doBlk(const int, Node<K,V> **, void **, bool);
    bool doRb1(const int, Node<K,V> **, void **, bool);
    bool doRb2(const int, Node<K,V> **, void **, bool);
    bool doPush(const int, Node<K,V> **, void **, bool);
    bool doW1(const int, Node<K,V> **, void **, bool);
    bool doW2(const int, Node<K,V> **, void **, bool);
    bool doW3(const int, Node<K,V> **, void **, bool);
    bool doW4(const int, Node<K,V> **, void **, bool);
    bool doW5(const int, Node<K,V> **, void **, bool);
    bool doW6(const int, Node<K,V> **, void **, bool);
    bool doW7(const int, Node<K,V> **, void **, bool);
    bool doRb1Sym(const int, Node<K,V> **, void **, bool);
    bool doRb2Sym(const int, Node<K,V> **, void **, bool);
    bool doPushSym(const int, Node<K,V> **, void **, bool);
    bool doW1Sym(const int, Node<K,V> **, void **, bool);
    bool doW2Sym(const int, Node<K,V> **, void **, bool);
    bool doW3Sym(const int, Node<K,V> **, void **, bool);
    bool doW4Sym(const int, Node<K,V> **, void **, bool);
    bool doW5Sym(const int, Node<K,V> **, void **, bool);
    bool doW6Sym(const int, Node<K,V> **, void **, bool);
    bool doW7Sym(const int, Node<K,V> **, void **, bool);

    // Originally, I tested (node->key == NO_KEY or node == root->left->left)
    // to see if node is a sentinel, but there is a nice observation:
    //     if an scx succeeds and node == root->left->left,
    //     then parent is root->left, so parent->key == NO_KEY.
    #define IS_SENTINEL(node, parent) ((node)->key == NO_KEY || (parent)->key == NO_KEY)

    // debug info
    debugCounters * const counters;
    
    long long debugKeySum(Node<K,V> * node) {
        if (node == NULL) return 0;
        if ((Node<K,V> *) node->left.load(memory_order_relaxed) == NULL) return (long long) node->key;
        return debugKeySum((Node<K,V> *) node->left.load(memory_order_relaxed))
             + debugKeySum((Node<K,V> *) node->right.load(memory_order_relaxed));
    }
    
public:
    const K& NO_KEY;
    const V& NO_VALUE;
    const V& RETRY;
    Chromatic(const K& _NO_KEY,
              const V& _NO_VALUE,
              const V& _RETRY,
              const int numProcesses,
              int neutralizeSignal,
              int allowedViolationsPerPath = 6);
    
    /**
     * This function must be called once by each thread that will
     * invoke any functions on this class.
     * 
     * It must be okay that we do this with the main thread and later with another thread!!!
     */
    void initThread(const int tid);
    
    void dfsDeallocateBottomUp(Node<K,V> * const u, set<void*>& seen, int *numNodes) {
        if (u == NULL) return;
        if ((Node<K,V>*) u->left.load(memory_order_relaxed) != NULL) {
            dfsDeallocateBottomUp((Node<K,V>*) u->left.load(memory_order_relaxed), seen, numNodes);
            dfsDeallocateBottomUp((Node<K,V>*) u->right.load(memory_order_relaxed), seen, numNodes);
        }
        if ((void *) u->scxRecord.load(memory_order_relaxed) != NULL) {
            seen.insert((void *) u->scxRecord.load(memory_order_relaxed));
        }
        DEBUG ++(*numNodes);
        recordmgr->deallocate(0 /* tid */, u);
    }
    ~Chromatic() {
        VERBOSE DEBUG COUTATOMIC("destructor chromatic");
        // free every node and scx record currently in the data structure.
        // an easy DFS, freeing from the leaves up, handles all nodes.
        // cleaning up scx records is a little bit harder if they are in progress or aborted.
        // they have to be collected and freed only once, since they can be pointed to by many nodes.
        // so, we keep them in a set, then free each set element at the end.
        set<void*> seen;
        int numNodes = 0;
        dfsDeallocateBottomUp(root, seen, &numNodes);
        for (set<void*>::iterator it = seen.begin(); it != seen.end(); it++) {
            recordmgr->deallocate(0 /* tid */, (SCXRecord<K,V>*) *it);
        }
        VERBOSE DEBUG COUTATOMIC(" deallocated nodes "<<numNodes<<" scx records "<<seen.size()<<endl);
        for (int tid=0;tid<recordmgr->NUM_PROCESSES;++tid) {
            for (int i=0;i<MAX_NODES-1;++i) {
                recordmgr->deallocate(tid, GET_ALLOCATED_NODE_PTR(tid, i));
            }
            recordmgr->deallocate(tid, GET_ALLOCATED_SCXRECORD_PTR(tid));
        }
        delete recordmgr;
        delete counters;
        delete[] allocatedSCXRecord;
    }

    Node<K,V> *getRoot(void) { return root; }
    const V insert(const int tid, const K& key, const V& val);
    const bool insertIfAbsent(const int tid, const K& key, const V& val);
    const pair<V,bool> erase(const int tid, const K& key);
    const pair<V,bool> find(const int tid, const K& key);
    bool contains(const int tid, const K& key);
    int size(void); /** warning: size is a LINEAR time operation, and does not return consistent results with concurrency **/
    
    /**
     * Debugging functions
     */
    
    long long debugKeySum() {
        return debugKeySum((Node<K,V> *) (((Node<K,V> *) root->left.load(memory_order_relaxed))->left.load(memory_order_relaxed)));
    }
    void debugPrintAllocatorStatus() {
        recordmgr->printStatus();
    }
    void debugPrintToFile(string prefix, long id1, string infix, long id2, string suffix) {
        stringstream ss;
        ss<<prefix<<id1<<infix<<id2<<suffix;
        COUTATOMIC("print to filename \""<<ss.str()<<"\""<<endl);
        fstream fs (ss.str().c_str(), fstream::out);
        root->printTreeFile(fs);
        fs.close();
    }
    void debugPrintToFileWeight(string prefix, long id1, string infix, long id2, string suffix) {
        stringstream ss;
        ss<<prefix<<id1<<infix<<id2<<suffix;
        COUTATOMIC("print to filename \""<<ss.str()<<"\""<<endl);
        fstream fs (ss.str().c_str(), fstream::out);
        root->printTreeFileWeight(fs);
        fs.close();
    }
    void clearCounters() {
        counters->clear();
        recordmgr->clearCounters();
    }
    debugCounters * const debugGetCounters() {
        return counters;
    }
    MasterRecordMgr * const debugGetRecordMgr() {
        return recordmgr;
    }
};

#include "chromatic_impl.h"

#endif	/* CHROMATIC_H */

