#!/bin/sh

LC_ALL=C
export LC_ALL

datadir="tests/data"

logfile="$datadir/seek.regression"
reffile="$1"

list=`grep '^tests/data/[ab]-' "$reffile"`
rm -f $logfile
for i in $list ; do
    echo ---------------- >> $logfile
    echo $i >> $logfile
    tests/seek_test $i >> $logfile
done

if diff -u "$reffile" "$logfile" ; then
    echo
    echo seek regression test: success
    exit 0
else
    echo
    echo seek regression test: error
    exit 1
fi
