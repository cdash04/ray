#!/bin/bash

# run a system test, but not on the compute grid
# the appropriate build must be available in builds/

TEST_NAME=$1
NSLOTS=$(cat /proc/cpuinfo|grep proc|wc -l)
RAY_GIT_PATH=~/git-clones/ray

echo "TEST_NAME= $TEST_NAME"
echo "NSLOTS= $NSLOTS"
echo "RAY_GIT_PATH= $RAY_GIT_PATH"
cd tests/$TEST_NAME

source ./main.sh