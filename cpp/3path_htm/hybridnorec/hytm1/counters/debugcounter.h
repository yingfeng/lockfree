/* 
 * File:   debugcounter.h
 * Author: trbot
 *
 * Created on September 27, 2015, 4:43 PM
 */

#ifndef C_DEBUGCOUNTER_H
#define	C_DEBUGCOUNTER_H

#include <plaf.h>

struct c_debugCounter {
    volatile long long * data; // data[tid*PREFETCH_SIZE_WORDS] = count for thread tid (padded to avoid false sharing)
    int NUM_PROCESSES;
};

void counterAdd(struct c_debugCounter *c, const int tid, const long long val);
void counterInc(struct c_debugCounter *c, const int tid);
void counterSet(struct c_debugCounter *c, const int tid, const long long val);
long long counterGet(struct c_debugCounter *c, const int tid);
long long counterGetTotal(struct c_debugCounter *c);
void counterClear(struct c_debugCounter *c);
void counterInit(struct c_debugCounter *c, const int numProcesses);
void counterDestroy(struct c_debugCounter *c);

#endif	/* DEBUGCOUNTER_H */
