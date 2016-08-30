This is a complete Java test harness capable of running all experiments for the
paper "A general technique for non-blocking trees." Shell scripts to compile
and run the experiments are included for Linux / Solaris.

COMPILING AND RUNNING:

To make the scripts executable, execute:
  chmod 755 compile, merge, run-experiments

Before you can use them, you must edit "compile" and "run-experiments" to
reflect the appropriate path to your "java", "javac" and "jar" binaries.

To compile, execute:
  ./compile

This will show some warnings about the deprecated Java Unsafe API.
This API is needed by the SkipTree data structure.

To run a quick test, execute:
  java -server -d64 -Xms3G -Xmx3G -Xbootclasspath/p:'./lib/scala-library.jar' -jar build/experiments.jar 8 5 2 Chromatic -param-6 -ins50 -del50 -keys1048576 -prefill -file-data-temp.csv

To run an STM-based algorithm, you must include deuceAgent.jar in the boot classpath and run the JAR that is instrumented for transactions, instead:
  java -server -d64 -Xms3G -Xmx3G -Xbootclasspath/p:'./lib/scala-library.jar:./lib/deuceAgent.jar' -jar build/experiments_instr.jar 8 5 2 RBSTM -param-norec -ins50 -del50 -keys16384 -prefill -file-data-temp.csv

The output of this run will appear in a file called data-temp.csv in the
root directory for the project.

To run experiments, execute:
  ./run-experiments

Output CSV files appear in the directory "build".
One CSV file is created per experiment (workload * key range * algorithm).
These CSV files can be merged into one file "data.csv" by executing:
  ./merge build/*.csv

INCLUDED DATA STRUCTURES:

BST             An implementation of the non-blocking BST of Ellen, Fatourou,
                Ruppert and val Breugel.
KST             A non-blocking k-ary search tree described in the paper
                "Non-blocking k-ary search trees" by Brown and Helga.
                This data structure requires a command line parameter, e.g.,
                "-param-4" or "-param-8" to specify the degree, k, of nodes.
4-ST            An implementation of the KST optimized for k=4.
Chromatic       A non-blocking chromatic tree described in the paper "A general
                technique for non-blocking trees" by Brown, Ellen and Ruppert.
                This data structure accepts a command line parameter.
                With "-param-0", after a thread performs an insert or delete,
                it performs rebalancing steps to fix any imbalance it created.
                With "-param-6", after a thread performs an insert or delete,
                it performs rebalancing steps only if it saw more than six
                "violations" (which indicate imbalance) as it traversed the
                tree to reach node where it inserted or deleted a key.
                This parameter accepts any integer value.
                The experiments in the paper use parameter values 0 and 6.
LockFreeAVL     A non-blocking relaxed AVL tree briefly mentioned in the same
                paper as Chromatic. Like Chromatic, this accepts a parameter
                (which serves the same purpose). Experiments showed 15 to be
                a good choice for this parameter.
AVL             The leading lock-based AVL tree, which is described in the
                paper "A practical concurrent binary search tree" by Bronson,
                Casper, Chafi and Olukotun.
Snap            A slightly slower variant of AVL that supports clone().
SkipList        The non-blocking skip list of the Java library. This is the
                class "java.util.concurrent.ConcurrentSkipListMap".
SkipTree        A non-blocking multiway search tree that combines elements of
                B-trees and skip lists. It is described in the paper
                "Lock-free multiway search trees" by Spiegel.
RBUnsync        This is a red black tree developed by Herlihy and Oracle to
                demonstrate the performance of software transactional memory.
                This particular version has no synchronization whatsoever.
                It is a sequential data structure and is NOT thread safe.
RBLock          Same as RBUnsync, but uses Java synchronized blocks to
                guarantee thread safety.
RBSTM           Same as RBLock, but uses DeuceSTM 1.3 instead of locks. At time
                of writing, DeuceSTM is the fastest Java STM that does not
                require modifications to the JVM.
                This algorithm (and all others that use DeuceSTM) require the
                user to specify the STM algorithm to use as a parameter on the
                command line by adding "-param-X" where X is an algorithm in
                the set {tl2, lsa, tl2cm, lsacm, norec}.
SkipListSTM     A skip list implemented using DeuceSTM 1.3.
Ctrie           A non-blocking concurrent hash trie, which is described in the
                paper "Concurrent tries with efficient non-blocking snapshots"
                by Prokopec, Bronson, Bagwell and Odersky.
                This uses hashing, and implements an unordered dictionary.
ConcurrentHMAP  java.util.concurrent.ConcurrentHashMap
SyncTMAP        java.util.TreeMap wrapped in synchronized blocks
TMAP            java.util.TreeMap
HMAP            java.util.HashMap

ADDING YOUR OWN DATA STRUCTURE(S):
  
It is easy to extend the test harness to run experiments using your own data
structure(s). To add a new data structure, there are three simple steps:
1. Copy an adapter class in "src/adapters" (such as "OptTreeAdapter.java") and
   implement the method stubs using your data structure.
   add() and remove() should return true if they changed the data structure,
   and false otherwise.
   get() should return the key it found, or null if none was found.
   contains() should return true if the key is in the data structure, and
   false otherwise.
   size() and sequentialSize() must both return the number of keys in the data
   structure. You can assume no concurrent operations occur when they are
   executing. There is no difference between these methods in this package.
   addListener() should be left empty, and getRoot() should simply return null.
   They both provide functionality that is not included in this package.
   If your data structure is not a tree, or you do not care about its depth,
   then getSumOfDepths() should simply return 0.
   Otherwise, it should return the sum, over every key k, of the depth of the
   node containing k (that would be found by a contains() or get() operation).
2. Copy one of the private factory classes in "src/main/Main.java" and
   implement it using your data structure.
   The newTree() method should return a new instance of the adapter for your
   data structure.
   The getName() method should return a string (with no spaces) that represents
   your data structure. This string is what you will provide to the test
   harness in "run-experiments" so that it uses your data structure.
3. Add an instance of your factory to the "factories" ArrayList near the top of
   "src/main/Main.java".

Finally, it is easy to edit "run-experiments" to include your data structure.
