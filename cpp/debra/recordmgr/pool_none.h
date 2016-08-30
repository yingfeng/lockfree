/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file provides a Pool plugin for the Record Manager.
 * Specifically, it implements a pass-through pool that does not actually
 * retain any objects. Instead, they are immediately freed instead of being
 * stored in a pool.
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

#ifndef POOL_NOOP_H
#define	POOL_NOOP_H

#include <cassert>
#include <iostream>
#include "blockbag.h"
#include "blockpool.h"
#include "pool_interface.h"
#include "machineconstants.h"
using namespace std;

template <typename T = void, class Alloc = allocator_interface<T> >
class pool_none : public pool_interface<T, Alloc> {
public:
    template<typename _Tp1>
    struct rebind {
        typedef pool_none<_Tp1, Alloc> other;
    };
    template<typename _Tp1, typename _Tp2>
    struct rebind2 {
        typedef pool_none<_Tp1, _Tp2> other;
    };
    
    string getSizeString() { return "no pool"; }
    /**
     * if the freebag contains any object, then remove one from the freebag
     * and return a pointer to it.
     * if not, then retrieve a new object from Alloc
     */
    inline T* get(const int tid) {
        MEMORY_STATS2 this->alloc->debug->addFromPool(tid, 1);
        return this->alloc->allocate(tid);
    }
    inline void add(const int tid, T* ptr) {
        this->alloc->deallocate(tid, ptr);
    }
    inline void addMoveFullBlocks(const int tid, blockbag<T> *bag, block<T> * const predecessor) {
        bag->clearWithoutFreeingElements();
        // note: this function will leak memory if no pool is used, but i believe it is only used by debraplus (which really should use a pool)
    }
    inline void addMoveFullBlocks(const int tid, blockbag<T> *bag) {
        this->alloc->deallocateAndClear(tid, bag);
//        T* ptr;
//        while (ptr = bag->remove()) {
//            add(tid, ptr);
//        }
    }
    inline void addMoveAll(const int tid, blockbag<T> *bag) {
        this->alloc->deallocateAndClear(tid, bag);
//        T* ptr;
//        while (ptr = bag->remove()) {
//            add(tid, ptr);
//        }
    }
    inline int computeSize(const int tid) {
        return 0;
    }
    
    void debugPrintStatus(const int tid) {

    }
    
    pool_none(const int numProcesses, Alloc * const _alloc, debugInfo * const _debug)
            : pool_interface<T, Alloc>(numProcesses, _alloc, _debug) {
        VERBOSE DEBUG cout<<"constructor pool_none"<<endl;
    }
    ~pool_none() {
        VERBOSE DEBUG cout<<"destructor pool_none"<<endl;
    }
};

#endif

