## define the following to be >= the # of logical processors in your system
PLAF = -DLOGICAL_PROCESSORS=128

GPP = g++
FLAGS = -std=c++0x -mcx16
FLAGS += -g
FLAGS += $(PLAF)
FLAGS += -DNDEBUG
FLAGS += -DDEBUG=if\(0\) -DDEBUG1=if\(0\) -DDEBUG2=if\(0\)
FLAGS += -DMEMORY_STATS=if\(1\) -DMEMORY_STATS2=if\(1\)
LDFLAGS = -L./lib
LDFLAGS += -I. -I./include -I./common -I./common/descriptors -I./common/recordmgr -I./kcas
LDFLAGS += -lpthread
LDFLAGS += -lurcu
machine=$(shell hostname)

## the following two lines are optional -- comment out to disable PAPI performance measurements
FLAGS += -DUSE_PAPI
LDFLAGS += -lpapi

all: kcas bst abtree bslack

kcas: kcas16-throwaway-rcu kcas16-throwaway-debra kcas16-throwaway-hazardptr kcas16-throwaway-none kcas16-reuse kcas2-throwaway-rcu kcas2-throwaway-debra kcas2-throwaway-hazardptr kcas2-throwaway-none kcas2-reuse
kcas16-throwaway-rcu:
	$(GPP) $(FLAGS) -o $(machine).kcas_throwaway_rcu_nopool_k16$(delaysuffix).out $(delayarg) -DUSE_RECLAIMER_RCU -DRECLAIM=rcu -DPOOL=none -DKCAS_MAXK=16 -DKCAS_MAXTHREADS=64 -O3 $(xargs) $(pinning) -DTHROWAWAY kcas/ubench.cpp $(LDFLAGS)
kcas16-throwaway-debra:
	$(GPP) $(FLAGS) -o $(machine).kcas_throwaway_debra_nopool_k16$(delaysuffix).out $(delayarg) -DRECLAIM=debra -DPOOL=none -DKCAS_MAXK=16 -DKCAS_MAXTHREADS=64 -O3 $(xargs) $(pinning) -DTHROWAWAY kcas/ubench.cpp $(LDFLAGS)
kcas16-throwaway-hazardptr:
	$(GPP) $(FLAGS) -o $(machine).kcas_throwaway_hazardptr_nopool_k16$(delaysuffix).out $(delayarg) -DRECLAIM=hazardptr -DPOOL=none -DKCAS_MAXK=16 -DKCAS_MAXTHREADS=64 -O3 $(xargs) $(pinning) -DTHROWAWAY kcas/ubench.cpp $(LDFLAGS)
kcas16-throwaway-none:
	$(GPP) $(FLAGS) -o $(machine).kcas_throwaway_none_nopool_k16$(delaysuffix).out $(delayarg) -DRECLAIM=none -DPOOL=none -DKCAS_MAXK=16 -DKCAS_MAXTHREADS=64 -O3 $(xargs) $(pinning) -DTHROWAWAY kcas/ubench.cpp $(LDFLAGS)
kcas16-reuse:
	$(GPP) $(FLAGS) -o $(machine).kcas_reuse_k16$(delaysuffix).out -DRECLAIM=none $(delayarg) -DPOOL=none -DKCAS_MAXK=16 -DKCAS_MAXTHREADS=64 -O3 $(xargs) $(pinning) kcas/ubench.cpp $(LDFLAGS)
kcas2-throwaway-rcu:
	$(GPP) $(FLAGS) -o $(machine).kcas_throwaway_rcu_nopool_k2$(delaysuffix).out $(delayarg) -DUSE_RECLAIMER_RCU -DRECLAIM=rcu -DPOOL=none -DKCAS_MAXK=2 -DKCAS_MAXTHREADS=64 -O3 $(xargs) $(pinning) -DTHROWAWAY kcas/ubench.cpp $(LDFLAGS)
