/**
 * Code for HyTM is loosely based on the code for TL2
 * (in particular, the data structures)
 * 
 * This is an implementation of Hybrid noREC with the optimization suggested in
 * the work on non-speculative operations in ASF.
 * 
 * [ note: we cannot distribute this without inserting the appropriate
 *         copyright notices as required by TL2 and STAMP ]
 * 
 * Authors: Trevor Brown (tabrown@cs.toronto.edu) and Srivatsan Ravi
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include "hybridnorec.h"
#include "../hytm1/platform_impl.h"
#include "stm.h"
#include "tmalloc.h"
#include "util.h"
#include "../murmurhash/MurmurHash3_impl.h"
#include <iostream>
#include <execinfo.h>
#include <stdint.h>
using namespace std;

#include "../hytm1/counters/debugcounters_cpp.h"
struct c_debugCounters *c_counters;

#define debug(x) (#x)<<"="<<x

//#define PREFETCH_SIZE_BYTES 192
#define USE_FULL_HASHTABLE
//#define USE_BLOOM_FILTER

// just for debugging
volatile int globallock = 0;

hybridnorec_globals_t g = {0,};

void printStackTrace() {

  void *trace[16];
  char **messages = (char **)NULL;
  int i, trace_size = 0;

  trace_size = backtrace(trace, 16);
  messages = backtrace_symbols(trace, trace_size);
  /* skip first stack frame (points here) */
  printf("  [bt] Execution path:\n");
  for (i=1; i<trace_size; ++i)
  {
    printf("    [bt] #%d %s\n", i, messages[i]);

    /**
     * find first occurrence of '(' or ' ' in message[i] and assume
     * everything before that is the file name.
     */
    int p = 0; //size_t p = 0;
    while(messages[i][p] != '(' && messages[i][p] != ' '
            && messages[i][p] != 0)
        ++p;
    
    char syscom[256];
    sprintf(syscom,"echo \"    `addr2line %p -e %.*s`\"", trace[i], p, messages[i]);
        //last parameter is the file name of the symbol
    if (system(syscom) < 0) {
        printf("ERROR: could not run necessary command to build stack trace\n");
        exit(-1);
    };
  }

  exit(-1);
}

void initSighandler() {
    /* Install our signal handler */
    struct sigaction sa;

    sa.sa_handler = (sighandler_t) /*(void *)*/ printStackTrace;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
}

void hytm_acquireLock(volatile int *lock) {
    while (1) {
        if (*lock) {
            PAUSE();
            continue;
        }
        if (__sync_bool_compare_and_swap(lock, 0, 1)) {
            return;
        }
    }
}

void hytm_releaseLock(volatile int *lock) {
    *lock = 0;
}













/**
 * 
 * TRY-LOCK IMPLEMENTATION AND LOCK TABLE
 * 
 */

class Thread;

//#define LOCKBIT 1
//class vLockSnapshot {
//public:
////private:
//    uint64_t lockstate;
//public:
//    __INLINE__ vLockSnapshot() {}
//    __INLINE__ vLockSnapshot(uint64_t _lockstate) {
//        lockstate = _lockstate;
//    }
//    __INLINE__ bool isLocked() const {
//        return lockstate & LOCKBIT;
//    }
//    __INLINE__ uint64_t version() const {
////        cout<<"LOCKSTATE="<<lockstate<<" ~LOCKBIT="<<(~LOCKBIT)<<" VERSION="<<(lockstate & (~LOCKBIT))<<endl;
//        return lockstate & (~LOCKBIT);
//    }
//    friend std::ostream& operator<<(std::ostream &out, const vLockSnapshot &obj);
//};
//
//class vLock {
//    union {
//        struct {
//            volatile uint64_t lock; // (Version,LOCKBIT)
//            volatile void* volatile owner; // invariant: NULL when lock is not held; non-NULL (and points to thread that owns lock) only when lock is held (but sometimes may be NULL when lock is held). guarantees that a thread can tell if IT holds the lock (but cannot necessarily tell who else holds the lock).
//        };
////        char bytes[PREFETCH_SIZE_BYTES];
//    };
//private:
//    __INLINE__ vLock(uint64_t lockstate) {
//        lock = lockstate;
//        owner = 0;
//    }
//public:
//    __INLINE__ vLock() {
//        lock = 0;
//        owner = NULL;
//    }
//    __INLINE__ vLockSnapshot getSnapshot() const {
//        vLockSnapshot retval (lock);
////        __sync_synchronize();
//        return retval;
//    }
////    __INLINE__ bool tryAcquire(void* thread) {
////        if (thread == owner) return true; // reentrant acquire
////        uint64_t val = lock & (~LOCKBIT);
////        bool retval = __sync_bool_compare_and_swap(&lock, val, val+1);
////        if (retval) {
////            owner = thread;
////        }
////        return retval;
////    }
////    __INLINE__ bool tryAcquire(void* thread, vLockSnapshot& oldval) {
////        if (thread == owner) return true; // reentrant acquire
////        bool retval = __sync_bool_compare_and_swap(&lock, oldval.version(), oldval.version()+1);
////        if (retval) {
////            owner = thread;
////        }
////        return retval;
////    }
//    __INLINE__ void release(void* thread) {
//        if (thread == owner) { // reentrant release (assuming the lock should be released on the innermost release() call)
//            owner = NULL;
//            SOFTWARE_BARRIER;
//            ++lock;
//        }
//    }
//
//    __INLINE__ bool glockedAcquire(void* thread, vLockSnapshot& oldval) {
////        if (thread == owner) return true; // reentrant acquire
//        if (oldval.version() == lock) {
//            ++lock;
//            SOFTWARE_BARRIER;
//            owner = thread;
//            return true;
//        }
//        return false;
//    }
//
//    // can be invoked only by a hardware transaction
//    __INLINE__ void htmIncrementVersion() {
//        lock += 2;
//    }
//
//    __INLINE__ bool isOwnedBy(void* thread) {
//        return (thread == owner);
//    }
//    
//    friend std::ostream& operator<<(std::ostream &out, const vLock &obj);
//};

