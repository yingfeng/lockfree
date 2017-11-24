/* 
 * File:   debugcounters.h
 * Author: trbot
 *
 * Created on January 26, 2015, 2:22 PM
 */

#ifndef DEBUGCOUNTERS_H
#define	DEBUGCOUNTERS_H

#include <string>
#include <sstream>
#include "globals_extern.h"
#include "recordmgr/debugcounter.h"
#include "common/rtm.h"
using namespace std;

// note: max abort code is 31
#define ABORT_MARKED 1
#define ABORT_UPDATE_FAILED 2
#define ABORT_PROCESS_ON_FALLBACK 3
#define ABORT_SCXRECORD_POINTER_CHANGED6 4
#define ABORT_SCXRECORD_POINTER_CHANGED5 5
#define ABORT_SCXRECORD_POINTER_CHANGED4 6
#define ABORT_SCXRECORD_POINTER_CHANGED3 7
#define ABORT_SCXRECORD_POINTER_CHANGED2 8
#define ABORT_SCXRECORD_POINTER_CHANGED1 9
#define ABORT_SCXRECORD_POINTER_CHANGED0 10
#define ABORT_SCXRECORD_POINTER_CHANGED_FASTHTM 11
#define ABORT_NODE_POINTER_CHANGED 12
#define ABORT_LLX_FAILED 13
#define ABORT_SCX_FAILED 14
#define ABORT_LOCK_HELD 15
#define ABORT_TLE_LOCKED 30

int getCompressedStatus(const int status) {
    return (status & 63) | ((status >> 24)<<6);
}

int getCompressedStatusAutomaticAbortCode(const int compressedStatus) {
    return compressedStatus & 63;
}

int getCompressedStatusExplicitAbortCode(const int compressedStatus) {
    return compressedStatus >> 6;
}

int getStatusExplicitAbortCode(const int status) {
    return status >> 24;
}

string getAutomaticAbortNames(const int compressedStatus) {
    stringstream ss;
    if (compressedStatus & _XABORT_EXPLICIT) ss<<" explicit";
    if (compressedStatus & _XABORT_RETRY) ss<<" retry";
    if (compressedStatus & _XABORT_CONFLICT) ss<<" conflict";
    if (compressedStatus & _XABORT_CAPACITY) ss<<" capacity";
    if (compressedStatus & _XABORT_DEBUG) ss<<" debug";
    if (compressedStatus & _XABORT_NESTED) ss<<" nested";
    return ss.str();
}

string getExplicitAbortName(const int compressedStatus) {
    int explicitCode = getCompressedStatusExplicitAbortCode(compressedStatus);
    if (explicitCode == ABORT_NODE_POINTER_CHANGED) return "node_pointer_changed";
    if (explicitCode == ABORT_SCXRECORD_POINTER_CHANGED6) return "scxrecord_pointer_changed6";
    if (explicitCode == ABORT_SCXRECORD_POINTER_CHANGED5) return "scxrecord_pointer_changed5";
    if (explicitCode == ABORT_SCXRECORD_POINTER_CHANGED4) return "scxrecord_pointer_changed4";
    if (explicitCode == ABORT_SCXRECORD_POINTER_CHANGED3) return "scxrecord_pointer_changed3";
    if (explicitCode == ABORT_SCXRECORD_POINTER_CHANGED2) return "scxrecord_pointer_changed2";
    if (explicitCode == ABORT_SCXRECORD_POINTER_CHANGED1) return "scxrecord_pointer_changed1";
    if (explicitCode == ABORT_SCXRECORD_POINTER_CHANGED0) return "scxrecord_pointer_changed0";
    if (explicitCode == ABORT_SCXRECORD_POINTER_CHANGED_FASTHTM) return "scxrecord_pointer_changed_fasthtm";
    if (explicitCode == ABORT_PROCESS_ON_FALLBACK) return "process_on_fallback";
    if (explicitCode == ABORT_UPDATE_FAILED) return "update_failed";
    if (explicitCode == ABORT_MARKED) return "marked";
    if (explicitCode == ABORT_LLX_FAILED) return "llx_failed";
    if (explicitCode == ABORT_SCX_FAILED) return "scx_failed";
    if (explicitCode >= 42) return "ASSERTION_FAILED";
    return "";
}

string getAllAbortNames(const int compressedStatus) {
    stringstream ss;
    ss<<getExplicitAbortName(compressedStatus)<<getAutomaticAbortNames(compressedStatus);
    return ss.str();
}

