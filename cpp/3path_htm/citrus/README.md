# Predicate RCU

Predicate RCU (PRCU) is a variant of [RCU](http://en.wikipedia.org/wiki/Read-copy-update)
offering a `wait-for-readers` (aka `synchronize_rcu`) operation that
can complete 10x-100x faster than standard RCU's `wait-for-readers`.
PRCU achieves this by allowing `wait-for-readers` to wait only for the 
completion of a *subset* of pre-existing RCU read-side critical sections,
instead of *all* readers.  The subset is specified using a user-defined
_predicate_.  (Some PRCU variants require an _iterable predicate_ 
representation, which is described later.)

PRCU thus facilitates using RCU as a general-purpose synchronization
mechanism.  PRCU is particularly suitable for algorithms with scalable,
fine-grained, concurrent update operations that have `wait-for-readers`
on their critical path.  In such cases standard `wait-for-readers`
quickly becomes a dominating bottleneck, but it is often possible to
define predicates so that `wait-for-readers` times drastically decrease
and remove the bottleneck.

PRCU is described in detail in the paper 

   [Predicate RCU: An RCU for Scalable Concurrent Updates][1], PPoPP 2015  
   [Maya Arbel](http://www.cs.technion.ac.il/~mayaarl) and [Adam Morrison](http://www.cs.technion.ac.il/~mad) (Technion)


## PRCU implementations

Running `make` builds libraries implementing the three PRCU algorithms
described in the paper:

- *EER-PRCU* evaluates the predicate for each reader.  Compared to [Userspace RCU](http://urcu.so/), `wait-for-readers` time decreases by 10x and read overhead is comparable.

- *D-PRCU* exploits the domain of values, requires an _iterable predicate_ (see below) for good performance.  `wait-for-readers` time decreases by 100x, but read overhead increases, especially if readers specify the same values.

- *DEER-PRCU* uses the domain of data structure values, but waits for each reader.  Requires an iterable predicate for good performance.  Compared to Userspace RCU, `wait-for-readers` time decreases by 10x and read overhead is comparable.


## Using PRCU

We follow the API of the paper: 

   1. Initialize RCU by calling `prcu_init(int num_threads)`.
   2. Each thread must call `prcu_register(int id)`, where `id` is an integer from the range `{0,..., num_threads-1}`.
   3. Read-side critical sections are encapsulated in `prcu_enter(int value)`/`prcu_exit(int value)`, where `value` is what the predicate evaluates.
   4. `wait-for-readers` is implemented via a `prcu_wait_for_readers(predicate pred, int min, range_predicate pred_next, predicate_info pred_info)` call, where `pred` is a regular (non-iterable predicate), `min` is the first buckets, `pred_next` is the predicate bucket iterator starting at `min` (see below).


## Iterable predicates

D-PRCU and DEER-PRCU use an _iterable predicate_, which is represented as the set
of values for which it holds.  (As opposed to a function taking a value and
returning true/false.)  Our representation of iterable predicates consists of
a `min` bucket---the first hash bucket for which the predicate holds---and a
`pred_next` function that implements an iterator over the buckets for which
the predicate holds, starting at `min`.

Technically, `pred_next` takes a current bucket and the `pred_info` structure,
and return the next bucket.  The `pred_info` can be used to additional data
from the caller of `wait-for-readers` to `pred_next`.  See the comments in 
`prcu.h` for further information.


## PRCU example: Citrus

As an example of using PRCU, we provide an implementation of the Citrus RCU-based
concurrent binary search tree that is modified to work with PRCU (`citrus.c`).
The Citrus tree implementation provides the standard set `insert`/`delete`/`contains`
API.  It can also be compiled with the Userspace RCU library by defining `EXTERNAL_RCU`.

The Citrus tree algorithm is described in detail in the following paper:

   [Concurrent Updates with RCU: Search Tree as an Example][2], PODC 2014  
   [Maya Arbel](http://www.cs.technion.ac.il/~mayaarl) and [Hagit Attiya](http://www.cs.technion.ac.il/~hagit) (Technion)

The Citrus code contains an example an iterable predicate.


### Citrus usage tips

   1. Initialize the tree by calling `init(int max_key)`, where `max_key` is the largest key used by any operation.  If unknown use a negative number to use the default max integer value.  
   2. Initialize PRCU and register each thread, as described above.
   3. Run with a scalable memory allocator, for example [jemalloc](www.canonware.com/jemalloc).


[1]: http://www.cs.technion.ac.il/~mayaarl/predicateRCU-camera-ready.pdf
[2]: http://www.cs.technion.ac.il/~mayaarl/podc047f.pdf

## License
Copyright 2015 
Maya Arbel (mayaarl [at] cs [dot] technion [dot] ac [dot] il).
Adam Morrison (mad [at] cs [dot] technion [dot] ac [dot] il). 

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>