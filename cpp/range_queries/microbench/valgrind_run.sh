#!/bin/bash
# 
# File:   valgrind_run.sh
# Author: trbot
#
# Created on Aug 17, 2017, 6:26:03 PM
#

datastructures="abtree bst lflist lazylist citrus skiplistlock"
rqtechniques="snapcollector lockfree rwlock htm_rwlock unsafe rlu"
args="-i 10 -d 10 -k 1000 -rq 2 -rqsize 100 -t 1000 -nrq 1 -nwork 47 -bind 0,24,12,36,1,25,13,37,2,26,14,38,3,27,15,39,4,28,16,40,5,29,17,41,6,30,18,42,7,31,19,43,8,32,20,44,9,33,21,45,10,34,22,46,11,35,23,47"

highest=0
curr=0

if [ "$#" -eq "1" ]; then
#    echo "arg=$1"
    valgrind --fair-sched=yes --tool=memcheck --leak-check=yes --read-inline-info=yes --read-var-info=yes ./tapuz40.$1.out $args > leakcheck.$1.txt 2>&1
    ./valgrind_showerrors.sh $1
    ./valgrind_showleaks.sh $1
    exit
fi

for counting in 1 0 ; do
for ds in $datastructures ; do
for alg in $rqtechniques ; do
    if [ "$alg" == "snapcollector" ] && [ "$ds" != "lflist" ] && [ "$ds" != "skiplistlock" ] ; then continue ; fi
    if [ "$alg" == "rlu" ] && [ "$ds" != "citrus" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
    dsalg=${ds}.rq_${alg}

    if ((counting==1)) ; then
        ((highest = highest+1))
        continue
    else
        ((curr = curr+1))
    fi
    
    fname="leakcheck.$dsalg.txt"
    echo "step $curr / $highest : $fname"
    valgrind --fair-sched=yes --tool=memcheck --leak-check=yes --read-inline-info=yes --read-var-info=yes ./tapuz40.$dsalg.out $args > $fname 2>&1
done
done
done