#include <map>
map<const void*, unsigned> addrToIx;
map<unsigned, const void*> ixToAddr;
volatile unsigned rename_ix = 0;
#include <sstream>
string stringifyIndex(unsigned ix) {
#if 1
    const unsigned NCHARS = 36;
    stringstream ss;
    if (ix == 0) return "0";
    while (ix > 0) {
        unsigned newchar = ix % NCHARS;
        if (newchar < 10) {
            ss<<(char)(newchar+'0');
        } else {
            ss<<(char)((newchar-10)+'A');
        }
        ix /= NCHARS;
    }
    string backwards = ss.str();
    stringstream ssr;
    for (string::reverse_iterator rit = backwards.rbegin(); rit != backwards.rend(); ++rit) {
        ssr<<*rit;
    }
    return ssr.str();
#elif 0
    const unsigned NCHARS = 26;
    stringstream ss;
    if (ix == 0) return "0";
    while (ix > 0) {
        unsigned newchar = ix % NCHARS;
        ss<<(char)(newchar+'A');
        ix /= NCHARS;
    }
    return ss.str();
#else 
    stringstream ss;
    ss<<ix;
    return ss.str();
#endif
}
string renamePointer(const void* p) {
    map<const void*, unsigned>::iterator it = addrToIx.find(p);
    if (it == addrToIx.end()) {
        unsigned newix = __sync_fetch_and_add(&rename_ix, 1);
        addrToIx[p] = newix;
        ixToAddr[newix] = p;
        return stringifyIndex(addrToIx[p]);
    } else {
        return stringifyIndex(it->second);
    }
}

//std::ostream& operator<<(std::ostream& out, const vLockSnapshot& obj) {
//    return out<<"ver="<<obj.version()
//                <<",locked="<<obj.isLocked()
//                ;//<<"> (raw="<<obj.lockstate<<")";
//}
//
//std::ostream& operator<<(std::ostream& out, const vLock& obj) {
//    return out<<"<"<<obj.getSnapshot()<<",owner="<<(obj.owner?((Thread_void*) obj.owner)->UniqID:-1)<<">@"<<renamePointer(&obj);
//}

///*
// * Consider 4M alignment for LockTab so we can use large-page support.
// * Alternately, we could mmap() the region with anonymous DZF pages.
// */
//#define _TABSZ  (1<<20)
//static vLock LockTab[_TABSZ];
//
///*
// * With PS the versioned lock words (the LockTab array) are table stable and
// * references will never fault.  Under PO, however, fetches by a doomed
// * zombie txn can fault if the referent is free()ed and unmapped
// */
//#if 0
//#define LDLOCK(a)                     LDNF(a)  /* for PO */
//#else
//#define LDLOCK(a)                     *(a)     /* for PS */
//#endif
//
///*
// * PSLOCK: maps variable address to lock address.
// * For PW the mapping is simply (UNS(addr)+sizeof(int))
// * COLOR attempts to place the lock(metadata) and the data on
// * different D$ indexes.
// */
//#define TABMSK (_TABSZ-1)
//#define COLOR (128)
//
///*
// * ILP32 vs LP64.  PSSHIFT == Log2(sizeof(intptr_t)).
// */
//#define PSSHIFT ((sizeof(void*) == 4) ? 2 : 3)
//#define PSLOCK(a) (LockTab + (((UNS(a)+COLOR) >> PSSHIFT) & TABMSK)) /* PS1M */










/**
 * 
 * THREAD CLASS
 *
 */

class List;
//class TypeLogs;

class Thread {
public:
    long UniqID;
    volatile long Retries;
    int IsRO;
    int isFallback;
    long AbortsHW; // # of times hw txns aborted
    long AbortsSW; // # of times sw txns aborted
    long CommitsHW;
    long CommitsSW;
    unsigned long long rng;
    unsigned long long xorrng [1];
    tmalloc_t* allocPtr;    /* CCM: speculatively allocated */
    tmalloc_t* freePtr;     /* CCM: speculatively free'd */
    List* rdSet;
    List* wrSet;
    sigjmp_buf* envPtr;
    int sequenceLock;
    
    Thread(long id);
    void destroy();
    void compileTimeAsserts() {
        CTASSERT(sizeof(*this) == sizeof(Thread_void));
    }
};// __attribute__((aligned(CACHE_LINE_SIZE)));











/**
 * 
 * LOG IMPLEMENTATION
 * 
 */

/* list element (serves as an entry in a read/write set) */
class AVPair {
public:
    AVPair* Next;
    AVPair* Prev;
    volatile intptr_t* addr;
    intptr_t value;
//    union {
//        long l;
//#ifdef __LP64__
//        float f[2];
//#else
//        float f[1];
//#endif
//        intptr_t p;
//    } value;
//    vLock* LockFor;     /* points to the vLock covering addr */
//    vLockSnapshot rdv;  /* read-version @ time of 1st read - observed */
    long Ordinal;
    AVPair** hashTableEntry;
//    int32_t hashTableIx;
    
