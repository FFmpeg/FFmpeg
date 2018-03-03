#!/bin/bash

./configure --enable-shared
make clean
make -j4
npm install
