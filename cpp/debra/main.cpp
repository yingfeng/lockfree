/**
 * C++ implementation of lock-free chromatic tree using LLX/SCX and DEBRA(+).
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

#include <fstream>
#include <sstream>
#include <sched.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <ctime>
#include <set>
#include <chrono>
#include <typeinfo>
#include <pthread.h>
#include <atomic>

#include "random.h"
#include "globals.h"
#include "recordmgr/record_manager.h"
#include "chromatic.h"
#include "bst.h"

// for DEBRA+
#define DEFAULT_NEUTRALIZE_SIGNAL SIGQUIT

using namespace std;

typedef long long test_type;

// parameters for a simple concurrent test (populated from the command-line))
int INS = 25;
int DEL = 25;
int MAXKEY = 16384;
int MILLIS_TO_RUN = -1;
bool PREFILL = false;
int NTHREADS = 1;
/* 
char * RECLAIM_TYPE;
char * ALLOC_TYPE;
char * POOL_TYPE;
char * DATA_STRUCTURE;
*/

// shared variables used in the concurrent test
bool start = false;
bool done = false;
atomic_int running; // number of threads that are running
debugCounter * keysum; // key sum hashes for all threads (including for prefilling)

chrono::time_point<chrono::high_resolution_clock> startTime;
chrono::time_point<chrono::high_resolution_clock> endTime;
long elapsedMillis;

const test_type NO_KEY = -1;
const test_type NO_VALUE = -1;
const int RETRY = -2;

// cpu sets for binding threads to cores
#ifdef HAS_CPU_SETS
cpu_set_t *cpusets[PHYSICAL_PROCESSORS];
#endif

template <class DataStructure>
class ThreadInfo {
public:
    int tid;
    DataStructure * tree;
};

template <class DataStructure>
void prefill(DataStructure * tree) {
    const double PREFILL_THRESHOLD = 0.03;
    for (int tid=0;tid<NTHREADS;++tid) {
        tree->initThread(tid); // it must be okay that we do this with the main thread and later with another thread.
    }
    Random& rng = rngs[0];
    const double expectedFullness = (INS+DEL ? INS / (double)(INS+DEL) : 0.5); // percent full in expectation
    const int expectedSize = (int)(MAXKEY * expectedFullness);
    int sz = 0;
    int tid = 0;
    for (int i=0;;++i) {
        VERBOSE {
            if (i&&((i % 1000000) == 0)) COUTATOMIC("PREFILL op# "<<i<<" sz="<<sz<<" expectedSize="<<expectedSize<<endl);
        }
        
        int key = rng.nextNatural(MAXKEY);
        int op = rng.nextNatural(100);
        if (op < 100*expectedFullness) {
            if (tree->insert(tid, key, key) == NO_VALUE) {
                ++sz;
                keysum->add(tid, key);
            }
        } else {
            if (tree->erase(tid, key).second) {
                --sz;
                keysum->add(tid, -key);
            }
        }
        int absdiff = (sz < expectedSize ? expectedSize - sz : sz - expectedSize);
        if (absdiff < expectedSize*PREFILL_THRESHOLD) {
            break;
        }
        
        ++tid; // cycle through tids so we have memory uniformly allocated in each thread's data structures
        if (tid >= NTHREADS) tid = 0;
    }
    tree->clearCounters();
    VERBOSE COUTATOMIC("finished prefilling to size "<<sz<<" for expected size "<<expectedSize<<endl);
}

template <class RecordMgr, class DataStructure>
void *threadWork(void *arg) {
    const int OPS_BETWEEN_TIME_CHECKS = 500;
    ThreadInfo<DataStructure> * info = (ThreadInfo<DataStructure> *) arg;
    int tid = info->tid;
    DataStructure * tree = info->tree;
#ifdef HAS_CPU_SETS
    sched_setaffinity(0, sizeof(cpusets[tid%PHYSICAL_PROCESSORS]), cpusets[tid%PHYSICAL_PROCESSORS]); // bind thread to core
    VERBOSE COUTATOMICTID("binding to cpu "<<(tid%PHYSICAL_PROCESSORS)<<endl);
#endif
    Random *rng = &rngs[tid*PREFETCH_SIZE_WORDS];
    tree->initThread(tid);
    running.fetch_add(1);
    __sync_synchronize();
    while (!start) { __sync_synchronize(); TRACE COUTATOMICTID("waiting to start"<<endl); } // wait to start
    int cnt = 0;
    while (!done) {
        if (((++cnt) % OPS_BETWEEN_TIME_CHECKS) == 0) {
            chrono::time_point<chrono::high_resolution_clock> __endTime = chrono::high_resolution_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(__endTime-startTime).count() >= MILLIS_TO_RUN) {
                done = true;
                __sync_synchronize();
                break;
            }
        }
        
        VERBOSE if (cnt&&((cnt % 1000000) == 0)) COUTATOMICTID("op# "<<cnt<<endl);
        int key = rng->nextNatural(MAXKEY);
        int op = rng->nextNatural(100);
        if (op < INS) {
            if (tree->insert(tid, key, key) == NO_VALUE) {
                keysum->add(tid, key);
            }
        } else if (op < INS+DEL) {
            if (tree->erase(tid, key).second) {
                keysum->add(tid, -key);
            }
        } else {
            tree->find(tid, key);
        }
    }
    running.fetch_add(-1);
    VERBOSE COUTATOMICTID("termination"<<endl);
    return NULL;
}

