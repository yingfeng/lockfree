#!/bin/bash

g++ hytm1.c testing.cpp -lpthread -o testing.out
if [ $? -eq 0 ]; then
    ./testing.out
else
    echo "Error during compilation!"
fi