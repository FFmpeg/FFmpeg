#!/bin/sh

LC_ALL=C
export LC_ALL

src_path=$1
target_exec=$2
target_path=$3

[ "${V-0}" -gt 0 ] && echov=echo || echov=:
[ "${V-0}" -gt 1 ] || exec 2>/dev/null

refdir="$src_path/tests/ref/seek"
datadir="tests/data"

list=$(ls -1 $datadir/vsynth2/* $datadir/acodec/* $datadir/lavf/*)
imgs=$(for i in $datadir/images/*; do echo "$i/%02d.${i##*/}"; done)
err=0

for i in $list $imgs; do
    base=$(basename $i)
    logfile="$datadir/$base.seek.regression"
    reffile="$refdir/$base.ref"
    echo "TEST SEEK   $base"
    $echov $target_exec $target_path/tests/seek_test $target_path/$i
    $target_exec $target_path/tests/seek_test $target_path/$i > $logfile
    diff -u -w "$reffile" "$logfile" || err=1
done

if [ $err = 0 ]; then
    echo
    echo seek regression test: success
    exit 0
else
    echo
    echo seek regression test: error
    exit 1
fi
