/* 
 * File:   testing.cpp (for hytm2)
 * Author: trbot
 *
 * Created on March 25, 2016, 11:28 PM
 */

#include <cstdlib>
#include <iostream>
#include <pthread.h>
#include <cstdio>
#include "stm.h"

using namespace std;

#include <signal.h>
#include <execinfo.h>

volatile bool start;
long globaln;
long globaliters;
long globalx;
long globaly;
long* globalarr;
long globalsz;
#define CHUNK 8

void test0(int iters) {
    // tm metadata for this thread
    const int id = 0;
    STM_THREAD_T* STM_SELF = STM_NEW_THREAD();
    STM_INIT_THREAD(STM_SELF, id);

    // work kernel
    long x = 0, y = 0;
    for (int i = 0; i < iters; ++i) {
        STM_BEGIN_WR();
        STM_WRITE_L(x, STM_READ_L(x) + 1);
        STM_WRITE_L(y, STM_READ_L(y) + 1);
        STM_END();
    }

    STM_FREE_THREAD(STM_SELF);

    cout << "x=" << x << " y=" << y << "... ";
    if (x != y || x != iters) {
        cout << "TEST FAILED (expected x=y=" << iters << ")" << endl;
        exit(-1);
    }
    cout << "success." << endl;
}

void ntest0_init(long n, long iters) {
    globaln = n;
    globaliters = iters;
    globalx = 0;
    globaly = 0;
}

void *ntest0_kernel(void* arg) {
    STM_THREAD_T* STM_SELF = (STM_THREAD_T*) arg;
    while (!start) {
        __sync_synchronize();
    }

    for (int i = 0; i < globaliters; ++i) {
        long x = 0, y = 0;
        STM_BEGIN_WR();
        x = STM_READ_L(globalx);
        y = STM_READ_L(globaly);
        STM_WRITE_L(globalx, x + 1);
        STM_WRITE_L(globaly, y + 1);
        STM_END();
//#if defined(HYTM_DEBUG_PRINT) || defined(HYTM_DEBUG_PRINT_LOCK)
//        printf("id=%ld x=%ld y=%ld\n", *((long*) arg), x, y);
//#endif
    }
}

void ntest0_validate() {
    cout << "x=" << globalx << " y=" << globaly << "... ";
    if (globalx != globaly || globalx != globaln * globaliters) {
        cout << "TEST FAILED (expected x=y=" << (globaln * globaliters) << ")" << endl;
        exit(-1);
    }
    cout << "success." << endl;
}

void ntest2_init(long n, long iters) {
    globaln = n;
    globaliters = iters;
    globalsz = 8;
    globalarr = (long*) malloc(sizeof (long) * globalsz);
    for (int i=0;i<globalsz;++i) globalarr[i] = 0;
    cout << "n=" << n << " iters=" << iters << " sz=" << globalsz << endl;
}

void *ntest2_kernel(void* arg) {
    STM_THREAD_T* STM_SELF = (STM_THREAD_T*) arg;
    while (!start) {
        __sync_synchronize();
    }

    for (int i = 0; i < globaliters; ++i) {
        STM_BEGIN_WR();
        long temp;
        for (int j=0;j<globalsz;++j) {
            temp = STM_READ_L(globalarr[j]);
            temp = STM_READ_L(globalarr[j]);
            temp = STM_READ_L(globalarr[j]);
            STM_WRITE_L(globalarr[j], temp+1);
            STM_WRITE_L(globalarr[j], temp+10);
            temp = STM_READ_L(globalarr[j]) - 10;
            STM_WRITE_L(globalarr[j], temp+100);
            temp = STM_READ_L(globalarr[j]) - 100;
            STM_WRITE_L(globalarr[j], temp+1);
            temp = STM_READ_L(globalarr[j]);
            temp = STM_READ_L(globalarr[j]);
            temp = STM_READ_L(globalarr[j]);
            temp = STM_READ_L(globalarr[j]);
            temp = STM_READ_L(globalarr[j]);
            STM_WRITE_L(globalarr[j], temp+1000);
            temp = STM_READ_L(globalarr[j]) - 1000;
            STM_WRITE_L(globalarr[j], temp+10);
            temp = STM_READ_L(globalarr[j]) - 10;
            STM_WRITE_L(globalarr[j], temp+100);
            temp = STM_READ_L(globalarr[j]) - 100;
            STM_WRITE_L(globalarr[j], temp+1);
        }
        STM_END();
//#if defined(HYTM_DEBUG_PRINT) || defined(HYTM_DEBUG_PRINT_LOCK)
//        printf("id=%ld x=%ld y=%ld\n", *((long*) arg), x, y);
//#endif
    }
}

