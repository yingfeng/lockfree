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



### How do I get set up? ###

* Summary of set up
* Configuration
* Dependencies
* Database configuration
* How to run tests
* Deployment instructions

### Contribution guidelines ###

* Writing tests
* Code review
* Other guidelines

### Who do I talk to? ###

* Repo owner or admin
* Other community or team contact