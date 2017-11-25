/**
 * Preliminary C++ implementation of unbalanced binary search tree using LLX/SCX.
 * 
 * Copyright (C) 2014 Trevor Brown
 * This preliminary implementation is CONFIDENTIAL and may not be distributed.
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
#include "scxrecord.h"
#include "node.h"
#include <record_manager.h>
#include <debugcounters.h>
#include <random.h>

using namespace std;

template <class K, class V>
class ReclamationInfo {
public:
    int type;
    void *llxResults[MAX_NODES];
    Node<K,V> *nodes[MAX_NODES];
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
    Compare cmp;
    
    // allocatedNodes[tid*PREFETCH_SIZE_WORDS+i] = an allocated node
    //     for i = 0..MAX_NODES-2
    Node<K,V> **allocatedNodes;
    #define GET_ALLOCATED_NODE_PTR(tid, i) allocatedNodes[tid*(PREFETCH_SIZE_WORDS+MAX_NODES)+i]
    #define REPLACE_ALLOCATED_NODE(tid, i) { GET_ALLOCATED_NODE_PTR(tid, i) = allocateNode(tid); /*GET_ALLOCATED_NODE_PTR(tid, i)->left.store((uintptr_t) NULL, memory_order_relaxed);*/ }

    #define IS_SENTINEL(node, parent) ((node)->key == NO_KEY || (parent)->key == NO_KEY)
    
    // debug info
    debugCounters * const counters;
    
    // descriptor reduction algorithm
    #define DESC1_ARRAY records
    #define DESC1_T SCXRecord<K,V>
    #define MUTABLES1_OFFSET_ALLFROZEN 0
    #define MUTABLES1_OFFSET_STATE 1
    #define MUTABLES1_MASK_ALLFROZEN 0x1
    #define MUTABLES1_MASK_STATE 0x6
    #define MUTABLES1_NEW(mutables) \
        ((((mutables)&MASK1_SEQ)+(1<<OFFSET1_SEQ)) \
        | (SCXRecord<K comma1 V>::STATE_INPROGRESS<<MUTABLES1_OFFSET_STATE))
    #define MUTABLES1_INIT_DUMMY SCXRecord<K comma1 V>::STATE_COMMITTED<<MUTABLES1_OFFSET_STATE | MUTABLES1_MASK_ALLFROZEN<<MUTABLES1_OFFSET_ALLFROZEN
    #include "../descriptors/descriptors_impl.h"
    char __padding_desc[PREFETCH_SIZE_BYTES];
    DESC1_T DESC1_ARRAY[LAST_TID1+1] __attribute__ ((aligned(64)));
    
    /**
     * this is what LLX returns when it is performed on a leaf.
     * the important qualities of this value are:
     *      - it is not NULL
     *      - it cannot be equal to any pointer to an scx record
     */
    #define LLX_RETURN_IS_LEAF ((void*) TAGPTR1_DUMMY_DESC(0))
    #define DUMMY_SCXRECORD ((void*) TAGPTR1_STATIC_DESC(0))

    // private function declarations
    
    inline Node<K,V>* allocateNode(const int tid);
    inline Node<K,V>* initializeNode(
                const int,
                Node<K,V> * const,
                const K&,
                const V&,
                Node<K,V> * const,
                Node<K,V> * const);
    inline SCXRecord<K,V>* initializeSCXRecord(
                const int,
                SCXRecord<K,V> * const,
                ReclamationInfo<K,V> * const,
                atomic_uintptr_t * const,
                Node<K,V> * const);
    int rangeQuery_lock(ReclamationInfo<K,V> * const, const int, void **input, void **output);
    int rangeQuery_vlx(ReclamationInfo<K,V> * const, const int, void **input, void **output);
    bool updateInsert_search_llx_scx(ReclamationInfo<K,V> * const, const int, void **input, void **output); // input consists of: const K& key, const V& val, const bool onlyIfAbsent
    bool updateErase_search_llx_scx(ReclamationInfo<K,V> * const, const int, void **input, void **output); // input consists of: const K& key, const V& val, const bool onlyIfAbsent
    void reclaimMemoryAfterSCX(
                const int tid,
                ReclamationInfo<K,V> * info);
    void helpOther(const int tid, tagptr_t tagptr);
    int help(const int tid, tagptr_t tagptr, SCXRecord<K,V> *ptr, bool helpingOther);
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
        Node<K,V> *rootleft = initializeNode(tid, allocateNode(tid), NO_KEY, NO_VALUE, NULL, NULL);
        root = initializeNode(tid, allocateNode(tid), NO_KEY, NO_VALUE, rootleft, NULL);
        cmp = Compare();
        allocatedNodes = new Node<K,V>*[numProcesses*(PREFETCH_SIZE_WORDS+MAX_NODES)];

        DESC1_INIT_ALL(numProcesses);
        SCXRecord<K,V> *dummy = TAGPTR1_UNPACK_PTR(DUMMY_SCXRECORD);
        dummy->mutables = MUTABLES1_INIT_DUMMY;
    }
    /**
     * This function must be called once by each thread that will
     * invoke any functions on this class.
     * 
     * It must be okay that we do this with the main thread and later with another thread!!!
     */
    void initThread(const int tid) {
        recmgr->initThread(tid);
        for (int i=0;i<MAX_NODES;++i) {
            REPLACE_ALLOCATED_NODE(tid, i);
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
    
    void dfsDeallocateBottomUp(Node<K,V> * const u, int *numNodes) {
        if (u == NULL) return;
        if ((Node<K,V>*) u->left.load(memory_order_relaxed) != NULL) {
            dfsDeallocateBottomUp((Node<K,V>*) u->left.load(memory_order_relaxed), numNodes);
            dfsDeallocateBottomUp((Node<K,V>*) u->right.load(memory_order_relaxed), numNodes);
        }
        MEMORY_STATS ++(*numNodes);
        recmgr->deallocate(0 /* tid */, u);
    }
    ~bst() {
        VERBOSE DEBUG COUTATOMIC("destructor bst");
        // free every node and scx record currently in the data structure.
        // an easy DFS, freeing from the leaves up, handles all nodes.
        // cleaning up scx records is a little bit harder if they are in progress or aborted.
        // they have to be collected and freed only once, since they can be pointed to by many nodes.
        // so, we keep them in a set, then free each set element at the end.
        int numNodes = 0;
        dfsDeallocateBottomUp(root, &numNodes);
        VERBOSE DEBUG COUTATOMIC(" deallocated nodes "<<numNodes<<endl);
        for (int tid=0;tid<recmgr->NUM_PROCESSES;++tid) {
            for (int i=0;i<MAX_NODES;++i) {
                recmgr->deallocate(tid, GET_ALLOCATED_NODE_PTR(tid, i));
            }
        }
        delete recmgr;
        delete counters;
    }

    Node<K,V> *getRoot(void) { return root; }
    const V insert(const int tid, const K& key, const V& val);
    const pair<V,bool> erase(const int tid, const K& key);
    const pair<V,bool> find(const int tid, const K& key);
    int rangeQuery(const int tid, const K& low, const K& hi, Node<K,V> const ** result);
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
    
    string tagptrToString(uintptr_t tagptr) {
        stringstream ss;
        if (tagptr) {
            if ((void*) tagptr == DUMMY_SCXRECORD) {
                ss<<"dummy";
            } else {
                SCXRecord<K,V> *ptr;
//                if (TAGPTR1_TEST(tagptr)) {
                    ss<<"<seq="<<UNPACK1_SEQ(tagptr)<<",tid="<<TAGPTR1_UNPACK_TID(tagptr)<<">";
                    ptr = TAGPTR1_UNPACK_PTR(tagptr);
//                }
                
                // print contents of actual scx record
                intptr_t mutables = ptr->mutables;
                ss<<"[";
                ss<<"state="<<MUTABLES1_UNPACK_FIELD(mutables, MUTABLES1_MASK_STATE, MUTABLES1_OFFSET_STATE);
                ss<<" ";
                ss<<"allFrozen="<<MUTABLES1_UNPACK_FIELD(mutables, MUTABLES1_MASK_ALLFROZEN, MUTABLES1_OFFSET_ALLFROZEN);
                ss<<" ";
                ss<<"seq="<<UNPACK1_SEQ(mutables);
                ss<<"]";
            }
        } else {
            ss<<"null";
        }
        return ss.str();
    }

//    friend ostream& operator<<(ostream& os, const SCXRecord<K,V>& obj) {
//        ios::fmtflags f( os.flags() );
////        cout<<"obj.type = "<<obj.type<<endl;
//        intptr_t mutables = obj.mutables;
//        os<<"["//<<"type="<<NAME_OF_TYPE[obj.type]
//          <<" state="<<SCX_READ_STATE(mutables)//obj.state
//          <<" allFrozen="<<SCX_READ_ALLFROZEN(mutables)//obj.allFrozen
////          <<"]";
////          <<" nodes="<<obj.nodes
////          <<" ops="<<obj.ops
//          <<"]" //" subtree="+subtree+"]";
//          <<"@0x"<<hex<<(long)(&obj);
//        os.flags(f);
//        return os;
//    }
    
    void clearCounters() {
        counters->clear();
//        recmgr->clearCounters();
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
    }
};

#endif	/* bst_H */

