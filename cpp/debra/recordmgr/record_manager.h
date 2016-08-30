/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file provides the record_manager class.
 *
 * This implementation uses variadic templates to allow easy instantiation of a
 * record manager that can allocate and reclaim several object types.
 *
 * This flexibility allows, e.g., performing epoch based reclamation
 * (using DEBRA(+)) with a single epoch number for multiple related object types,
 * effectively reclaiming related objects together, in the same epoch.
 * This can reduce the overhead of maintaining separate epochs for, e.g.,
 * tree nodes and SCX records.
 *
 * Conceptually, a record_manager instance is a set of record managers, one for
 * each type being allocated/reclaimed, and template types are used to determine
 * which record manager is appropriate for a given object type.
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

#ifndef RECORD_MANAGER_H
#define	RECORD_MANAGER_H

#include <atomic>
#include "globals.h"
#include "record_manager_single_type.h"

#include <iostream>
#include <exception>
#include <stdexcept>
#include <typeinfo>
using namespace std;

inline CallbackReturn callbackReturnTrue(CallbackArg arg) {
    return true;
}

// compile time check for duplicate template parameters
// compare first with rest to find any duplicates
template <typename T> void check_duplicates(void) {}
template <typename T, typename First, typename... Rest>
void check_duplicates(void) {
    if (typeid(T) == typeid(First)) {
        throw logic_error("duplicate template arguments provided to RecordManagerSet");
    }
    check_duplicates<T, Rest...>();
}

// base case: empty template
// this is a compile time check for invalid arguments
template <class Reclaim, class Alloc, class Pool, typename... Rest>
class RecordManagerSet {
public:
    RecordManagerSet(const int numProcesses, RecoveryMgr<void *> * const _recoveryMgr) {}
    template <typename T>
    record_manager_single_type<T, Reclaim, Alloc, Pool> * get(T * const recordType) {
        throw logic_error("invalid type passed to RecordManagerSet::get()");
        return NULL;
    }
    void clearCounters(void) {}
    void registerThread(const int tid) {}
    void printStatus() {}
    inline void qUnprotectAll(const int tid) {}
    inline void getReclaimers(const int tid, void ** const reclaimers, int index) {}
    inline void enterQuiescentState(const int tid) {}
    inline void leaveQuiescentStateForEach(const int tid) {}
    inline void leaveQuiescentState(const int tid, const bool callForEach) {}
};

// "recursive" case
template <class Reclaim, class Alloc, class Pool, typename First, typename... Rest>
class RecordManagerSet<Reclaim, Alloc, Pool, First, Rest...> : RecordManagerSet<Reclaim, Alloc, Pool, Rest...> {
    record_manager_single_type<First, Reclaim, Alloc, Pool> * const mgr;
public:
    RecordManagerSet(const int numProcesses, RecoveryMgr<void *> * const _recoveryMgr)
        : mgr(new record_manager_single_type<First, Reclaim, Alloc, Pool>(numProcesses, _recoveryMgr))
        , RecordManagerSet<Reclaim, Alloc, Pool, Rest...>(numProcesses, _recoveryMgr)
        {
        //cout<<"RecordManagerSet with First="<<typeid(First).name()<<" and sizeof...(Rest)="<<sizeof...(Rest)<<endl;
        check_duplicates<First, Rest...>(); // check if first is in {rest...}
    }
    ~RecordManagerSet() {
        delete mgr;
        // note: should automatically call the parent class' destructor afterwards
    }
    // note: the compiled code for get() should be a single read and return statement
    template<typename T>
    inline record_manager_single_type<T, Reclaim, Alloc, Pool> * get(T * const recordType) {
        if (typeid(First) == typeid(T)) {
            //cout<<"MATCH: typeid(First)="<<typeid(First).name()<<" typeid(T)="<<typeid(T).name()<<endl;
            return (record_manager_single_type<T, Reclaim, Alloc, Pool> *) mgr;
        } else {
            //cout<<"NO MATCH: typeid(First)="<<typeid(First).name()<<" typeid(T)="<<typeid(T).name()<<endl;
            return ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->get(recordType);
        }
    }
    // note: recursion should be compiled out
    void clearCounters(void) {
        mgr->clearCounters();
        ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->clearCounters();
    }
    void registerThread(const int tid) {
        mgr->initThread(tid);
        ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->registerThread(tid);
    }
    void printStatus() {
        mgr->printStatus();
        ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->printStatus();
    }
    inline void qUnprotectAll(const int tid) {
        mgr->qUnprotectAll(tid);
        ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->qUnprotectAll(tid);
    }
    inline void getReclaimers(const int tid, void ** const reclaimers, int index) {
        reclaimers[index] = mgr->reclaim;
        ((RecordManagerSet <Reclaim, Alloc, Pool, Rest...> *) this)->getReclaimers(tid, reclaimers, 1+index);
    }
    inline void enterQuiescentState(const int tid) {
        mgr->enterQuiescentState(tid);
        ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->enterQuiescentState(tid);
    }
    inline void leaveQuiescentStateForEach(const int tid) {
        mgr->leaveQuiescentState(tid, NULL, 0);
        ((RecordManagerSet <Reclaim, Alloc, Pool, Rest...> *) this)->leaveQuiescentStateForEach(tid);
    }
    inline void leaveQuiescentState(const int tid, const bool callForEach) {
        if (callForEach) {
            leaveQuiescentStateForEach(tid);
        } else {
            void * reclaimers[1+sizeof...(Rest)];
            getReclaimers(tid, reclaimers, 0);
            get((First *) NULL)->leaveQuiescentState(tid, reclaimers, 1+sizeof...(Rest));
            __sync_synchronize(); // memory barrier needed (only) for epoch based schemes at the moment...
        }
    }
};

