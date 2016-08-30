/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file provides a block pool class for use by the Record Manager.
 * A block pool contains a pool of blocks for reuse by a thread (to reduce
 * the amount of allocation and deallocation of blocks used in block bags).
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

#ifndef BLOCKPOOL_H
#define	BLOCKPOOL_H

#include "blockbag.h"
#include "machineconstants.h"
#include <iostream>
using namespace std;

#define MAX_BLOCK_POOL_SIZE 16

template <typename T>
class block;

template <typename T>
class blockpool {
private:
    block<T> *pool[MAX_BLOCK_POOL_SIZE];
    int poolSize;

    long debugAllocated;
    long debugPoolDeallocated;
    long debugPoolAllocated;
    long debugFreed;
public:
    blockpool() {
        poolSize = 0;
        debugAllocated = 0;
        debugPoolAllocated = 0;
        debugPoolDeallocated = 0;
        debugFreed = 0;
    }
    ~blockpool() {
        VERBOSE DEBUG cout<<"destructor blockpool;";
        for (int i=0;i<poolSize;++i) {
            //DEBUG ++debugFreed;
            assert(pool[i]->isEmpty());
            delete pool[i];                           // warning: uses locks
        }
        VERBOSE DEBUG cout<<" blocks allocated "<<debugAllocated<<" pool-allocated "<<debugPoolAllocated<<" freed "<<debugFreed<<" pool-deallocated "<<debugPoolDeallocated<<endl;
    }
    block<T>* allocateBlock(block<T> * const next) {
        if (poolSize) {
            //DEBUG ++debugPoolAllocated;
            block<T> *result = pool[--poolSize]; // pop a block off the stack
            *result = block<T>(next);
            assert(result->next == next);
            assert(result->computeSize() == 0);
            assert(result->isEmpty());
            return result;
        } else {
            //DEBUG ++debugAllocated;
            return new block<T>(next);                // warning: uses locks
        }
    }
    void deallocateBlock(block<T> * const b) {
        assert(b->isEmpty());
        if (poolSize == MAX_BLOCK_POOL_SIZE) {
            //DEBUG ++debugFreed;
            delete b;                                 // warning: uses locks
        } else {
            //DEBUG ++debugPoolDeallocated;
            pool[poolSize++] = b;
        }
    }
};

#endif	/* BLOCKPOOL_H */

