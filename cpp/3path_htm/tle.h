/* 
 * File:   tle.h
 * Author: trbot
 *
 * Created on January 23, 2016, 5:11 PM
 */

#ifndef TLE_H
#define	TLE_H

#include "debugcounters.h"
#include "globals_extern.h"
#include "common/rtm.h"
#include "globals.h"

void acquireLock(volatile int *lock) {
    while (1) {
        if (*lock) {
            __asm__ __volatile__("pause;");
            continue;
        }
        if (__sync_bool_compare_and_swap(lock, false, true)) {
            return;
        }
    }
}

void releaseLock(volatile int *lock) {
    *lock = false;
}

bool readLock(volatile int *lock) {
    return *lock;
}

class TLEScope {
private:
    int full_attempts;
    int volatile * const lock;
    int tid;
    bool locked;
    bool ended;
    debugCounter ** csucc;
    debugCounter ** cfail;
    debugCounter ** chtmabort;
public:
    __rtm_force_inline TLEScope(int volatile * const _lock, const int maxAttempts, const int _tid, debugCounter ** _succ, debugCounter ** _fail, debugCounter ** _htmabort) : lock(_lock), tid(_tid) {
        csucc = _succ;
        cfail = _fail;
        chtmabort = _htmabort;
        full_attempts = 0;
        locked = false;
        ended = false;

        // try transactions
        int status;
        while (full_attempts++ < maxAttempts) {
            int attempts = MAX_FAST_HTM_RETRIES;
TXN1: (0);
            status = XBEGIN();
            if (status == _XBEGIN_STARTED) {
                if (*lock > 0) XABORT(ABORT_TLE_LOCKED);
                // run the critical section
                goto criticalsection;
            } else {
aborthere:      (0); // aborted
#ifdef RECORD_ABORTS
                chtmabort[PATH_FAST_HTM*MAX_ABORT_STATUS+getCompressedStatus(status)]->inc(tid);
                //chtmabort->registerHTMAbort(tid, status, PATH_FAST_HTM);
#endif
                while (*lock) {
                    __asm__ __volatile__("pause;");
                }
            }
        }
        // acquire lock
        while (1) {
            if (*lock) {
                __asm__ __volatile__("pause;");
                continue;
            }
            if (__sync_bool_compare_and_swap(lock, false, true)) {
                locked = true;
                break;
            }
        }
criticalsection: (0);
    }
    ~TLEScope() {
        end();
    }
    void end() {
        if (!ended) {
            ended = true;
            if (locked) {
                *lock = false;
                // locked = false; // unnecessary since this is going out of scope
                csucc[PATH_FALLBACK]->inc(tid);
                cfail[PATH_FAST_HTM]->add(tid, full_attempts-1);
            } else {
                XEND();
                csucc[PATH_FAST_HTM]->inc(tid);
                cfail[PATH_FAST_HTM]->add(tid, full_attempts-1);
            }
        }
    }
    bool isFallback() {
        return locked;
    }
};

#endif	/* TLE_H */

