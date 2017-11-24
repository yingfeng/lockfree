#ifndef HYTM2_H
#define HYTM2_H 1

#ifdef __cplusplus
extern "C" {
#endif

#define TM_NAME "Hybrid noREC"
//#define HTM_ATTEMPT_THRESH -1
#ifndef HTM_ATTEMPT_THRESH
    #define HTM_ATTEMPT_THRESH 30
#endif
//#define TXNL_MEM_RECLAMATION

#define MAX_RETRIES 1000000
    
#include "../hytm1/counters/debugcounters.h"
extern struct c_debugCounters *c_counters;

//#define HYTM_DEBUG_PRINT
#define HYTM_DEBUG_PRINT_LOCK

#define HYTM_DEBUG0 if(0)
#define HYTM_DEBUG1 HYTM_DEBUG0 if(1)
#define HYTM_DEBUG2 HYTM_DEBUG1 if(1)
#define HYTM_DEBUG3 HYTM_DEBUG2 if(0)

#ifdef HYTM_DEBUG_PRINT
    #define aout(x) { \
        cout<<x<<endl; \
    }
#elif defined(HYTM_DEBUG_PRINT_LOCK)
    #define aout(x) { \
        hytm_acquireLock(&globallock); \
        cout<<x<<endl; \
        hytm_releaseLock(&globallock); \
    }
#else
    #define aout(x) 
#endif

//#define debug(x) (#x)<<"="<<x
//#define LONG_VALIDATION
#define VALIDATE_INV(x) VALIDATE (x)->validateInvariants()
#define VALIDATE if(0)
#define ERROR(x) { \
    cerr<<"ERROR: "<<x<<endl; \
    printStackTrace(); \
    exit(-1); \
}

// just for debugging
extern volatile int globallock;

struct hybridnorec_globals_t {
    volatile char padding0[PREFETCH_SIZE_BYTES];
    volatile int gsl;
    volatile char padding1[PREFETCH_SIZE_BYTES];
    volatile int esl;
    volatile char padding2[PREFETCH_SIZE_BYTES];
};

extern hybridnorec_globals_t g;

#define BIG_CONSTANT(x) (x##LLU)





    
#include <stdint.h>
#include "../hytm1/platform.h"
#include "tmalloc.h"

#  include <setjmp.h>
#  define SIGSETJMP(env, savesigs)      sigsetjmp(env, savesigs)
#  define SIGLONGJMP(env, val)          siglongjmp(env, val); assert(0)

/*
 * Prototypes
 */

void     TxClearRWSets (void* _Self);
void*    TxNewThread   ();

void     TxFreeThread  (void*);
void     TxInitThread  (void*, long id);
int      TxCommit      (void*);
void     TxAbort       (void*);

intptr_t TxLoad(void* Self, volatile intptr_t* addr);
void TxStore(void* Self, volatile intptr_t* addr, intptr_t value);

void     TxOnce        ();
void     TxShutdown    ();

void*    TxAlloc       (void*, size_t);
void     TxFree        (void*, void*);

#ifdef __cplusplus
}
#endif

#endif
