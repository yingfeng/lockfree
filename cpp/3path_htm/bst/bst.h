/**
 * Preliminary C++ implementation of unbalanced binary search tree using LLX/SCX.
 * 
 * Copyright (C) 2014 Trevor Brown
 * 
 * Why is this code so long?
 * - Because this file defines FOUR implementations
 *   (1) transactional lock elision (suffix _tle)
 *   (2) hybrid tm based implementation (suffix _tm)
 *   (3) 3-path implementation (suffixes _fallback, _middle, _fast)
 *   (4) global locking (suffix _lock_search_inplace)
 * - Because the LLX and SCX synchronization primitives are implemented here
 *   (including memory reclamation for SCX records)
 */

#ifndef BST_H
#define	BST_H

#include <string>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>
#include <setjmp.h>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdexcept>
#include <bitset>
#include <record_manager.h>
#include <debugcounters.h>
#include <rtm.h>
#include <random.h>
#include <tle.h>
#include "scxrecord.h"
#include "node.h"
#ifdef TM
#include "../hybridnorec/hybridnorec/tm.h"
//#include "../hybridnorec/hytm1/tm.h"
#endif

#ifndef IF_ALWAYS_RETRY_WHEN_BIT_SET
#define IF_ALWAYS_RETRY_WHEN_BIT_SET if(0)
#endif

using namespace std;

#define OLD_SCXRECORD_RECLAMATION

class bst_retired_info {
public:
    void * obj;
    void * volatile * ptrToObj;
    volatile bool * nodeContainingPtrToObjIsMarked;
    bst_retired_info(
            void * _obj,
            void * volatile * _ptrToObj,
            volatile bool * _nodeContainingPtrToObjIsMarked)
            : obj(_obj),
              ptrToObj(_ptrToObj),
              nodeContainingPtrToObjIsMarked(_nodeContainingPtrToObjIsMarked) {}
    bst_retired_info() {}
};

template <class K, class V>
class ReclamationInfo {
public:
    int type;
    Node<K,V> *nodes[MAX_NODES];
    void *llxResults[MAX_NODES];
    int state;
    int numberOfNodes;
    int numberOfNodesToFreeze;
    int numberOfNodesToReclaim;
    int numberOfNodesAllocated;
    int path;
    bool capacityAborted[NUMBER_OF_PATHS];
    int lastAbort;
};

template <class K, class V, class Compare, class RecManager>
class bst {
private:
    RecManager * const shmem;
    volatile int lock; // used for TLE

//    const int N; // number of violations to allow on a search path before we fix everything on it
    volatile char padding0[PREFETCH_SIZE_BYTES];
    Node<K,V> *root;        // actually const
    volatile char padding1[PREFETCH_SIZE_BYTES];
    SCXRecord<K,V> *dummy;  // actually const
    Compare cmp;
    
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
    volatile char padding2[PREFETCH_SIZE_BYTES];
    SCXRecord<K,V> **allocatedSCXRecord;
    volatile char padding3[PREFETCH_SIZE_BYTES];
    #define GET_ALLOCATED_SCXRECORD_PTR(tid) allocatedSCXRecord[tid*PREFETCH_SIZE_WORDS]
    #define REPLACE_ALLOCATED_SCXRECORD(tid) GET_ALLOCATED_SCXRECORD_PTR(tid) = allocateSCXRecord(tid)

    // similarly, allocatedNodes[tid*PREFETCH_SIZE_WORDS+i] = an allocated node
    //     for i = 0..MAX_NODES-2
    volatile char padding4[PREFETCH_SIZE_BYTES];
    Node<K,V> **allocatedNodes;
    volatile char padding5[PREFETCH_SIZE_BYTES];
    #define GET_ALLOCATED_NODE_PTR(tid, i) allocatedNodes[tid*(PREFETCH_SIZE_WORDS+MAX_NODES)+i]
    #define REPLACE_ALLOCATED_NODE(tid, i) { GET_ALLOCATED_NODE_PTR(tid, i) = allocateNode(tid); /*GET_ALLOCATED_NODE_PTR(tid, i)->left.store((uintptr_t) NULL, memory_order_relaxed);*/ }
    
    // debug info
    debugCounters * const counters;
    volatile char padding6[PREFETCH_SIZE_BYTES];
    
    /**
     * this is what LLX returns when it is performed on a leaf.
     * the important qualities of this value are:
     *      - it is not NULL
     *      - it cannot be equal to any pointer to an scx record
     */
    #define LLX_RETURN_IS_LEAF ((void*) 1)

