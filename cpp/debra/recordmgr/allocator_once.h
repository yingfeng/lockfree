/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file provides an Allocator plugin for the Record Manager.
 * Specifically, it implements a simple bump allocator that allocates a huge
 * slab of memory once, and then parcels it out in multiples of the
 * cache-line size.
 * This allocator does not support actually freeing memory to the OS.
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

#ifndef ALLOC_ONCE_H
#define	ALLOC_ONCE_H

#include "machineconstants.h"
#include "globals.h"
#include "allocator_interface.h"
#include <cstdlib>
#include <cassert>
#include <iostream>
#include <vector>
using namespace std;

#define MIN(a, b) ((a) < (b) ? (a) : (b))

template<typename T = void>
class allocator_once : public allocator_interface<T> {
private:
    const int cachelines;    // # cachelines needed to store an object of type T
    // for bump allocation from a contiguous chunk of memory
    T ** mem;             // mem[tid] = pointer to current array to perform bump allocation from
    size_t * memBytes;       // memBytes[tid*PREFETCH_SIZE_WORDS] = size of mem in bytes
    T ** current;         // current[tid*PREFETCH_SIZE_WORDS] = pointer to current position in array mem

    T* bump_memory_next(const int tid) {
        T* result = current[tid*PREFETCH_SIZE_WORDS];
        current[tid*PREFETCH_SIZE_WORDS] = (T*) (((char*) current[tid*PREFETCH_SIZE_WORDS]) + (cachelines*BYTES_IN_CACHELINE));
        return result;
    }
    int bump_memory_bytes_remaining(const int tid) {
        return (((char*) mem[tid])+memBytes[tid*PREFETCH_SIZE_WORDS]) - ((char*) current[tid*PREFETCH_SIZE_WORDS]);
    }
    bool bump_memory_full(const int tid) {
        return (((char*) current[tid*PREFETCH_SIZE_WORDS])+cachelines*BYTES_IN_CACHELINE > ((char*) mem[tid])+memBytes[tid*PREFETCH_SIZE_WORDS]);
    }

public:
    template<typename _Tp1>
    struct rebind {
        typedef allocator_once<_Tp1> other;
    };

    // reserve space for ONE object of type T
    T* allocate(const int tid) {
        if (bump_memory_full(tid)) return NULL;
        return bump_memory_next(tid);
    }
    void static deallocate(const int tid, T * const p) {
        // no op for this allocator; memory is freed only by the destructor.
    }
    void deallocateAndClear(const int tid, blockbag<T> * const bag) {
        // the bag is cleared, which makes it seem like we're leaking memory,
        // but it will be freed in the destructor as we release the huge
        // slabs of memory.
        bag->clearWithoutFreeingElements();
    }

    void debugPrintStatus(const int tid) {}
    
    void initThread(const int tid) {
//        // touch each page of memory before our trial starts
//        long pagesize = sysconf(_SC_PAGE_SIZE);
//        int last = (int) (memBytes[tid*PREFETCH_SIZE_WORDS]/pagesize);
//        VERBOSE COUTATOMICTID("touching each page... memBytes="<<memBytes[tid*PREFETCH_SIZE_WORDS]<<" pagesize="<<pagesize<<" last="<<last<<endl);
//        for (int i=0;i<last;++i) {
//            TRACE COUTATOMICTID("    "<<tid<<" touching page "<<i<<" at address "<<(long)((long*)(((char*) mem[tid])+i*pagesize))<<endl);
//            *((long*)(((char*) mem[tid])+i*pagesize)) = 0;
//        }
//        VERBOSE COUTATOMICTID(" finished touching each page."<<endl);
    }

    allocator_once(const int numProcesses, debugInfo * const _debug)
            : allocator_interface<T>(numProcesses, _debug)
            , cachelines((sizeof(T)+(BYTES_IN_CACHELINE-1))/BYTES_IN_CACHELINE) {
        VERBOSE DEBUG COUTATOMIC("constructor allocator_once"<<endl);
        mem = new T*[numProcesses];
        memBytes = new size_t[numProcesses*PREFETCH_SIZE_WORDS];
        current = new T*[numProcesses*PREFETCH_SIZE_WORDS];
        for (int tid=0;tid<numProcesses;++tid) {
            long long newSizeBytes = 5945123147L / PHYSICAL_PROCESSORS; // divide several GB amongst all threads.
            VERBOSE COUTATOMIC("newSizeBytes        = "<<newSizeBytes<<endl);
            assert((newSizeBytes % (cachelines*BYTES_IN_CACHELINE)) == 0);

            mem[tid] = (T*) malloc((size_t) newSizeBytes);
            if (mem[tid] == NULL) {
                cerr<<"could not allocate memory"<<endl;
                exit(-1);
            }
            //COUTATOMIC("successfully allocated"<<endl);
            memBytes[tid*PREFETCH_SIZE_WORDS] = (size_t) newSizeBytes;
            current[tid*PREFETCH_SIZE_WORDS] = mem[tid];
            // align on cacheline boundary
            int mod = (int) (((long) mem[tid]) % BYTES_IN_CACHELINE);
            if (mod > 0) {
                // we are ignoring the first mod bytes of mem, because if we
                // use them, we will not be aligning objects to cache lines.
                current[tid*PREFETCH_SIZE_WORDS] = (T*) (((char*) mem[tid]) + BYTES_IN_CACHELINE - mod);
            } else {
                current[tid*PREFETCH_SIZE_WORDS] = mem[tid];
            }
            assert((((long) current[tid*PREFETCH_SIZE_WORDS]) % BYTES_IN_CACHELINE) == 0);
        }
    }
    ~allocator_once() {
        long allocated = 0;
        for (int tid=0;tid<this->NUM_PROCESSES;++tid) {
            allocated += (((char*) current[tid*PREFETCH_SIZE_WORDS]) - ((char*) mem[tid]));
        }
        VERBOSE COUTATOMIC("destructor allocator_once allocated="<<allocated<<" bytes, or "<<(allocated/(cachelines*BYTES_IN_CACHELINE))<<" objects of size "<<sizeof(T)<<" occupying "<<cachelines<<" cache lines"<<endl);
        // free all allocated blocks of memory
        for (int tid=0;tid<this->NUM_PROCESSES;++tid) {
            delete mem[tid];
        }
        delete[] mem;
        delete[] memBytes;
        delete[] current;
    }
};
#endif	/* ALLOC_ONCE_H */

