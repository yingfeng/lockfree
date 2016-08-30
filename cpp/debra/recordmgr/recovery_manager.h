/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file essentially implements the neutralizing / crash recovery mechanism
 * used by DEBRA+.
 *
 * The actual implementation of neutralizing uses sigsetjmp/siglongjmp.
 * Consequently, the crash recovery mechanism requires a "free" signal.
 * Technically, spuriously sending that signal will not break anything.
 * It will just slow a thread down by neutralizing it (if it is in a 
 * non-quiescent state).
 *
 * WARNING: this implementation of neutralizing only works for a SINGLE instance
 *       of record_manager, which must be globally available in a signal handler.
 *       There are simple ways to modify it to work with multiple record_manager
 *       instances. E.g., we can maintain a global list of all record_manager
 *       instances for access by the signal handler, and then have each thread
 *       executing the signal handler search this list to find the relevant
 *       instance (namely, the one where the thread is in a non-quiescent state).
 *       
 * Copyright (C) 2016 Trevor Brown
 * Contact (tabrown [at] cs [dot] toronto [dot edu]) with any questions or comments.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RECOVERY_MANAGER_H
#define	RECOVERY_MANAGER_H

#include <cassert>
#include <csignal>
#include <setjmp.h>
#include "globals.h"
#include "debugcounter.h"

// for crash recovery
static pthread_key_t pthreadkey;
static struct sigaction ___act;
static void *___singleton = NULL;
//extern pthread_key_t pthreadkey;
//extern struct sigaction ___act;
//extern void *___singleton;

static pthread_t registeredThreads[MAX_TID_POW2];
static void *errnoThreads[MAX_TID_POW2];
static sigjmp_buf *setjmpbuffers;
//extern pthread_t registeredThreads[MAX_TID_POW2];
//extern void *errnoThreads[MAX_TID_POW2];
//extern sigjmp_buf *setjmpbuffers;

static debugCounter countInterrupted(MAX_TID_POW2);
static debugCounter countLongjmp(MAX_TID_POW2);
//extern debugCounter countInterrupted;
//extern debugCounter countLongjmp;
#define MAX_THREAD_ADDR 10000

#ifdef CRASH_RECOVERY_USING_SETJMP
#define CHECKPOINT_AND_RUN_UPDATE(tid, finishedbool) \
    if (MasterRecordMgr::supportsCrashRecovery() && sigsetjmp(setjmpbuffers[(tid)], 0)) { \
        recordmgr->enterQuiescentState((tid)); \
        (finishedbool) = recoverAnyAttemptedSCX((tid), -1); \
        recordmgr->recoveryMgr->unblockCrashRecoverySignal(); \
    } else
#define CHECKPOINT_AND_RUN_QUERY(tid) \
    if (MasterRecordMgr::supportsCrashRecovery() && sigsetjmp(setjmpbuffers[(tid)], 0)) { \
        recordmgr->enterQuiescentState((tid)); \
        recordmgr->recoveryMgr->unblockCrashRecoverySignal(); \
    } else
#endif

template <class MasterRecordMgr>
void crashhandler(int signum, siginfo_t *info, void *uctx) {
    MasterRecordMgr * const recordmgr = (MasterRecordMgr * const) ___singleton;
#ifdef SIGHANDLER_IDENTIFY_USING_PTHREAD_GETSPECIFIC
    int tid = (int) ((long) pthread_getspecific(pthreadkey));
#endif
    TRACE COUTATOMICTID("received signal "<<signum<<endl);

    // if i'm active (not in a quiescent state), i must throw an exception
    // and clean up after myself, instead of continuing my operation.
    DEBUG countInterrupted.inc(tid);
    __sync_synchronize();
    if (!recordmgr->isQuiescent(tid)) {
#ifdef PERFORM_RESTART_IN_SIGHANDLER
        recordmgr->enterQuiescentState(tid);
        DEBUG countLongjmp.inc(tid);
        __sync_synchronize();
#ifdef CRASH_RECOVERY_USING_SETJMP
        siglongjmp(setjmpbuffers[tid], 1);
#endif
#endif
    }
    // otherwise, i simply continue my operation as if nothing happened.
    // this lets me behave nicely when it would be dangerous for me to be
    // restarted (being in a Q state is analogous to having interrupts 
    // disabled in an operating system kernel; however, whereas disabling
    // interrupts blocks other processes' progress, being in a Q state
    // implies that you cannot block the progress of any other thread.)
}