    AVPair() {}
    AVPair(AVPair* _Next, AVPair* _Prev, long _Ordinal)
        : Next(_Next), Prev(_Prev), addr(0), value(0), Ordinal(_Ordinal), hashTableEntry(0)
    {}
    
    void validateInvariants() {
        
    }
};

std::ostream& operator<<(std::ostream& out, const AVPair& obj) {
    return out<<"[addr="<<renamePointer((void*) obj.addr)
            //<<" val="<<obj.value.l
            //<<" lock@"<<renamePointer(obj.LockFor)
            //<<" prev="<<obj.Prev
            //<<" next="<<obj.Next
            //<<" ord="<<obj.Ordinal
            //<<" rdv="<<obj.rdv<<"@"<<(uintptr_t)(long*)&obj
            <<"]@"<<renamePointer(&obj);
}

//template <typename T>
//inline void assignValue(AVPair* e, T value);
//template <>
//inline void assignValue<long>(AVPair* e, long value) {
//    e->value.l = value;
//}
//template <>
//inline void assignValue<float>(AVPair* e, float value) {
//    e->value.f[0] = value;
//}
//
//template <typename T>
//inline void replayStore(AVPair* e);
//template <>
//inline void replayStore<long>(AVPair* e) {
//    *((long*)e->addr) = e->value.l;
//}
//template <>
//inline void replayStore<float>(AVPair* e) {
//    *((float*)e->addr) = e->value.f[0];
//}
//
//template <typename T>
//inline T unpackValue(AVPair* e);
//template <>
//inline long unpackValue<long>(AVPair* e) {
//    return e->value.l;
//}
//template <>
//inline float unpackValue<float>(AVPair* e) {
//    return e->value.f[0];
//}
//
//class Log;
//
//template <typename T>
//inline Log * getTypedLog(TypeLogs * typelogs);

#define HASHTABLE_CLEAR_FROM_LIST

enum hytm_config {
    INIT_WRSET_NUM_ENTRY = 1024,
    INIT_RDSET_NUM_ENTRY = 8192,
    INIT_LOCAL_NUM_ENTRY = 1024,
};

#ifdef USE_FULL_HASHTABLE
    class HashTable {
    public:
        AVPair** data;
        long sz;        // number of elements in the hash table
        long cap;       // capacity of the hash table
    private:
        void validateInvariants() {
            // hash table capacity is a power of 2
            long htc = cap;
            while (htc > 0) {
                if ((htc & 1) && (htc != 1)) {
                    ERROR(debug(cap)<<" is not a power of 2");
                }
                htc /= 2;
            }
            // htabcap >= 2*htabsz
            if (requiresExpansion()) {
                ERROR("hash table capacity too small: "<<debug(cap)<<" "<<debug(sz));
            }
        #ifdef LONG_VALIDATION
            // htabsz = size of hash table
            long _htabsz = 0;
            for (int i=0;i<cap;++i) {
                if (data[i]) {
                    ++_htabsz; // # non-null entries of htab
                }
            }
            if (sz != _htabsz) {
                ERROR("hash table size incorrect: "<<debug(sz)<<" "<<debug(_htabsz));
            }
        #endif
        }

    public:
        __INLINE__ void init(const long _sz) {
            // assert: _sz is a power of 2!
            HYTM_DEBUG3 aout("hash table "<<renamePointer(this)<<" init");
            sz = 0;
            cap = 2 * _sz;
            data = (AVPair**) malloc(sizeof(AVPair*) * cap);
            memset(data, 0, sizeof(AVPair*) * cap);
            VALIDATE_INV(this);
        }

        __INLINE__ void destroy() {
            HYTM_DEBUG3 aout("hash table "<<renamePointer(this)<<" destroy");
            free(data);
        }

        __INLINE__ int32_t hash(volatile intptr_t* addr) {
            intptr_t p = (intptr_t) addr;
            // assert: htabcap is a power of 2
    #ifdef __LP64__
            p ^= p >> 33;
            p *= BIG_CONSTANT(0xff51afd7ed558ccd);
            p ^= p >> 33;
            p *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
            p ^= p >> 33;
    #else
            p ^= p >> 16;
            p *= 0x85ebca6b;
            p ^= p >> 13;
            p *= 0xc2b2ae35;
            p ^= p >> 16;
    #endif
            assert(0 <= (p & (cap-1)) && (p & (cap-1)) < INT32_MAX);
            return p & (cap-1);
        }

        __INLINE__ int32_t findIx(volatile intptr_t* addr) {
            int32_t ix = hash(addr);
            while (data[ix]) {
                if (data[ix]->addr == addr) {
                    return ix;
                }
                ix = (ix + 1) & (cap-1);
            }
            return -1;
        }

        __INLINE__ AVPair* find(volatile intptr_t* addr) {
            int32_t ix = findIx(addr);
            if (ix < 0) return NULL;
            return data[ix];
        }

        // assumes there is space for e, and e is not in the hash table
        __INLINE__ void insertFresh(AVPair* e) {
            HYTM_DEBUG3 aout("hash table "<<renamePointer(this)<<" insertFresh("<<debug(e)<<")");
            VALIDATE_INV(this);
            int32_t ix = hash(e->addr);
            while (data[ix]) { // assumes hash table does NOT contain e
                ix = (ix + 1) & (cap-1);
            }
            data[ix] = e;
#ifdef HASHTABLE_CLEAR_FROM_LIST
            e->hashTableEntry = &data[ix];
//            e->hashTableIx = ix;
#endif
            ++sz;
            VALIDATE_INV(this);
        }
        
        __INLINE__ int requiresExpansion() {
            return 2*sz > cap;
        }
        
    private:
        // expand table by a factor of 2
        __INLINE__ void expandAndClear() {
            HYTM_DEBUG2 aout("hash table "<<renamePointer(this)<<" expanding from size "<<sz<<" and cap "<<cap);
            AVPair** olddata = data;
            init(cap); // note: cap will be doubled by init
            free(olddata);
            HYTM_DEBUG2 aout("hash table "<<renamePointer(this)<<" new size "<<sz<<" and cap "<<cap);
        }

    public:
        __INLINE__ void expandAndRehashFromList(AVPair* head, AVPair* stop) {
            HYTM_DEBUG2 aout("hash table "<<renamePointer(this)<<" pre expandAndRehashFromList sz="<<sz<<" and cap="<<cap);
            VALIDATE_INV(this);
            expandAndClear();
            for (AVPair* e = head; e != stop; e = e->Next) {
                insertFresh(e);
            }
            VALIDATE_INV(this);
            HYTM_DEBUG2 aout("hash table "<<renamePointer(this)<<" post expandAndRehashFromList sz="<<sz<<" and cap="<<cap);
        }
        
        __INLINE__ void clear(AVPair* head, AVPair* stop) {
#ifdef HASHTABLE_CLEAR_FROM_LIST
            for (AVPair* e = head; e != stop; e = e->Next) {
                //assert(*e->hashTableEntry);
                //assert(*e->hashTableEntry == e);
                *e->hashTableEntry = NULL;
//                assert(e->hashTableIx >= 0 && e->hashTableIx < cap);
//                assert(data[e->hashTableIx] == e);
//                data[e->hashTableIx] = 0;
            }
//            for (int i=0;i<cap;++i) {
//                assert(data[i] == 0);
//            }
#else
            memset(data, 0, sizeof(AVPair*) * cap);
#endif
        }

        void validateContainsAllAndSameSize(AVPair* head, AVPair* stop, const int listsz) {
            // each element of list appears in hash table
            for (AVPair* e = head; e != stop; e = e->Next) {
                if (find(e->addr) != e) {
                    ERROR("element "<<debug(*e)<<" of list was not in hash table");
                }
            }
            if (listsz != sz) {
                ERROR("list and hash table sizes differ: "<<debug(listsz)<<" "<<debug(sz));
            }
        }
    };
