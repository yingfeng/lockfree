# PHYSICAL_PROCESSORS should contain the number of hardware threads in the system.
# E.g., if the system has 4 cores with 2 hyperthreads, each, then it should be 8.
SYSDEFS = -DPHYSICAL_PROCESSORS=8
# MAX_TID_POW2 must be at least n+1, where n is the largest number of threads you can use.
# It MUST be a power of two.
SYSDEFS += -DMAX_TID_POW2=128
SYSDEFS += -DVERBOSE=if\(1\)

CXXFLAGS = -g -std=c++11 -O3 $(SYSDEFS)
CXXFLAGS += -DNDEBUG
CXXFLAGS += -DDEBUG=if\(0\) -DDEBUG0=if\(0\) -DDEBUG1=if\(0\) -DDEBUG2=if\(0\) 
CXXFLAGS += -DMEMORY_STATS=if\(0\) -DMEMORY_STATS2=if\(0\)

LDFLAGS = -pthread -latomic

DEPS = main.cpp Makefile *.h recordmgr/*.h

all: bst chromatic test-record-manager

bst: bst-reclaim-none bst-reclaim-hazardptr bst-reclaim-debra bst-reclaim-debraplus
chromatic: chromatic-reclaim-none chromatic-reclaim-hazardptr chromatic-reclaim-debra chromatic-reclaim-debraplus

test-record-manager: test/record_manager.cpp Makefile *.h recordmgr/*.h
	$(CXX) test/record_manager.cpp -o $@ -g -std=c++11 -O3 $(SYSDEFS) $(LDFLAGS)

bst-reclaim-none: bst-reclaim-none-alloc-new-pool-none bst-reclaim-none-alloc-new-pool-ptas bst-reclaim-none-alloc-once-pool-none bst-reclaim-none-alloc-once-pool-ptas bst-reclaim-none-alloc-bump-pool-none bst-reclaim-none-alloc-bump-pool-ptas
bst-reclaim-hazardptr: bst-reclaim-hazardptr-alloc-new-pool-none bst-reclaim-hazardptr-alloc-new-pool-ptas bst-reclaim-hazardptr-alloc-once-pool-none bst-reclaim-hazardptr-alloc-once-pool-ptas bst-reclaim-hazardptr-alloc-bump-pool-none bst-reclaim-hazardptr-alloc-bump-pool-ptas
bst-reclaim-debra: bst-reclaim-debra-alloc-new-pool-none bst-reclaim-debra-alloc-new-pool-ptas bst-reclaim-debra-alloc-once-pool-none bst-reclaim-debra-alloc-once-pool-ptas bst-reclaim-debra-alloc-bump-pool-none bst-reclaim-debra-alloc-bump-pool-ptas
bst-reclaim-debraplus: bst-reclaim-debraplus-alloc-new-pool-none bst-reclaim-debraplus-alloc-new-pool-ptas bst-reclaim-debraplus-alloc-once-pool-none bst-reclaim-debraplus-alloc-once-pool-ptas bst-reclaim-debraplus-alloc-bump-pool-none bst-reclaim-debraplus-alloc-bump-pool-ptas

chromatic-reclaim-none: chromatic-reclaim-none-alloc-new-pool-none chromatic-reclaim-none-alloc-new-pool-ptas chromatic-reclaim-none-alloc-once-pool-none chromatic-reclaim-none-alloc-once-pool-ptas chromatic-reclaim-none-alloc-bump-pool-none chromatic-reclaim-none-alloc-bump-pool-ptas
chromatic-reclaim-hazardptr: chromatic-reclaim-hazardptr-alloc-new-pool-none chromatic-reclaim-hazardptr-alloc-new-pool-ptas chromatic-reclaim-hazardptr-alloc-once-pool-none chromatic-reclaim-hazardptr-alloc-once-pool-ptas chromatic-reclaim-hazardptr-alloc-bump-pool-none chromatic-reclaim-hazardptr-alloc-bump-pool-ptas
chromatic-reclaim-debra: chromatic-reclaim-debra-alloc-new-pool-none chromatic-reclaim-debra-alloc-new-pool-ptas chromatic-reclaim-debra-alloc-once-pool-none chromatic-reclaim-debra-alloc-once-pool-ptas chromatic-reclaim-debra-alloc-bump-pool-none chromatic-reclaim-debra-alloc-bump-pool-ptas
chromatic-reclaim-debraplus: chromatic-reclaim-debraplus-alloc-new-pool-none chromatic-reclaim-debraplus-alloc-new-pool-ptas chromatic-reclaim-debraplus-alloc-once-pool-none chromatic-reclaim-debraplus-alloc-once-pool-ptas chromatic-reclaim-debraplus-alloc-bump-pool-none chromatic-reclaim-debraplus-alloc-bump-pool-ptas

bst-reclaim-none-alloc-new-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_none -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-none-alloc-new-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_none -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-none-alloc-once-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_none -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-none-alloc-once-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_none -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-none-alloc-bump-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_none -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-none-alloc-bump-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_none -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
    
bst-reclaim-hazardptr-alloc-new-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_hazardptr -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-hazardptr-alloc-new-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_hazardptr -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-hazardptr-alloc-once-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_hazardptr -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-hazardptr-alloc-once-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_hazardptr -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-hazardptr-alloc-bump-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_hazardptr -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-hazardptr-alloc-bump-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_hazardptr -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)

bst-reclaim-debra-alloc-new-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_debra -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-debra-alloc-new-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_debra -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-debra-alloc-once-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_debra -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-debra-alloc-once-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_debra -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-debra-alloc-bump-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_debra -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-debra-alloc-bump-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_debra -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)

bst-reclaim-debraplus-alloc-new-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_debraplus -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-debraplus-alloc-new-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_debraplus -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-debraplus-alloc-once-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_debraplus -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-debraplus-alloc-once-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_debraplus -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-debraplus-alloc-bump-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_debraplus -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
bst-reclaim-debraplus-alloc-bump-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=BST -DRECLAIM_TYPE=reclaimer_debraplus -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)

chromatic-reclaim-none-alloc-new-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_none -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-none-alloc-new-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_none -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-none-alloc-once-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_none -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-none-alloc-once-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_none -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-none-alloc-bump-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_none -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-none-alloc-bump-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_none -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
    
chromatic-reclaim-hazardptr-alloc-new-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_hazardptr -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-hazardptr-alloc-new-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_hazardptr -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-hazardptr-alloc-once-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_hazardptr -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-hazardptr-alloc-once-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_hazardptr -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-hazardptr-alloc-bump-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_hazardptr -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-hazardptr-alloc-bump-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_hazardptr -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)

chromatic-reclaim-debra-alloc-new-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_debra -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-debra-alloc-new-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_debra -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-debra-alloc-once-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_debra -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-debra-alloc-once-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_debra -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-debra-alloc-bump-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_debra -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-debra-alloc-bump-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_debra -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)

chromatic-reclaim-debraplus-alloc-new-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_debraplus -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-debraplus-alloc-new-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_debraplus -DALLOC_TYPE=allocator_new -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-debraplus-alloc-once-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_debraplus -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-debraplus-alloc-once-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_debraplus -DALLOC_TYPE=allocator_once -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-debraplus-alloc-bump-pool-none: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_debraplus -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_none $(CXXFLAGS) $(LDFLAGS)
chromatic-reclaim-debraplus-alloc-bump-pool-ptas: $(DEPS)
	$(CXX) main.cpp -o $@ -DDATA_STRUCTURE=Chromatic -DRECLAIM_TYPE=reclaimer_debraplus -DALLOC_TYPE=allocator_bump -DPOOL_TYPE=pool_perthread_and_shared $(CXXFLAGS) $(LDFLAGS)