template <class MasterRecordMgr>
class RecoveryMgr {
public:
    const int NUM_PROCESSES;
    const int neutralizeSignal;
    
    inline int getTidInefficient(const pthread_t me) {
        int tid = -1;
        for (int i=0;i<NUM_PROCESSES;++i) {
            if (pthread_equal(registeredThreads[i], me)) {
                tid = i;
            }
        }
        // fail to find my tid -- should be impossible
        if (tid == -1) {
            COUTATOMIC("THIS SHOULD NEVER HAPPEN"<<endl);
            assert(false);
            exit(-1);
        }
        return tid;
    }
    inline int getTidInefficientErrno() {
        int tid = -1;
        for (int i=0;i<NUM_PROCESSES;++i) {
            // here, we use the fact that errno is defined to be a thread local variable
            if (&errno == errnoThreads[i]) {
                tid = i;
            }
        }
        // fail to find my tid -- should be impossible
        if (tid == -1) {
            COUTATOMIC("THIS SHOULD NEVER HAPPEN"<<endl);
            assert(false);
            exit(-1);
        }
        return tid;
    }
    inline int getTid_pthread_getspecific() {
        void * result = pthread_getspecific(pthreadkey);
        if (!result) {
            assert(false);
            COUTATOMIC("ERROR: failed to get thread id using pthread_getspecific"<<endl);
            exit(-1);
        }
        return (int) ((long) result);
    }
    inline pthread_t getPthread(const int tid) {
        return registeredThreads[tid];
    }
    
    void initThread(const int tid) {
        // create mapping between tid and pthread_self for the signal handler
        // and for any thread that neutralizes another
        registeredThreads[tid] = pthread_self();

        // here, we use the fact that errno is defined to be a thread local variable
        errnoThreads[tid] = &errno;
        if (pthread_setspecific(pthreadkey, (void*) (long) tid)) {
            COUTATOMIC("ERROR: failure of pthread_setspecific for tid="<<tid<<endl);
        }
        const long __readtid = (long) ((int *) pthread_getspecific(pthreadkey));
        VERBOSE DEBUG COUTATOMICTID("did pthread_setspecific, pthread_getspecific of "<<__readtid<<endl);
        assert(__readtid == tid);
    }
    
    void unblockCrashRecoverySignal() {
        __sync_synchronize();
        sigset_t oldset;
        sigemptyset(&oldset);
        sigaddset(&oldset, neutralizeSignal);
        if (pthread_sigmask(SIG_UNBLOCK, &oldset, NULL)) {
            VERBOSE COUTATOMIC("ERROR UNBLOCKING SIGNAL"<<endl);
            exit(-1);
        }
    }
    
    RecoveryMgr(const int numProcesses, const int _neutralizeSignal, MasterRecordMgr * const masterRecordMgr)
            : neutralizeSignal(_neutralizeSignal), NUM_PROCESSES(numProcesses) {
        setjmpbuffers = new sigjmp_buf[numProcesses];
        pthread_key_create(&pthreadkey, NULL);
        
        if (MasterRecordMgr::supportsCrashRecovery()) {
            // set up crash recovery signal handling for this process
            memset(&___act, 0, sizeof(___act));
            ___act.sa_sigaction = crashhandler<MasterRecordMgr>; // specify signal handler
            ___act.sa_flags = SA_RESTART | SA_SIGINFO; // restart any interrupted sys calls instead of silently failing
            sigfillset(&___act.sa_mask);               // block signals during handler
            if (sigaction(_neutralizeSignal, &___act, NULL)) {
                COUTATOMIC("ERROR: could not register signal handler for signal "<<_neutralizeSignal<<endl);
                assert(false);
                exit(-1);
            } else {
                VERBOSE COUTATOMIC("registered signal "<<_neutralizeSignal<<" for crash recovery"<<endl);
            }
        }
        // set up shared pointer to this class instance for the signal handler
        ___singleton = (void *) masterRecordMgr;
    }
    ~RecoveryMgr() {
        delete[] setjmpbuffers;
    }
};

#endif	/* RECOVERY_MANAGER_H */