#elif defined(USE_BLOOM_FILTER)
    typedef unsigned long bloom_filter_data_t;
    #define BLOOM_FILTER_DATA_T_BITS (sizeof(bloom_filter_data_t)*8)
    #define BLOOM_FILTER_BITS 512
    #define BLOOM_FILTER_WORDS (BLOOM_FILTER_BITS/sizeof(bloom_filter_data_t))
    class HashTable {
    public:
        bloom_filter_data_t filter[BLOOM_FILTER_WORDS]; // bloom filter data
    private:
        void validateInvariants() {

        }

    public:
        __INLINE__ void init() {
            for (unsigned i=0;i<BLOOM_FILTER_WORDS;++i) {
                filter[i] = 0;
            }
            VALIDATE_INV(this);
        }

        __INLINE__ void destroy() {
            HYTM_DEBUG3 aout("hash table "<<this<<" destroy");
        }

        __INLINE__ unsigned hash(volatile intptr_t* key) {
            intptr_t p = (intptr_t) key;
    #ifdef __LP64__
            p ^= p >> 33;
            p *= BIG_CONSTANT(0xff51afd7ed558ccd);
            p ^= p >> 33;
            p *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
            p ^= p >> 33;
    #else
            p ^= p >> 16;
            p *= 0x85ebca6b;
            p ^= p >> 13;
            p *= 0xc2b2ae35;
            p ^= p >> 16;
    #endif
            return p & (BLOOM_FILTER_BITS-1);
        }

        __INLINE__ bool contains(volatile intptr_t* key) {
            unsigned targetBit = hash(key);
            bloom_filter_data_t fword = filter[targetBit / BLOOM_FILTER_DATA_T_BITS];
            return fword & (1<<(targetBit & (BLOOM_FILTER_DATA_T_BITS-1))); // note: using x&(sz-1) where sz is a power of 2 as a shortcut for x%sz
        }

        // assumes there is space for e, and e is not in the hash table
        __INLINE__ void insertFresh(volatile intptr_t* key) {
            HYTM_DEBUG3 aout("hash table "<<this<<" insertFresh("<<debug(key)<<")");
            VALIDATE_INV(this);
            unsigned targetBit = hash(key);
            unsigned wordix = targetBit / BLOOM_FILTER_DATA_T_BITS;
            assert(wordix >= 0 && wordix < BLOOM_FILTER_WORDS);
            filter[wordix] |= (1<<(targetBit & (BLOOM_FILTER_DATA_T_BITS-1))); // note: using x&(sz-1) where sz is a power of 2 as a shortcut for x%sz
            VALIDATE_INV(this);
        }
    };
#else
    class HashTable {};
#endif

class List {
public:
    // linked list (for iteration)
    AVPair* head;
    AVPair* put;    /* Insert position - cursor */
    AVPair* tail;   /* CCM: Pointer to last valid entry */
    AVPair* end;    /* CCM: Pointer to last entry */
    long ovf;       /* Overflow - request to grow */
    long initcap;
    long currsz;
    
