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
using namespace std;

class debugCounters {
public:
    const int NUM_PROCESSES;
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
    void clear() {
        for (int j=0;j<NUMBER_OF_PATHS;++j) {
            updateChange[j]->clear();
            pathSuccess[j]->clear();
            pathFail[j]->clear();
            rebalancingSuccess[j]->clear();
            rebalancingFail[j]->clear();
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
            updateChange[j] = new debugCounter(NUM_PROCESSES);
            pathSuccess[j] = new debugCounter(NUM_PROCESSES);
            pathFail[j] = new debugCounter(NUM_PROCESSES);
            rebalancingSuccess[j] = new debugCounter(NUM_PROCESSES);
            rebalancingFail[j] = new debugCounter(NUM_PROCESSES);
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
            delete updateChange[j];
            delete pathSuccess[j];
            delete pathFail[j];
            delete rebalancingSuccess[j];
            delete rebalancingFail[j];
        }
    }
};

#endif	/* DEBUGCOUNTERS_H */

