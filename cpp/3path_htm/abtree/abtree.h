/* 
 * File:   abtree.h
 * Author: trbot
 *
 * Created on September 27, 2015, 5:25 PM
 *
 * Why is this code so long?
 * - Because this file defines THREE implementations
 *   (1) transactional lock elision (suffix _tle)
 *   (2) hybrid tm based implementation (suffix _tm) -- currently bugged
 *   (3) 3-path implementation (suffixes _fallback, _middle, _fast)
 * - Because the LLX and SCX synchronization primitives are implemented here
 *   (including memory reclamation for SCX records)
 */

#ifndef ABTREE_H
#define	ABTREE_H

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
#ifdef TM
#include "../hybridnorec/hybridnorec/tm.h"
//#include "../hybridnorec/hytm1/tm.h"
#endif

#ifndef IF_ALWAYS_RETRY_WHEN_BIT_SET
#define IF_ALWAYS_RETRY_WHEN_BIT_SET if(0)
#endif

#ifdef NDEBUG
#define TXN_ASSERT(pred) ;
#define NOTXN_ASSERT ;
#else
#define TXN_ASSERT(pred) { if (!(pred)) { if (XTEST()) { XEND(); } cout<<"assertion "<<(#pred)<<" violated at line "<<__LINE__<<endl; exit(-1); } }
#define NOTXN_ASSERT { if (XTEST()) { XEND(); cout<<"transaction running when it should not be at line "<<__LINE__<<endl; exit(-1); } }
#endif

using namespace std;

/**
 * relaxed (a,b)-tree
 * keys in leaves are unsorted
 * keys in internal nodes are sorted
 */

template <typename K>
struct kvpair {
    K key;
    void * val;
    kvpair() {}
};

template <typename K, class Compare>
int kv_compare(const void * _a, const void * _b) {
    const kvpair<K> * a = (const kvpair<K> *) _a;
    const kvpair<K> * b = (const kvpair<K> *) _b;
    static Compare cmp;
    return cmp(a->key, b->key) ? -1
         : cmp(b->key, a->key) ? 1
         : 0;
}
//struct kv_comparator {
//    Compare cmp;
//    kv_comparator(Compare _cmp) : cmp(_cmp) {}
//    bool operator() (const kvpair<K>& a, const kvpair<K>& b) {
//        return cmp(a.key, b.key);
//    }
//};

volatile char padding0[PREFETCH_SIZE_BYTES];
long version[MAX_TID_POW2*PREFETCH_SIZE_WORDS]; // version[i*PREFETCH_SIZE_WORDS] = current version number for thread i
volatile char padding1[PREFETCH_SIZE_BYTES];
#define VERSION_NUMBER(tid) (version[(tid)*PREFETCH_SIZE_WORDS])
#define INIT_VERSION_NUMBER(tid) (VERSION_NUMBER(tid) = ((tid << 1) | 1))
#define NEXT_VERSION_NUMBER(tid) (VERSION_NUMBER(tid) += (MAX_TID_POW2 << 1))
#define IS_VERSION_NUMBER(infoPtr) (((long) (infoPtr)) & 1)

template <int DEGREE, typename K>
struct abtree_Node;

template <int DEGREE, typename K>
struct abtree_SCXRecord;

template <int DEGREE, typename K>
class wrapper_info {
public:
    const static int MAX_NODES = 4;
    abtree_Node<DEGREE,K> * nodes[MAX_NODES];
    abtree_SCXRecord<DEGREE,K> * scxRecordsSeen[MAX_NODES];
    abtree_Node<DEGREE,K> * newNode;
    void * volatile * field;
    int state;
    char numberOfNodes;
    char numberOfNodesToFreeze;
    char numberOfNodesAllocated;
    char path;
    int lastAbort;
    wrapper_info() {
        path = 0;
        state = 0;
        numberOfNodes = 0;
        numberOfNodesToFreeze = 0;
        numberOfNodesAllocated = 0;
        lastAbort = 0;
    }
};

template <int DEGREE, typename K>
struct abtree_SCXRecord {
    const static int STATE_INPROGRESS = 0;
    const static int STATE_COMMITTED = 1;
    const static int STATE_ABORTED = 2;

