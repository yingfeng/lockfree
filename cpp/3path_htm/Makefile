PLAF = -DLOGICAL_PROCESSORS=48 -DMAX_TID_POW2=128

FLAGS = -std=c++0x -O3 -mcx16 -mrtm
FLAGS += -g
FLAGS += -DNDEBUG -DRECORD_ABORTS
FLAGS += $(PLAF)
FLAGS += -DIF_ALWAYS_RETRY_WHEN_BIT_SET=if\(0\)
FLAGS += -DMEMORY_STATS=if\(0\) -DMEMORY_STATS2=if\(0\)
FLAGS += -DDEBUG=if\(0\) -DDEBUG0=if\(0\) -DDEBUG1=if\(0\) -DDEBUG2=if\(0\) -DDEBUG3=if\(0\)
#FLAGS += -DDEBUG=if\(1\) -DDEBUG0=if\(1\) -DDEBUG1=if\(1\) -DDEBUG2=if\(1\) -DDEBUG3=if\(1\)
FLAGS += -DSKIP_DATA_STRUCTURE_DESTRUCTION
FLAGS += -I. -I./common -I./common/recordmgr
LDFLAGS = -lpthread
GPP = g++

machine=$(shell hostname)

all: hybridnorec bst-hytm abtree-3path abtree-tle bst-3path bst-tle

.PHONY: hybridnorec
hybridnorec:
	cd hybridnorec/hybridnorec ; make -j clean ; make -j ; cd ../..

abtree-3path:
	$(GPP) $(FLAGS) -o $(machine).abtree-3path.out -DABTREE -DWAIT_FOR_FALLBACK main.cpp $(LDFLAGS) 
abtree-tle:
	$(GPP) $(FLAGS) -o $(machine).abtree-tle.out -DABTREE -DFIND_FUNC=find_tle -DRQ_FUNC=rangeQuery_tle -DINSERT_FUNC=insert_tle -DERASE_FUNC=erase_tle main.cpp $(LDFLAGS) 
bst-3path:
	$(GPP) $(FLAGS) -o $(machine).bst-3path.out -DBST -DP1ALG1 -DP2ALG6 -DP3ALG12 -DWAIT_FOR_FALLBACK main.cpp $(LDFLAGS)
bst-tle:
	$(GPP) $(FLAGS) -o $(machine).bst-tle.out -DBST -DFIND_FUNC=find_tle -DRQ_FUNC=rangeQuery_tle -DINSERT_FUNC=insert_tle -DERASE_FUNC=erase_tle -DP1ALG1 -DP2ALG6 -DP3ALG12 -DWAIT_FOR_FALLBACK main.cpp $(LDFLAGS) 
bst-hytm:
	$(GPP) $(FLAGS) -o $(machine).bst-hytm.out -DBST -DTM -DHYBRIDNOREC -DTEST_TYPE_L -DFIND_FUNC=find_tm -DRQ_FUNC=rangeQuery_tm -DINSERT_FUNC=insert_tm -DERASE_FUNC=erase_tm -DP1ALG1 -DP2ALG6 -DP3ALG12 -DWAIT_FOR_FALLBACK main.cpp -I./hybridnorec/hybridnorec/ -L./hybridnorec/hybridnorec/ $(LDFLAGS) -lhybridnorec
