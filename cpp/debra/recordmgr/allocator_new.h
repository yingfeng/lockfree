/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file provides an Allocator plugin for the Record Manager.
 * Specifically, it provides a wrapper for the standard C++ new operator.
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

#ifndef ALLOC_NEW_H
#define	ALLOC_NEW_H

#include "machineconstants.h"
#include "pool_interface.h"
#include <cstdlib>
#include <cassert>
#include <iostream>
using namespace std;

template<typename T = void>
class allocator_new : public allocator_interface<T> {
public:
    template<typename _Tp1>
    struct rebind {
        typedef allocator_new<_Tp1> other;
    };
    
    // reserve space for ONE object of type T
    T* allocate(const int tid) {
//        // first, try to get an object from the freeBag
//        T* result = this->pool->get(tid);
//        if (result) return result;
        // allocate a new object
        MEMORY_STATS {
            this->debug->addAllocated(tid, 1);
            VERBOSE {
                if ((this->debug->getAllocated(tid) % 2000) == 0) {
                    debugPrintStatus(tid);
                }
            }
        }
        return new T; //(T*) malloc(sizeof(T));
    }
    void deallocate(const int tid, T * const p) {
        // note: allocators perform the actual freeing/deleting, since
        // only they know how memory was allocated.
        // pools simply call deallocate() to request that it is freed.
        // allocators do not invoke pool functions.
        MEMORY_STATS this->debug->addDeallocated(tid, 1);
        delete p;
    }
    void deallocateAndClear(const int tid, blockbag<T> * const bag) {
        while (!bag->isEmpty()) {
            T* ptr = bag->remove();
            deallocate(tid, ptr);
        }
    }
    
    void debugPrintStatus(const int tid) {
//        cout<</*"thread "<<tid<<" "<<*/"allocated "<<this->debug->getAllocated(tid)<<" objects of size "<<(sizeof(T));
//        cout<<" ";
////        this->pool->debugPrintStatus(tid);
//        cout<<endl;
    }
    
    void initThread(const int tid) {}
    
    allocator_new(const int numProcesses, debugInfo * const _debug)
            : allocator_interface<T>(numProcesses, _debug) {
        VERBOSE DEBUG cout<<"constructor allocator_new"<<endl;
    }
    ~allocator_new() {
        VERBOSE DEBUG cout<<"destructor allocator_new"<<endl;
    }
};

#endif	/* ALLOC_NEW_H */