kcas2-throwaway-debra:
	$(GPP) $(FLAGS) -o $(machine).kcas_throwaway_debra_nopool_k2$(delaysuffix).out $(delayarg) -DRECLAIM=debra -DPOOL=none -DKCAS_MAXK=2 -DKCAS_MAXTHREADS=64 -O3 $(xargs) $(pinning) -DTHROWAWAY kcas/ubench.cpp $(LDFLAGS)
kcas2-throwaway-hazardptr:
	$(GPP) $(FLAGS) -o $(machine).kcas_throwaway_hazardptr_nopool_k2$(delaysuffix).out $(delayarg) -DRECLAIM=hazardptr -DPOOL=none -DKCAS_MAXK=2 -DKCAS_MAXTHREADS=64 -O3 $(xargs) $(pinning) -DTHROWAWAY kcas/ubench.cpp $(LDFLAGS)
kcas2-throwaway-none:
	$(GPP) $(FLAGS) -o $(machine).kcas_throwaway_none_nopool_k2$(delaysuffix).out $(delayarg) -DRECLAIM=none -DPOOL=none -DKCAS_MAXK=2 -DKCAS_MAXTHREADS=64 -O3 $(xargs) $(pinning) -DTHROWAWAY kcas/ubench.cpp $(LDFLAGS)
kcas2-reuse:
	$(GPP) $(FLAGS) -o $(machine).kcas_reuse_k2$(delaysuffix).out $(delayarg) -DRECLAIM=none -DPOOL=none -DKCAS_MAXK=2 -DKCAS_MAXTHREADS=64 -O3 $(xargs) $(pinning) kcas/ubench.cpp $(LDFLAGS)

.PHONY: $(machine).bst_reuse.out $(machine).bst_reuse_rcu.out $(machine).bst_throwaway.out $(machine).bst_throwaway_rcu.out
bst: $(machine).bst_reuse.out $(machine).bst_reuse_rcu.out $(machine).bst_throwaway.out $(machine).bst_throwaway_rcu.out
$(machine).bst_reuse.out:
	$(GPP) $(FLAGS) -o $@ -O3 $(xargs) -DBST $(pinning) main.cpp $(LDFLAGS)
$(machine).bst_throwaway.out:
	$(GPP) $(FLAGS) -o $@ -O3 $(xargs) -DBST_THROWAWAY $(pinning) main.cpp $(LDFLAGS)
$(machine).bst_reuse_rcu.out:
	$(GPP) $(FLAGS) -o $@ -O3 $(xargs) -DUSE_RECLAIMER_RCU -DBST $(pinning) main.cpp $(LDFLAGS)
$(machine).bst_throwaway_rcu.out:
	$(GPP) $(FLAGS) -o $@ -O3 $(xargs) -DUSE_RECLAIMER_RCU -DBST_THROWAWAY $(pinning) main.cpp $(LDFLAGS)

.PHONY: $(machine).bslack_throwaway.out $(machine).bslack_reuse.out
bslack: $(machine).bslack_throwaway.out $(machine).bslack_reuse.out
$(machine).bslack_throwaway.out:
	$(GPP) $(FLAGS) -o $@ -O3 $(xargs) -DBSLACK_THROWAWAY $(pinning) main.cpp $(LDFLAGS)
$(machine).bslack_reuse.out:
	$(GPP) $(FLAGS) -o $@ -O3 $(xargs) -DBSLACK_REUSE $(pinning) main.cpp $(LDFLAGS)

.PHONY: $(machine).abtree_throwaway.out $(machine).abtree_reuse.out
abtree: $(machine).abtree_throwaway.out $(machine).abtree_reuse.out
$(machine).abtree_throwaway.out:
	$(GPP) $(FLAGS) -o $@ -O3 $(xargs) -DUSE_SIMPLIFIED_ABTREE_REBALANCING -DBSLACK_THROWAWAY $(pinning) main.cpp $(LDFLAGS)
$(machine).abtree_reuse.out:
	$(GPP) $(FLAGS) -o $@ -O3 $(xargs) -DUSE_SIMPLIFIED_ABTREE_REBALANCING -DBSLACK_REUSE $(pinning) main.cpp $(LDFLAGS)
