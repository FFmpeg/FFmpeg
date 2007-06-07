#!/bin/sh

datadir="tests/data"

logfile="$datadir/seek.regression"
reffile="$1"

list=`ls tests/data/a-* tests/data/b-* | sort`
rm -f $logfile
for i in $list ; do
    echo ---------------- >> $logfile
    echo $i >> $logfile
    echo $i | grep -v 'b-libav[01][0-9][.]' 2> /dev/null &&
    tests/seek_test $i >> $logfile
done

if diff -u "$reffile" "$logfile" ; then
    echo
    echo Regression test succeeded.
    exit 0
else
    echo
    echo Regression test: Error.
    exit 1
fi
