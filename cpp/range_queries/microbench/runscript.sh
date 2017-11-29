#!/bin/bash

#rm -r -f temp.out
#mkdir temp.out
#rm compiling.out

trials=5

cols="%6s %12s %12s %12s %8s %6s %6s %8s %6s %6s %8s %12s %12s %12s %12s"
headers="step machine ds alg k u rq rqsize nrq nwork trial throughput rqs updates finds"

machine=`hostname`
if [[ $machine == *"pomela"* ]] || [[ $machine == *"rack-mad-01"* ]]; then
    thmax=64
    thinc=8
    pinning_policy="0,8,32,40,48,56,16,24,1,9,33,41,49,57,17,25,2,10,34,42,50,58,18,26,3,11,35,43,51,59,19,27,4,12,36,44,52,60,20,28,5,13,37,45,53,61,21,29,6,14,38,46,54,62,22,30,7,15,39,47,55,63,23,31"
elif [[ $machine == *"tapuz"* ]]; then
    thmax=48
    thinc=8
    pinning_policy="0,24,12,36,1,25,13,37,2,26,14,38,3,27,15,39,4,28,16,40,5,29,17,41,6,30,18,42,7,31,19,43,8,32,20,44,9,33,21,45,10,34,22,46,11,35,23,47"
else
    echo "ERROR: unrecognized machine $machine"
    exit 1
fi

datastructures="abtree bst lflist lazylist citrus skiplistlock"
rqtechniques="snapcollector lockfree rwlock htm_rwlock unsafe rlu"
skip_steps_before=0
skip_steps_after=1000000

outdir=data
fsummary=$outdir/summary.txt

rm -r -f $outdir.old
mv -f $outdir $outdir.old
mkdir $outdir

#echo "Prefetching disable state: 0x0"
#wrmsr -a 0x1a4 0x0

cnt1=10000
cnt2=10000

rm -f warnings.txt

if [ "$#" -eq "1" ] ; then
    testingmode=1
    prefill_and_time="-t 1"
else
    testingmode=0
    prefill_and_time="-p -t 3000"
fi