    HashTable tab;
    
private:
    __INLINE__ AVPair* extendList() {
        VALIDATE_INV(this);
        // Append at the tail. We want the front of the list,
        // which sees the most traffic, to remains contiguous.
        ovf++;
        AVPair* e = (AVPair*) malloc(sizeof(*e));
        assert(e);
        tail->Next = e;
        *e = AVPair(NULL, tail, tail->Ordinal+1);
        end = e;
        VALIDATE_INV(this);
        return e;
    }
    
    void validateInvariants() {
        // currsz == size of list
        long _currsz = 0;
        AVPair* stop = put;
        for (AVPair* e = head; e != stop; e = e->Next) {
            VALIDATE_INV(e);
            ++_currsz;
        }
        if (currsz != _currsz) {
            ERROR("list size incorrect: "<<debug(currsz)<<" "<<debug(_currsz));
        }

        // capacity is correct and next fields are not too far
        long _currcap = 0;
        for (AVPair* e = head; e; e = e->Next) {
            VALIDATE_INV(e);
            if (e->Next > head+initcap && ovf == 0) {
                ERROR("list has AVPair with a next field that jumps off the end of the AVPair array, but ovf is 0; "<<debug(*e));
            }
            if (e->Next && e->Next != e+1) {
                ERROR("list has incorrect distance between AVPairs; "<<debug(e)<<" "<<debug(e->Next));
            }
            ++_currcap;
        }
        if (_currcap != initcap) {
            ERROR("list capacity incorrect: "<<debug(initcap)<<" "<<debug(_currcap));
        }
    }
public:
    void init(Thread* Self, long _initcap) {
        HYTM_DEBUG3 aout("list "<<renamePointer(this)<<" init");
        // assert: _sz is a power of 2

        // Allocate the primary list as a large chunk so we can guarantee ascending &
        // adjacent addresses through the list. This improves D$ and DTLB behavior.
        head = (AVPair*) malloc((sizeof (AVPair) * _initcap) + CACHE_LINE_SIZE);
        assert(head);
        memset(head, 0, sizeof(AVPair) * _initcap);
        AVPair* curr = head;
        put = head;
        end = NULL;
        tail = NULL;
        for (int i = 0; i < _initcap; i++) {
            AVPair* e = curr++;
            *e = AVPair(curr, tail, i); // note: curr is invalid in the last iteration
            tail = e;
        }
        tail->Next = NULL; // fix invalid next pointer from last iteration
        initcap = _initcap;
        ovf = 0;
        currsz = 0;
        VALIDATE_INV(this);
#ifdef USE_FULL_HASHTABLE
        tab.init(_initcap);
#elif defined(USE_BLOOM_FILTER)
        tab.init();
#endif
    }
    
    void destroy() {
        HYTM_DEBUG3 aout("list "<<renamePointer(this)<<" destroy");
        /* Free appended overflow entries first */
        AVPair* e = end;
        if (e != NULL) {
            while (e->Ordinal >= initcap) {
                AVPair* tmp = e;
                e = e->Prev;
                free(tmp);
            }
        }
        /* Free contiguous beginning */
        free(head);
#if defined(USE_FULL_HASHTABLE) || defined(USE_BLOOM_FILTER)
        tab.destroy();
#endif
    }
    
    __INLINE__ void clear() {
        HYTM_DEBUG3 aout("list "<<renamePointer(this)<<" clear");
        VALIDATE_INV(this);
#ifdef USE_FULL_HASHTABLE
        tab.clear(head, put);
#elif defined(USE_BLOOM_FILTER)
        tab.init();
#endif
        put = head;
        tail = NULL;
        currsz = 0;
        VALIDATE_INV(this);
    }

    __INLINE__ AVPair* find(volatile intptr_t* addr) {
#ifdef USE_FULL_HASHTABLE
        return tab.find(addr);
#elif defined(USE_BLOOM_FILTER)
        if (!tab.contains(addr)) return NULL;
#endif
        AVPair* stop = put;
        for (AVPair* e = head; e != stop; e = e->Next) {
            if (e->addr == addr) {
                return e;
            }
        }
        return NULL;
    }
    
private:
    __INLINE__ AVPair* append(Thread* Self, volatile intptr_t* addr, intptr_t value) {
        AVPair* e = put;
        if (e == NULL) e = extendList();
        tail = e;
        put = e->Next;
        e->addr = addr;
        e->value = value;
        VALIDATE ++currsz;
        return e;
    }
    
public:
    __INLINE__ void insertReplace(Thread* Self, volatile intptr_t* addr, intptr_t value, bool onlyIfAbsent) {
        HYTM_DEBUG3 aout("list "<<renamePointer(this)<<" insertReplace("<<debug(renamePointer((const void*) (void*) addr))<<","<<debug(value)<<")");
        AVPair* e = find(addr);
        if (e) {
            if (!onlyIfAbsent) e->value = value;
        } else {
            e = append(Self, addr, value);
#ifdef USE_FULL_HASHTABLE
            // insert in hash table
            if (tab.requiresExpansion()) tab.expandAndRehashFromList(head, put);
            tab.insertFresh(e);
#elif defined(USE_BLOOM_FILTER)
            tab.insertFresh(addr);
#endif
        }
    }

    // Transfer the data in the log to its ultimate location.
    __INLINE__ void writeForward() {
        //HYTM_DEBUG3 aout("list "<<renamePointer(this)<<" writeForward");
        AVPair* stop = put;
        for (AVPair* e = head; e != stop; e = e->Next) {
            *e->addr = e->value;
        }
    }
    
