This is a software artifact to accompany the paper:

Reuse, don't recycle: transforming lock-free algorithms that throw away descriptors.
Maya Arbel-Raviv and Trevor Brown. DISC 2017.

In this artifact, we provide the following data structures:
1. Unbalanced external binary search tree
    a. Implementation with throwaway descriptors
       (./bst_throwaway/)
    b. Implementation with weak (reusable) descriptors
       (./bst/)
2. Relaxed (a,b)-tree
    a. Implementation with throwaway descriptors
       (./bslack_throwaway/ compiled with -DUSE_SIMPLIFIED_ABTREE_REBALANCING)
    b. Implementation with weak (reusable) descriptors
       (./bslack_reuse/ compiled with -DUSE_SIMPLIFIED_ABTREE_REBALANCING)
3. Relaxed b-slack tree
    a. Implementation with throwaway descriptors
       (./bslack_throwaway/)
    b. Implementation with weak (reusable) descriptors
       (./bslack_reuse/)
4. Atomic k-word compare-and-swap (k-CAS)
    a. Implementation with throwaway descriptors
       (./kcas/kcas_throwaway.h)
    b. Implementation with weak (reusable) descriptors
       (./kcas/kcas_reuse.h)

The throwaway descriptor implementations have various choices of
memory reclamation schemes:
    1. DEBRA: distributed epoch based memory reclamation
    2. HP: Hazard pointers
    3. RCU: read-copy update
    4. None: leaking descriptors

Two simple test harnesses are provided
    - main.cpp (for data structures 1-3)
      a set/dictionary microbenchmark
    - kcas/ubench.cpp (for data structure 4)
      an array-based k-cas benchmark microbenchmark
We also provide scripts to generate many experiments from the paper
    - scripts/kcas-reuse-vs-throw
    - reuse-vs-throw
and scripts to produce CSV files from the results of those experiments
    - scripts/kcas-reuse-vs-throw.format
    - reuse-vs-throw.format

Compiling:
    1. Edit Makefile to appropriately set the PLAF variable on line 1.
    2. Run "make -j"

Compilation yields the following binaries:
    bst_throwaway.out                       -- Implementation 1a
    bst_throwaway_rcu.out                   -- Implementation 1a with
                                               descriptor reclamation using RCU
    bst_reuse.out                           -- Implementation 1b
    bst_reuse_rcu.out                       -- Implementation 1b with
                                               descriptor reclamation using RCU
    abtree_throwaway.out                    -- Implementation 2a
    abtree_reuse.out                        -- Implementation 2b
    bslack_throwaway.out                    -- Implementation 3a
    bslack_reuse.out                        -- Implementation 3b
    kcas_throwaway_debra_nopool_k2.out      -- Implementation 4a (2-CAS) with
                                               descriptor reclamation using DEBRA
    kcas_throwaway_debra_nopool_k16.out     -- Implementation 4a (16-CAS) with
                                               descriptor reclamation using DEBRA
    kcas_throwaway_hazardptr_nopool_k2.out  -- Implementation 4a (2-CAS) with
                                               descriptor reclamation using HP
    kcas_throwaway_hazardptr_nopool_k16.out -- Implementation 4a (16-CAS) with
                                               descriptor reclamation using HP
    kcas_throwaway_none_nopool_k2.out       -- Implementation 4a (2-CAS) with
                                               no descriptor reclamation
    kcas_throwaway_none_nopool_k16.out      -- Implementation 4a (16-CAS) with
                                               no descriptor reclamation
    kcas_throwaway_rcu_nopool_k2.out        -- Implementation 4a (2-CAS) with
                                               descriptor reclamation using RCU
    kcas_throwaway_rcu_nopool_k16.out       -- Implementation 4a (16-CAS) with
                                               descriptor reclamation using RCU
    kcas_reuse_k2.out                       -- Implementation 4b
    kcas_reuse_k16.out                      -- Implementation 4b

