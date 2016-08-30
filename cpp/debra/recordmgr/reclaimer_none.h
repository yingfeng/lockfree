/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file provides a Reclaimer plugin for the Record Manager.
 * Specifically, it provides a no-op wrapper that does not actually reclaim memory.
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

#ifndef RECLAIM_NOOP_H
#define	RECLAIM_NOOP_H

#include <cassert>
#include <iostream>
#include "pool_interface.h"
#include "reclaimer_interface.h"
using namespace std;

template <typename T = void, class Pool = pool_interface<T> >
class reclaimer_none : public reclaimer_interface<T, Pool> {
private:
public:
    template<typename _Tp1>
    struct rebind {
        typedef reclaimer_none<_Tp1, Pool> other;
    };
    template<typename _Tp1, typename _Tp2>
    struct rebind2 {
        typedef reclaimer_none<_Tp1, _Tp2> other;
    };
    
    string getSizeString() { return "no reclaimer"; }
    inline static bool shouldHelp() {
        return true;
    }
    
    inline static bool isQuiescent(const int tid) {
        return true;
    }
    inline static bool isProtected(const int tid, T * const obj) {
        return true;
    }
    inline static bool isQProtected(const int tid, T * const obj) {
        return false;
    }
    
    // for hazard pointers (and reference counting)
    inline static bool protect(const int tid, T * const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true) {
        return true;
    }
    inline static void unprotect(const int tid, T * const obj) {}
    inline static bool qProtect(const int tid, T * const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true) {
        return true;
    }
    inline static void qUnprotectAll(const int tid) {}
    
    // rotate the epoch bags and reclaim any objects retired two epochs ago.
    inline static void rotateEpochBags(const int tid) {
    }
    // invoke this at the beginning of each operation that accesses
    // objects reclaimed by this epoch manager.
    // returns true if the call rotated the epoch bags for thread tid
    // (and reclaimed any objects retired two epochs ago).
    // otherwise, the call returns false.
    inline static bool leaveQuiescentState(const int tid, void * const * const reclaimers, const int numReclaimers) {
        return false;
    }
    inline static void enterQuiescentState(const int tid) {
    }
    
    // for all schemes except reference counting
    inline static void retire(const int tid, T* p) {
    }

    void debugPrintStatus(const int tid) {
    }

    reclaimer_none(const int numProcesses, Pool *_pool, debugInfo * const _debug, RecoveryMgr<void *> * const _recoveryMgr = NULL)
            : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr) {
        VERBOSE DEBUG cout<<"constructor reclaimer_none"<<endl;
    }
    ~reclaimer_none() {
        VERBOSE DEBUG cout<<"destructor reclaimer_none"<<endl;
    }

}; // end class

#endif