for counting in 1 0 ; do
    if (($counting==0)); then
        echo "Total trials: $cnt1 ... $cnt2"
        printf "${cols}\n" $headers > $fsummary
        cat $fsummary
    fi
    
    ############################################################################
    ## BASIC MAX-THREAD EXPERIMENTS (rq size 100)
    ## (varying update rate and data structure size)
    ############################################################################

    if (($counting==1)); then
        echo "EXPERIMENT START $cnt2: BASIC MAX-THREAD EXPERIMENTS (rq size 100; varying update rate and data structure size)" >> $fsummary
        tail -1 $fsummary
    fi
    rq=0
    rqsize=100
    for u in 0 10 50 ; do
    for k in 1000 10000 100000 1000000 10000000 ; do
    for ds in $datastructures ; do
    for alg in $rqtechniques ; do
    for nrq in 0 1; do
        nwork="`expr $thmax - $nrq`"
        for ((trial=0;trial<$trials;++trial)) ; do
            if [ "$k" == "10000000" ] && [ "$ds" != "abtree" ] ; then continue ; fi
            if [ "$k" == "1000000" ] && [ "$ds" != "abtree" ] ; then continue ; fi
            if [ "$k" == "100000" ]  && [ "$ds" != "abtree" ] && [ "$ds" != "bst" ] && [ "$ds" != "citrus" ] && [ "$ds" != "skiplistlock" ]  ; then continue ; fi
            if [ "$k" == "10000" ]   && [ "$ds" != "lflist" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
            if [ "$k" == "1000" ]    && [ "$ds" != "lflist" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
            if [ "$alg" == "snapcollector" ] && [ "$ds" != "lflist" ] && [ "$ds" != "skiplistlock" ] ; then continue ; fi
            if [ "$alg" == "rlu" ] && [ "$ds" != "citrus" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
            dsalg=${ds}.rq_${alg}

            if (($counting==1)); then
                cnt2=`expr $cnt2 + 1`
                if ((($cnt2 % 100) == 0)); then echo "Counting trials: $cnt2" ; fi
            else
                cnt1=`expr $cnt1 + 1`
                if ((cnt1 < skip_steps_before)); then continue; fi
                if ((cnt1 > skip_steps_after)); then continue; fi

                fname="$outdir/step$cnt1.$machine.${ds}.${alg}.k$k.u$u.rq$rq.rqsize$rqsize.nrq$nrq.nwork$nwork.trial$trial.out"
                #echo "FNAME=$fname"
                cmd="./${machine}.${dsalg}.out -i $u -d $u -k $k -rq $rq -rqsize $rqsize ${prefill_and_time} -nrq $nrq -nwork $nwork -bind ${pinning_policy}"
                echo "env LD_PRELOAD=../lib/libjemalloc.so TREE_MALLOC=../lib/libjemalloc.so numactl --interleave=all $cmd" > $fname
                env LD_PRELOAD=../lib/libjemalloc.so TREE_MALLOC=../lib/libjemalloc.so numactl --interleave=all $cmd >> $fname
                printf "${cols}" $cnt1 $machine $ds $alg $k $u $rq $rqsize $nrq $nwork $trial "`cat $fname | grep 'total throughput' | cut -d':' -f2`" "`cat $fname | grep 'total rq' | cut -d':' -f2`" "`cat $fname | grep 'total updates' | cut -d':' -f2`" "`cat $fname | grep 'total find' | cut -d':' -f2`" >> $fsummary
                tail -1 $fsummary
                echo
                printf "%120s          %s\n" "$fname" "`head -1 $fname`" >> $fsummary

                throughput=`cat $fname | grep "total throughput" | cut -d":" -f2 | tr -d " "`
                if [ "$throughput" == "" ] || [ "$throughput" -le "0" ] ; then echo "WARNING: thoughput $throughput in file $fname" >> warnings.txt ; cat warnings.txt | tail -1 ; fi
            fi
        done
    done
    done
    done
    done
    done

    ############################################################################
    ## IMPACT OF INCREASING RQ THREAD COUNT ON UPDATE THREADS (rq size 100)
    ############################################################################

    if (($counting==1)); then
        echo "EXPERIMENT START $cnt2: IMPACT OF INCREASING RQ THREAD COUNT ON UPDATE THREADS (rq size 100)" >> $fsummary
        tail -1 $fsummary
    fi
    rq=0
    rqsize=100
    nworks="42"

    for u in 10 50 ; do
    for k in 10000 100000 1000000 ; do
    for ds in $datastructures ; do
    for alg in $rqtechniques ; do
    for nwork in $nworks ; do
        #remaining_threads=`expr $thmax - $nwork`
        nrqs="0 1 2 3 4 5 6"
        #for ((x=0; x<${remaining_threads}; x++)); do nrqs="$nrqs $x" ; done
        #nrqs="$nrqs $remaining_threads"

        for nrq in $nrqs ; do
            if [ "$nrq" -eq "0" ] && [ "$nwork" -eq "0" ] ; then continue ; fi

            for ((trial=0;trial<$trials;++trial)) ; do
                if [ "$k" == "10000000" ] && [ "$ds" != "abtree" ] ; then continue ; fi
                if [ "$k" == "1000000" ] && [ "$ds" != "abtree" ] ; then continue ; fi
                if [ "$k" == "100000" ]  && [ "$ds" != "abtree" ] && [ "$ds" != "bst" ] && [ "$ds" != "citrus" ] && [ "$ds" != "skiplistlock" ]  ; then continue ; fi
                if [ "$k" == "10000" ]   && [ "$ds" != "lflist" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
                if [ "$k" == "1000" ]    && [ "$ds" != "lflist" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
                if [ "$alg" == "snapcollector" ] && [ "$ds" != "lflist" ] && [ "$ds" != "skiplistlock" ] ; then continue ; fi
                if [ "$alg" == "rlu" ] && [ "$ds" != "citrus" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
                dsalg=${ds}.rq_${alg}

                if (($counting==1)); then
                    cnt2=`expr $cnt2 + 1`
                    if ((($cnt2 % 100) == 0)); then echo "Counting trials: $cnt2" ; fi
                else
                    cnt1=`expr $cnt1 + 1`
                    if ((cnt1 < skip_steps_before)); then continue; fi
                    if ((cnt1 > skip_steps_after)); then continue; fi

                    fname="$outdir/step$cnt1.$machine.${ds}.${alg}.k$k.u$u.rq$rq.rqsize$rqsize.nrq$nrq.nwork$nwork.trial$trial.out"
                    cmd="./${machine}.${dsalg}.out -i $u -d $u -k $k -rq $rq -rqsize $rqsize ${prefill_and_time} -nrq $nrq -nwork $nwork -bind ${pinning_policy}"
                    echo "env LD_PRELOAD=../lib/libjemalloc.so TREE_MALLOC=../lib/libjemalloc.so numactl --interleave=all $cmd" > $fname
                    env LD_PRELOAD=../lib/libjemalloc.so TREE_MALLOC=../lib/libjemalloc.so numactl --interleave=all $cmd >> $fname
                    printf "${cols}" $cnt1 $machine $ds $alg $k $u $rq $rqsize $nrq $nwork $trial "`cat $fname | grep 'total throughput' | cut -d':' -f2`" "`cat $fname | grep 'total rq' | cut -d':' -f2`" "`cat $fname | grep 'total updates' | cut -d':' -f2`" "`cat $fname | grep 'total find' | cut -d':' -f2`" >> $fsummary
                    tail -1 $fsummary
                    echo
                    printf "%120s          %s\n" "$fname" "`head -1 $fname`" >> $fsummary

                    throughput=`cat $fname | grep "total throughput" | cut -d":" -f2 | tr -d " "`
                    if [ "$throughput" == "" ] || [ "$throughput" -le "0" ] ; then echo "WARNING: thoughput $throughput in file $fname" >> warnings.txt ; cat warnings.txt | tail -1 ; fi
                fi
            done
        done
    done
    done
    done
    done
    done

    ############################################################################
    ## IMPACT OF INCREASING UPDATE THREAD COUNT ON RQ THREADS (rq size 100)
    ############################################################################

    if (($counting==1)); then
        echo "EXPERIMENT START $cnt2: IMPACT OF INCREASING UPDATE THREAD COUNT ON RQ THREADS (rq size 100)" >> $fsummary
        tail -1 $fsummary
    fi
    rq=0
    rqsize=100
    nrqs="1 4"
    for u in 10 50 ; do
    for k in 10000 100000 1000000 ; do
    for ds in $datastructures ; do
    for alg in $rqtechniques ; do
    for nrq in $nrqs ; do
        remaining_threads=`expr $thmax - $nrq`
        nworks="0 1"
        for ((x=$thinc; x<${remaining_threads}; x+=$thinc)); do nworks="$nworks $x" ; done
        nworks="$nworks $remaining_threads"

        for nwork in $nworks ; do
            if [ "$nrq" -eq "0" ] && [ "$nwork" -eq "0" ] ; then continue ; fi

            for ((trial=0;trial<$trials;++trial)) ; do
                if [ "$k" == "10000000" ] && [ "$ds" != "abtree" ] ; then continue ; fi
                if [ "$k" == "1000000" ] && [ "$ds" != "abtree" ] ; then continue ; fi
                if [ "$k" == "100000" ]  && [ "$ds" != "abtree" ] && [ "$ds" != "bst" ] && [ "$ds" != "citrus" ] && [ "$ds" != "skiplistlock" ]  ; then continue ; fi
                if [ "$k" == "10000" ]   && [ "$ds" != "lflist" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
                if [ "$k" == "1000" ]    && [ "$ds" != "lflist" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
                if [ "$alg" == "snapcollector" ] && [ "$ds" != "lflist" ] && [ "$ds" != "skiplistlock" ] ; then continue ; fi
                if [ "$alg" == "rlu" ] && [ "$ds" != "citrus" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
                dsalg=${ds}.rq_${alg}

                if (($counting==1)); then
                    cnt2=`expr $cnt2 + 1`
                    if ((($cnt2 % 100) == 0)); then echo "Counting trials: $cnt2" ; fi
                else
                    cnt1=`expr $cnt1 + 1`
                    if ((cnt1 < skip_steps_before)); then continue; fi
                    if ((cnt1 > skip_steps_after)); then continue; fi

                    fname="$outdir/step$cnt1.$machine.${ds}.${alg}.k$k.u$u.rq$rq.rqsize$rqsize.nrq$nrq.nwork$nwork.trial$trial.out"
                    cmd="./${machine}.${dsalg}.out -i $u -d $u -k $k -rq $rq -rqsize $rqsize ${prefill_and_time} -nrq $nrq -nwork $nwork -bind ${pinning_policy}"
                    echo "env LD_PRELOAD=../lib/libjemalloc.so TREE_MALLOC=../lib/libjemalloc.so numactl --interleave=all $cmd" > $fname
                    env LD_PRELOAD=../lib/libjemalloc.so TREE_MALLOC=../lib/libjemalloc.so numactl --interleave=all $cmd >> $fname
                    printf "${cols}" $cnt1 $machine $ds $alg $k $u $rq $rqsize $nrq $nwork $trial "`cat $fname | grep 'total throughput' | cut -d':' -f2`" "`cat $fname | grep 'total rq' | cut -d':' -f2`" "`cat $fname | grep 'total updates' | cut -d':' -f2`" "`cat $fname | grep 'total find' | cut -d':' -f2`" >> $fsummary
                    tail -1 $fsummary
                    echo
                    printf "%120s          %s\n" "$fname" "`head -1 $fname`" >> $fsummary

                    throughput=`cat $fname | grep "total throughput" | cut -d":" -f2 | tr -d " "`
                    if [ "$throughput" == "" ] || [ "$throughput" -le "0" ] ; then echo "WARNING: thoughput $throughput in file $fname" >> warnings.txt ; cat warnings.txt | tail -1 ; fi
                fi
            done
        done
    done
    done
    done
    done
    done

    ############################################################################
    ## SMALL RQ (k/1000) VS BIG RQ (k/10) (VS ITERATION (rqsize=k))
    ## (for latency graphs, and for comparison with iterators)
    ############################################################################

    if (($counting==1)); then
        echo "EXPERIMENT START $cnt2: SMALL RQ (k/1000) VS BIG RQ (k/10) (VS ITERATION (rqsize=k); for latency graphs, and for comparison with iterators)" >> $fsummary
        tail -1 $fsummary
    fi
    rq=0
    for u in 10 50 ; do
    for k in 10000 100000 1000000 ; do
    kdiv10=`expr $k / 10`
    kdiv100=`expr $k / 100`
    kdiv1000=`expr $k / 1000`
    for rqsize in $k $kdiv10 $kdiv100 $kdiv1000 ; do
    for ds in $datastructures ; do
    for alg in $rqtechniques ; do
    for nrq in 1; do
        nwork="`expr $thmax - $nrq`"
        for ((trial=0;trial<$trials;++trial)) ; do
            if [ "$k" == "10000000" ] && [ "$ds" != "abtree" ] ; then continue ; fi
            if [ "$k" == "1000000" ] && [ "$ds" != "abtree" ] ; then continue ; fi
            if [ "$k" == "100000" ]  && [ "$ds" != "abtree" ] && [ "$ds" != "bst" ] && [ "$ds" != "citrus" ] && [ "$ds" != "skiplistlock" ]  ; then continue ; fi
            if [ "$k" == "10000" ]   && [ "$ds" != "lflist" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
            if [ "$k" == "1000" ]    && [ "$ds" != "lflist" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
            if [ "$alg" == "snapcollector" ] && [ "$ds" != "lflist" ] && [ "$ds" != "skiplistlock" ] ; then continue ; fi
            if [ "$alg" == "rlu" ] && [ "$ds" != "citrus" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
            dsalg=${ds}.rq_${alg}

            if (($counting==1)); then
                cnt2=`expr $cnt2 + 1`
                if ((($cnt2 % 100) == 0)); then echo "Counting trials: $cnt2" ; fi
            else
                cnt1=`expr $cnt1 + 1`
                if ((cnt1 < skip_steps_before)); then continue; fi
                if ((cnt1 > skip_steps_after)); then continue; fi

                fname="$outdir/step$cnt1.$machine.${ds}.${alg}.k$k.u$u.rq$rq.rqsize$rqsize.nrq$nrq.nwork$nwork.trial$trial.out"
                cmd="./${machine}.${dsalg}.out -i $u -d $u -k $k -rq 0 -rqsize $rqsize ${prefill_and_time} -nrq $nrq -nwork $nwork -bind ${pinning_policy}"
                echo "env LD_PRELOAD=../lib/libjemalloc.so TREE_MALLOC=../lib/libjemalloc.so numactl --interleave=all $cmd" > $fname
                env LD_PRELOAD=../lib/libjemalloc.so TREE_MALLOC=../lib/libjemalloc.so numactl --interleave=all $cmd >> $fname
                printf "${cols}" $cnt1 $machine $ds $alg $k $u $rq $rqsize $nrq $nwork $trial "`cat $fname | grep 'total throughput' | cut -d':' -f2`" "`cat $fname | grep 'total rq' | cut -d':' -f2`" "`cat $fname | grep 'total updates' | cut -d':' -f2`" "`cat $fname | grep 'total find' | cut -d':' -f2`" >> $fsummary
                tail -1 $fsummary
                echo
                printf "%120s          %s\n" "$fname" "`head -1 $fname`" >> $fsummary

                throughput=`cat $fname | grep "total throughput" | cut -d":" -f2 | tr -d " "`
                if [ "$throughput" == "" ] || [ "$throughput" -le "0" ] ; then echo "WARNING: thoughput $throughput in file $fname" >> warnings.txt ; cat warnings.txt | tail -1 ; fi
            fi
        done
    done
    done
    done
    done
    done
    done

    ############################################################################
    ## MIXED WORKLOAD WHERE ALL THREADS DO UPDATES AND RQs (rqsize=100)
    ############################################################################

    if (($counting==1)); then
        echo "EXPERIMENT START $cnt2: MIXED WORKLOAD WHERE ALL THREADS DO UPDATES AND RQs (rqsize=100)" >> $fsummary
        tail -1 $fsummary
    fi
    rqsize=100
    for rq in 2 ; do
    #us=`expr 50 - $rq`
    #us="10 $us"
    us="10"
    for u in $us; do
    for k in 10000 100000 1000000 ; do
    for ds in $datastructures ; do
    for alg in $rqtechniques ; do
    for nrq in 0; do
        nwork="`expr $thmax - $nrq`"
        for ((trial=0;trial<$trials;++trial)) ; do
            if [ "$k" == "10000000" ] && [ "$ds" != "abtree" ] ; then continue ; fi
            if [ "$k" == "1000000" ] && [ "$ds" != "abtree" ] ; then continue ; fi
            if [ "$k" == "100000" ]  && [ "$ds" != "abtree" ] && [ "$ds" != "bst" ] && [ "$ds" != "citrus" ] && [ "$ds" != "skiplistlock" ]  ; then continue ; fi
            if [ "$k" == "10000" ]   && [ "$ds" != "lflist" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
            if [ "$k" == "1000" ]    && [ "$ds" != "lflist" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
            if [ "$alg" == "snapcollector" ] && [ "$ds" != "lflist" ] && [ "$ds" != "skiplistlock" ] ; then continue ; fi
            if [ "$alg" == "rlu" ] && [ "$ds" != "citrus" ] && [ "$ds" != "lazylist" ] ; then continue ; fi
            dsalg=${ds}.rq_${alg}

            if (($counting==1)); then
                cnt2=`expr $cnt2 + 1`
                if ((($cnt2 % 100) == 0)); then echo "Counting trials: $cnt2" ; fi
            else
                cnt1=`expr $cnt1 + 1`
                if ((cnt1 < skip_steps_before)); then continue; fi
                if ((cnt1 > skip_steps_after)); then continue; fi

                fname="$outdir/step$cnt1.$machine.${ds}.${alg}.k$k.u$u.rq$rq.rqsize$rqsize.nrq$nrq.nwork$nwork.trial$trial.out"
                cmd="./${machine}.${dsalg}.out -i $u -d $u -k $k -rq $rq -rqsize $rqsize ${prefill_and_time} -nrq $nrq -nwork $nwork -bind ${pinning_policy}"
                echo "env LD_PRELOAD=../lib/libjemalloc.so TREE_MALLOC=../lib/libjemalloc.so numactl --interleave=all $cmd" > $fname
                env LD_PRELOAD=../lib/libjemalloc.so TREE_MALLOC=../lib/libjemalloc.so numactl --interleave=all $cmd >> $fname
                printf "${cols}" $cnt1 $machine $ds $alg $k $u $rq $rqsize $nrq $nwork $trial "`cat $fname | grep 'total throughput' | cut -d':' -f2`" "`cat $fname | grep 'total rq' | cut -d':' -f2`" "`cat $fname | grep 'total updates' | cut -d':' -f2`" "`cat $fname | grep 'total find' | cut -d':' -f2`" >> $fsummary
                tail -1 $fsummary
                echo
                printf "%120s          %s\n" "$fname" "`head -1 $fname`" >> $fsummary

                throughput=`cat $fname | grep "total throughput" | cut -d":" -f2 | tr -d " "`
                if [ "$throughput" == "" ] || [ "$throughput" -le "0" ] ; then echo "WARNING: thoughput $throughput in file $fname" >> warnings.txt ; cat warnings.txt | tail -1 ; fi
            fi
        done
    done
    done
    done
    done
    done
    done

done

#echo "Prefetching disable state: 0x0"
#wrmsr -a 0x1a4 0x0

if [ "`cat warnings.txt | wc -l`" -ne 0 ]; then
    echo "NOTE: THERE WERE WARNINGS. PRINTING THEM..."
    cat warnings.txt
fi

mv results.db results.db.old
python create_db.py
