#!/bin/sh

if [ "$1" = "--version" ]; then
    rc.exe -?
    exit $?
fi

if [ $# -lt 2 ]; then
    echo "Usage: mswindres [-I/include/path ...] [-DSOME_DEFINE ...] [-o output.o] input.rc [output.o]" >&2
    exit 0
fi

EXTRA_OPTS="-nologo"

while [ $# -gt 2 ]; do
    case $1 in
    -D*) EXTRA_OPTS="$EXTRA_OPTS -d$(echo $1 | sed -e "s/^..//" -e "s/ /\\\\ /g")" ;;
    -I*) EXTRA_OPTS="$EXTRA_OPTS -i$(echo $1 | sed -e "s/^..//" -e "s/ /\\\\ /g")" ;;
    -o)  OPT_OUT="$2"; shift ;;
    esac
    shift
done

IN="$1"
if [ -z "$OPT_OUT" ]; then
    OUT="$2"
else
    OUT="$OPT_OUT"
fi

eval set -- $EXTRA_OPTS
rc.exe "$@" -fo "$OUT" "$IN"