template <class RecordMgr, class DataStructure>
void performExperiment(DataStructure * tree) {
    // get random number generator seeded with time
    // we use this rng to seed per-thread rng's that use a different algorithm
    srand(time(NULL));

    pthread_t *threads[NTHREADS];
    ThreadInfo<DataStructure> info[NTHREADS];
    for (int i=0;i<NTHREADS;++i) {
        threads[i] = new pthread_t;
        info[i].tid = i;
        info[i].tree = tree;
        rngs[i*PREFETCH_SIZE_WORDS].setSeed(rand());
    }
    
    if (PREFILL) prefill<DataStructure>(tree);

    for (int i=0;i<NTHREADS;++i) {
        if (pthread_create(threads[i], NULL, threadWork<RecordMgr, DataStructure>, &info[i])) {
            cerr<<"ERROR: could not create thread"<<endl;
            exit(-1);
        }
    }
    
    while (running.load() < NTHREADS) {
        TRACE COUTATOMIC("main thread: waiting for threads to START running="<<running.load()<<endl);
    } // wait for all threads to be ready
    COUTATOMIC("main thread: starting timer..."<<endl);
    startTime = chrono::high_resolution_clock::now();
    __sync_synchronize();
    start = true;
    for (int i=0;i<NTHREADS;++i) {
        VERBOSE COUTATOMIC("main thread: attempting to join thread "<<i<<endl);
        if (pthread_join(*threads[i], NULL)) {
            cerr<<"ERROR: could not join thread"<<endl;
            exit(-1);
        }
        VERBOSE COUTATOMIC("main thread: joined thread "<<i<<endl);
    }
    endTime = chrono::high_resolution_clock::now();
    elapsedMillis = chrono::duration_cast<chrono::milliseconds>(endTime-startTime).count();
    COUTATOMIC((elapsedMillis/1000.)<<"s"<<endl);

    for (int i=0;i<NTHREADS;++i) {
        delete threads[i];
    }
}

