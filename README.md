# Software Artifacts #

A repository of software artifacts for my papers.
The papers that accompany these implementations are available at: http://tbrown.pro

### Java lock-free data structure library ###

* Introduced the first lock-free unbalanced binary search tree implementation.
* Introduced the first implementation of LLX and SCX synchronization primitives.
* Also introduced: k-ary search trees, relaxed AVL trees, Chromatic trees, b-slack trees.
* Includes experimental test harness with support for 13 competing data structures.
* Path to artifact: /java

### C++ lock-free data structure library ###

* Introduced the first C++ implementation of LLX and SCX synchronization primitives.
* Provides unbalanced BSTs and relaxed (a,b)-trees using LLX and SCX.
* Includes four different transactional memory based algorithms for each data structure.
* Path to artifact: /cpp/3path_htm

### Lock-free memory reclamation in C++ ###

* Provides a record manager library with allocation, reclamation and object pooling plugins (including five allocators and four memory reclamation algorithms).
* Memory reclamation algorithms include: hazard pointers, DEBRA, DEBRA+
* Includes lock-free BSTs and Chromatic trees implemented using this library.
* Path to artifact: /cpp/debra

### Reusable descriptors for lock-free data structures in C++ ###

* Provides a lock-free reusable descriptor library.
* Includes four advanced lock-free data structures accelerated using this library.
* Path to artifact: /cpp/weak_descriptors
* Coauthored with Maya Arbel-Raviv.

### Support for range query operations in C++ ###

* Provides three novel algorithms for adding range query operations to data structures.
* Includes seven different data structures augmented with range query support (up to five variants of each data structure).
* Includes an in-memory database (DBx1000) benchmark.
* Path to artifact: /cpp/range_queries
* Coauthored with Maya Arbel-Raviv.