#!/bin/bash

CFLAGS=-fPIC CXXFLAGS=-fPIC ./configure --enable-pic --extra-ldexeflags=-pie --arch=x86_64
make clean
make -j4
npm install
