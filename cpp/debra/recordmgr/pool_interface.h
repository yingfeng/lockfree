/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file contains the interface for Pool plugins.
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

#ifndef POOL_INTERFACE_H
#define	POOL_INTERFACE_H

#include <iostream>
#include "allocator_interface.h"
#include "debug_info.h"
#include "blockpool.h"
#include "blockbag.h"
using namespace std;

template <typename T = void, class Alloc = allocator_interface<T> >
class pool_interface {
public:
    debugInfo * const debug;
    
    const int NUM_PROCESSES;
    blockpool<T> **blockpools; // allocated (or not) and freed by descendants
    Alloc *alloc;

    template<typename _Tp1>
    struct rebind {
        typedef pool_interface<_Tp1, Alloc> other;
    };
    template<typename _Tp1, typename _Tp2>
    struct rebind2 {
        typedef pool_interface<_Tp1, _Tp2> other;
    };
    
    string getSizeString() { return ""; }
//    long long getSizeInNodes() { return 0; }
    /**
     * if the pool contains any object, then remove one from the pool
     * and return a pointer to it. otherwise, return NULL.
     */
    inline T* get(const int tid);
    inline void add(const int tid, T* ptr);
    inline void addMoveFullBlocks(const int tid, blockbag<T> *bag);
    inline void addMoveAll(const int tid, blockbag<T> *bag);
    inline int computeSize(const int tid);
    
    void debugPrintStatus(const int tid);
    
    pool_interface(const int numProcesses, Alloc * const _alloc, debugInfo * const _debug)
            : NUM_PROCESSES(numProcesses), alloc(_alloc), debug(_debug) {
        VERBOSE DEBUG cout<<"constructor pool_interface"<<endl;
        this->blockpools = new blockpool<T>*[numProcesses];
        for (int tid=0;tid<numProcesses;++tid) {
            this->blockpools[tid] = new blockpool<T>();
        }
    }
    ~pool_interface() {
        VERBOSE DEBUG cout<<"destructor pool_interface"<<endl;
        for (int tid=0;tid<this->NUM_PROCESSES;++tid) {
            delete this->blockpools[tid];
        }
        delete[] this->blockpools;
    }
};

#endif

