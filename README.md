# Concurrent data structures #

A repository of some of my data structure implementations in Java and C++, and test harnesses for running microbenchmarks.

### Lock-free chromatic tree ###

* Available for both Java and C++
* C++ version has various memory reclamation options implemented, including hazard pointers, DEBRA and DEBRA+. 
* Path to Java version: java/src/algorithms/published/ConcurrentChromaticTreeMap*
* Path to C++ version: cpp/debra/chromatic*

### Lock-free relaxed AVL tree ###

* Available in Java
* Path: java/src/algorithms/published/ConcurrentRelaxedAVLMap*

### Lock-free BST of Ellen et al. ###

* Available for both Java and C++
* C++ version has various memory reclamation options implemented, including hazard pointers, DEBRA and DEBRA+. 
* Path to Java version: java/src/algorithms/published/ConcurrentBSTMap*
* Path to C++ version: cpp/debra/bst*

### Lock-free k-ary search tree ###

* Available in Java
* Also implements range queries (subSet())
* Path: java/src/algorithms/published/LockFreeKSTRQ*
* Optimized version for k=4 with no range query operation: java/src/algorithms/published/LockFree4ST*

### Java test harness ###

* Comes with support for a large number of data structure implementations
* Supports range queries for some data structures
* See README in java/

### C++ test harness ###

* Can easily swap in several memory allocation and reclamation schemes
* Comes with support for the lock-free BST and Chromatic trees
* See README in cpp/debra/

### The DEBRA(+) memory reclamation algorithm ###

* See README in cpp/debra/

### Repository contact ###

Trevor Brown (me [at] tbrown [dot] pro)

The papers that accompany these implementations are available at: http://tbrown.pro