    #define UPDATE_FUNCTION(name) bool (bst<K,V,Compare,RecManager>::*name)(ReclamationInfo<K,V> * const, const int, void **, void **)
    #define CAST_UPDATE_FUNCTION(name) (UPDATE_FUNCTION()) &bst<K,V,Compare,RecManager>::name

#ifdef OLD_SCXRECORD_RECLAMATION
    /* 2-bit state | 5-bit highest index reached | 24-bit frozen flags for each element of nodes[] on which a freezing CAS was performed = total 31 bits (highest bit unused) */
    #define ABORT_STATE_INIT(i, flags) (SCXRecord<K,V>::STATE_ABORTED | ((i)<<2) | ((flags)<<7))
    #define STATE_GET_FLAGS(state) ((state) & 0x7FFFFF80)
    #define STATE_GET_HIGHEST_INDEX_REACHED(state) (((state) & 0x7C)>>2)
    #define STATE_GET_WITH_FLAG_OFF(state, i) ((state) & ~(1<<(i+7)))
#else
    #define ABORT_STATE_INIT(i, freezeCount) (SCXRecord<K,V>::STATE_ABORTED | ((i)<<2) | ((freezeCount)<<16))
    #define STATE_GET_HIGHEST_INDEX_REACHED(state) (((state) & ((1<<16)-1))>>2)
    #define STATE_GET_REFCOUNT(state) ((state)>>16)
    #define STATE_REFCOUNT_UNIT (1<<16)
#endif
    
    #define VERSION_NUMBER(tid) (version[(tid)*PREFETCH_SIZE_WORDS])
    #define INIT_VERSION_NUMBER(tid) (VERSION_NUMBER(tid) = ((tid << 1) | 1))
    #define NEXT_VERSION_NUMBER(tid) (VERSION_NUMBER(tid) += (MAX_TID_POW2 << 1))
    #define IS_VERSION_NUMBER(infoPtr) (((long) (infoPtr)) & 1)
    long version[MAX_TID_POW2*PREFETCH_SIZE_WORDS]; // version[i*PREFETCH_SIZE_WORDS] = current version number for thread i

    volatile char padding7[PREFETCH_SIZE_BYTES];
    atomic_uint numFallback; // number of processes on the fallback path
    volatile char padding8[PREFETCH_SIZE_BYTES];

    int numSlowHTM;  // TODO: consider making this a SNZI object
    volatile char padding9[PREFETCH_SIZE_BYTES];
    
    // Originally, I tested (node->key == NO_KEY or node == root->left->left)
    // to see if node is a sentinel, but there is a nice observation:
    //     if an scx succeeds and node == root->left->left,
    //     then parent is root->left, so parent->key == NO_KEY.
    #define IS_SENTINEL(node, parent) ((node)->key == NO_KEY || (parent)->key == NO_KEY)

    inline int getState(const bool, SCXRecord<K,V> * const);
    
