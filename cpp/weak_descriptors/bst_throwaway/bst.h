/**
 * Preliminary C++ implementation of unbalanced binary search tree using LLX/SCX.
 * 
 * Copyright (C) 2014 Trevor Brown
 * This preliminary implementation is CONFIDENTIAL and may not be distributed.
 */

#ifndef BST_THROWAWAY_H
#define	BST_THROWAWAY_H

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
#include "scxrecord.h"
#include "node.h"
#include <record_manager.h>
#include <debugcounters.h>
#include <random.h>

using namespace std;

class bst_retired_info {
public:
    void * obj;
    atomic_uintptr_t * ptrToObj;
    atomic_bool * nodeContainingPtrToObjIsMarked;
    bst_retired_info(
            void *_obj,
            atomic_uintptr_t *_ptrToObj,
            atomic_bool * _nodeContainingPtrToObjIsMarked)
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
    RecManager * const recmgr;
    volatile int lock; // used for TLE

    const int N; // number of violations to allow on a search path before we fix everything on it
    Node<K,V> *root;        // actually const
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
    SCXRecord<K,V> **allocatedSCXRecord;
    #define GET_ALLOCATED_SCXRECORD_PTR(tid) allocatedSCXRecord[tid*PREFETCH_SIZE_WORDS]
    #define REPLACE_ALLOCATED_SCXRECORD(tid) GET_ALLOCATED_SCXRECORD_PTR(tid) = allocateSCXRecord(tid)

    // similarly, allocatedNodes[tid*PREFETCH_SIZE_WORDS+i] = an allocated node
    //     for i = 0..MAX_NODES-2
    Node<K,V> **allocatedNodes;
    #define GET_ALLOCATED_NODE_PTR(tid, i) allocatedNodes[tid*(PREFETCH_SIZE_WORDS+MAX_NODES)+i]
    #define REPLACE_ALLOCATED_NODE(tid, i) { GET_ALLOCATED_NODE_PTR(tid, i) = allocateNode(tid); /*GET_ALLOCATED_NODE_PTR(tid, i)->left.store((uintptr_t) NULL, memory_order_relaxed);*/ }
    
    // debug info
    debugCounters * const counters;
    
    /**
     * this is what LLX returns when it is performed on a leaf.
     * the important qualities of this value are:
     *      - it is not NULL
     *      - it cannot be equal to any pointer to an scx record
     */
    #define LLX_RETURN_IS_LEAF ((void*) 1)
    #define UPDATE_FUNCTION(name) bool (bst<K,V,Compare,RecManager>::*name)(ReclamationInfo<K,V> * const, const int, void **, void **)
    #define CAST_UPDATE_FUNCTION(name) (UPDATE_FUNCTION()) &bst<K,V,Compare,RecManager>::name

    /* 2-bit state | 5-bit highest index reached | 24-bit frozen flags for each element of nodes[] on which a freezing CAS was performed = total 31 bits (highest bit unused) */
    #define ABORT_STATE_INIT(i, flags) (SCXRecord<K,V>::STATE_ABORTED | ((i)<<2) | ((flags)<<7))
    #define STATE_GET_FLAGS(state) ((state) & 0x7FFFFF80)
    #define STATE_GET_HIGHEST_INDEX_REACHED(state) (((state) & 0x7C)>>2)
    #define STATE_GET_WITH_FLAG_OFF(state, i) ((state) & ~(1<<(i+7)))

    #define VERSION_NUMBER(tid) (version[(tid)*PREFETCH_SIZE_WORDS])
    #define INIT_VERSION_NUMBER(tid) (VERSION_NUMBER(tid) = ((tid << 1) | 1))
    #define NEXT_VERSION_NUMBER(tid) (VERSION_NUMBER(tid) += (MAX_TID_POW2 << 1))
    #define IS_VERSION_NUMBER(infoPtr) (((long) (infoPtr)) & 1)
    long version[MAX_TID_POW2*PREFETCH_SIZE_WORDS]; // version[i*PREFETCH_SIZE_WORDS] = current version number for thread i

