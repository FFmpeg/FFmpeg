#!/bin/sh
#
# common regression functions for avconv
#
#

test="${1#regtest-}"
test_ref=$2
raw_src_dir=$3
target_exec=$4
target_path=$5
threads=${6:-1}
cpuflags=${8:-all}

datadir="./tests/data"
target_datadir="${target_path}/${datadir}"

this="$test.$test_ref"
outfile="$datadir/$test_ref/"

# various files
avconv="$target_exec ${target_path}/avconv"
raw_src="${target_path}/$raw_src_dir/%02d.pgm"
raw_dst="$datadir/$this.out.yuv"
pcm_src="$target_datadir/asynth1.sw"
crcfile="$datadir/$this.crc"
target_crcfile="$target_datadir/$this.crc"

cleanfiles="$raw_dst $crcfile"
trap 'rm -f -- $cleanfiles' EXIT

mkdir -p "$datadir"
mkdir -p "$outfile"

[ "${V-0}" -gt 0 ] && echov=echov || echov=:

echov(){
    echo "$@" >&3
}

. $(dirname $0)/md5.sh

AVCONV_OPTS="-nostats -y -cpuflags $cpuflags"
COMMON_OPTS="-flags +bitexact -idct simple -sws_flags +accurate_rnd+bitexact"
DEC_OPTS="$COMMON_OPTS -threads $threads"
ENC_OPTS="$COMMON_OPTS -threads 1 -dct fastint"

run_avconv()
{
    $echov $avconv $AVCONV_OPTS $*
    $avconv $AVCONV_OPTS $*
}

do_avconv()
{
    f="$1"
    shift
    set -- $* ${target_path}/$f
    run_avconv $*
    do_md5sum $f
    echo $(wc -c $f)
}

do_avconv_crc()
{
    f="$1"
    shift
    run_avconv $* -f crc "$target_crcfile"
    echo "$f $(cat $crcfile)"
}
