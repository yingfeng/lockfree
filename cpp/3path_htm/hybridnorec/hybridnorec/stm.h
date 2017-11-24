/* =============================================================================
 *
 * stm.h
 *
 * User program interface for STM. For an STM to interface with STAMP, it needs
 * to have its own stm.h for which it redefines the macros appropriately.
 *
 * =============================================================================
 *
 * Author: Chi Cao Minh
 *
 * =============================================================================
 *
 * Edited by Trevor Brown (tabrown@cs.toronto.edu)
 *
 * =============================================================================
 */


#ifndef STM_H
#define STM_H 1

#  include <setjmp.h>
#include <stdio.h>

extern volatile long CommitTallySW;

typedef struct Thread_void {
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
    void* allocPtr;    /* CCM: speculatively allocated */
    void* freePtr;     /* CCM: speculatively free'd */
    void* rdSet;
    void* wrSet;
//    void* LocalUndo;
    sigjmp_buf* envPtr;
    int sequenceLock;
} Thread_void;

#include "hybridnorec.h"
#include "util.h"

#define STM_THREAD_T                    void
#define STM_SELF                        Self
#define STM_RO_FLAG                     ROFlag

#define STM_MALLOC(size)                TxAlloc(STM_SELF, size) /*malloc(size)*/
#define STM_FREE(ptr)                   TxFree(STM_SELF, ptr)

#  define STM_JMPBUF_T                  sigjmp_buf
#  define STM_JMPBUF                    buf


#define STM_VALID()                     (1)
#define STM_RESTART()                   TxAbort(STM_SELF)

#define STM_STARTUP()                   TxOnce()
#define STM_SHUTDOWN()                  TxShutdown()

#define STM_NEW_THREAD(id)              TxNewThread()
#define STM_INIT_THREAD(t, id)          TxInitThread(t, id)
#define STM_FREE_THREAD(t)              TxFreeThread(t)





#  define STM_BEGIN(isReadOnly,jbuf)    do { \
                                            /*volatile char padding1[16384]; padding1[0] = 0;*/ \
                                            /*STM_JMPBUF_T STM_JMPBUF;*/ \
                                            /*volatile char padding2[16384]; padding2[0] = 0;*/ \
                                            \
                                            Thread_void* ___Self = (Thread_void*) STM_SELF; \
                                            TxClearRWSets(STM_SELF); \
                                            \
                                            HYTM_XBEGIN_ARG_T ___xarg; \
                                            ___Self->Retries = 0; \
                                            ___Self->isFallback = 0; \
                                            ___Self->envPtr = &(jbuf); \
                                            int ___htmattempts; \
                                            /*printf("HTM_ATTEMPT_THRESH=%d\n", HTM_ATTEMPT_THRESH);*/ \
                                            for (___htmattempts = 0 ; ___htmattempts < HTM_ATTEMPT_THRESH; ++___htmattempts) { \
                                                /*printf("h/w loop iteration\n");*/ \
                                                while (g.esl) { PAUSE(); } \
                                                ___Self->IsRO = 1; \
                                                if (HYTM_XBEGIN(___xarg)) { \
                                                    if (g.esl) HYTM_XABORT(0); \
                                                    break; \
                                                } else { /* if we aborted */ \
                                                    hytm_registerHTMAbort(c_counters, ___Self->UniqID, X_ABORT_GET_STATUS(___xarg), PATH_FAST_HTM); \
                                                    ++___Self->AbortsHW; \
                                                } \
                                            } \
                                            /*printf("exited loop\n");*/ \
                                            if (___htmattempts < HTM_ATTEMPT_THRESH) break; \
                                            /*printf("passed h/w break\n");*/ \
                                            /* STM attempt */ \
                                            /*HYTM_DEBUG2 aout("thread "<<___Self->UniqID<<" started s/w tx attempt "<<(___Self->AbortsSW+___Self->CommitsSW)<<"; s/w commits so far="<<___Self->CommitsSW);*/ \
                                            /*HYTM_DEBUG1 if ((___Self->CommitsSW % 50000) == 0) aout("thread "<<___Self->UniqID<<" has committed "<<___Self->CommitsSW<<" s/w txns");*/ \
                                            HYTM_DEBUG2 printf("thread %ld started s/w tx; attempts so far=%ld, s/w commits so far=%ld\n", ___Self->UniqID, (___Self->AbortsSW+___Self->CommitsSW), ___Self->CommitsSW); \
                                            HYTM_DEBUG1 if ((___Self->CommitsSW % 100) == 0) printf("thread %ld has committed %ld s/w txns (over all threads so far=%ld)\n", ___Self->UniqID, ___Self->CommitsSW, CommitTallySW); \
                                            ___Self->isFallback = 1; \
                                            if (sigsetjmp((jbuf), 1)) { \
                                                TxClearRWSets(STM_SELF); \
                                                /*if (___Self->Retries > 1) {*/ \
                                                    /*printf("Validation: aborted sw txn with %ld retries\n", ___Self->Retries);*/ \
                                                /*}*/ \
                                            } \
                                            ___Self->IsRO = 1; \
                                            ___Self->sequenceLock = g.gsl; \
                                            while (___Self->sequenceLock & 1) { \
                                                ___Self->sequenceLock = g.gsl; \
                                                PAUSE(); \
                                            } \
                                            /*TxStart(STM_SELF, &(jbuf), SETJMP_RETVAL, &STM_RO_FLAG);*/ \
                                            SOFTWARE_BARRIER; \
                                            /*printf("begin software attempt\n");*/ \
                                        } while (0); /* enforce comma */