    void validateContainsAllAndSameSize(HashTable* tab) {
#ifdef USE_FULL_HASHTABLE
        if (currsz != tab->sz) {
            ERROR("hash table "<<debug(tab->sz)<<" has different size from list "<<debug(currsz));
        }
        AVPair* stop = put;
        // each element of hash table appears in list
        for (int i=0;i<tab->cap;++i) {
            AVPair* elt = tab->data[i];
            if (elt) {
                // element in hash table; is it in the list?
                bool found = false;
                for (AVPair* e = head; e != stop; e = e->Next) {
                    if (e == elt) {
                        found = true;
                    }
                }
                if (!found) {
                    ERROR("element "<<debug(*elt)<<" of hash table was not in list");
                }
            }
        }
#endif
    }
};

std::ostream& operator<<(std::ostream& out, const List& obj) {
    AVPair* stop = obj.put;
    for (AVPair* curr = obj.head; curr != stop; curr = curr->Next) {
        out<<*curr<<(curr->Next == stop ? "" : " ");
    }
    return out;
}

//// can be invoked only by a transaction on the software path.
//// writeSet must point to the write-set for this Thread that
//// contains addresses/values of type T.
//__INLINE__ bool validateGSLOrValues(Thread* Self, List* avpairs, bool holdingLocks) {
//    HYTM_DEBUG3 aout("validateGSLOrValues "<<*avpairs);//<<" "<<debug(holdingLocks));
//    assert(Self->isFallback);
//
//    // the norec optimization: if the sequence number didn't change, then neither did any memory locations.
//    int currGSL = gsl;
//    if (currGSL == Self->sequenceLock) {
//        return true;
//    }
//    
//    // validation retry loop
//    while (1) {
//        
//        // wait until sequence lock is not held
//        while (currGSL & 1) {
//            PAUSE();
//            currGSL = gsl;
//            if (holdingLocks) break;
//        }
//
//        // do value based validation
//        AVPair* const stop = avpairs->put;
//        for (AVPair* curr = avpairs->head; curr != stop; curr = curr->Next) {
//            if (curr->value != *curr->addr) {
//                return false;
//            }
//        }
//        
//        // if sequence lock has not changed, then our value based validation saw a snapshot
//        if (currGSL == gsl) {
//            // save the new gsl value we read as the last time when we can serialize all reads that we did so far
//            Self->sequenceLock = currGSL;
//            return true;
//        } else {
//            currGSL = gsl;
//        }
//    }
//}
//
//__INLINE__ bool validateReadSet(Thread* Self, bool holdingLocks) {
////    return validateGSLOrValues(Self, &Self->rdSet->locks.list, holdingLocks);
////    return validateGSLOrValues<long>(Self, &Self->rdSet->l.addresses.list)
////        && validateGSLOrValues<float>(Self, &Self->rdSet->f.addresses.list);
//    return validateGSLOrValues(Self, Self->rdSet, holdingLocks);
//}

__INLINE__ intptr_t AtomicAdd(volatile intptr_t* addr, intptr_t dx) {
    intptr_t v;
    for (v = *addr; CAS(addr, v, v + dx) != v; v = *addr) {}
    return (v + dx);
}










/**
 * 
 * THREAD CLASS IMPLEMENTATION
 * 
 */

volatile long StartTally = 0;
volatile long AbortTallyHW = 0;
volatile long AbortTallySW = 0;
volatile long CommitTallyHW = 0;
volatile long CommitTallySW = 0;

Thread::Thread(long id) {
    HYTM_DEBUG1 aout("new thread with id "<<id);
    memset(this, 0, sizeof(Thread)); /* Default value for most members */
    UniqID = id;
    rng = id + 1;
    xorrng[0] = rng;

    wrSet = (List*) malloc(sizeof(*wrSet));//(TypeLogs*) malloc(sizeof(TypeLogs));
    rdSet = (List*) malloc(sizeof(*rdSet));//(TypeLogs*) malloc(sizeof(TypeLogs));
    //LocalUndo = (TypeLogs*) malloc(sizeof(TypeLogs));
    wrSet->init(this, INIT_WRSET_NUM_ENTRY);
    rdSet->init(this, INIT_RDSET_NUM_ENTRY);
    //LocalUndo->init(this, INIT_LOCAL_NUM_ENTRY);

    allocPtr = tmalloc_alloc(1);
    freePtr = tmalloc_alloc(1);
    assert(allocPtr);
    assert(freePtr);
}

void Thread::destroy() {
//    AtomicAdd((volatile intptr_t*)((void*) (&StartTally)), Starts);
    AtomicAdd((volatile intptr_t*)((void*) (&AbortTallySW)), AbortsSW);
    AtomicAdd((volatile intptr_t*)((void*) (&AbortTallyHW)), AbortsHW);
    AtomicAdd((volatile intptr_t*)((void*) (&CommitTallySW)), CommitsSW);
    AtomicAdd((volatile intptr_t*)((void*) (&CommitTallyHW)), CommitsHW);
    tmalloc_free(allocPtr);
    tmalloc_free(freePtr);
    wrSet->destroy();
    rdSet->destroy();
//    LocalUndo->destroy();
    free(wrSet);
    free(rdSet);
//    free(LocalUndo);
}










/**
 * 
 * IMPLEMENTATION OF TM OPERATIONS
 * 
 */

void TxClearRWSets(void* _Self) {
    Thread* Self = (Thread*) _Self;
    Self->wrSet->clear();
    Self->rdSet->clear();
//    Self->LocalUndo->clear();
}

