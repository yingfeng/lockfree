/* 
 * File:   globals_extern.h
 * Author: trbot
 *
 * Created on March 9, 2015, 1:32 PM
 */

#ifndef GLOBALS_EXTERN_H
#define	GLOBALS_EXTERN_H

#include <string>
using namespace std;

#include <debugprinting.h>
#include <atomic>

#define __rtm_force_inline __attribute__((always_inline))

#ifndef DEBUG
#define DEBUG if(0)
#define DEBUG1 if(0)
#define DEBUG2 if(0)
#endif

#ifdef __unix__
#define POSIX_SYSTEM
#else
#error NOT UNIX SYSTEM
#endif

#ifndef TRACE_DEFINED
extern std::atomic_bool ___trace;
#define TRACE_TOGGLE {bool ___t = ___trace; ___trace = !___t;}
#define TRACE_ON {___trace = true;}
#define TRACE DEBUG if(___trace)
extern std::atomic_bool ___validateops;
#define VALIDATEOPS_ON {___validateops = true;}
#define VALIDATEOPS DEBUG if(___validateops)
#define TRACE_DEFINED
#endif

extern double INS;
extern double DEL;
extern double RQ;
extern int RQSIZE;
extern int MAXKEY;
extern int OPS_PER_THREAD;
extern int MILLIS_TO_RUN;
extern bool PREFILL;
extern int WORK_THREADS;
extern int RQ_THREADS;
extern int TOTAL_THREADS;
extern char * RECLAIM_TYPE;
extern char * ALLOC_TYPE;
extern char * POOL_TYPE;
extern int MAX_FAST_HTM_RETRIES;
extern int MAX_SLOW_HTM_RETRIES;
extern bool PRINT_TREE;
extern bool NO_THREADS;
//extern int THREAD_PINNING;

#define NUMBER_OF_PATHS 1

#endif	/* GLOBALS_EXTERN_H */

