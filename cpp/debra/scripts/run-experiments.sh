#!/bin/bash

##
# Run this to perform some experiments, and then run create-csv.sh > myfile.csv
# to create a CSV file from the results.
#
# example usage:
#   ./run-experiments.sh
#   ./create-csv.sh > myfile.csv
##

# edit the following to specify experiments to run
datastructure="chromatic"
#datastructure="bst"
ratios="0_0 50_50"
maxkeys="10000"
allocators="new"
algs="debra.ptas debraplus.ptas hazardptr.ptas none.none"
threadcounts=( 1 2 4 8 )
ntrials=10
millis=1000
# stop editing here

runningpath=".."
cmdprefix="env LD_PRELOAD=./lib/libjemalloc.so"
outpath="${runningpath}/output"
SEPARATOR="~"

# clean up
mkdir $outpath
rm -f tmp-exps.out tmp-test.out tmp-run.out

# construct experiments
i=0
echo -n "experiment prep steps..."
for ratio in $ratios; do
for maxkey in $maxkeys; do
for allocator in $allocators; do
for alg in $algs; do
    echo -n " $i"
    reclaimer=`echo $alg | cut -d"." -f1`
    pool=`echo $alg | cut -d"." -f2`
    ins=`echo $ratio | cut -d"_" -f1`
    del=`echo $ratio | cut -d"_" -f2`

    # construct 1ms trials to test each experiment type
    cmd="$cmdprefix ${runningpath}/${datastructure}-reclaim-${reclaimer}-alloc-${allocator}-pool-${pool} -i $ins -d $del -k $maxkey -n 1 -t 1"
    echo $cmd >> tmp-test.out

    # construct full trials
    for threads in "${threadcounts[@]}"; do
    for (( trial=1; trial<=$ntrials; trial++ )); do
        cmd="$cmdprefix ${runningpath}/${datastructure}-reclaim-${reclaimer}-alloc-${allocator}-pool-${pool} -p -i $ins -d $del -k $maxkey -n $threads -t $millis"
        filename="${outpath}/perf-p-i$ins-d$del-k$maxkey-n$threads-t$millis-$reclaimer-$allocator-$pool-trial${trial}.out"
        echo "$cmd${SEPARATOR}$filename" >> tmp-exps.out
    done
    done
    i=`expr $i + 1`
done
done
done
done
echo "... done."

# run tests for each experiment type
cnttest=`cat tmp-test.out | wc -l`
i=0
while read line; do
    echo -n "testing step $i/$cnttest: \"$line\"... "
    echo "$line" > tmp-run.out
    $line >> tmp-run.out 2>&1
    throughput=`cat tmp-run.out | grep "throughput" | cut -d":" -f2 | tr -d " "`
    if [ "$throughput" != "" ]; then
        if [ "$throughput" -gt "0" ]; then
            echo "done."
        fi
    else
        echo "FAILED."
        echo "ERROR: throughput line is: \"$throughput\""
        exit 1
    fi
    i=`expr $i + 1`
done < tmp-test.out

# run experiments
cntexps=`cat tmp-exps.out | wc -l`
i=0
while read line; do
    cmd=`echo $line | cut -d"${SEPARATOR}" -f1`
    filename=`echo $line | cut -d"${SEPARATOR}" -f2`
    echo -n "experiment step $i/$cntexps: $filename... "
    echo "$cmd" > $filename
    $cmd >> $filename 2>&1
#    echo -n "done."
    throughput=`cat $filename | grep "throughput" | cut -d":" -f2 | tr -d " "`
    echo "throughput=$throughput."
    i=`expr $i + 1`
done < tmp-exps.out

# clean up
rm -f tmp-exps.out tmp-test.out tmp-run.out