int TxCommit(void* _Self) {
    Thread* Self = (Thread*) _Self;
    
    // software path
    if (Self->isFallback) {
        // return immediately if txn is read-only
        if (Self->IsRO) {
            HYTM_DEBUG2 aout("thread "<<Self->UniqID<<" commits read-only txn");
            ++Self->CommitsSW;
            goto success;
        }
        
        // acquire global sequence lock
        int oldval = Self->sequenceLock;
        while ((oldval & 1) || !__sync_bool_compare_and_swap(&g.gsl, oldval, oldval + 1)) {
            PAUSE();
            oldval = g.gsl;
        }
        countersProbStartTime(c_counters, Self->UniqID, 0.);
        
        // acquire extra sequence lock
        // note: the original alg writes sequenceLock+1, where sequenceLock was read from gsl, 
        //      which is NOT the same sequence as esl, so they do NOT know
        //      that esl monotonically increases.
        //      however, this is not a problem, since the only purpose in changing
        //      esl is (a) to cause concurrent hardware transactions to abort due to
        //      conflicts (and, of course, htm is not susceptible to the aba problem),
        //      and (b) to indicate (odd esl) that program data values are being changed
        //      by the stm path.
        //      realizing this, we can just use a single bit.
        //      the paper notes this in footnote 7.
        
        // validate the read-set
//        if (failedFirst && !validateReadSet(Self, true)) {
//            // release all locks and abort
//            HYTM_DEBUG2 aout("thread "<<Self->UniqID<<" TxCommit failed validation -> abort");
//            esl = 0;
//            ++gsl;
//            __sync_synchronize();
//            TxAbort(Self);
//        }
        
        // the norec optimization: if the sequence number didn't change, then neither did any memory locations.
        // do value based validation
        AVPair* const stop = Self->rdSet->put;
        for (AVPair* curr = Self->rdSet->head; curr != stop; curr = curr->Next) {
            if (curr->value != *curr->addr) {
                g.esl = 0;
                g.gsl = oldval + 2;
                TxAbort(Self);
            }
        }
        
        // perform the actual writes
        g.esl = 1;
        //__sync_synchronize(); // not needed on x86/64 since the write to esl cannot be moved after these writes
        Self->wrSet->writeForward();
        // release gsl and esl
        g.esl = 0;
        g.gsl = oldval + 2;

        countersProbEndTime(c_counters, Self->UniqID, c_counters->timingOnFallback);
        ++Self->CommitsSW;
        counterInc(c_counters->htmCommit[PATH_FALLBACK], Self->UniqID);
        
    // hardware path
    } else {
        if (!Self->IsRO) {
            int seq = g.gsl;
            if (seq & 1) {
                TxAbort(Self);
            }
            g.gsl = seq + 2;
        }
        HYTM_XEND();
        ++Self->CommitsHW;
        counterInc(c_counters->htmCommit[PATH_FAST_HTM], Self->UniqID);
    }
    
success:
//#ifdef TXNL_MEM_RECLAMATION
//    // "commit" speculative frees and speculative allocations
//    tmalloc_releaseAllForward(Self->freePtr, NULL);
//    tmalloc_clear(Self->allocPtr);
//#endif
    return true;
}

void TxAbort(void* _Self) {
    Thread* Self = (Thread*) _Self;
    
    // software path
    if (Self->isFallback) {
        ++Self->Retries;
        ++Self->AbortsSW;
        if (Self->Retries > MAX_RETRIES) {
            aout("TOO MANY ABORTS. QUITTING.");
            aout("BEGIN DEBUG ADDRESS MAPPING:");
            hytm_acquireLock(&globallock);
                for (unsigned i=0;i<rename_ix;++i) {
                    cout<<stringifyIndex(i)<<"="<<ixToAddr[i]<<" ";
                }
                cout<<endl;
            hytm_releaseLock(&globallock);
            aout("END DEBUG ADDRESS MAPPING.");
            exit(-1);
        }
        hytm_registerHTMAbort(c_counters, Self->UniqID, 0, PATH_FALLBACK);
        countersProbEndTime(c_counters, Self->UniqID, c_counters->timingOnFallback);
        
//#ifdef TXNL_MEM_RECLAMATION
//        // "abort" speculative allocations and speculative frees
//        tmalloc_releaseAllReverse(Self->allocPtr, NULL);
//        tmalloc_clear(Self->freePtr);
//#endif
        
        // longjmp to start of txn
        SIGLONGJMP(*Self->envPtr, 1);
        ASSERT(0);
        
    // hardware path
    } else {
        HYTM_XABORT(0);
    }
}

// TODO: SWITCH BACK TO SEPARATE LISTS FOR LONG / FLOAT.
// CAN'T UNDERSTAND WHY WRITES OF INTPTR_T SHOULD WORK WHEN WE
// REALLY WANT TO WRITE A FLOAT...
// it appears to screw up htm txns but not stm ones, for some reason
// that i don't understand at all. (reads make sense, but not writes.)