#define MAX_ABORT_STATUS 4096
class debugCounters {
public:
    const int NUM_PROCESSES;
    debugCounter * htmCapacityAbortThenCommit[NUMBER_OF_PATHS];
#ifdef RECORD_ABORTS
    debugCounter * htmAbort[NUMBER_OF_PATHS*MAX_ABORT_STATUS]; // one third of these are useless
#else
    debugCounter ** htmAbort;
#endif
    debugCounter * htmRetryAbortRetried[NUMBER_OF_PATHS];
    debugCounter * htmCommit[NUMBER_OF_PATHS];
    debugCounter * updateChange[NUMBER_OF_PATHS];
    debugCounter * pathSuccess[NUMBER_OF_PATHS];
    debugCounter * pathFail[NUMBER_OF_PATHS];
    debugCounter * rebalancingSuccess[NUMBER_OF_PATHS];
    debugCounter * rebalancingFail[NUMBER_OF_PATHS];
    debugCounter * const llxSuccess;
    debugCounter * const llxFail;
    debugCounter * const insertSuccess;
    debugCounter * const insertFail;
    debugCounter * const eraseSuccess;
    debugCounter * const eraseFail;
    debugCounter * const findSuccess;
    debugCounter * const findFail;
    debugCounter * const rqSuccess;
    debugCounter * const rqFail;
    debugCounter * garbage;
#ifdef RECORD_ABORTS
    void registerHTMAbort(const int tid, const int status, const int path) {
        htmAbort[path*MAX_ABORT_STATUS+getCompressedStatus(status)]->inc(tid);
    }
#endif
    void clear() {
        for (int j=0;j<NUMBER_OF_PATHS;++j) {
#ifdef RECORD_ABORTS
            for (int i=0;i<MAX_ABORT_STATUS;++i) {
                htmAbort[j*MAX_ABORT_STATUS+i]->clear();
            }
#endif
            htmCommit[j]->clear();
            updateChange[j]->clear();
            pathSuccess[j]->clear();
            pathFail[j]->clear();
            rebalancingSuccess[j]->clear();
            rebalancingFail[j]->clear();
            htmCapacityAbortThenCommit[j]->clear();
            htmRetryAbortRetried[j]->clear();
        }
        llxSuccess->clear();
        llxFail->clear();
        insertSuccess->clear();
        insertFail->clear();
        eraseSuccess->clear();
        eraseFail->clear();
        findSuccess->clear();
        findFail->clear();
        rqSuccess->clear();
        rqFail->clear();
        garbage->clear();
    }
    debugCounters(const int numProcesses) :
            NUM_PROCESSES(numProcesses),
            llxSuccess(new debugCounter(numProcesses)),
            llxFail(new debugCounter(numProcesses)),
            insertSuccess(new debugCounter(numProcesses)),
            insertFail(new debugCounter(numProcesses)),
            eraseSuccess(new debugCounter(numProcesses)),
            eraseFail(new debugCounter(numProcesses)),
            findSuccess(new debugCounter(numProcesses)),
            findFail(new debugCounter(numProcesses)),
            rqSuccess(new debugCounter(numProcesses)),
            rqFail(new debugCounter(numProcesses)),
            garbage(new debugCounter(numProcesses)) {
        for (int j=0;j<NUMBER_OF_PATHS;++j) {
#ifdef RECORD_ABORTS
            for (int i=0;i<MAX_ABORT_STATUS;++i) {
                htmAbort[j*MAX_ABORT_STATUS+i] = new debugCounter(NUM_PROCESSES);
            }
#endif
            htmCommit[j] = new debugCounter(NUM_PROCESSES);
            updateChange[j] = new debugCounter(NUM_PROCESSES);
            pathSuccess[j] = new debugCounter(NUM_PROCESSES);
            pathFail[j] = new debugCounter(NUM_PROCESSES);
            rebalancingSuccess[j] = new debugCounter(NUM_PROCESSES);
            rebalancingFail[j] = new debugCounter(NUM_PROCESSES);
            htmCapacityAbortThenCommit[j] = new debugCounter(NUM_PROCESSES);
            htmRetryAbortRetried[j] = new debugCounter(NUM_PROCESSES);
        }
    }
    ~debugCounters() {
        delete llxSuccess;
        delete llxFail;
        delete insertSuccess;
        delete insertFail;
        delete eraseSuccess;
        delete eraseFail;
        delete findSuccess;
        delete findFail;
        delete rqSuccess;
        delete rqFail;
        delete garbage;
        for (int j=0;j<NUMBER_OF_PATHS;++j) {
#ifdef RECORD_ABORTS
            for (int i=0;i<MAX_ABORT_STATUS;++i) {
                delete htmAbort[j*MAX_ABORT_STATUS+i];
            }
#endif
            delete htmCommit[j];
            delete updateChange[j];
            delete pathSuccess[j];
            delete pathFail[j];
            delete rebalancingSuccess[j];
            delete rebalancingFail[j];
            delete htmCapacityAbortThenCommit[j];
            delete htmRetryAbortRetried[j];
        }
    }
};

#endif	/* DEBUGCOUNTERS_H */

