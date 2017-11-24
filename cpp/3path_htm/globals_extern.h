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

extern bool ALLOWABLE_PATH_CONCURRENCY[14][14];
extern string PATH_NAMES[];

#define NUMBER_OF_PATHS 3
#define PATH_FAST_HTM 0
#define PATH_SLOW_HTM 1
#define PATH_FALLBACK 2

#if defined(BST)

    #if defined(P3ALG13)
    #define RQNAME rangeQuery_lock
    #elif defined(P3ALG12)
    #define RQNAME rangeQuery_vlx
    #else
    #error "must specify fallback algorithm 12 or 13"
    #endif

    #if defined(P1ALG1)
    #define P1NUM 1
    #define UPDATEINSERT_P1 updateInsert_txn_search_inplace
    #define P1NAME "txn_search_inplace"
    #elif defined(P1ALG2)
    #define P1NUM 2
    #define UPDATEINSERT_P1 updateInsert_txn_search_inplace_markingw
    #define P1NAME "txn_search_inplace_markingw"
    #elif defined(P1ALG3)
    #define P1NUM 3
    #define UPDATEINSERT_P1 updateInsert_txn_search_replace
    #define P1NAME "txn_search_replace"
    #elif defined(P1ALG4)
    #define P1NUM 4
    #define UPDATEINSERT_P1 updateInsert_txn_search_replace_markingw
    #define P1NAME "txn_search_replace_markingw"
    #elif defined(P1ALG5)
    #define P1NUM 5
    #define UPDATEINSERT_P1 updateInsert_txn_search_replace_markingw_infow
    #define P1NAME "txn_search_replace_markingw_infow"
    #elif defined(P1ALG6)
    #define P1NUM 6
    #define UPDATEINSERT_P1 updateInsert_txn_search_replace_markingw_infowr
    #define P1NAME "txn_search_replace_markingwr_infowr"
    #elif defined(P1ALG7)
    #define P1NUM 7
    #define UPDATEINSERT_P1 updateInsert_search_txn_inplace_markingwr
    #define P1NAME "search_txn_inplace_markingwr"
    #elif defined(P1ALG8)
    #define P1NUM 8
    #define UPDATEINSERT_P1 updateInsert_search_txn_replace_markingwr
    #define P1NAME "search_txn_replace_markingwr"
    #elif defined(P1ALG9)
    #define P1NUM 9
    #define UPDATEINSERT_P1 updateInsert_search_txn_replace_markingwr_infow
    #define P1NAME "search_txn_replace_markingwr_infow"
    #elif defined(P1ALG10)
    #define P1NUM 10
    #define UPDATEINSERT_P1 updateInsert_search_txn_replace_markingwr_infowr
    #define P1NAME "search_txn_replace_markingwr_infowr"
    #elif defined(P1ALG11)
    #define P1NUM 11
    #define UPDATEINSERT_P1 updateInsert_search_llx_scxhtm
    #define P1NAME "search_llx_scxhtm"
    #elif defined(P1ALG12)
    #define P1NUM 12
    #define UPDATEINSERT_P1 updateInsert_search_llx_scx
    #define P1NAME "search_llx_scx"
    #elif defined(P1ALG13)
    #define P1NUM 13
    #define UPDATEINSERT_P1 updateInsert_lock_search_inplace
    #define P1NAME "lock_search_inplace"
    #endif

    #if defined(P2ALG1)
    #define P2NUM 1
    #define UPDATEINSERT_P2 updateInsert_txn_search_inplace
    #define P2NAME "txn_search_inplace"
    #elif defined(P2ALG2)
    #define P2NUM 2
    #define UPDATEINSERT_P2 updateInsert_txn_search_inplace_markingw
    #define P2NAME "txn_search_inplace_markingw"
    #elif defined(P2ALG3)
    #define P2NUM 3
    #define UPDATEINSERT_P2 updateInsert_txn_search_replace
    #define P2NAME "txn_search_replace"
    #elif defined(P2ALG4)
    #define P2NUM 4
    #define UPDATEINSERT_P2 updateInsert_txn_search_replace_markingw
    #define P2NAME "txn_search_replace_markingw"
    #elif defined(P2ALG5)
    #define P2NUM 5
    #define UPDATEINSERT_P2 updateInsert_txn_search_replace_markingw_infow
    #define P2NAME "txn_search_replace_markingw_infow"
    #elif defined(P2ALG6)
    #define P2NUM 6
    #define UPDATEINSERT_P2 updateInsert_txn_search_replace_markingw_infowr
    #define P2NAME "txn_search_replace_markingwr_infowr"
    #elif defined(P2ALG7)
    #define P2NUM 7
    #define UPDATEINSERT_P2 updateInsert_search_txn_inplace_markingwr
    #define P2NAME "search_txn_inplace_markingwr"
    #elif defined(P2ALG8)
    #define P2NUM 8
    #define UPDATEINSERT_P2 updateInsert_search_txn_replace_markingwr
    #define P2NAME "search_txn_replace_markingwr"
    #elif defined(P2ALG9)
    #define P2NUM 9
    #define UPDATEINSERT_P2 updateInsert_search_txn_replace_markingwr_infow
    #define P2NAME "search_txn_replace_markingwr_infow"
    #elif defined(P2ALG10)
    #define P2NUM 10
    #define UPDATEINSERT_P2 updateInsert_search_txn_replace_markingwr_infowr
    #define P2NAME "search_txn_replace_markingwr_infowr"
    #elif defined(P2ALG11)
    #define P2NUM 11
    #define UPDATEINSERT_P2 updateInsert_search_llx_scxhtm
    #define P2NAME "search_llx_scxhtm"
    #elif defined(P2ALG12)
    #define P2NUM 12
    #define UPDATEINSERT_P2 updateInsert_search_llx_scx
    #define P2NAME "search_llx_scx"
    #elif defined(P2ALG13)
    #define P2NUM 13
    #define UPDATEINSERT_P2 updateInsert_lock_search_inplace
    #define P2NAME "lock_search_inplace"
    #endif

    #if defined(P3ALG1)
    #define P3NUM 1
    #define UPDATEINSERT_P3 updateInsert_txn_search_inplace
    #define P3NAME "txn_search_inplace"
    #elif defined(P3ALG2)
    #define P3NUM 2
    #define UPDATEINSERT_P3 updateInsert_txn_search_inplace_markingw
    #define P3NAME "txn_search_inplace_markingw"
    #elif defined(P3ALG3)
    #define P3NUM 3
    #define UPDATEINSERT_P3 updateInsert_txn_search_replace
    #define P3NAME "txn_search_replace"
    #elif defined(P3ALG4)
    #define P3NUM 4
    #define UPDATEINSERT_P3 updateInsert_txn_search_replace_markingw
    #define P3NAME "txn_search_replace_markingw"
    #elif defined(P3ALG5)
    #define P3NUM 5
    #define UPDATEINSERT_P3 updateInsert_txn_search_replace_markingw_infow
    #define P3NAME "txn_search_replace_markingw_infow"
    #elif defined(P3ALG6)
    #define P3NUM 6
    #define UPDATEINSERT_P3 updateInsert_txn_search_replace_markingw_infowr
    #define P3NAME "txn_search_replace_markingwr_infowr"
    #elif defined(P3ALG7)
    #define P3NUM 7
    #define UPDATEINSERT_P3 updateInsert_search_txn_inplace_markingwr
    #define P3NAME "search_txn_inplace_markingwr"
    #elif defined(P3ALG8)
    #define P3NUM 8
    #define UPDATEINSERT_P3 updateInsert_search_txn_replace_markingwr
    #define P3NAME "search_txn_replace_markingwr"
    #elif defined(P3ALG9)
    #define P3NUM 9
    #define UPDATEINSERT_P3 updateInsert_search_txn_replace_markingwr_infow
    #define P3NAME "search_txn_replace_markingwr_infow"
    #elif defined(P3ALG10)
    #define P3NUM 10
    #define UPDATEINSERT_P3 updateInsert_search_txn_replace_markingwr_infowr
    #define P3NAME "search_txn_replace_markingwr_infowr"
    #elif defined(P3ALG11)
    #define P3NUM 11
    #define UPDATEINSERT_P3 updateInsert_search_llx_scxhtm
    #define P3NAME "search_llx_scxhtm"
    #elif defined(P3ALG12)
    #define P3NUM 12
    #define UPDATEINSERT_P3 updateInsert_search_llx_scx
    #define P3NAME "search_llx_scx"
    #elif defined(P3ALG13)
    #define P3NUM 13
    #define UPDATEINSERT_P3 updateInsert_lock_search_inplace
    #define P3NAME "lock_search_inplace"
    #endif

    #if defined(P1ALG1)
    #define UPDATEERASE_P1 updateErase_txn_search_inplace
    #elif defined(P1ALG2)
    #define UPDATEERASE_P1 updateErase_txn_search_inplace_markingw
    #elif defined(P1ALG3)
    #define UPDATEERASE_P1 updateErase_txn_search_replace
    #elif defined(P1ALG4)
    #define UPDATEERASE_P1 updateErase_txn_search_replace_markingw
    #elif defined(P1ALG5)
    #define UPDATEERASE_P1 updateErase_txn_search_replace_markingw_infow
    #elif defined(P1ALG6)
    #define UPDATEERASE_P1 updateErase_txn_search_replace_markingw_infowr
    #elif defined(P1ALG7)
    #define UPDATEERASE_P1 updateErase_search_txn_inplace_markingwr
    #elif defined(P1ALG8)
    #define UPDATEERASE_P1 updateErase_search_txn_replace_markingwr
    #elif defined(P1ALG9)
    #define UPDATEERASE_P1 updateErase_search_txn_replace_markingwr_infow
    #elif defined(P1ALG10)
    #define UPDATEERASE_P1 updateErase_search_txn_replace_markingwr_infowr
    #elif defined(P1ALG11)
    #define UPDATEERASE_P1 updateErase_search_llx_scxhtm
    #elif defined(P1ALG12)
    #define UPDATEERASE_P1 updateErase_search_llx_scx
    #elif defined(P1ALG13)
    #define UPDATEERASE_P1 updateErase_lock_search_inplace
    #endif

    #if defined(P2ALG1)
    #define UPDATEERASE_P2 updateErase_txn_search_inplace
    #elif defined(P2ALG2)
    #define UPDATEERASE_P2 updateErase_txn_search_inplace_markingw
    #elif defined(P2ALG3)
    #define UPDATEERASE_P2 updateErase_txn_search_replace
    #elif defined(P2ALG4)
    #define UPDATEERASE_P2 updateErase_txn_search_replace_markingw
    #elif defined(P2ALG5)
    #define UPDATEERASE_P2 updateErase_txn_search_replace_markingw_infow
    #elif defined(P2ALG6)
    #define UPDATEERASE_P2 updateErase_txn_search_replace_markingw_infowr
    #elif defined(P2ALG7)
    #define UPDATEERASE_P2 updateErase_search_txn_inplace_markingwr
    #elif defined(P2ALG8)
    #define UPDATEERASE_P2 updateErase_search_txn_replace_markingwr
    #elif defined(P2ALG9)
    #define UPDATEERASE_P2 updateErase_search_txn_replace_markingwr_infow
    #elif defined(P2ALG10)
    #define UPDATEERASE_P2 updateErase_search_txn_replace_markingwr_infowr
    #elif defined(P2ALG11)
    #define UPDATEERASE_P2 updateErase_search_llx_scxhtm
    #elif defined(P2ALG12)
    #define UPDATEERASE_P2 updateErase_search_llx_scx
    #elif defined(P2ALG13)
    #define UPDATEERASE_P2 updateErase_lock_search_inplace
    #endif

    #if defined(P3ALG1)
    #define UPDATEERASE_P3 updateErase_txn_search_inplace
    #elif defined(P3ALG2)
    #define UPDATEERASE_P3 updateErase_txn_search_inplace_markingw
    #elif defined(P3ALG3)
    #define UPDATEERASE_P3 updateErase_txn_search_replace
    #elif defined(P3ALG4)
    #define UPDATEERASE_P3 updateErase_txn_search_replace_markingw
    #elif defined(P3ALG5)
    #define UPDATEERASE_P3 updateErase_txn_search_replace_markingw_infow
    #elif defined(P3ALG6)
    #define UPDATEERASE_P3 updateErase_txn_search_replace_markingw_infowr
    #elif defined(P3ALG7)
    #define UPDATEERASE_P3 updateErase_search_txn_inplace_markingwr
    #elif defined(P3ALG8)
    #define UPDATEERASE_P3 updateErase_search_txn_replace_markingwr
    #elif defined(P3ALG9)
    #define UPDATEERASE_P3 updateErase_search_txn_replace_markingwr_infow
    #elif defined(P3ALG10)
    #define UPDATEERASE_P3 updateErase_search_txn_replace_markingwr_infowr
    #elif defined(P3ALG11)
    #define UPDATEERASE_P3 updateErase_search_llx_scxhtm
    #elif defined(P3ALG12)
    #define UPDATEERASE_P3 updateErase_search_llx_scx
    #elif defined(P3ALG13)
    #define UPDATEERASE_P3 updateErase_lock_search_inplace
    #endif

#else

    #define P1NAME "fast"
    #define P2NAME "middle"
    #define P3NAME "fallback"

#endif

#endif	/* GLOBALS_EXTERN_H */

