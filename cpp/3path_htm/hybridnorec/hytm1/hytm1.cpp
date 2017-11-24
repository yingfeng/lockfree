/**
 * Code for HyTM is loosely based on the code for TL2
 * (in particular, the data structures)
 * 
 * This is essentially an implementation of TLE, but with an abort function
 * (including on the fallback path). To implement the abort function, we log
 * old values before writing.
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
#include "platform_impl.h"
#include "hytm1.h"
#include "stm.h"
#include "tmalloc.h"
#include "util.h"
#include "../murmurhash/MurmurHash3_impl.h"
#include <iostream>
#include <execinfo.h>
#include <stdint.h>
using namespace std;

#include "counters/debugcounters_cpp.h"
struct c_debugCounters *c_counters;
#define debug(x) (#x)<<"="<<x


//#define USE_FULL_HASHTABLE
//#define USE_BLOOM_FILTER

// just for debugging
char ___padding0[4096];
volatile long globallock = 0; // for printf and cout
char ___padding1[4096];
volatile long tleLock = 0; // for tle software path
char ___padding2[4096];

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

__INLINE__ void hytm_acquireLock(volatile long *lock) {
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

__INLINE__ void hytm_releaseLock(volatile long *lock) {
    *lock = 0;
}












/**
 * 
 * TRY-LOCK IMPLEMENTATION AND LOCK TABLE
 * 
 */

class Thread;

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










/**
 * 
 * THREAD CLASS
 *
 */

class List;

class Thread {
public:
    union {
        struct {
            long UniqID;
            volatile long Retries;
        //    int* ROFlag; // not used by stamp
            int IsRO;
            int isFallback;
        //    long Starts; // how many times the user called TxBegin
            long AbortsHW; // # of times hw txns aborted
            long AbortsSW; // # of times sw txns aborted
            long CommitsHW;
            long CommitsSW;
            unsigned long long rng;
            unsigned long long xorrng [1];
            tmalloc_t* allocPtr;    /* CCM: speculatively allocated */
            tmalloc_t* freePtr;     /* CCM: speculatively free'd */
        //    TypeLogs* rdSet;
        //    TypeLogs* wrSet;
        //    TypeLogs* LocalUndo;
            List* rdSet;
            List* wrSet;
            sigjmp_buf* envPtr;
        };
        char bytes[PREFETCH_SIZE_BYTES];
    };
    
