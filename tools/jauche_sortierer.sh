#!/bin/sh
#GPL
#TODO
#add pixelformat/sampleformat into the path of the codecs

FFP=../ffprobe
TMP=$(mktemp) || exit 1
TARGET=$1
shift

for v do
    BASE=$(basename $v)
    echo $v | egrep -i '(public|private)' >/dev/null && echo Warning $v may be private
    $FFP $v 2> $TMP
    FORM=$((grep 'Input #0, ' -m1 $TMP || echo 'Input #0, unknown') | sed 's/Input #0, \([a-zA-Z0-9_]*\).*/\1/' )
    mkdir -p $TARGET/container/$FORM
    ln -s $v $TARGET/container/$FORM/$BASE
    eval $(grep 'Stream #0\.[^:]*: [a-zA-Z0-9][^:]*: [a-zA-Z0-9]' $TMP | sed 's#[^:]*: \([a-zA-Z0-9]*\)[^:]*: \([a-zA-Z0-9]*\).*#mkdir -p '$TARGET'/\1/\2 ; ln -s '$v' '$TARGET'/\1/\2/'$BASE' ; #')
done

rm $TMP
