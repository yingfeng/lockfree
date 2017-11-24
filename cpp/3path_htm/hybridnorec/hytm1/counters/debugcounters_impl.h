/* 
 * File:   debugcounters_impl.h
 * Author: trbot
 *
 * Created on June 21, 2016, 9:50 PM
 */

#ifndef C_DEBUGCOUNTERS_IMPL_H
#define	C_DEBUGCOUNTERS_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include "debugcounters.h"
    
int hytm_getCompressedStatus(const int status) {
    return (status & 63) | ((status >> 24)<<6);
}

int hytm_getCompressedStatusAutomaticAbortCode(const int compressedStatus) {
    return compressedStatus & 63;
}

int hytm_getCompressedStatusExplicitAbortCode(const int compressedStatus) {
    return compressedStatus >> 6;
}

int hytm_getStatusExplicitAbortCode(const int status) {
    return status >> 24;
}

void hytm_registerHTMAbort(struct c_debugCounters *cs, const int tid, const int status, const int path) {
#ifdef RECORD_ABORTS
    counterInc(cs->htmAbort[path*MAX_ABORT_STATUS+hytm_getCompressedStatus(status)], tid);
#endif
}

void countersClear(struct c_debugCounters *cs) {
    int j=0;
    for (;j<NUMBER_OF_PATHS;++j) {
        int i=0;
#ifdef RECORD_ABORTS
        for (;i<MAX_ABORT_STATUS;++i) {
            counterClear(cs->htmAbort[j*MAX_ABORT_STATUS+i]);
        }
#endif
        counterClear(cs->htmCommit[j]);
    }
    counterClear(cs->garbage);
    counterClear(cs->timingTemp);
    counterClear(cs->timingOnFallback);
}

void countersInit(struct c_debugCounters *cs, const int numProcesses) {
    cs->NUM_PROCESSES = numProcesses;
    int j=0;
    for (;j<NUMBER_OF_PATHS;++j) {
        int i=0;
#ifdef RECORD_ABORTS
        for (;i<MAX_ABORT_STATUS;++i) {
            cs->htmAbort[j*MAX_ABORT_STATUS+i] = (struct c_debugCounter *) malloc(sizeof(struct c_debugCounter));
            counterInit(cs->htmAbort[j*MAX_ABORT_STATUS+i], cs->NUM_PROCESSES);
        }
#endif
        cs->htmCommit[j] = (struct c_debugCounter *) malloc(sizeof(struct c_debugCounter));
        counterInit(cs->htmCommit[j], cs->NUM_PROCESSES);
    }
    cs->garbage = (struct c_debugCounter *) malloc(sizeof(struct c_debugCounter));
    counterInit(cs->garbage, cs->NUM_PROCESSES);
    cs->timingTemp = (struct c_debugCounter *) malloc(sizeof(struct c_debugCounter));
    counterInit(cs->timingTemp, cs->NUM_PROCESSES);
    cs->timingOnFallback = (struct c_debugCounter *) malloc(sizeof(struct c_debugCounter));
    counterInit(cs->timingOnFallback, cs->NUM_PROCESSES);
}

void countersDestroy(struct c_debugCounters *cs) {
    int j=0;
    for (;j<NUMBER_OF_PATHS;++j) {
        int i=0;
#ifdef RECORD_ABORTS
        for (;i<MAX_ABORT_STATUS;++i) {
            counterDestroy(cs->htmAbort[j*MAX_ABORT_STATUS+i]);
            free(cs->htmAbort[j*MAX_ABORT_STATUS+i]);
        }
#endif
        counterDestroy(cs->htmCommit[j]);
        free(cs->htmCommit[j]);
    }
    counterDestroy(cs->garbage);
    free(cs->garbage);
    counterDestroy(cs->timingTemp);
    free(cs->timingTemp);
    counterDestroy(cs->timingOnFallback);
    free(cs->timingOnFallback);
}

#define TIMING_PROBABILITY_THRESH 0.01

void countersProbStartTime(struct c_debugCounters *cs, const int tid, const double randomZeroToOne) {
//    if (randomZeroToOne < TIMING_PROBABILITY_THRESH) {
//        timespec t;
//        clock_gettime(CLOCK_MONOTONIC, &t);
//        long long now = t.tv_sec * 1000000000LL + t.tv_nsec;
//        counterSet(cs->timingTemp, tid, now);
//    }
}
void countersProbEndTime(struct c_debugCounters *cs, const int tid, struct c_debugCounter *timingCounter) {
//    long long start = counterGet(cs->timingTemp, tid);
//    if (start > 0) {
//        timespec t;
//        clock_gettime(CLOCK_MONOTONIC, &t);
//        long long now = t.tv_sec * 1000000000LL + t.tv_nsec;
//        counterSet(cs->timingTemp, tid, 0);
//        counterAdd(timingCounter, tid, (now-start)/*/TIMING_PROBABILITY_THRESH*/);
//    }
}

#ifdef	__cplusplus
}
#endif

#endif	/* DEBUGCOUNTERS_IMPL_H */

