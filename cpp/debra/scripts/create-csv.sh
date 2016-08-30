#!/bin/sh

##
# Run this after running run-experiments.sh to create CSV data from the results.
# The CSV data is sent to stdout.
#
# example usage: ./create-csv.sh > myfile.csv
##

outpath="../output"

echo "reclaim,alloc,pool,ins,del,maxkey,nthreads,trial,elapsedmillis,throughput,success,cmd,file"
for y in `ls ${outpath}/perf-p-*`
do
    # ${outpath}/perf-p-i50-d50-k100-n1-t5000-reclaimer_debra-allocator_once-pool_perthread_and_shared-trial1.out
    cnt=`echo $y | tr -cd / | wc -c` # handle any extra slashes in ${outpath}
    cnt=`expr $cnt + 1`
    x=`echo $y | cut -d"/" -f$cnt | cut -d"-" -f2-`

    # p-i50-d50-k100-n1-t5000-reclaimer_debra-allocator_once-pool_perthread_and_shared-trial1.out
    ins=`echo $x | cut -d"-" -f2 | cut -d"i" -f2`
    del=`echo $x | cut -d"-" -f3 | cut -d"d" -f2`
    maxkey=`echo $x | cut -d"-" -f4 | cut -d"k" -f2`
    nthreads=`echo $x | cut -d"-" -f5 | cut -d"n" -f2`
    elapsedmillis=`echo $x | cut -d"-" -f6 | cut -d"t" -f2`
    reclaim=`echo $x | cut -d"-" -f7`
    alloc=`echo $x | cut -d"-" -f8`
    pool=`echo $x | cut -d"-" -f9`
    trial=`echo $x | cut -d"-" -f10 | cut -d"l" -f2 | cut -d"." -f1`

    throughput=`cat $y | grep "throughput" | cut -d":" -f2 | tr -d " "`
    cmd=`cat $y | head -1 | cut -d" " -f5-`
    finished=`cat $y | grep "main thread: dele"`
    if [ "$finished" = "main thread: deleting tree..." ]; then
        succ=true
    else
        succ=false
    fi
    echo $reclaim,$alloc,$pool,$ins,$del,$maxkey,$nthreads,$trial,$elapsedmillis,$throughput,$succ,$cmd,$y
done