    volatile char numberOfNodes;
    volatile char numberOfNodesToFreeze;

    volatile int allFrozen;
    volatile int state; // state of the scx
    abtree_Node<DEGREE,K> * volatile newNode;
    void * volatile * field;
    abtree_Node<DEGREE,K> * volatile nodes[wrapper_info<DEGREE,K>::MAX_NODES];                // array of pointers to nodes
    abtree_SCXRecord<DEGREE,K> * volatile scxRecordsSeen[wrapper_info<DEGREE,K>::MAX_NODES];  // array of pointers to scx records
}; //__attribute__((aligned (PREFETCH_SIZE_BYTES)));

template <int DEGREE, typename K>
struct abtree_Node {
    abtree_SCXRecord<DEGREE,K> * volatile scxRecord;
#ifdef TM
    volatile long leaf;
    volatile long marked;
    volatile long tag;
    volatile long size; // number of keys; positive for internal, negative for leaves
#else
    volatile int leaf;
    volatile int marked;
    volatile int tag;
    volatile int size; // number of keys; positive for internal, negative for leaves
#endif
    volatile K keys[DEGREE];
    void * volatile ptrs[DEGREE];
    
#ifdef TM
    __rtm_force_inline long isLeaf_tm(TM_ARGDECL_ALONE) {
        return TM_SHARED_READ_L(leaf);
    }
    __rtm_force_inline int getKeyCount_tm(TM_ARGDECL_ALONE) {
        long sz = TM_SHARED_READ_L(size);
        return isLeaf_tm(TM_ARG_ALONE) ? sz : sz-1;
    }
    __rtm_force_inline long getABDegree_tm(TM_ARGDECL_ALONE) {
        return TM_SHARED_READ_L(size);
    }
    template <class Compare>
    __rtm_force_inline long getChildIndex_tm(TM_ARGDECL_ALONE, const K& key, Compare cmp) {
        long nkeys = getKeyCount_tm(TM_ARG_ALONE);
        long retval = 0;
        while (retval < nkeys) {
            const K temp = TM_SHARED_READ_L(keys[retval]);
            if (cmp(key, temp)) break;
            ++retval;
        }
        return retval;
    }
    template <class Compare>
    __rtm_force_inline abtree_Node<DEGREE,K> * getChild_tm(TM_ARGDECL_ALONE, const K& key, Compare cmp) {
        return (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(ptrs[getChildIndex_tm(TM_ARG_ALONE, key, cmp)]);
    }
    template <class Compare>
    __rtm_force_inline int getKeyIndex_tm(TM_ARGDECL_ALONE, const K& key, Compare cmp) {
        long nkeys = getKeyCount_tm(TM_ARG_ALONE);
        for (int i=0;i<nkeys;++i) {
            const K temp = TM_SHARED_READ_L(keys[i]);
            if (!cmp(key, temp) && !cmp(temp, key)) return i;
        }
        return nkeys;
    }
#endif
    __rtm_force_inline bool isLeaf() {
        return leaf;
    }
    __rtm_force_inline int getKeyCount() {
        TXN_ASSERT(size >= 0);
        TXN_ASSERT(size <= DEGREE);
        TXN_ASSERT(isLeaf() || size > 0);
        return isLeaf() ? size : size-1;
    }
    __rtm_force_inline int getABDegree() {
        return size;
    }
    template <class Compare>
    __rtm_force_inline int getChildIndex(const K& key, Compare cmp) {
        int nkeys = getKeyCount();
        int retval = 0;
        while (retval < nkeys && !cmp(key, (const K&) keys[retval])) {
            TXN_ASSERT(keys[retval] >= 0 && keys[retval] < MAXKEY);
            ++retval;
        }
        return retval;
    }
    template <class Compare>
    __rtm_force_inline abtree_Node<DEGREE,K> * getChild(const K& key, Compare cmp) {
        return (abtree_Node<DEGREE,K> *) ptrs[getChildIndex(key, cmp)];
    }
    template <class Compare>
    __rtm_force_inline int getKeyIndex(const K& key, Compare cmp) {
        int nkeys = getKeyCount();
        for (int i=0;i<nkeys;++i) {
            TXN_ASSERT(keys[i] >= 0 && keys[i] < MAXKEY);
            if (!cmp(key, (const K&) keys[i]) && !cmp((const K&) keys[i], key)) return i;
        }
        return nkeys;
    }
    
    // somewhat slow version that detects cycles in the tree
    void printTreeFile(ostream& os, set<abtree_Node<DEGREE,K> *> *seen) {
        int __state = (IS_VERSION_NUMBER(scxRecord) ? abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED : scxRecord->state);
        //os<<"@"<<(long long)(void *)this;
        os<<"("<<((__state & abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED) ? "" : (__state & abtree_SCXRecord<DEGREE,K>::STATE_ABORTED) ? "A" : "I")
               <<(marked?"m":"")
//               <<getKeyCount()
               <<(tag?"*":"")
               <<(isLeaf() ? "L" : "");
        os<<"[";
//        os<<",[";
        for (int i=0;i<getKeyCount();++i) {
            os<<(i?",":"")<<keys[i];
        }
        os<<"]"; //,["<<marked<<","<<scxRecord->state<<"]";
        if (isLeaf()) {
//            for (int i=0;i<getKeyCount();++i) {
//                os<<","<<(long long) ptrs[i];
//            }
        } else {
            for (int i=0;i<1+getKeyCount();++i) {
                abtree_Node<DEGREE,K> * __node = (abtree_Node<DEGREE,K> *) ptrs[i];
//                if (getKeyCount()) os<<",";
                os<<",";
                if (__node == NULL) {
                    os<<"-";
                } else if (seen->find(__node) != seen->end()) {  // for finding cycles
                    os<<"!"; // cycle!                          // for finding cycles
                } else {
                    seen->insert(__node);
                    __node->printTreeFile(os, seen);
                }                
            }
        }
        os<<")";
    }
    void printTreeFile(ostream& os) {
        set<abtree_Node<DEGREE,K> *> seen;
        printTreeFile(os, &seen);
    }
}; //__attribute__((aligned (PREFETCH_SIZE_BYTES)));

template <int DEGREE, typename K, class Compare, class RecManager>
class abtree {
public:
    RecManager * const recordmgr;
    Compare cmp;
    const int MIN_DEGREE;
private:
    volatile char padding0[PREFETCH_SIZE_BYTES];
    volatile int lock;
    volatile char padding1[PREFETCH_SIZE_BYTES];
    
