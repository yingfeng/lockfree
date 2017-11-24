/* 
 * File:   debugcounters.h
 * Author: trbot
 *
 * Created on June 21, 2016, 9:50 PM
 */

#ifndef C_DEBUGCOUNTERS_H
#define	C_DEBUGCOUNTERS_H

#ifdef	__cplusplus
extern "C" {
#endif

#include "debugcounter.h"
#include <time.h>

#define NUMBER_OF_PATHS 3
#define PATH_FAST_HTM 0
#define PATH_SLOW_HTM 1
#define PATH_FALLBACK 2

// note: max abort code is 31
#define ABORT_PROCESS_ON_FALLBACK 3
#define ABORT_LOCK_HELD 15
#define ABORT_TLE_LOCKED 30

int hytm_getCompressedStatus(const int status);

int hytm_getCompressedStatusAutomaticAbortCode(const int compressedStatus);

int hytm_getCompressedStatusExplicitAbortCode(const int compressedStatus);

int hytm_getStatusExplicitAbortCode(const int status);

#define MAX_ABORT_STATUS 4096

struct c_debugCounters {
    int NUM_PROCESSES;
#ifdef RECORD_ABORTS
    struct c_debugCounter * htmAbort[NUMBER_OF_PATHS*MAX_ABORT_STATUS]; // one third of these are useless
#else
    struct c_debugCounter ** htmAbort;
#endif
    struct c_debugCounter * htmCommit[NUMBER_OF_PATHS];
    struct c_debugCounter * garbage;
    struct c_debugCounter * timingTemp; // per process timestamps: 0 if not currently timing, o/w > 0
    struct c_debugCounter * timingOnFallback; // per process total DURATIONS over execution (scaled down by the probability of timing on a countersProbStartTime call)
};

void hytm_registerHTMAbort(struct c_debugCounters *cs, const int tid, const int status, const int path);
void countersClear(struct c_debugCounters *cs);
void countersInit(struct c_debugCounters *cs, const int numProcesses);
void countersDestroy(struct c_debugCounters *cs);
void countersProbStartTime(struct c_debugCounters *cs, const int tid, const double randomZeroToOne);
void countersProbEndTime(struct c_debugCounters *cs, const int tid, struct c_debugCounter *timingCounter);

#ifdef	__cplusplus
}
#endif

#endif	/* DEBUGCOUNTERS_H */