template <class RecordMgr, class DataStructure>
void printOutput(DataStructure * tree) {
    COUTATOMIC(typeid(tree).name()<<endl);

    tree->debugPrintAllocatorStatus();

    RecordMgr *recordmgr = tree->debugGetRecordMgr();
    COUTATOMIC("total allocated nodes         : "<<recordmgr->getDebugInfo((Node<test_type,test_type> *) NULL)->getTotalAllocated()<<endl);
    COUTATOMIC("total allocated scx records   : "<<recordmgr->getDebugInfo((SCXRecord<test_type,test_type> *) NULL)->getTotalAllocated()<<endl);
    COUTATOMIC("total deallocated nodes       : "<<recordmgr->getDebugInfo((Node<test_type,test_type> *) NULL)->getTotalDeallocated()<<endl);
    COUTATOMIC("total deallocated scx records : "<<recordmgr->getDebugInfo((SCXRecord<test_type,test_type> *) NULL)->getTotalDeallocated()<<endl);
    COUTATOMIC("total retired nodes           : "<<recordmgr->getDebugInfo((Node<test_type,test_type> *) NULL)->getTotalRetired()<<endl);
    COUTATOMIC("total retired scx records     : "<<recordmgr->getDebugInfo((SCXRecord<test_type,test_type> *) NULL)->getTotalRetired()<<endl);
    COUTATOMIC("total returned to pool nodes  : "<<recordmgr->getDebugInfo((Node<test_type,test_type> *) NULL)->getTotalToPool()<<endl);
    COUTATOMIC("total returned to pool scx    : "<<recordmgr->getDebugInfo((SCXRecord<test_type,test_type> *) NULL)->getTotalToPool()<<endl);
    COUTATOMIC("total taken from pool nodes   : "<<recordmgr->getDebugInfo((Node<test_type,test_type> *) NULL)->getTotalFromPool()<<endl);
    COUTATOMIC("total taken from pool scx     : "<<recordmgr->getDebugInfo((SCXRecord<test_type,test_type> *) NULL)->getTotalFromPool()<<endl);
    COUTATOMIC("total taken blocks of nodes   : "<<recordmgr->getDebugInfo((Node<test_type,test_type> *) NULL)->getTotalTaken()<<endl);
    COUTATOMIC("total taken blocks of scx     : "<<recordmgr->getDebugInfo((SCXRecord<test_type,test_type> *) NULL)->getTotalTaken()<<endl);
    COUTATOMIC("total given blocks of nodes   : "<<recordmgr->getDebugInfo((Node<test_type,test_type> *) NULL)->getTotalGiven()<<endl);
    COUTATOMIC("total given blocks of scx     : "<<recordmgr->getDebugInfo((SCXRecord<test_type,test_type> *) NULL)->getTotalGiven()<<endl);
    COUTATOMIC("bytes allocated for nodes/scx : "<<(recordmgr->getDebugInfo((Node<test_type,test_type> *) NULL)->getTotalAllocated()*sizeof(Node<test_type, test_type>) + recordmgr->getDebugInfo((SCXRecord<test_type,test_type> *) NULL)->getTotalAllocated()*sizeof(SCXRecord<test_type, test_type>))<<endl);
    COUTATOMIC(endl);
    
    long long threadsKeySum = keysum->getTotal();
    long long treeKeySum = tree->debugKeySum();
    if (threadsKeySum == treeKeySum) {
        cout<<"Validation OK: threadsKeySum = "<<threadsKeySum<<" treeKeySum="<<treeKeySum<<endl;
    } else {
        cout<<"Validation FAILURE: threadsKeySum = "<<threadsKeySum<<" treeKeySum="<<treeKeySum<<endl;
        exit(-1);
    }

    debugCounters * const counters = tree->debugGetCounters();
    COUTATOMIC("total llx true                : "<<counters->llxSuccess->getTotal()<<endl);
    COUTATOMIC("total llx false               : "<<counters->llxFail->getTotal()<<endl);
    COUTATOMIC("total insert succ             : "<<counters->insertSuccess->getTotal()<<endl);
    COUTATOMIC("total insert retry            : "<<counters->insertFail->getTotal()<<endl);
    COUTATOMIC("total erase succ              : "<<counters->eraseSuccess->getTotal()<<endl);
    COUTATOMIC("total erase retry             : "<<counters->eraseFail->getTotal()<<endl);
    COUTATOMIC("total find succ               : "<<counters->findSuccess->getTotal()<<endl);
    COUTATOMIC("total find retry              : "<<counters->findFail->getTotal()<<endl);
    const long totalSucc = counters->insertSuccess->getTotal()+counters->eraseSuccess->getTotal()+counters->findSuccess->getTotal();
    const long throughput = (long) (totalSucc / (elapsedMillis/1000.));
    COUTATOMIC("total succ insert+erase+find  : "<<totalSucc<<endl);
    COUTATOMIC("throughput (succ ops/sec)     : "<<throughput<<endl);
    COUTATOMIC("elapsed milliseconds          : "<<elapsedMillis<<endl);
    COUTATOMIC(endl);

    COUTATOMIC("neutralize signal receipts    : "<<countInterrupted.getTotal()<<endl);
    COUTATOMIC("siglongjmp count              : "<<countLongjmp.getTotal()<<endl);
    COUTATOMIC(endl);
    
    // free tree
    VERBOSE COUTATOMIC("main thread: deleting tree..."<<endl);
    delete tree;
}

template <class RecordMgr, class DataStructure>
void bootstrapExperiment(DataStructure * tree) {
    performExperiment<RecordMgr, DataStructure>(tree);
    printOutput<RecordMgr, DataStructure>(tree);
}

template <class Reclaim, class Alloc, class Pool>
void bootstrapExperiment() {
/*
    // determine the correct data structure to use
    typedef record_manager<Reclaim, Alloc, Pool, Node<test_type,test_type>, SCXRecord<test_type,test_type> > RecordMgr;
    if (strcmp(DATA_STRUCTURE, "BST") == 0) {
        typedef BST<test_type, test_type, less<test_type>, RecordMgr> DataStructure;
        bootstrapExperiment<RecordMgr, DataStructure>(new DataStructure(NO_KEY, NO_VALUE, RETRY, NTHREADS, DEFAULT_NEUTRALIZE_SIGNAL));
    } else if (strcmp(DATA_STRUCTURE, "Chromatic") == 0) {
        typedef Chromatic<test_type, test_type, less<test_type>, RecordMgr> DataStructure;
        bootstrapExperiment<RecordMgr, DataStructure>(new DataStructure(NO_KEY, NO_VALUE, RETRY, NTHREADS, DEFAULT_NEUTRALIZE_SIGNAL));
    } else {
        cout<<"bad data structure type"<<endl;
        exit(1);
    }
*/
    typedef record_manager<Reclaim, Alloc, Pool, Node<test_type,test_type>, SCXRecord<test_type,test_type> > RecordMgr;
    typedef DATA_STRUCTURE<test_type, test_type, less<test_type>, RecordMgr> DataStructure;
    bootstrapExperiment<RecordMgr, DataStructure>(new DataStructure(NO_KEY, NO_VALUE, RETRY, NTHREADS, DEFAULT_NEUTRALIZE_SIGNAL));
}