    __rtm_force_inline SCXRecord<K,V>* allocateSCXRecord(const int tid);
    __rtm_force_inline Node<K,V>* allocateNode(const int tid);
    __rtm_force_inline SCXRecord<K,V>* initializeSCXRecord(
                const int,
                SCXRecord<K,V> * const,
                ReclamationInfo<K,V> * const,
                Node<K,V> * volatile * const,
                Node<K,V> * const);
    __rtm_force_inline Node<K,V>* initializeNode(
                const int,
                Node<K,V> * const,
                const K&,
                const V&,
                //const int,
                Node<K,V> * const,
                Node<K,V> * const);
    inline bool tryRetireSCXRecord(const int tid, SCXRecord<K,V> * const scx, Node<K,V> * const node);
    void unblockCrashRecoverySignal();
    void blockCrashRecoverySignal();
    void simulateSignalReceipt(const int __tid, const int location);
    bool recoverAnyAttemptedSCX(const int tid, const int location);
    void htmWrapper(UPDATE_FUNCTION(), UPDATE_FUNCTION(), UPDATE_FUNCTION(), const int, void **input, void **output);
    int rangeQuery_txn(ReclamationInfo<K,V> * const, const int, void **input, void **output);
    int rangeQuery_lock(ReclamationInfo<K,V> * const, const int, void **input, void **output);
    int rangeQuery_vlx(ReclamationInfo<K,V> * const, const int, void **input, void **output);
    bool updateInsert_txn_search_inplace(ReclamationInfo<K,V> * const, const int, void **input, void **output); // input consists of: const K& key, const V& val, const bool onlyIfAbsent
    bool updateInsert_txn_search_replace_markingw_infowr(ReclamationInfo<K,V> * const, const int, void **input, void **output); // input consists of: const K& key, const V& val, const bool onlyIfAbsent
    bool updateInsert_search_llx_scx(ReclamationInfo<K,V> * const, const int, void **input, void **output); // input consists of: const K& key, const V& val, const bool onlyIfAbsent
    bool updateInsert_lock_search_inplace(ReclamationInfo<K,V> * const, const int, void **input, void **output); // input consists of: const K& key, const V& val, const bool onlyIfAbsent
    bool updateErase_txn_search_inplace(ReclamationInfo<K,V> * const, const int, void **input, void **output); // input consists of: const K& key, const V& val, const bool onlyIfAbsent
    bool updateErase_txn_search_replace_markingw_infowr(ReclamationInfo<K,V> * const, const int, void **input, void **output); // input consists of: const K& key, const V& val, const bool onlyIfAbsent
    bool updateErase_search_llx_scx(ReclamationInfo<K,V> * const, const int, void **input, void **output); // input consists of: const K& key, const V& val, const bool onlyIfAbsent
    bool updateErase_lock_search_inplace(ReclamationInfo<K,V> * const, const int, void **input, void **output); // input consists of: const K& key, const V& val, const bool onlyIfAbsent
    void replaceUsedObjects(const int, const bool, const int, const int);
    void reclaimMemoryAfterSCX(
                const int tid,
                ReclamationInfo<K,V> * info);
    int help(const int tid, SCXRecord<K,V> *scx, bool helpingOther);
    inline void* llx(
            const int tid,
            Node<K,V> *node,
            Node<K,V> **retLeft,
            Node<K,V> **retRight);
    inline void* llx_htm(
            const int tid,
            Node<K,V> *node,
            Node<K,V> **retLeft,
            Node<K,V> **retRight);
    __rtm_force_inline void* llx_intxn_markingwr_infowr(
            const int tid,
            Node<K,V> *node,
            Node<K,V> **retLeft,
            Node<K,V> **retRight);
    inline bool scx(
                const int tid,
                ReclamationInfo<K,V> * const,
                Node<K,V> * volatile * field,         // pointer to a "field pointer" that will be changed
                Node<K,V> * newNode);
    inline bool scx_htm(
                const int tid,
                ReclamationInfo<K,V> * const,
                Node<K,V> * volatile * field,         // pointer to a "field pointer" that will be changed
                Node<K,V> * newNode);
    __rtm_force_inline bool scx_intxn_markingwr_infowr(
                const int tid,
                ReclamationInfo<K,V> * const,
                Node<K,V> * volatile * field,         // pointer to a "field pointer" that will be changed
                Node<K,V> * newNode);
    inline int computeSize(Node<K,V>* node);
    
    long long debugKeySum(Node<K,V> * node);
    bool validate(Node<K,V> * const node, const int currdepth, const int leafdepth);

public:
    const K& NO_KEY;
    const V& NO_VALUE;
    const V& RETRY;
    bst(const K& _NO_KEY,
                const V& _NO_VALUE,
                const V& _RETRY,
                const int numProcesses)
            : NO_KEY(_NO_KEY)
            , NO_VALUE(_NO_VALUE)
            , RETRY(_RETRY)
            , shmem(new RecManager(numProcesses, SIGQUIT))
            , counters(new debugCounters(numProcesses)) {
        VERBOSE DEBUG COUTATOMIC("constructor bst"<<endl);
        const int tid = 0;
        numFallback = 0;
        numSlowHTM = 0;
        shmem->enterQuiescentState(tid); // block crash recovery signal for this thread, and enter an initial quiescent state.
        dummy = allocateSCXRecord(tid);
        //dummy->type = SCXRecord<K,V>::TYPE_NOOP;
        dummy->state = SCXRecord<K,V>::STATE_ABORTED; // this is a NO-OP, so it shouldn't start as InProgress; aborted is just more efficient than committed, since we won't try to help marked leaves, which always have the dummy scx record...
        Node<K,V> *rootleft = initializeNode(tid, allocateNode(tid), NO_KEY, NO_VALUE, /*1,*/ NULL, NULL);
        root = initializeNode(tid, allocateNode(tid), NO_KEY, NO_VALUE, /*1,*/ rootleft, NULL);
        cmp = Compare();
        allocatedSCXRecord = new SCXRecord<K,V>*[numProcesses*PREFETCH_SIZE_WORDS];
        allocatedNodes = new Node<K,V>*[numProcesses*(PREFETCH_SIZE_WORDS+MAX_NODES)];
        for (int tid=0;tid<numProcesses;++tid) {
            INIT_VERSION_NUMBER(tid);
            GET_ALLOCATED_SCXRECORD_PTR(tid) = NULL;
        }
    }
    /**
     * This function must be called once by each thread that will
     * invoke any functions on this class.
     * 
     * It must be okay that we do this with the main thread and later with another thread!!!
     */
    void initThread(const int tid) {
        shmem->initThread(tid);
        if (GET_ALLOCATED_SCXRECORD_PTR(tid) == NULL) {
            REPLACE_ALLOCATED_SCXRECORD(tid);
            for (int i=0;i<MAX_NODES;++i) {
                REPLACE_ALLOCATED_NODE(tid, i);
            }
        }
    }
    void deinitThread(const int tid) {
    }
    
