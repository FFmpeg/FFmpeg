#!/bin/sh

n=10

case "$1" in
    -n)  n=$2;      shift 2 ;;
    -n*) n=${1#-n}; shift   ;;
esac

exec sed ${n}q "$@"
