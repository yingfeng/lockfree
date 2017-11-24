#ifndef HYTM1_H
#define HYTM1_H 1

#ifdef __cplusplus
extern "C" {
#endif

#define TM_NAME "HyTM1"
//#define HTM_ATTEMPT_THRESH 0
#ifndef HTM_ATTEMPT_THRESH
    #define HTM_ATTEMPT_THRESH 5
#endif
//#define TXNL_MEM_RECLAMATION

#define MAX_RETRIES 100000

//#define HYTM_DEBUG_PRINT
#define HYTM_DEBUG_PRINT_LOCK

#define HYTM_DEBUG0 if(0)
#define HYTM_DEBUG1 HYTM_DEBUG0 if(1)
#define HYTM_DEBUG2 HYTM_DEBUG1 if(0)
#define HYTM_DEBUG3 HYTM_DEBUG2 if(0)

#include "counters/debugcounters.h"
extern struct c_debugCounters *c_counters;
    
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

//#define LONG_VALIDATION
#define VALIDATE_INV(x) VALIDATE (x)->validateInvariants()
#define VALIDATE if(0)
#define ERROR(x) { \
    cerr<<"ERROR: "<<x<<endl; \
    printStackTrace(); \
    exit(-1); \
}

// just for debugging
extern volatile long globallock;

#define BIG_CONSTANT(x) (x##LLU)






#include <stdint.h>
#include "platform.h"
#include "tmalloc.h"

#  include <setjmp.h>
#  define SIGSETJMP(env, savesigs)      sigsetjmp(env, savesigs)
#  define SIGLONGJMP(env, val)          siglongjmp(env, val); assert(0)

/*
 * Prototypes
 */

void     TxClearRWSets (void* _Self);
void     TxStart       (void*, sigjmp_buf*, int, int*);
void*    TxNewThread   ();

void     TxFreeThread  (void*);
void     TxInitThread  (void*, long id);
int      TxCommit      (void*);
void     TxAbort       (void*);

intptr_t TxLoad(void* Self, volatile intptr_t* addr);
void TxStore(void* Self, volatile intptr_t* addr, intptr_t value);

//long TxLoadl(void* Self, volatile long* addr);
////intptr_t TxLoadp(void* Self, volatile intptr_t* addr);
//float TxLoadf(void* Self, volatile float* addr);
//
//long TxStorel(void* Self, volatile long* addr, long value);
////intptr_t TxStorep(void* Self, volatile intptr_t* addr, intptr_t value);
//float TxStoref(void* Self, volatile float* addr, float value);
//
////long TxStoreLocall(void* Self, volatile long* addr, long value);
////intptr_t TxStoreLocalp(void* Self, volatile intptr_t* addr, intptr_t value);
////float TxStoreLocalf(void* Self, volatile float* addr, float value);

void     TxOnce        ();
void     TxShutdown    ();

void*    TxAlloc       (void*, size_t);
void     TxFree        (void*, void*);

#ifdef __cplusplus
}
#endif

#endif