#define STM_BEGIN_RD(jbuf)              STM_BEGIN(1,jbuf)
#define STM_BEGIN_WR(jbuf)              STM_BEGIN(0,jbuf)
#define STM_END()                       SOFTWARE_BARRIER; TxCommit(STM_SELF)


typedef volatile intptr_t               vintp;

#define STM_READ_L(var)                 TxLoad(STM_SELF, (vintp*)(void*)&(var))
#define STM_READ_F(var)                 IP2F(TxLoad(STM_SELF, \
                                                    (vintp*)FP2IPP(&(var))))
#define STM_READ_P(var)                 IP2VP(TxLoad(STM_SELF, \
                                                     (vintp*)(void*)&(var)))

#define STM_WRITE_L(var, val)           TxStore(STM_SELF, \
                                                (vintp*)(void*)&(var), \
                                                (intptr_t)(val))
/**
 * the following cast does not work when compiled in x64,
 * since typically 2*sizeof(float) == sizeof(intptr).
 * consequently, writing to a float also writes to the
 * adjacent float...
 */
#define STM_WRITE_F(var, val)           TxStore(STM_SELF, \
                                                (vintp*)FP2IPP(&(var)), \
                                                F2IP(val))
#define STM_WRITE_P(var, val)           TxStore(STM_SELF, \
                                                (vintp*)(void*)&(var), \
                                                VP2IP(val))

#define STM_LOCAL_WRITE_L(var, val)     ({var = val; var;})
#define STM_LOCAL_WRITE_F(var, val)     ({var = val; var;})
#define STM_LOCAL_WRITE_P(var, val)     ({var = val; var;})
//*/

/*
#define STM_READ_L(var)                 TxLoadl(STM_SELF, (volatile long*)(void*)&(var))
#define STM_READ_F(var)                 TxLoadf(STM_SELF, (volatile float*)&(var))
#define STM_READ_P(var)                 TxLoadl(STM_SELF, (volatile intptr_t*)(void*)&(var))
#define STM_WRITE_L(var, val)           TxStorel(STM_SELF, (volatile long*)(void*)&(var), (long)(val))
#define STM_WRITE_F(var, val)           TxStoref(STM_SELF, (volatile float*)&(var), (float)(val))
#define STM_WRITE_P(var, val)           TxStorel(STM_SELF, (volatile intptr_t*)&(var), (volatile intptr_t)(void*)(val))
//*/
/*
#define STM_LOCAL_WRITE_L(var, val)     TxStoreLocall(STM_SELF, (volatile long*)(void*)&(var), (long)(val))
#define STM_LOCAL_WRITE_F(var, val)     TxStoreLocalf(STM_SELF, (volatile float*)&(var), (float)(val))
#define STM_LOCAL_WRITE_P(var, val)     TxStoreLocalp(STM_SELF, (volatile intptr_t*)&(var), (volatile intptr_t)(void*)(val))
//*/

//#define STM_READ_L(var)                 IP2L(TxLoad(STM_SELF, LP2IPP(&(var))))
//#define STM_READ_F(var)                 IP2F(TxLoad(STM_SELF, FP2IPP(&(var))))
//#define STM_READ_P(var)                 IP2VP(TxLoad(STM_SELF, (volatile intptr_t*)(void*)&(var)))
//#define STM_WRITE_L(var, val)           TxStore(STM_SELF, LP2IPP(&(var)), L2IP((val)))
//#define STM_WRITE_F(var, val)           TxStore(STM_SELF, FP2IPP(&(var)), F2IP((val)))
//#define STM_WRITE_P(var, val)           TxStore(STM_SELF, (volatile intptr_t*)&(var), (volatile intptr_t)(void*)(val))


#endif /* STM_H */


/* =============================================================================
 *
 * End of stm.h
 *
 * =============================================================================
 */
