/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file provides a lock free block bag used by pool_perthread_and_shared.
 * It is used as a mechanism for efficiently passing blocks of reclaimed
 * objects from one thread to another.
 * At a high level, pool_perthread_and_shared gives each thread a private
 * block bag, and also maintains a shared bag.
 * Each thread passes its excess blocks to the shared bag, and first checks
 * the shared bag before allocating new objects when its private bag is empty.
 * 
 * A lock free bag operates on elements of type block<T> (defined in blockbag.h).
 * This class does NOT allocate or deallocate any memory.
 * Instead, it simply chains blocks together using their next pointers.
 * The implementation is a stack, with push and pop at the head.
 * The ABA problem is avoided using version numbers with a double-wide CAS.
 * Any contention issues with using a simple stack, and overhead issues with
 * double-wide CAS, are unimportant, because operations on this bag only
 * happen once a process has filled up several blocks in a block bag and
 * needs to hand one or more blocks off to the shared bag (or emptied a
 * full block that it retrieved from the shared bag).
 * Thus, the number of operations on a lock free block bag is several orders of
 * magnitude smaller than the number of operations on a block bag.
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

#ifndef LOCKFREESTACK_H
#define	LOCKFREESTACK_H

#include <atomic>
#include <iostream>
#include "blockbag.h"
using namespace std;

template <typename T>
class lockfreeblockbag {
private:
    struct tagged_ptr {
        block<T> *ptr;
        long tag;
    };
    std::atomic<tagged_ptr> head;
public:
    lockfreeblockbag() {
        VERBOSE DEBUG cout<<"constructor lockfreeblockbag lockfree="<<head.is_lock_free()<<endl;
        assert(head.is_lock_free());
        head.store(tagged_ptr({NULL,0}));
    }
    ~lockfreeblockbag() {
        VERBOSE DEBUG cout<<"destructor lockfreeblockbag; ";
        block<T> *curr = head.load(memory_order_relaxed).ptr;
        int debugFreed = 0;
        while (curr) {
            block<T> * const temp = curr;
            curr = curr->next;
            //DEBUG ++debugFreed;
            delete temp;
        }
        VERBOSE DEBUG cout<<"freed "<<debugFreed<<endl;
    }
    block<T>* getBlock() {
        while (true) {
            tagged_ptr expHead = head.load(memory_order_relaxed);
            if (expHead.ptr != NULL) {
                if (head.compare_exchange_weak(
                        expHead,
                        tagged_ptr({expHead.ptr->next, expHead.tag+1}))) {
                    block<T> *result = expHead.ptr;
                    result->next = NULL;
                    return result;
                }
            } else {
                return NULL;
            }
        }
    }
    void addBlock(block<T> *b) {
        while (true) {
            tagged_ptr expHead = head.load(memory_order_relaxed);
            b->next = expHead.ptr;
            if (head.compare_exchange_weak(
                    expHead,
                    tagged_ptr({b, expHead.tag+1}))) {
                return;
            }
        }
    }
    // NOT thread safe
    int sizeInBlocks() {
        int result = 0;
        block<T> *curr = head.load(memory_order_relaxed).ptr;
        while (curr) {
            ++result;
            curr = curr->next;
        }
        return result;
    }
    // thread safe, but concurrent operations are very likely to starve it
    long long size() {
        while (1) {
            long long result = 0;
            block<T> *originalHead = head.load(memory_order_relaxed).ptr;
            block<T> *curr = originalHead;
            while (curr) {
                result += curr->computeSize();
                curr = curr->next;
            }
            if (head.load(memory_order_relaxed).ptr == originalHead) {
                return result;
            }
        }
    }
};

#endif	/* LOCKFREESTACK_H */

