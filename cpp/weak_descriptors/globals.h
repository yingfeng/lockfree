/* 
 * File:   globals.h
 * Author: trbot
 *
 * Created on March 9, 2015, 1:32 PM
 */

#ifndef GLOBALS_H
#define	GLOBALS_H

#include <string>
using namespace std;

#include <plaf.h>

#ifndef SOFTWARE_BARRIER
#define SOFTWARE_BARRIER asm volatile("": : :"memory")
#endif

#include "recordmgr/globals.h"
#include "recordmgr/debugprinting.h"

double INS;
double DEL;
double RQ;
int RQSIZE;
int MAXKEY;
int OPS_PER_THREAD;
int MILLIS_TO_RUN;
bool PREFILL;
int WORK_THREADS;
int RQ_THREADS;
int TOTAL_THREADS;
char * RECLAIM_TYPE;
char * ALLOC_TYPE;
char * POOL_TYPE;
int MAX_FAST_HTM_RETRIES;
int MAX_SLOW_HTM_RETRIES;
bool PRINT_TREE;
bool NO_THREADS;
//int THREAD_PINNING;

#endif	/* GLOBALS_H */

