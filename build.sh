#!/bin/bash

./configure --enable-pic --extra-ldexeflags=-pie
make clean
make -j4
npm install
