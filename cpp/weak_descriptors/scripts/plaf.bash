#!/bin/bash
#
# File:   plaf.bash
# Author: tabrown
#
# Created on 17-Jun-2016, 3:59:24 PM
#

machine=`hostname`
if [ "$machine" == "csl-pomela1" ]; then
    maxthreadcount="64"
    overthreadcount1="128"
    overthreadcount2="256"
    threadcounts="1 2 4 8 16 24 32 40 48 56 64"
    pin_scatter="POMELA6_SCATTER"
    pin_cluster="IDENTITY"
    pin_none="NONE"
    cmdprefix="numactl --interleave=all"
elif [ "$machine" == "csl-pomela6" ]; then
    maxthreadcount="64"
    overthreadcount1="128"
    overthreadcount2="256"
    threadcounts="1 2 4 8 16 24 32 40 48 56 64"
    pin_scatter="POMELA6_SCATTER"
    pin_cluster="IDENTITY"
    pin_none="NONE"
    cmdprefix="numactl --interleave=all"
elif [ "$machine" == "pomela3" ]; then
    maxthreadcount="64"
    overthreadcount1="128"
    overthreadcount2="256"
    threadcounts="1 2 4 8 16 24 32 40 48 56 64"
    pin_scatter="POMELA6_SCATTER"
    pin_cluster="IDENTITY"
    pin_none="NONE"
    cmdprefix="numactl --interleave=all"
elif [ "$machine" == "tapuz40" ]; then
    maxthreadcount="48"
    overthreadcount1="96"
    overthreadcount2="192"
    threadcounts="1 2 4 8 12 16 24 32 40 48"
    pin_scatter="TAPUZ40_SCATTER"
    pin_cluster="TAPUZ40_CLUSTER"
    pin_none="NONE"
    cmdprefix="numactl --interleave=all"
elif [ "$machine" == "theoryhtm" ]; then
    maxthreadcount="8"
    overthreadcount1="16"
    overthreadcount2="32"
    threadcounts="1 2 3 4 5 6 7 8"
    pin_scatter="IDENTITY"
    pin_cluster="IDENTITY"
    pin_none="NONE"
    cmdprefix=""
else
    echo "ERROR: unknown machine $machine"
    exit 1
fi

g++ -O3 add.cpp -o add
