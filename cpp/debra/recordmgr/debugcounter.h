/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file provides counters used for debugging.
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

#ifndef DEBUGCOUNTER_H
#define	DEBUGCOUNTER_H

#include <string>
#include <sstream>
#include "machineconstants.h"
using namespace std;

class debugCounter {
private:
    const int NUM_PROCESSES;
    volatile long long * data; // data[tid*PREFETCH_SIZE_WORDS] = count for thread tid (padded to avoid false sharing)
public:
    void add(const int tid, const long long val) {
        data[tid*PREFETCH_SIZE_WORDS] += val;
    }
    void inc(const int tid) {
        add(tid, 1);
    }
    long long get(const int tid) {
        return data[tid*PREFETCH_SIZE_WORDS];
    }
    long long getTotal() {
        long result = 0;
        for (int tid=0;tid<NUM_PROCESSES;++tid) {
            result += get(tid);
        }
        return result;
    }
    void clear() {
        for (int tid=0;tid<NUM_PROCESSES;++tid) {
            data[tid*PREFETCH_SIZE_WORDS] = 0;
        }
    }
    debugCounter(const int numProcesses) : NUM_PROCESSES(numProcesses) {
        data = new long long[numProcesses*PREFETCH_SIZE_WORDS];
        clear();
    }
    ~debugCounter() {
        delete[] data;
    }
};

#endif	/* DEBUGCOUNTER_H */

