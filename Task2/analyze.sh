#!/bin/bash

MyPass=$HOME/TuringAdvancedProgramming19B/Task2/MyPass.so
clang -Xclang -load -Xclang $MyPass -c TestMe.c