void ntest2_validate() {
    const long exp = globaln * globaliters * 2;
    for (int i=0;i<globalsz;++i) {
        cout << " arr[i]=" << globalarr[i];
        if (i+1 < globalsz && globalarr[i] != globalarr[i+1]) {
            cout << "TEST FAILED (expected arr["<<i<<"]=arr["<<i+1<<"])" << endl;
            exit(-1);
        }
    }
    cout<<"... ";
    if (globalarr[0] != exp) {
        cout << "TEST FAILED (expected arr[0]=" << exp << ")" << endl;
        exit(-1);
    }
    cout << "success." << endl;
}

void ntest1_init(long n, long iters) {
    globaln = n;
    globaliters = iters;
    globalsz = n * CHUNK;
    globalarr = (long*) malloc(sizeof (long) * globalsz);
    cout << "n=" << n << " iters=" << iters << " chunksize=" << CHUNK << endl;
}

void *ntest1_kernel(void* arg) {
    STM_THREAD_T* STM_SELF = (STM_THREAD_T*) arg;
    while (!start) {
        __sync_synchronize();
    }

    int tid = (int) *((long*) STM_SELF);
    int start = tid * CHUNK; // start index
    for (int k = 0; k < globaliters; ++k) {
        STM_BEGIN_WR();
        long sum = 0;
        for (int i = 0; i < CHUNK; ++i) {
            sum += STM_READ_L(globalarr[start + i]);
        }
        for (int i = 0; i < CHUNK; ++i) {
            STM_WRITE_L(globalarr[start + i], sum);
        }
        start = (start + CHUNK) % globalsz;
        STM_END();
        //#if defined(HYTM_DEBUG_PRINT) || defined(HYTM_DEBUG_PRINT_LOCK)
        //        printf("id=%ld x=%ld y=%ld\n", *((long*) arg), x, y);
        //#endif
    }
}

void ntest1_validate() {
    for (int k = 0; k < globaln; ++k) {
        bool first = true;
        long val;
        for (int i = 0; i < CHUNK; ++i) {
            if (first) {
                val = globalarr[k * CHUNK + i];
                first = false;
            } else {
                if (val != globalarr[k * CHUNK + i]) {
                    cout << "TEST FAILED (expected "
                                << "globalarr[" << k << "*" << CHUNK << "+" << i << "]=" << val
                                << ", but got " << globalarr[k * CHUNK + i] << ")" << endl;
                    exit(-1);
                }
            }
        }
    }
    cout << "success." << endl;
    free(globalarr);

    //    cout<<"x="<<globalx<<" y="<<globaly<<"... ";
    //    if (globalx != globaly || globalx != globaln*globaliters) {
    //        cout<<"TEST FAILED (expected x=y="<<(globaln*globaliters)<<")"<<endl;
    //        exit(-1);
    //    }
    //    cout<<"success."<<endl;
}

void run_test(int n, void (*validate)(void), void *(*kernel)(void*)) {
    globaln = n;
    start = 0;

    pthread_t * pthreads[n];
    STM_THREAD_T * stmthreads[n];

    for (int i = 0; i < n; ++i) {
        pthreads[i] = (pthread_t*) malloc(sizeof (pthread_t));
        stmthreads[i] = STM_NEW_THREAD();
        STM_INIT_THREAD(stmthreads[i], i);
        pthread_create(pthreads[i], NULL, kernel, stmthreads[i]);
    }

    start = 1;
    __sync_synchronize();

    for (int i = 0; i < n; ++i) {
        pthread_join(*pthreads[i], NULL);
        STM_FREE_THREAD(stmthreads[i]);
        free(pthreads[i]);
    }

    validate();
}

//void bt_sighandler(int sig, struct sigcontext ctx) {
//
//    void *trace[16];
//    char **messages = (char **) NULL;
//    int i, trace_size = 0;
//
//    if (sig == SIGSEGV)
//        printf("Got signal %d, faulty address is %p, "
//                "from %p\n", sig, (void*) ctx.cr2, (void*) ctx.rip); //eip);
//    else
//        printf("Got signal %d\n", sig);
//
//    trace_size = backtrace(trace, 16);
//    /* overwrite sigaction with caller's address */
//    trace[1] = (void*) (long*) ctx.rip; //eip;
//    messages = backtrace_symbols(trace, trace_size);
//    /* skip first stack frame (points here) */
//    printf("[bt] Execution path:\n");
//    for (i = 1; i < trace_size; ++i) {
//        printf("[bt] #%d %s\n", i, messages[i]);
//
//        /* find first occurence of '(' or ' ' in message[i] and assume
//         * everything before that is the file name. (Don't go beyond 0 though
//         * (string terminator)*/
//        int p = 0;
//        while (messages[i][p] != '(' && messages[i][p] != ' '
//                    && messages[i][p] != 0)
//            ++p;
//
//        char syscom[256];
//        sprintf(syscom, "addr2line %p -e %.*s", trace[i], p, messages[i]);
//        //last parameter is the file name of the symbol
//        if (system(syscom) < 0) {
//            printf("ERROR: could not run necessary command to build stack trace\n");
//            exit(-1);
//        }
//    }
//
//    exit(0);
//}
//
//void initSighandler() {
//    /* Install our signal handler */
//    struct sigaction sa;
//
//    sa.sa_handler = (sighandler_t) /*(void *)*/ bt_sighandler;
//    sigemptyset(&sa.sa_mask);
//    sa.sa_flags = SA_RESTART;
//
//    sigaction(SIGSEGV, &sa, NULL);
//    sigaction(SIGUSR1, &sa, NULL);
//}

