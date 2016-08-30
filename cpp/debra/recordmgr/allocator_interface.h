/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file contains the interface for Allocator plugins.
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

#ifndef ALLOC_INTERFACE_H
#define	ALLOC_INTERFACE_H

#include "debug_info.h"
#include "blockbag.h"
#include <iostream>
using namespace std;

template <typename T = void>
class allocator_interface {
public:
    debugInfo * const debug;
    
    const int NUM_PROCESSES;
    
    template<typename _Tp1>
    struct rebind {
        typedef allocator_interface<_Tp1> other;
    };
    
    // allocate space for one object of type T
    T* allocate(const int tid);
    void deallocate(const int tid, T * const p);
    void deallocateAndClear(const int tid, blockbag<T> * const bag);
    void initThread(const int tid);
    
    void debugPrintStatus(const int tid);

    allocator_interface(const int numProcesses, debugInfo * const _debug)
            : NUM_PROCESSES(numProcesses), debug(_debug) {
        VERBOSE DEBUG cout<<"constructor allocator_interface"<<endl;
    }
    ~allocator_interface() {
        VERBOSE DEBUG cout<<"destructor allocator_interface"<<endl;
    }
};

#endif
