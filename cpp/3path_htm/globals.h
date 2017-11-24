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

#ifndef SOFTWARE_BARRIER
#define SOFTWARE_BARRIER asm volatile("": : :"memory")
#endif

#include <debugprinting.h>

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

string PATH_NAMES[] = {"fast htm", "slow htm", "fallback"};

// the following describes the allowable concurrency between the numbered algorithms for the BST (numbering is defined in globals_extern.h)
bool ALLOWABLE_PATH_CONCURRENCY[14][14] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,1,1,1,1,1,0,0,0,0,0,0,0}, // 1
    {0,1,1,1,1,1,1,1,1,1,1,0,0,0}, // 2
    {0,1,1,1,1,1,1,0,0,0,0,0,0,0}, // 3
    {0,1,1,1,1,1,1,1,1,1,1,0,0,0}, // 4
    {0,1,1,1,1,1,1,1,1,1,1,0,0,0}, // 5
    {0,1,1,1,1,1,1,1,1,1,1,1,1,0}, // 6
    {0,0,1,0,1,1,1,1,1,1,1,0,0,0}, // 7
    {0,0,1,0,1,1,1,1,1,1,1,0,0,0}, // 8
    {0,0,1,0,1,1,1,1,1,1,1,0,0,0}, // 9
    {0,0,1,0,1,1,1,1,1,1,1,1,1,0}, // 10
    {0,0,0,0,0,0,1,0,0,0,1,1,1,0}, // 11
    {0,0,0,0,0,0,1,0,0,0,1,1,1,0}, // 12
    {0,0,0,0,0,0,0,0,0,0,0,0,0,1}  // 13
};

#endif	/* GLOBALS_H */

