#!/bin/sh
# 
# File:   testing.sh
# Author: trbot
#
# Created on 13-Jul-2016, 5:58:26 PM
#

g++ -mx32 -g testing.cpp -O3 -DHTM_ATTEMPT_THRESH=20 -lpthread -lhybridnorec -L. && time ./a.out