/*__INLINE__*/
intptr_t TxLoad(void* _Self, volatile intptr_t* addr) {
    Thread* Self = (Thread*) _Self;
    
    // software path
    if (Self->isFallback) {
//        printf("txLoad(id=%ld, addr=0x%lX) on fallback\n", Self->UniqID, (unsigned long)(void*) addr);
        
        // check whether addr is in the write-set
        AVPair* av = Self->wrSet->find(addr);
        if (av) return av->value;//unpackValue(av);

        // addr is NOT in the write-set, so we read it
        intptr_t val = *addr;

        // add the value we read to the read-set
        // note: if addr changed, it was not changed by us (since we write only in commit)
        Self->rdSet->insertReplace(Self, addr, val, true);
        
        // validate reads

        // the norec optimization: if the sequence number didn't change, then neither did any memory locations.
        AVPair* stop = NULL;
        int currGSL = g.gsl;
        if (currGSL == Self->sequenceLock) {
            goto validated;
        }
        // validation retry loop
        while (1) {
            // wait until sequence lock is not held
            while (currGSL & 1) {
                PAUSE();
                currGSL = g.gsl;
            }
            // do value based validation
            stop = Self->rdSet->put;
            for (AVPair* curr = Self->rdSet->head; curr != stop; curr = curr->Next) {
                if (curr->value != *curr->addr) {
                    HYTM_DEBUG2 aout("thread "<<Self->UniqID<<" TxRead failed validation -> aborting (retries="<<Self->Retries<<")");
                    TxAbort(Self);
                }
            }
            // if sequence lock has not changed, then our value based validation saw a snapshot
            if (currGSL == g.gsl) {
                // save the new gsl value we read as the last time when we can serialize all reads that we did so far
                Self->sequenceLock = currGSL; // assert: even number (unlocked)
                break;
            } else {
                currGSL = g.gsl;
            }
        }
validated:
        
//        if (!validateReadSet(Self, false)) {
//            HYTM_DEBUG2 aout("thread "<<Self->UniqID<<" TxRead failed validation -> aborting (retries="<<Self->Retries<<")");
//            TxAbort(Self);
//        }

        //        printf("    txLoad(id=%ld, ...) success\n", Self->UniqID);
        return val;
        
    // hardware path
    } else {
        // actually read addr
        intptr_t val = *addr;
        return val;
    }
}

/*__INLINE__*/
void TxStore(void* _Self, volatile intptr_t* addr, intptr_t value) {
    Thread* Self = (Thread*) _Self;
    Self->IsRO = false; // txn is not read-only
    
    // software path
    if (Self->isFallback) {
//        printf("txStore(id=%ld, addr=0x%lX, val=%ld) on fallback\n", Self->UniqID, (unsigned long)(void*) addr, (long) value);
        
        // add addr to the write-set
        Self->wrSet->insertReplace(Self, addr, value, false);

//        printf("    txStore(id=%ld, ...) success\n", Self->UniqID);
//        return value;
        
        // think through 2 writes
        // think through write then read
        // validate after reading from write-set??
        // add new testing.cpp kernel that is like a double or triple increment with multiple reads and writes... but still easy to verify...
        
    // hardware path
    } else {
        *addr = value;
    }
}










/**
 * 
 * FRAMEWORK FUNCTIONS
 * (PROBABLY DON'T NEED TO BE CHANGED WHEN CREATING A VARIATION OF THIS TM)
 * 
 */

void TxOnce() {
//    CTASSERT((_TABSZ & (_TABSZ - 1)) == 0); /* must be power of 2 */
    
    initSighandler(); /**** DEBUG CODE ****/
    c_counters = (c_debugCounters *) malloc(sizeof(c_debugCounters));
    countersInit(c_counters, MAX_TID_POW2);                
    printf("%s system ready (up to %d htm txns and %d s/w retries)\n", TM_NAME, HTM_ATTEMPT_THRESH, MAX_RETRIES);
//    memset(LockTab, 0, _TABSZ*sizeof(vLock));
}

void TxShutdown() {
    printf("%s system shutdown:\n", //  Starts=%li CommitsHW=%li AbortsHW=%li CommitsSW=%li AbortsSW=%li\n",
                TM_NAME //,
                //CommitTallyHW+CommitTallySW,
                //CommitTallyHW, AbortTallyHW,
                //CommitTallySW, AbortTallySW
                );

    countersPrint(c_counters);
    countersDestroy(c_counters);
    free(c_counters);
}

void* TxNewThread() {
    Thread* t = (Thread*) malloc(sizeof(Thread));
    assert(t);
    return t;
}

void TxFreeThread(void* _t) {
    Thread* t = (Thread*) _t;
    t->destroy();
    free(t);
}

void TxInitThread(void* _t, long id) {
    Thread* t = (Thread*) _t;
    *t = Thread(id);
}

/* =============================================================================
 * TxAlloc
 *
 * CCM: simple transactional memory allocation
 * =============================================================================
 */
void* TxAlloc(void* _Self, size_t size) {
//#ifdef TXNL_MEM_RECLAMATION
//    Thread* Self = (Thread*) _Self;
//    void* ptr = tmalloc_reserve(size);
//    if (ptr) {
//        tmalloc_append(Self->allocPtr, ptr);
//    }
//
//    return ptr;
//#else
//    return malloc(size);
//#endif
    return NULL;
}

/* =============================================================================
 * TxFree
 *
 * CCM: simple transactional memory de-allocation
 * =============================================================================
 */
void TxFree(void* _Self, void* ptr) {
//#ifdef TXNL_MEM_RECLAMATION
//    Thread* Self = (Thread*) _Self;
//    tmalloc_append(Self->freePtr, ptr);
//#else
////    free(ptr);
//#endif
}

//long TxLoadl(void* _Self, volatile long* addr) {
//    Thread* Self = (Thread*) _Self;
//    return TxLoad(Self, addr);
//}
//float TxLoadf(void* _Self, volatile float* addr) {
//    Thread* Self = (Thread*) _Self;
//    return TxLoad(Self, addr);
//}
//
//long TxStorel(void* _Self, volatile long* addr, long value) {
//    Thread* Self = (Thread*) _Self;
//    return TxStore(Self, addr, value);
//}
//float TxStoref(void* _Self, volatile float* addr, float value) {
//    Thread* Self = (Thread*) _Self;
//    return TxStore(Self, addr, value);
//}