    Thread(long id);
    void destroy();
    void compileTimeAsserts() {
        CTASSERT(sizeof(*this) == sizeof(Thread_void));
    }
} __attribute__((aligned(64)));













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
    long Ordinal;
    AVPair** hashTableEntry;
    
    AVPair() {}
    AVPair(AVPair* _Next, AVPair* _Prev, long _Ordinal)
        : Next(_Next), Prev(_Prev), addr(0), value(0), Ordinal(_Ordinal), hashTableEntry(0)
    {}
    
    void validateInvariants() {}
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
#endif
            VALIDATE ++sz;
            VALIDATE_INV(this);
        }
        
        __INLINE__ int requiresExpansion() {
            return 2*sz > cap;
        }
        
    private:
        // expand table by a factor of 2
        __INLINE__ void expandAndClear() {
            AVPair** olddata = data;
            init(cap); // note: cap will be doubled by init
            free(olddata);
        }

    public:
        __INLINE__ void expandAndRehashFromList(AVPair* head, AVPair* stop) {
            HYTM_DEBUG3 aout("hash table "<<renamePointer(this)<<" expandAndRehashFromList");
            VALIDATE_INV(this);
            expandAndClear();
            for (AVPair* e = head; e != stop; e = e->Next) {
                insertFresh(e);
            }
            VALIDATE_INV(this);
        }
        
        __INLINE__ void clear(AVPair* head, AVPair* stop) {
#ifdef HASHTABLE_CLEAR_FROM_LIST
            for (AVPair* e = head; e != stop; e = e->Next) {
                //assert(*e->hashTableEntry);
                //assert(*e->hashTableEntry == e);
                *e->hashTableEntry = NULL;
            }
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
    __INLINE__ void insertReplace(Thread* Self, volatile intptr_t* addr, intptr_t value) {
        HYTM_DEBUG3 aout("list "<<renamePointer(this)<<" insertReplace("<<debug(renamePointer((const void*) (void*) addr))<<","<<debug(value)<<")");
        AVPair* e = find(addr);
        if (e) {
            e->value = value;
        } else {
            e = append(Self, addr, value);
#ifdef USE_FULL_HASHTABLE
            // insert in hash table
//            if (tab.requiresExpansion()) tab.expandAndRehashFromList(head, put);
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
    
    // Transfer the data in the log to its ultimate location.
    __INLINE__ void writeBackward() {
        //HYTM_DEBUG3 aout("list "<<renamePointer(this)<<" writeBackward");
        for (AVPair* e = tail; e; e = e->Prev) {
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
    AtomicAdd((volatile intptr_t*)((void*) (&AbortTallySW)), AbortsSW);
    AtomicAdd((volatile intptr_t*)((void*) (&AbortTallyHW)), AbortsHW);
    AtomicAdd((volatile intptr_t*)((void*) (&CommitTallySW)), CommitsSW);
    AtomicAdd((volatile intptr_t*)((void*) (&CommitTallyHW)), CommitsHW);
    tmalloc_free(allocPtr);
    tmalloc_free(freePtr);
    wrSet->destroy();
    rdSet->destroy();
    free(wrSet);
    free(rdSet);
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
    tmalloc_clear(Self->allocPtr);
    tmalloc_clear(Self->freePtr);
}

int TxCommit(void* _Self) {
    Thread* Self = (Thread*) _Self;
    if (Self->isFallback) {
        hytm_releaseLock(&tleLock);
//#ifdef TXNL_MEM_RECLAMATION
//        tmalloc_clear(Self->allocPtr);
//        tmalloc_releaseAllForward(Self->freePtr, NULL);
//#endif
        counterInc(c_counters->htmCommit[PATH_FALLBACK], Self->UniqID);
        countersProbEndTime(c_counters, Self->UniqID, c_counters->timingOnFallback);
    } else {
//#ifdef TXNL_MEM_RECLAMATION
//        tmalloc_clear(Self->allocPtr);
//        tmalloc_releaseAllForward(Self->freePtr, NULL);
//#endif
        HYTM_XEND();
        counterInc(c_counters->htmCommit[PATH_FAST_HTM], Self->UniqID);
    }
    return true;
}

void TxAbort(void* _Self) {
    Thread* Self = (Thread*) _Self;
    if (Self->isFallback) {
        /* Clean up after an abort. Restore any modified locations. */
        Self->wrSet->writeBackward();
        
        // release global lock
        countersProbEndTime(c_counters, Self->UniqID, c_counters->timingOnFallback);
        hytm_releaseLock(&tleLock);

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
        
#ifdef TXNL_MEM_RECLAMATION
        // "abort" speculative allocations and speculative frees
        // Rollback any memory allocation, and longjmp to retry the txn
        tmalloc_releaseAllReverse(Self->allocPtr, NULL);
        tmalloc_clear(Self->freePtr);
#endif

        SIGLONGJMP(*Self->envPtr, 1);
        ASSERT(0);
    } else {
        HYTM_XABORT(0);
    }
}

/*__INLINE__*/
intptr_t TxLoad(void* _Self, volatile intptr_t* addr) {
    return *addr;
}

/*__INLINE__*/
void TxStore(void* _Self, volatile intptr_t* addr, intptr_t value) {
//    Thread* Self = (Thread*) _Self;
//    if (Self->isFallback) {
//        Self->wrSet->insertReplace(Self, addr, *addr);
//    }
    *addr = value;
}









/**
 * 
 * FRAMEWORK FUNCTIONS
 * (PROBABLY DON'T NEED TO BE CHANGED WHEN CREATING A VARIATION OF THIS TM)
 * 
 */

void TxOnce() {
    initSighandler(); /**** DEBUG CODE ****/
    c_counters = (c_debugCounters *) malloc(sizeof(c_debugCounters));
    countersInit(c_counters, MAX_TID_POW2);
    printf("%s %s\n", TM_NAME, "system ready\n");
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
#ifdef TXNL_MEM_RECLAMATION
    Thread* Self = (Thread*) _Self;
    void* ptr = tmalloc_reserve(size);
    if (ptr) {
        tmalloc_append(Self->allocPtr, ptr);
    }

    return ptr;
#else
    return malloc(size);
#endif
}

/* =============================================================================
 * TxFree
 *
 * CCM: simple transactional memory de-allocation
 * =============================================================================
 */
void TxFree(void* _Self, void* ptr) {
#ifdef TXNL_MEM_RECLAMATION
    Thread* Self = (Thread*) _Self;
    tmalloc_append(Self->freePtr, ptr);
#else
    free(ptr);
#endif
}