template <class Reclaim, class Alloc, class Pool, typename RecordTypesFirst, typename... RecordTypesRest>
class record_manager {
protected:
    typedef record_manager<Reclaim,Alloc,Pool,RecordTypesFirst,RecordTypesRest...> SelfType;
    RecordManagerSet<Reclaim,Alloc,Pool,RecordTypesFirst,RecordTypesRest...> * rmset;
    
public:
    const int NUM_PROCESSES;
    RecoveryMgr<SelfType> * const recoveryMgr;
    
    record_manager(const int numProcesses, const int _neutralizeSignal)
            : NUM_PROCESSES(numProcesses)
            , recoveryMgr(new RecoveryMgr<SelfType>(numProcesses, _neutralizeSignal, this))
    {
        rmset = new RecordManagerSet<Reclaim, Alloc, Pool, RecordTypesFirst, RecordTypesRest...>(numProcesses, (RecoveryMgr<void *> *) recoveryMgr);
    }
    ~record_manager() {
        delete recoveryMgr;
        delete rmset;
    }
    void initThread(const int tid) {
        rmset->registerThread(tid);
        recoveryMgr->initThread(tid);
        enterQuiescentState(tid);
    }
    void clearCounters() {
        rmset->clearCounters();
    }
    void printStatus(void) {
        rmset->printStatus();
    }
    template <typename T>
    debugInfo * getDebugInfo(T * const recordType) {
        return &rmset->get((T *) NULL)->debugInfoRecord;
    }
    
    // for hazard pointers

    template <typename T>
    inline bool isProtected(const int tid, T * const obj) {
        return rmset->get((T *) NULL)->isProtected(tid, obj);
    }
    
    template <typename T>
    inline bool protect(const int tid, T * const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool hintMemoryBarrier = true) {
        return rmset->get((T *) NULL)->protect(tid, obj, notRetiredCallback, callbackArg, hintMemoryBarrier);
    }
    
    template <typename T>
    inline void unprotect(const int tid, T * const obj) {
        rmset->get((T *) NULL)->unprotect(tid, obj);
    }
    
    // for DEBRA+
    
    // warning: qProtect must be reentrant and lock-free (i.e., async-signal-safe)
    template <typename T>
    inline bool qProtect(const int tid, T * const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool hintMemoryBarrier = true) {
        return rmset->get((T *) NULL)->qProtect(tid, obj, notRetiredCallback, callbackArg, hintMemoryBarrier);
    }
    
    template <typename T>
    inline bool isQProtected(const int tid, T * const obj) {
        return rmset->get((T *) NULL)->isQProtected(tid, obj);
    }
    
    inline void qUnprotectAll(const int tid) {
        assert(!Reclaim::supportsCrashRecovery() || isQuiescent(tid));
        rmset->qUnprotectAll(tid);
    }

    // for epoch based reclamation
    inline bool isQuiescent(const int tid) {
        return rmset->get((RecordTypesFirst *) NULL)->isQuiescent(tid); // warning: if quiescence information is logically shared between all types, with the actual data being associated only with the first type (as it is here), then isQuiescent will return inconsistent results if called in functions that recurse on the template argument list in this class.
    }
    inline void enterQuiescentState(const int tid) {
//        VERBOSE DEBUG2 COUTATOMIC("record_manager_single_type::enterQuiescentState(tid="<<tid<<")"<<endl);
        if (Reclaim::quiescenceIsPerRecordType()) {
//            cout<<"setting quiescent state for all record types\n";
            rmset->enterQuiescentState(tid);
        } else {
            // only call enterQuiescentState for one object type
//            cout<<"setting quiescent state for just one record type: "<<typeid(RecordTypesFirst).name()<<"\n";
            rmset->get((RecordTypesFirst *) NULL)->enterQuiescentState(tid);
        }
    }
    inline void leaveQuiescentState(const int tid) {
//        assert(isQuiescent(tid));
//        VERBOSE DEBUG2 COUTATOMIC("record_manager_single_type::leaveQuiescentState(tid="<<tid<<")"<<endl);
        // for some types of reclaimers, different types of records retired in the same
        // epoch can be reclaimed together (by aggregating their epochs), so we don't actually need
        // separate calls to leaveQuiescentState for each object type.
        // if appropriate, we make a single call to leaveQuiescentState,
        // and it takes care of all record types managed by this record manager.
        //cout<<"quiescenceIsPerRecordType = "<<Reclaim::quiescenceIsPerRecordType()<<endl;
        rmset->leaveQuiescentState(tid, Reclaim::quiescenceIsPerRecordType());
    }

    // for all schemes
    template <typename T>
    inline void retire(const int tid, T * const p) {
        assert(!Reclaim::supportsCrashRecovery() || isQuiescent(tid));
        rmset->get((T *) NULL)->retire(tid, p);
    }

    template <typename T>
    inline T * allocate(const int tid) {
        assert(!Reclaim::supportsCrashRecovery() || isQuiescent(tid));
        return rmset->get((T *) NULL)->allocate(tid);
    }
    
    // optional function which can be used if it is safe to call free()
    template <typename T>
    inline void deallocate(const int tid, T * const p) {
        assert(!Reclaim::supportsCrashRecovery() || isQuiescent(tid));
        rmset->get((T *) NULL)->deallocate(tid, p);
    }

    inline static bool shouldHelp() { // FOR DEBUGGING PURPOSES
        return Reclaim::shouldHelp();
    }
    inline static bool supportsCrashRecovery() {
        return Reclaim::supportsCrashRecovery();
    }
};

#endif
