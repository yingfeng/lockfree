##
# C++ implementation of lock-free chromatic tree using LLX/SCX and DEBRA(+).
# Copyright (C) 2016 Trevor Brown
##

################################################################################
Getting up and running
################################################################################

- Edit the "SYSDEFS" variable in the Makefile to match your system.
- Compile using "make -j". This will compile all binaries in parallel, and will
  produce executable files in the following format.
  (bst|chromatic)-reclaim-(none|hazardptr|debra|debraplus)-alloc-(new|once|bump)-pool-(none|ptas)
- This code includes both an unbalanced BST (bst.h and bst_impl.h),
  and a balanced BST (chromatic.h and chromatic_impl.h)
- For a quick test, run:
    "./chromatic-reclaim-debraplus-alloc-new-pool-ptas -p -i 25 -d 25 -k 10000 -n 8 -t 2000"
- To use the unbalanced BST instead of the (Chromatic) balanced BST, use the
  binaries beginning with "bst".
- Ideally, a fast and scalable allocator should be used.
  On many systems, this will greatly improve performance.
  Included with this project is jemalloc 4.2.1 (see lib/libjemalloc.*).
  On Linux, it can be used to replace the system malloc/free as follows.
  "env LD_PRELOAD=./lib/libjemalloc.so ./chromatic-reclaim-debraplus-alloc-new-pool-ptas -p -i 25 -d 25 -k 10000 -n 8 -t 2000"
- The script "scripts/run-experiments.sh" runs and produces data for a sequence
  of experiments. It should be straightforward to edit the script to run many
  typical microbenchmarks that you might want to run. The script produces one
  file in "output/" for each run.
- The results produced by "run-experiments.sh" can be transformed into CSV
  format using "scripts/create-csv.sh". (See the script for instructions.)

################################################################################
Command-line arguments for harness-o3
################################################################################

-p      prefill the tree to half full
-i #    percentage of operations that should be insertion
-d #    percentage of operations that should be deletion (the remainder are searches)
-k #    size of key range (threads draw uniform random keys from [0, k))
-n #    number of threads
-t #    milliseconds to run

Regarding the allocator options:
- new (class allocator_new) is simply a wrapper for the C++ "new" operator
- once (class allocator_once) allocates one huge slab for each thread at the
  beginning of the execution and eahc thread bump allocates from its slab.
  If a thread exhausts its slab, then allocation returns NULL, and an error
  message is emitted. The execution then segfaults, because of the NULL pointer.
- bump (class allocator_bump) allocates a small slab (a few MB) for each thread
  and each thread bump allocates from its current slab. When a thread exhausts
  its slab, it allocates a new slab.

Regarding the pool options:
- ptas (class pool_per_thread_and_shared) causes retired records
  to be cached in thread-local pools and reused instead of being released to the
  operating system. If a per-thread pool gets too large, blocks of records are
  sent to a shared pool. When a per-thread pool is empty, a thread first checks
  the shared pool to see if it can take a block of records from there. If not,
  the thread goes to the allocator to allocate a new record.
- none (class pool_none) causes records to be deallocated (returned to the OS)
  instead of being placed in a pool.

The reclaimer options are described in the paper, so we describe them only
briefly, here:
- none (class reclaimer_none) simply leaks memory instead of reclaiming it
- hazardptr (class reclaimer_hazardptr) uses hazard pointers
- debra (class reclaimer_debra) uses DEBRA
- debraplus (class reclaimer_debraplus) uses DEBRA+ (which is fault tolerant)

If you want no memory reclamation, you should use the binaries of the form:
  "(bst|chromatic)-reclaim-none-alloc-(new|once|bump)-pool-none"

This option leaks memory, since allocated records are never reused or freed.
Consequently, you may quickly run out of memory, unless the rate at which
memory is allocated is fairly slow in your workload.

Although there is a lot of code in bst_impl.h and chromatic_impl.h pertaining to
memory reclamation (and crash recovery), this code should cause no runtime
overhead if you opt not to reclaim memory, because all calls to functions of the
record manager should be statically compiled out.

################################################################################
Using a record manager
################################################################################

In order to use the record manager abstraction and any of the reclaimer,
allocator, and pool options listed above, a data structure should include
"record_manager.h", and create an instance of the class record_manager,
specifying the desired reclaimer, allocator and pool types, respectively,
as the first three template arguments.
After these three template arguments, one can specify any number of record types
to be allocated and reclaimed by the record manager.
For example, one might declare a reclaimer for a tree of Node records and
Descriptor records as follows.

record_manager<reclaimer_debraplus<>, allocator_new<>, pool_perthread_and_shared<>,
        Node, Descriptor> manager;

If one would like to be able to interchange different record managers for the same
data structure, then one can make the record manager type a template parameter
for the data structure.

See chromatic.h and bst.h for two examples of (similar) data structures using
a record manager. Each data structure takes a record manager type as a template
parameter.

Then, the data structure should use the operations provided by the record manager,
as described in the paper.

################################################################################
Using DEBRA (without crash recovery)
################################################################################

To use DEBRA, this simply involves invoking leaveQuiescentState() at the start
of each data structure operation, enterQuiescentState() at the end of each
data structure operation, and retire() each time a record is removed from the
data structure (meaning it is no longer reachable by following pointers from
an entry point). Records are then reclaimed at some point after they are passed
to retire().

The only assumption made by DEBRA is that threads do not hold pointers to
records in the data structure (except for entry points) between operations.
This assumption, which is made by all epoch based reclamation schemes,
is described in more detail in the paper.

If you know that no thread will ever access a particular record, you can invoke
deallocate() instead of retire(), which will immediately reclaim the record.

Note: unlike deallocate(), retire() does NOT require you to know that a record
will no longer be accessed by any thread. It simply requires you to know that
the record has been removed from the data structure.

Adding crash recovery, so that one can use DEBRA+, is straightforward for a
large class of data structures. See the paper for details.