    // for fallback
    
    abtree_SCXRecord<DEGREE,K> * volatile dummy;
    volatile char padding2[PREFETCH_SIZE_BYTES];
    abtree_Node<DEGREE,K> * volatile root;
    volatile char padding3[PREFETCH_SIZE_BYTES];
    
    __rtm_force_inline abtree_Node<DEGREE,K>* allocateNode(const int tid);
    __rtm_force_inline bool isSentinel(abtree_Node<DEGREE,K> * node);
#ifdef TM    
    __rtm_force_inline bool isSentinel_tm(TM_ARGDECL_ALONE, abtree_Node<DEGREE,K> * node);
#endif
    
    // for LLX and SCX
    
    /**
     * this is what LLX returns when it is performed on a leaf.
     * the important qualities of this value are:
     *      - it is not NULL
     *      - it cannot be equal to any pointer to an scx record
     */
    #define LLX_RETURN_IS_LEAF ((void*) 1)
    __rtm_force_inline abtree_SCXRecord<DEGREE,K>* allocateSCXRecord(const int tid);

    void* llx(const int tid, abtree_Node<DEGREE,K> *node, void **retPointers);
    bool scx(const int tid, wrapper_info<DEGREE,K> * info);
    int help(const int tid, abtree_SCXRecord<DEGREE,K> * scxRecord, bool helpingOther);
    
    __rtm_force_inline void * llx_txn(const int tid, abtree_Node<DEGREE,K> *node, void **retPointers);
    __rtm_force_inline bool scx_txn(const int tid, wrapper_info<DEGREE,K> * info);

    // for DEBRA
    
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
    volatile char padding4[PREFETCH_SIZE_BYTES];
    abtree_SCXRecord<DEGREE,K> **allocatedSCXRecord;
    volatile char padding5[PREFETCH_SIZE_BYTES];
    #define REPLACE_ALLOCATED_SCXRECORD(tid) GET_ALLOCATED_SCXRECORD_PTR(tid) = allocateSCXRecord(tid)
    #define GET_ALLOCATED_SCXRECORD_PTR(tid) allocatedSCXRecord[tid*PREFETCH_SIZE_WORDS]
    
