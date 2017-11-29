#!/bin/bash

watch -n 10 "head -1 nohup.out ; tail -10 nohup.out ; echo ; cat ./failed_rq_counts.out"