    // Originally, I tested (node->key == NO_KEY or node == root->left->left)
    // to see if node is a sentinel, but there is a nice observation:
    //     if an scx succeeds and node == root->left->left,
    //     then parent is root->left, so parent->key == NO_KEY.
    #define IS_SENTINEL(node, parent) ((node)->key == NO_KEY || (parent)->key == NO_KEY)

    inline int getState(const bool, SCXRecord<K,V> * const);
    
    inline SCXRecord<K,V>* allocateSCXRecord(const int tid);
    inline Node<K,V>* allocateNode(const int tid);
    inline SCXRecord<K,V>* initializeSCXRecord(
                const int,
                SCXRecord<K,V> * const,
                ReclamationInfo<K,V> * const,
                atomic_uintptr_t * const,
                Node<K,V> * const);
    inline Node<K,V>* initializeNode(
                const int,
                Node<K,V> * const,
                const K&,
                const V&,
                //const int,
                Node<K,V> * const,
                Node<K,V> * const);
    inline bool tryRetireSCXRecord(const int tid, SCXRecord<K,V> * const scx, Node<K,V> * const node);
    int rangeQuery_lock(ReclamationInfo<K,V> * const, const int, void **input, void **output);
    int rangeQuery_vlx(ReclamationInfo<K,V> * const, const int, void **input, void **output);
    bool updateInsert_search_llx_scx(ReclamationInfo<K,V> * const, const int, void **input, void **output); // input consists of: const K& key, const V& val, const bool onlyIfAbsent
    bool updateErase_search_llx_scx(ReclamationInfo<K,V> * const, const int, void **input, void **output); // input consists of: const K& key, const V& val, const bool onlyIfAbsent
    void reclaimMemoryAfterSCX(
                const int tid,
                ReclamationInfo<K,V> * info);
    int help(const int tid, SCXRecord<K,V> *scx, bool helpingOther);
    inline void* llx(
            const int tid,
            Node<K,V> *node,
            Node<K,V> **retLeft,
            Node<K,V> **retRight);
    inline bool scx(
                const int tid,
                ReclamationInfo<K,V> * const,
                atomic_uintptr_t *field,         // pointer to a "field pointer" that will be changed
                Node<K,V> *newNode);
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
                const int numProcesses,
                int suspectedCrashSignal,
                int allowedViolationsPerPath = 6)
        : N(allowedViolationsPerPath),
                NO_KEY(_NO_KEY),
                NO_VALUE(_NO_VALUE),
                RETRY(_RETRY),
                recmgr(new RecManager(numProcesses, suspectedCrashSignal)),
                counters(new debugCounters(numProcesses)) {

        VERBOSE DEBUG COUTATOMIC("constructor bst"<<endl);
        const int tid = 0;
        recmgr->enterQuiescentState(tid); // block crash recovery signal for this thread, and enter an initial quiescent state.
        dummy = allocateSCXRecord(tid);
        dummy->state.store(SCXRecord<K,V>::STATE_ABORTED, memory_order_relaxed); // this is a NO-OP, so it shouldn't start as InProgress; aborted is just more efficient than committed, since we won't try to help marked leaves, which always have the dummy scx record...
        Node<K,V> *rootleft = initializeNode(tid, allocateNode(tid), NO_KEY, NO_VALUE, NULL, NULL);
        root = initializeNode(tid, allocateNode(tid), NO_KEY, NO_VALUE, rootleft, NULL);
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
        recmgr->initThread(tid);
        if (GET_ALLOCATED_SCXRECORD_PTR(tid) == NULL) {
            REPLACE_ALLOCATED_SCXRECORD(tid);
            for (int i=0;i<MAX_NODES;++i) {
                REPLACE_ALLOCATED_NODE(tid, i);
            }
        }
    }
    void deinitThread(const int tid) {
        recmgr->deinitThread(tid);
    }
    
    long long getSizeInNodes(Node<K,V> * const u) {
        if (u == NULL) return 0;
        return 1 + getSizeInNodes((Node<K,V>*) u->left.load(memory_order_relaxed))
                 + getSizeInNodes((Node<K,V>*) u->right.load(memory_order_relaxed));
    }
    long long getSizeInNodes() {
        return getSizeInNodes((Node<K,V>*) root);
    }
    string getSizeString() {
        stringstream ss;
        int preallocated = MAX_NODES * recmgr->NUM_PROCESSES;
        ss<<getSizeInNodes()<<" nodes in tree and "<<preallocated<<" preallocated but unused";
        return ss.str();
    }
    long long getSize(Node<K,V> * const u) {
        if (u == NULL) return 0;
        if ((Node<K,V>*) u->left.load(memory_order_relaxed) == NULL) return 1; // is leaf
        return getSize((Node<K,V>*) u->left.load(memory_order_relaxed))
             + getSize((Node<K,V>*) u->right.load(memory_order_relaxed));
    }
    long long getSize() {
        return getSize((Node<K,V>*) root);
    }
    
    void dfsDeallocateBottomUp(Node<K,V> * const u, set<void*>& seen, int *numNodes) {
        if (u == NULL) return;
        if ((Node<K,V>*) u->left.load(memory_order_relaxed) != NULL) {
            dfsDeallocateBottomUp((Node<K,V>*) u->left.load(memory_order_relaxed), seen, numNodes);
            dfsDeallocateBottomUp((Node<K,V>*) u->right.load(memory_order_relaxed), seen, numNodes);
        }
        SCXRecord<K,V>* rec = (SCXRecord<K,V>*) u->scxRecord.load(memory_order_relaxed);
        if (rec != NULL && !IS_VERSION_NUMBER(rec)) {
            seen.insert((Node<K,V>*) u->scxRecord.load(memory_order_relaxed));
        }
        DEBUG ++(*numNodes);
        recmgr->deallocate(0 /* tid */, u);
    }
    ~bst() {
        VERBOSE DEBUG COUTATOMIC("destructor bst");
        // free every node and scx record currently in the data structure.
        // an easy DFS, freeing from the leaves up, handles all nodes.
        // cleaning up scx records is a little bit harder if they are in progress or aborted.
        // they have to be collected and freed only once, since they can be pointed to by many nodes.
        // so, we keep them in a set, then free each set element at the end.
// note: the following is disabled for speed during experimental runs. the OS will release all memory, anyway.
#if 0
        set<void*> seen;
        int numNodes = 0;
        dfsDeallocateBottomUp(root, seen, &numNodes);
        for (set<void*>::iterator it = seen.begin(); it != seen.end(); it++) {
            recmgr->deallocate(0 /* tid */, (SCXRecord<K,V>*) *it);
        }
        VERBOSE DEBUG COUTATOMIC(" deallocated nodes "<<numNodes<<" scx records "<<seen.size()<<endl);
        for (int tid=0;tid<recmgr->NUM_PROCESSES;++tid) {
            for (int i=0;i<MAX_NODES;++i) {
                recmgr->deallocate(tid, GET_ALLOCATED_NODE_PTR(tid, i));
            }
            recmgr->deallocate(tid, GET_ALLOCATED_SCXRECORD_PTR(tid));
        }
#endif
        delete recmgr;
        delete counters;
        delete[] allocatedSCXRecord;
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
    bool contains(const int tid, const K& key);
    int size(void); /** warning: size is a LINEAR time operation, and does not return consistent results with concurrency **/
    
    void debugPrintAllocatorStatus() {
        recmgr->printStatus();
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
        //recmgr->clearCounters();
    }
    debugCounters * const debugGetCounters() {
        return counters;
    }
    RecManager * const debugGetRecMgr() {
        return recmgr;
    }
    
    bool validate(const long long keysum, const bool checkkeysum);
    long long debugKeySum() {
        return debugKeySum((Node<K,V> *) ((Node<K,V> *) root->left.load(memory_order_relaxed))->left.load(memory_order_relaxed));
        //return debugKeySum((Node<K,V> *) root->left.load(memory_order_relaxed));
    }
};

#endif	/* BST_THROWAWAY_H */

