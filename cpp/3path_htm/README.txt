This is a software artifact to accompany the paper:

A template for implementing fast lock-free trees using HTM.
Trevor Brown. PODC 2017.

In this artifact, I provide the following data structures:
1. Relaxed (a,b)-tree
    a. Lock-free implementation from LLX/SCX
    b. 2-path and 3-path HTM-based implementations
    c. TLE-based implementation
    d. Global locking implementation
    See abtree/abtree(_impl).h
2. Unbalanced external binary search tree
    a. Lock-free implementation from LLX/SCX
    b. 2-path and 3-path HTM-based implementations
    c. TLE-based implementation
    d. Global locking implementation
    See bst/bst(_impl).h

Memory reclamation is performed using DEBRA, a fast distributed epoch-based
memory reclamation scheme (PODC 2015).

A simple test harness is provided (main.cpp) along with a script to run some
experiments (scripts/run-all), and a script to take the results of those
experiments and turn them into a CSV file (scripts/format).

Compiling:
    1. Edit Makefile to appropriately set the PLAF variable on line 1.
    2. Run "make -j"

Compilation yields the following binaries:
    abtree-3path.out -- Lock-free, 2-path and 3-path (a,b)-tree implementations
    abtree-tle.out   -- TLE-based and global locking (a,b)-tree implementations
    bst-3path.out    -- Lock-free, 2-path and 3-path BST implementations
    bst-tle.out      -- TLE-based and global locking BST implementations
    bst-hytm.out     -- Hybrid transactional memory (Hybrid noREC) based BST

Each of these binaries takes the following command line arguments (in any order)
    -nrq NN         number of "range query" threads (which perform 100% RQs)
    -nwork NN       number of "worker" threads (which perform a mixed workload)
    -i NN           percentage of insertion operations for worker threads
    -d NN           percentage of deletion operations for worker threads
    -rq NN          percentage of range query operations for worker threads
                    (workers perform (100 - i - d - rq)% searches)
    -rqsize NN      maximum size of a range query (number of keys)
    -k NN           size of fixed key range
                    (keys for ins/del/search are drawn uniformly from [0, k).)
    -mr XX          memory reclamation scheme -- one of: "debra" or "none"
    -ma XX          memory allocator -- one of: "new" or "bump"
                    (new uses basic C++ new/delete keywords.
                     bump implements simple per-thread bump allocators.)
    -mp XX          object pools -- one of: "none" or "perthread_and_shared"
                    (the former does not use object pools.
                     the latter uses object pools, to the EXCLUSION of freeing
                     memory. in other words, memory is NEVER freed to the OS.)
    -t NN           number of milliseconds to run a trial
    -p              optional: if present, the trees will be prefilled to
                    contain 1/2 of key range [0, k) at the start of each trial.
    -htmfast NN     number of attempts to make on the FAST path
    -htmslow NN     number of attempts to make on the MIDDLE path
    -bind XX        optional: thread pinning/binding policy
                    XX is a comma separated list of logical processor indexes
                    or ranges of logical processors.
                    for example, "-bind 0-11,24-35,12-23,36-47" will cause
                    the first 48 threads to be pinned to logical processors:
                    0,1,2,3,4,5,6,7,8,9,1,11,24,25,26,27,...
                    if -bind is not specified, then threads will not be pinned.
                    if there are both "worker" and "range query" threads, then
                    worker threads are pinned first, followed by range query
                    threads.

Note that we include two scalable allocator implementations in lib/
These scalable allocators, tcmalloc and jemalloc, override C++ new/delete,
    so they can be used in a natural way with "-ma new".
To use jemalloc (RECOMMENDED):
    Prefix your command line with LD_PRELOAD=./lib/libjemalloc-5.0.1-25.so
To use tcmalloc:
    Prefix your command line with LD_PRELOAD=./lib/libtcmalloc-4.2.1.so

Running:

Command line arguments for:
    Workload: 25% ins, 25% del, 50% search, no RQs, key range [0, 10^6),
              running 1-second trials with prefilling,
              with 24 "worker" threads and NO "range query" threads,
              with threads pinned to logical processors 0-11 and 24-35,
    reclaiming memory with DEBRA,
    allocating with C++ new/delete,
    without object pooling

    "-i 25 -d 25 -rq 0 -rqsize 0 -k 1000000 -t 1000 -p -nwork 24 -nrq 0 -bind 0-11,24-35 -mr debra -ma new -mp none"

Now, I'll briefly explain how to specify which data structure to run.
Note that each binary implements several different variants of each data structure.
The arguments -htmfast and -htmslow are used to select the variant (since the
variants are essentially stripped down versions of the 3-path and TLE-based
algorithms that do not run certain code paths).

To run the 3-path BST with 20 attempts on FAST and 20 attempts on MIDDLE:
    $ bst-3path.out -htmfast 20 -htmslow 20 -i 25 -d 25 -rq 0 -rqsize 0 -k 1000000 -t 1000 -p -nwork 24 -nrq 0 -bind 0-11,24-35 -mr debra -ma new -mp none

To run the 2-path-non-concurrent BST with 20 attempts in HTM:
    $ bst-3path.out -htmfast 20 -htmslow -1 -i 25 -d 25 -rq 0 -rqsize 0 -k 1000000 -t 1000 -p -nwork 24 -nrq 0 -bind 0-11,24-35 -mr debra -ma new -mp none

To run the 2-path-concurrent BST with 20 attempts in HTM:
    $ bst-3path.out -htmfast -1 -htmslow 20 -i 25 -d 25 -rq 0 -rqsize 0 -k 1000000 -t 1000 -p -nwork 24 -nrq 0 -bind 0-11,24-35 -mr debra -ma new -mp none

To run the LLX/SCX-based BST:
    $ bst-3path.out -htmfast -1 -htmslow -1 -i 25 -d 25 -rq 0 -rqsize 0 -k 1000000 -t 1000 -p -nwork 24 -nrq 0 -bind 0-11,24-35 -mr debra -ma new -mp none

To run the TLE-based BST with 40 attempts in HTM:
    $ bst-3path-tle.out -htmfast 40 -htmslow -1 -i 25 -d 25 -rq 0 -rqsize 0 -k 1000000 -t 1000 -p -nwork 24 -nrq 0 -bind 0-11,24-35 -mr debra -ma new -mp none

To run the Global-locking BST:
    $ bst-3path-tle.out -htmfast -1 -htmslow -1 -i 25 -d 25 -rq 0 -rqsize 0 -k 1000000 -t 1000 -p -nwork 24 -nrq 0 -bind 0-11,24-35 -mr debra -ma new -mp none

To run the Hybrid noREC based BST (number of attempts in HTM is fixed at 20; define HTM_ATTEMPT_THRESH to change):
    $ bst-hytm.out -htmfast -1 -htmslow -1 -i 25 -d 25 -rq 0 -rqsize 0 -k 1000000 -t 1000 -p -nwork 24 -nrq 0 -bind 0-11,24-35 -mr debra -ma new -mp none

To generate data for a variety of workloads, data structures and allocators:
    $ cd scripts
    $ ./run-all
    $ ./format > data.csv

Bonus:
    A 3-path implementation of the RCU-based CITRUS tree is included in citrus/.
    This implementation uses 3-path techniques to accelerate RCU.
    With HTM, we can elide some RCU synchronize calls.
    For details, see the full paper on arXiv: https://arxiv.org/abs/1708.04838