template <class Reclaim, class Alloc>
void bootstrapExperiment() {
/*
    // determine the correct pool class
    if (strcmp(POOL_TYPE, "perthread_and_shared") == 0) {
        bootstrapExperiment<Reclaim, Alloc, pool_perthread_and_shared<> >();
    } else if (strcmp(POOL_TYPE, "none") == 0) {
        bootstrapExperiment<Reclaim, Alloc, pool_none<> >();
    } else {
        cout<<"bad pool type"<<endl;
        exit(1);
    }
*/
    bootstrapExperiment<Reclaim, Alloc, POOL_TYPE<> >();
}

template <class Reclaim>
void bootstrapExperiment() {
/*
    // determine the correct allocator class
    if (strcmp(ALLOC_TYPE, "once") == 0) {
        bootstrapExperiment<Reclaim, allocator_once<> >();
    } else if (strcmp(ALLOC_TYPE, "bump") == 0) {
        bootstrapExperiment<Reclaim, allocator_bump<> >();
    } else if (strcmp(ALLOC_TYPE, "new") == 0) {
        bootstrapExperiment<Reclaim, allocator_new<> >();
    } else {
        cout<<"bad allocator type"<<endl;
        exit(1);
    }
*/
    bootstrapExperiment<Reclaim, ALLOC_TYPE<> >();
}

void bootstrapExperiment() {
    // determine the correct reclaimer class
/* 
    if (strcmp(RECLAIM_TYPE, "none") == 0) {
        bootstrapExperiment<reclaimer_none<> >();
    } else if (strcmp(RECLAIM_TYPE, "debra") == 0) {
        bootstrapExperiment<reclaimer_debra<> >();
    } else if (strcmp(RECLAIM_TYPE, "debraplus") == 0) {
        bootstrapExperiment<reclaimer_debraplus<> >();
    } else if (strcmp(RECLAIM_TYPE, "hazardptr") == 0) {
        bootstrapExperiment<reclaimer_hazardptr<> >();
    } else {
        cout<<"bad reclaimer type"<<endl;
        exit(1);
    }
*/
    bootstrapExperiment<RECLAIM_TYPE<> >();
}

#define XSTR(x) #x
#define STR(x) XSTR(x)
#define PRINT(a) {cout<<(#a)<<"="<<(a)<<endl;}

int main(int argc, char** argv) {
#ifdef HAS_CPU_SETS
    // create cpu sets for binding threads to cores
    int size = CPU_ALLOC_SIZE(PHYSICAL_PROCESSORS);
    for (int i=0;i<PHYSICAL_PROCESSORS;++i) {
        cpusets[i] = CPU_ALLOC(PHYSICAL_PROCESSORS);
        CPU_ZERO_S(size, cpusets[i]);
        CPU_SET_S(i, size, cpusets[i]);
    }
#endif
    // read command-line arguments
    for (int i=1;i<argc;++i) {
        if (strcmp(argv[i], "-i") == 0) {
            INS = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0) {
            DEL = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-k") == 0) {
            MAXKEY = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0) {
            NTHREADS = atoi(argv[++i]);
/*        
        } else if (strcmp(argv[i], "-mr") == 0) {
            RECLAIM_TYPE = argv[++i];
        } else if (strcmp(argv[i], "-ma") == 0) {
            ALLOC_TYPE = argv[++i];
        } else if (strcmp(argv[i], "-mp") == 0) {
            POOL_TYPE = argv[++i];
        } else if (strcmp(argv[i], "-ds") == 0) {
            DATA_STRUCTURE = argv[++i];
*/
        } else if (strcmp(argv[i], "-t") == 0) {
            MILLIS_TO_RUN = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-p") == 0) {
            PREFILL = true;
        } else {
            cout<<"bad arguments"<<endl;
            exit(1);
        }
    }
    
    PRINT(INS);
    PRINT(DEL);
    PRINT(MAXKEY);
    PRINT(NTHREADS);
    PRINT(MILLIS_TO_RUN);
    PRINT(PREFILL);
    PRINT(STR(RECLAIM_TYPE));
    PRINT(STR(ALLOC_TYPE));
    PRINT(STR(POOL_TYPE));
    PRINT(STR(DATA_STRUCTURE));
    
    // run the experiment
    keysum = new debugCounter(NTHREADS);
    bootstrapExperiment();
    delete keysum;
    return 0;
}