The binaries for data structures 1-3 take the following arguments (in any order)
    -nrq NN         number of "range query" threads (which perform 100% RQs)
    -nwork NN       number of "worker" threads (which perform a mixed workload)
    -i NN           percentage of insertion operations for worker threads
    -d NN           percentage of deletion operations for worker threads
    -rq NN          percentage of range query operations for worker threads
                    (workers perform (100 - i - d - rq)% searches)
    -rqsize NN      maximum size of a range query (number of keys)
    -k NN           size of fixed key range
                    (keys for ins/del/search are drawn uniformly from [0, k).)
    -mr XX          memory reclamation scheme -- one of: "debra", "rcu", "none"
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

The binaries for data structure 4 take the following arguments (in any order)
    -n NN           number of threads performing k-cas operations
    -k NN           size of the array on which k-cas operations are performed
    -t NN           number of milliseconds to run a trial
    -bind XX        optional: thread pinning/binding policy (see above)

Note that we include a scalable allocator implementation in lib/
This scalable allocator, jemalloc, overrides C++ new/delete,
    so it can be used in a natural way with "-ma new".
To use jemalloc (RECOMMENDED):
    Prefix your command line with LD_PRELOAD=./lib/libjemalloc-5.0.1-25.so

Running:

Command line arguments for example workload for data structures 1-3:
    25% ins, 25% del, 50% search, no RQs, key range [0, 10^6),
    running 1-second trials with prefilling,
    with 24 "worker" threads and NO "range query" threads,
    with threads pinned to logical processors 0-11 and 24-35,
    reclaiming nodes and descriptors with DEBRA,
    allocating with C++ new/delete,
    without object pooling

    "-i 25 -d 25 -rq 0 -rqsize 0 -k 1000000 -t 1000 -p -nwork 24 -nrq 0 -bind 0-11,24-35 -mr debra -ma new -mp none"

Example: run the BST with throwaway descriptors
    $ bst_throwaway.out -i 25 -d 25 -rq 0 -rqsize 0 -k 1000000 -t 1000 -p -nwork 24 -nrq 0 -bind 0-11,24-35 -mr debra -ma new -mp none

Example: run the BST with RCU-based memory reclamation:
    $ bst_throwaway_rcu.out -i 25 -d 25 -rq 0 -rqsize 0 -k 1000000 -t 1000 -p -nwork 24 -nrq 0 -bind 0-11,24-35 -mr rcu -ma new -mp none
    Note: for the BST, RCU is compiled in a separate binary (and we must set "-mr rcu")

Example: run the BST with weak (reusable) descriptors, with node reclamation using DEBRA
    $ bst_reuse.out -i 25 -d 25 -rq 0 -rqsize 0 -k 1000000 -t 1000 -p -nwork 24 -nrq 0 -bind 0-11,24-35 -mr debra -ma new -mp none

Command line arguments for example workload for data structure 4:
    24 threads, running 1-second trials, in an array of size 2^20

    "-n 24 -t 1000 -k 1048576"

Example: run a 16-CAS experiment with descriptor reclamation using DEBRA
    $ kcas_throwaway_debra_nopool_k16.out -n 24 -t 1000 -k 1048576

Example: run a 2-CAS experiment with weak (reusable) descriptors
    $ kcas_reuse_k2.out -n 24 -t 1000 -k 1048576

To generate data for a variety of workloads, data structures and allocators:
    $ cd scripts
    $ ./reuse-vs-throw
    $ ./reuse-vs-throw.format `hostname`
      (see output file: ../`hostname`.output.reuse-vs-throw/reuse-vs-throw.csv)
    $ ./kcas-reuse-vs-throw
    $ ./kcas-reuse-vs-throw.format `hostname`
      (see output file: ../`hostname`.output.kcas-reuse-vs-throw/kcas-reuse-vs-throw.csv)

If you get warnings at the end of reuse-vs-throw.format or kcas-reuse-vs-throw.format,
typically you don't need to worry. I just have the scripts emit warnings for any
text field that I expected to find using grep, but could not find. There are several
reasons why this can happen. The most obvious one is, in the benchmark code,
I sometimes omit some output lines when they are zero.
(This makes the output a bit more human friendly, but messes with parsing.)