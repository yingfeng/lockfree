/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file illustrates how to import, construct, and invoke functions on an
 * instance of, the record_manager class.
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

#define DEBUG if(1)
#define DEBUG1 if(1)
#define DEBUG2 if(1)
#define MEMORY_STATS if(1)
#define MEMORY_STATS2 if(1)

#include <cstdlib>
#include <signal.h>
#include <cassert>
#include "../recordmgr/record_manager.h"

using namespace std;

typedef long long test_type;

class Node {
    test_type x;
    test_type y;
};

int main(int argc, char** argv) {
    const int NUMBER_OF_THREADS = 4;
    const int NEUTRALIZE_SIGNAL = SIGQUIT;      // for DEBRA+
    const int tid = 0;                          // thread ids should be in [0..NUMBER_OF_THREADS-1]

    typedef reclaimer_debraplus<test_type> Reclaimer;
    typedef allocator_new<test_type> Allocator;
    typedef pool_perthread_and_shared<test_type> Pool;
    
    {
    
        record_manager<Reclaimer, Allocator, Pool, Node> mgr (NUMBER_OF_THREADS, NEUTRALIZE_SIGNAL);
        // additional types of records can be specified as template arguments after "Node".
        // e.g., record_manager<Reclaimer, Allocator, Pool, Node, Descriptor, Entry, Point3D> mgr (NUMBER_OF_THREADS, NEUTRALIZE_SIGNAL);

        mgr.initThread(tid);                        // must be called before a thread can call any other functions of mgr
        
        mgr.clearCounters();                        // for debugging

        mgr.enterQuiescentState(tid);               // for DEBRA(+)
        
        Node * x = mgr.allocate<Node>(tid);         // for all schemes (equivalent to malloc())
        Node * y = mgr.allocate<Node>(tid);         // for all schemes

        assert(mgr.isQuiescent(tid));               // for DEBRA and DEBRA+
        mgr.leaveQuiescentState(tid);               // for DEBRA and DEBRA+
        assert(!mgr.isQuiescent(tid));              // for DEBRA and DEBRA+
        
        assert(!mgr.isQProtected(tid, x));          // for DEBRA+
        assert(mgr.qProtect(tid, x, callbackReturnTrue, NULL, false)); // for DEBRA+
        assert(mgr.isQProtected(tid, x));           // for DEBRA+
        
        mgr.enterQuiescentState(tid);               // for DEBRA and DEBRA+
        assert(mgr.isQuiescent(tid));               // for DEBRA and DEBRA+
        
        mgr.qUnprotectAll(tid);                     // for DEBRA+
        assert(!mgr.isQProtected(tid, x));          // for DEBRA+

        assert(mgr.isProtected(tid, x));            // for hazard pointers (hardwired to true for DEBRA(+))
        assert(mgr.protect(tid, x, callbackReturnTrue, NULL, false)); // for hazard pointers (hardwired to true for DEBRA(+))
        assert(mgr.isProtected(tid, x));            // for hazard pointers (hardwired to true for DEBRA(+))
        
        mgr.unprotect(tid, x);                      // for hazard pointers
        assert(mgr.isProtected(tid, x));            // for hazard pointers (hardwired to true for DEBRA(+))
        
        mgr.retire(tid, x);                         // for all schemes
        mgr.deallocate(tid, y);                     // for all schemes (equivalent to free())
        
        mgr.printStatus();                          // for debugging
        
    } // trigger destruction of record manager by letting it go out of scope
    
    cout<<endl;
    cout<<"Passed basic tests."<<endl;
    
    return 0;
}