//void initSighandler();

int main(int argc, char** argv) {
    const int NPROCESSORS = 8;
//    initSighandler();
    STM_STARTUP();
//    cout<<"Main-thread test 0."<<endl;
//    test0(0);
//    test0(1);
//    test0(2);
//    test0(100);
//    test0(1000000);
//    test0(5000000);
//    test0(10000000);
    cout<<"HTM_ATTEMPT_THRESH="<<HTM_ATTEMPT_THRESH<<endl;
    cout<<"Spawned-thread test 0."<<endl;
    for (int n=1;n<=NPROCESSORS;++n) {
        cout<<n<<" threads"<<endl;
        ntest0_init(n, 0); run_test(n, ntest0_validate, ntest0_kernel);
        ntest0_init(n, 1); run_test(n, ntest0_validate, ntest0_kernel);
        ntest0_init(n, 10); run_test(n, ntest0_validate, ntest0_kernel);
        ntest0_init(n, 100); run_test(n, ntest0_validate, ntest0_kernel);
        ntest0_init(n, 1000); run_test(n, ntest0_validate, ntest0_kernel);
        ntest0_init(n, 10000); run_test(n, ntest0_validate, ntest0_kernel);
        ntest0_init(n, 100000); run_test(n, ntest0_validate, ntest0_kernel);
        if (n <= 8) { ntest0_init(n, 1000000/n); run_test(n, ntest0_validate, ntest0_kernel); }
        if (n <= 2) { ntest0_init(n, 10000000/n); run_test(n, ntest0_validate, ntest0_kernel); }
    }
    cout<<"Spawned-thread test 2."<<endl;
    for (int n=1;n<=NPROCESSORS;++n) {
        cout<<n<<" threads"<<endl;
        ntest2_init(n, 0); run_test(n, ntest2_validate, ntest2_kernel);
        ntest2_init(n, 1); run_test(n, ntest2_validate, ntest2_kernel);
        ntest2_init(n, 10); run_test(n, ntest2_validate, ntest2_kernel);
        ntest2_init(n, 100); run_test(n, ntest2_validate, ntest2_kernel);
        ntest2_init(n, 1000); run_test(n, ntest2_validate, ntest2_kernel);
        ntest2_init(n, 10000); run_test(n, ntest2_validate, ntest2_kernel);
        ntest2_init(n, 100000); run_test(n, ntest2_validate, ntest2_kernel);
        if (n <= 8) { ntest2_init(n, 1000000/n); run_test(n, ntest2_validate, ntest2_kernel); }
        if (n <= 2) { ntest2_init(n, 10000000/n); run_test(n, ntest2_validate, ntest2_kernel); }
    }
//    cout<<"Spawned-thread test 1."<<endl;
//    for (int n=1;n<=NPROCESSORS;++n) {
//        cout<<n<<" threads"<<endl;
//        ntest1_init(n, 0); run_test(n, ntest1_validate, ntest1_kernel);
//        ntest1_init(n, 1); run_test(n, ntest1_validate, ntest1_kernel);
//        ntest1_init(n, 10); run_test(n, ntest1_validate, ntest1_kernel);
//        ntest1_init(n, 100); run_test(n, ntest1_validate, ntest1_kernel);
//        ntest1_init(n, 1000); run_test(n, ntest1_validate, ntest1_kernel);
//        ntest1_init(n, 10000); run_test(n, ntest1_validate, ntest1_kernel);
//        ntest1_init(n, 100000); run_test(n, ntest1_validate, ntest1_kernel);
////        if (n <= 8) { ntest1_init(n, 1000000/n); run_test(n, ntest1_validate, ntest1_kernel); }
////        if (n <= 2) { ntest1_init(n, 10000000/n); run_test(n, ntest1_validate, ntest1_kernel); }
//    }

//    const int n = 2;
//    ntest0_init(n, 1000); run_test(n, ntest0_validate, ntest0_kernel);

    STM_SHUTDOWN();
    return 0;
}