    // similarly, allocatedNodes[tid*PREFETCH_SIZE_WORDS+i] = an allocated node
    //     for i = 0..MAX_NODES-2
    abtree_Node<DEGREE,K> **allocatedNodes;
    volatile char padding6[PREFETCH_SIZE_BYTES];
    #define REPLACE_ALLOCATED_NODE(tid, i) GET_ALLOCATED_NODE_PTR(tid, i) = allocateNode(tid)
    #define GET_ALLOCATED_NODE_PTR(tid, i) allocatedNodes[tid*(PREFETCH_SIZE_WORDS+wrapper_info<DEGREE,K>::MAX_NODES)+i]

    void replaceUsedObjects(const int, const bool, const int, const int);
    
    /* 2-bit state | 5-bit highest index reached | 24-bit frozen flags for each element of nodes[] on which a freezing CAS was performed = total 31 bits (highest bit unused) */
    #define ABORT_STATE_INIT(i, flags) (abtree_SCXRecord<DEGREE,K>::STATE_ABORTED | ((i)<<2) | ((flags)<<7))
    #define STATE_GET_FLAGS(state) ((state) & 0x7FFFFF80)
    #define STATE_GET_HIGHEST_INDEX_REACHED(state) (((state) & 0x7C)>>2)
    #define STATE_GET_WITH_FLAG_OFF(state, i) ((state) & ~(1<<(i+7)))
    inline bool tryRetireSCXRecord(const int tid, abtree_SCXRecord<DEGREE,K> * const scx, abtree_Node<DEGREE,K> * const node);
    void reclaimMemoryAfterSCX(const int tid, wrapper_info<DEGREE,K> * const info, bool usedVersionNumber);

    // for 3path
    
    volatile int numFallback; // number of processes on the fallback path
    volatile char padding7[PREFETCH_SIZE_BYTES];

    __rtm_force_inline abtree_SCXRecord<DEGREE,K> * createSCXRecord(const int tid, void * volatile * const field, abtree_Node<DEGREE,K> * const newNode, abtree_Node<DEGREE,K> ** const nodes, abtree_SCXRecord<DEGREE,K> ** const scxRecordsSeen, const int numberOfNodes, const int numberOfNodesToFreeze);
    
    __rtm_force_inline void join_fallback() {
        __sync_fetch_and_add(&numFallback, 1);
    }
    __rtm_force_inline void leave_fallback() {
        __sync_fetch_and_add(&numFallback, -1);
    }

    #define abtree_LLX_FNPTR(name) void* (abtree<DEGREE,K,Compare,RecManager>::*name)(const int, abtree_Node<DEGREE,K> *, void **)
    #define abtree_SCX_FNPTR(name) bool (abtree::*name)(const int, abtree_SCXRecord<DEGREE,K> *)

    #define abtree_UPDATE_FUNCTION(name) bool (abtree::*name)(wrapper_info<DEGREE,K> * const, const int, void **, void **)
    #define abtree_CAST_UPDATE_FUNCTION(name) (abtree_UPDATE_FUNCTION()) &abtree::name
//    #define abtree_CAST_UPDATE_FUNCTION(name) name
    __rtm_force_inline void initializePath(wrapper_info<DEGREE,K> * const info);
    void htmWrapper(
            abtree_UPDATE_FUNCTION(),
            abtree_UPDATE_FUNCTION(),
            abtree_UPDATE_FUNCTION(),
            const int tid,
            void **input,
            void **output);

