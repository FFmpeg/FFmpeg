#!/bin/sh

LC_ALL=C
export LC_ALL

datadir="tests/data"

logfile="$datadir/copy.regression"
reffile="$1"

list=$(grep -oh ' ./tests/data/.*' tests/ref/{acodec,lavf,vsynth1}/*| sort)
rm -f $logfile
for i in $list ; do
    echo ---------------- >> $logfile
    echo $i >> $logfile
    ./ffmpeg_g -flags +bitexact -i $i -acodec copy -vcodec copy -y first.nut
    ./ffmpeg_g -flags +bitexact -i first.nut -acodec copy -vcodec copy -y second.nut
    cmp first.nut second.nut >> $logfile
    md5sum first.nut >> $logfile
done

if diff -u -w "$reffile" "$logfile" ; then
    echo
    echo copy regression test: success
    exit 0
else
    echo
    echo copy regression test: error
    exit 1
fi