    void dfsDeallocateBottomUp(Node<K,V> * const u, set<void*>& seen, int *numNodes) {
        if (u == NULL) return;
        if (u->left != NULL) {
            dfsDeallocateBottomUp(u->left, seen, numNodes);
            dfsDeallocateBottomUp(u->right, seen, numNodes);
        }
        SCXRecord<K,V>* rec = u->scxRecord;
        if (rec != NULL && !IS_VERSION_NUMBER(rec)) {
            seen.insert(u->scxRecord);
        }
        DEBUG ++(*numNodes);
        shmem->deallocate(0 /* tid */, u);
    }
    ~bst() {
#ifndef SKIP_DATA_STRUCTURE_DESTRUCTION
        VERBOSE DEBUG COUTATOMIC("destructor bst");
        // free every node and scx record currently in the data structure.
        // an easy DFS, freeing from the leaves up, handles all nodes.
        // cleaning up scx records is a little bit harder if they are in progress or aborted.
        // they have to be collected and freed only once, since they can be pointed to by many nodes.
        // so, we keep them in a set, then free each set element at the end.
        set<void*> seen;
        int numNodes = 0;
        dfsDeallocateBottomUp(root, seen, &numNodes);
        for (set<void*>::iterator it = seen.begin(); it != seen.end(); it++) {
            shmem->deallocate(0 /* tid */, (SCXRecord<K,V>*) *it);
        }
        VERBOSE DEBUG COUTATOMIC(" deallocated nodes "<<numNodes<<" scx records "<<seen.size()<<endl);
        for (int tid=0;tid<shmem->NUM_PROCESSES;++tid) {
            for (int i=0;i<MAX_NODES;++i) {
                shmem->deallocate(tid, GET_ALLOCATED_NODE_PTR(tid, i));
            }
            shmem->deallocate(tid, GET_ALLOCATED_SCXRECORD_PTR(tid));
        }
        delete shmem;
        delete counters;
        delete[] allocatedSCXRecord;
#endif
    }

    Node<K,V> *getRoot(void) { return root; }
    const V insert(const int tid, const K& key, const V& val);
    const V insert_tle(const int tid, const K& key, const V& val);
    const pair<V,bool> erase(const int tid, const K& key);
    const pair<V,bool> erase_tle(const int tid, const K& key);
    const pair<V,bool> find(const int tid, const K& key);
    const pair<V,bool> find_tle(const int tid, const K& key);
    int rangeQuery(const int tid, const K& low, const K& hi, Node<K,V> const ** result);
    int rangeQuery_tle(const int tid, const K& low, const K& hi, Node<K,V> const ** result);
#ifdef TM
    const pair<V,bool> find_tm(TM_ARGDECL_ALONE, const int tid, const K& key);
    const pair<V,bool> erase_tm(TM_ARGDECL_ALONE, const int tid, const K& key);
    const V insert_tm(TM_ARGDECL_ALONE, const int tid, const K& key, const V& val);
    int rangeQuery_tm(TM_ARGDECL_ALONE, const int tid, const K& low, const K& hi, Node<K,V> const ** result);
#endif
    //bool contains(const int tid, const K& key);
    int size(void); /** warning: size is a LINEAR time operation, and does not return consistent results with concurrency **/
    
    void debugPrintAllocatorStatus() {
        shmem->printStatus();
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
        shmem->clearCounters();
    }
    debugCounters * const debugGetCounters() {
        return counters;
    }
    RecManager * const debugGetShmem() {
        return shmem;
    }
    
    bool validate(const long long keysum, const bool checkkeysum);
    long long debugKeySum() {
        return debugKeySum(root->left->left);
        //return debugKeySum(root->left);
    }
};

#endif	/* BST_H */