    bool rebalance_tle(const int, const K&);
    bool rootJoinParent_tle(const int tid, abtree_Node<DEGREE,K> * const p, abtree_Node<DEGREE,K> * const l, const int lindex, TLEScope * const scope);
    bool tagJoinParent_tle(const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, TLEScope * const scope);
    bool tagSplit_tle(const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, TLEScope * const scope);
    bool joinSibling_tle(const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex, TLEScope * const scope);
    bool redistributeSibling_tle(const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex, TLEScope * const scope);
    
#ifdef TM
    bool rebalance_tm(TM_ARGDECL_ALONE, const int, const K&);
    bool rootJoinParent_tm(TM_ARGDECL_ALONE, const int tid, abtree_Node<DEGREE,K> * const p, abtree_Node<DEGREE,K> * const l, const int lindex);
    bool tagJoinParent_tm(TM_ARGDECL_ALONE, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex);
    bool tagSplit_tm(TM_ARGDECL_ALONE, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex);
    bool joinSibling_tm(TM_ARGDECL_ALONE, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex);
    bool redistributeSibling_tm(TM_ARGDECL_ALONE, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex);
#endif
    
    bool erase_fallback(wrapper_info<DEGREE,K> * const, const int, const K&, bool * const, void ** const result);
    bool insert_fallback(wrapper_info<DEGREE,K> * const, const int, const K&, void * const, const bool, bool * const, void ** const result);
    bool rangeQuery_fallback(wrapper_info<DEGREE,K> * const, const int tid, const K& low, const K& hi, abtree_Node<DEGREE,K> const ** result, int * const cnt);
    bool rebalance_fallback(wrapper_info<DEGREE,K> * const, const int, const K&, bool * const);

    bool erase_middle(wrapper_info<DEGREE,K> * const, const int, const K&, bool * const, void ** const result);
    bool insert_middle(wrapper_info<DEGREE,K> * const, const int, const K&, void * const, const bool, bool * const, void ** const result);
    bool rebalance_middle(wrapper_info<DEGREE,K> * const, const int, const K&, bool * const);
    
    bool erase_fast(wrapper_info<DEGREE,K> * const, const int, const K&, bool * const, void ** const result);
    bool insert_fast(wrapper_info<DEGREE,K> * const, const int, const K&, void * const, const bool, bool * const, void ** const result);
    bool rangeQuery_fast(wrapper_info<DEGREE,K> * const, const int tid, const K& low, const K& hi, abtree_Node<DEGREE,K> const ** result, int * const cnt);
    bool rebalance_fast(wrapper_info<DEGREE,K> * const, const int, const K&, bool * const);

    template <bool in_txn> bool rootJoinParent_fallback(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const p, abtree_Node<DEGREE,K> * const l, const int lindex);
    template <bool in_txn> __rtm_force_inline bool tagJoinParent_fallback(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex);
    template <bool in_txn> bool tagSplit_fallback(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex);
    template <bool in_txn> bool joinSibling_fallback(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex);
    template <bool in_txn> __rtm_force_inline bool redistributeSibling_fallback(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex);

    bool rootJoinParent_fast(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const p, abtree_Node<DEGREE,K> * const l, const int lindex);
    bool tagJoinParent_fast(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex);
    bool tagSplit_fast(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex);
    bool joinSibling_fast(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex);
    bool redistributeSibling_fast(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex);

    long long debugKeySum(abtree_Node<DEGREE,K> * node);
    bool validate(abtree_Node<DEGREE,K> * const node, const int currdepth, const int leafdepth);
public:
    long long pagesize;
    void * NO_VALUE;
    const int NUM_PROCESSES;
    const K& NO_KEY;
    volatile char padding8[PREFETCH_SIZE_BYTES];
    debugCounters * const counters; // debug info
    volatile char padding9[PREFETCH_SIZE_BYTES];

