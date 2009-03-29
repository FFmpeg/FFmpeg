#!/bin/sh

LC_ALL=C
export LC_ALL

target_exec=$2
target_path=$3

datadir="tests/data"

logfile="$datadir/seek.regression"
reffile="$1"

list=`grep '^tests/data/[ab]-' "$reffile"`
rm -f $logfile
for i in $list ; do
    echo ---------------- >> $logfile
    echo $i >> $logfile
    $target_exec $target_path/tests/seek_test $target_path/$i >> $logfile
done

if diff -u -w "$reffile" "$logfile" ; then
    echo
    echo seek regression test: success
    exit 0
else
    echo
    echo seek regression test: error
    exit 1
fi