    abtree(const int numProcesses,
            int suspectedCrashSignal,
            const K& _NO_KEY,
            const int _MIN_DEGREE)
    : NUM_PROCESSES(numProcesses)
    , NO_KEY(_NO_KEY)
    , NO_VALUE((void *) -1LL)
    , counters(new debugCounters(numProcesses))
    , recordmgr(new RecManager(numProcesses, suspectedCrashSignal))
    , MIN_DEGREE(_MIN_DEGREE)
    {
        pagesize = sysconf(_SC_PAGESIZE);
        cout<<"NO_VALUE="<<(long long)NO_VALUE<<endl;
        if (_MIN_DEGREE > DEGREE/2) {
            cout<<"ERROR: MIN DEGREE must be <= DEGREE/2."<<endl;
            exit(-1);
        }
        allocatedNodes = new abtree_Node<DEGREE,K>*[numProcesses*(PREFETCH_SIZE_WORDS+wrapper_info<DEGREE,K>::MAX_NODES)];
        allocatedSCXRecord = new abtree_SCXRecord<DEGREE,K>*[numProcesses*PREFETCH_SIZE_WORDS];
        for (int i=0;i<numProcesses*PREFETCH_SIZE_WORDS;++i) {
            allocatedSCXRecord[i] = NULL;
        }
        
        const int tid = 0;
        initThread(tid);
        dummy = allocateSCXRecord(tid);
        dummy->state = abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED;
        
        abtree_Node<DEGREE,K> *rootleft = allocateNode(tid);
        rootleft->scxRecord = dummy;
        rootleft->marked = false;
        rootleft->tag = false;
        rootleft->size = 0;
        rootleft->leaf = true;
        
        root = allocateNode(tid);
        root->ptrs[0] = rootleft;
        root->scxRecord = dummy;
        root->marked = false;
        root->tag = false;
        root->size = 1;
        root->leaf = false;
        
        numFallback = 0;
        lock = 0;
    }

    const void * insert_tle(const int, const K&, void * const);
    const pair<void *,bool> erase_tle(const int, const K&);
    const pair<void *,bool> find_tle(const int tid, const K& key);
    int rangeQuery_tle(const int tid, const K& low, const K& hi, abtree_Node<DEGREE,K> const ** result);
#ifdef TM
    const void * insert_tm(TM_ARGDECL_ALONE, const int, const K&, void * const);
    const pair<void *,bool> erase_tm(TM_ARGDECL_ALONE, const int, const K&);
    const pair<void *,bool> find_tm(TM_ARGDECL_ALONE, const int tid, const K& key);
    int rangeQuery_tm(TM_ARGDECL_ALONE, const int tid, const K& low, const K& hi, abtree_Node<DEGREE,K> const ** result);
#endif

    const void* insert(const int tid, const K& key, void * const val);
    bool insertIfAbsent(const int tid, const K& key, void * val); /*{
        cerr<<"ERROR: insertIfAbsent not implemented for this data structure"<<endl;
        exit(-1);
        return false;
    }*/
    const pair<void*,bool> erase(const int tid, const K& key);
    const pair<void*,bool> find(const int tid, const K& key);
    int rangeQuery(const int tid, const K& low, const K& hi, abtree_Node<DEGREE,K> const ** result);
    int rangeQuery_runOnFallback(const int tid, const K& low, const K& hi, abtree_Node<DEGREE,K> const ** result) {
        cerr<<"ERROR: rangeQuery_runOnFallback not implemented for this data structure"<<endl;
        exit(-1);
        return false;
    }
    bool validate(const long long keysum, const bool checkkeysum);
    
    long long debugKeySum() {
        return debugKeySum((abtree_Node<DEGREE,K> *) root->ptrs[0]);
    }
    debugCounters * const debugGetCounters() {
        return counters;
    }
    void debugPrint() {
        ((abtree_Node<DEGREE,K> *) root/*->ptrs[0]*/)->printTreeFile(cout);
    }
    void debugPrintToFile(string prefix, long id1, string infix, long id2, string suffix) {
        stringstream ss;
        ss<<prefix<<id1<<infix<<id2<<suffix;
        COUTATOMIC("print to filename \""<<ss.str()<<"\""<<endl);
        fstream fs (ss.str().c_str(), fstream::out);
        root->printTreeFile(fs);
        fs.close();
    }
    void clearCounters() {
        counters->clear();
    }
    /**
     * This function must be called once by each thread that will
     * invoke any functions on this class.
     * 
     * It must be okay that we do this with the main thread and later with another thread!!!
     */
    void initThread(const int tid) {
        INIT_VERSION_NUMBER(tid);
        recordmgr->initThread(tid);
        if (GET_ALLOCATED_SCXRECORD_PTR(tid) == NULL) {
            REPLACE_ALLOCATED_SCXRECORD(tid);
            for (int i=0;i<wrapper_info<DEGREE,K>::MAX_NODES;++i) {
                REPLACE_ALLOCATED_NODE(tid, i);
            }
        }
    }
    void deinitThread(const int tid) {
    }
};

#endif	/* ABTREE_H */

