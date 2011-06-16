#!/bin/sh
#
# common regression functions for ffmpeg
#
#

test="${1#regtest-}"
test_ref=$2
raw_src_dir=$3
target_exec=$4
target_path=$5
threads=${6:-1}

datadir="./tests/data"
target_datadir="${target_path}/${datadir}"

this="$test.$test_ref"
logdir="$datadir/regression/$test_ref"
logfile="$logdir/$test"
outfile="$datadir/$test_ref/"
errfile="$datadir/$this.err"

# various files
ffmpeg="$target_exec ${target_path}/ffmpeg"
tiny_psnr="tests/tiny_psnr"
raw_src="${target_path}/$raw_src_dir/%02d.pgm"
raw_dst="$datadir/$this.out.yuv"
raw_ref="$datadir/$test_ref.ref.yuv"
pcm_src="$target_datadir/asynth1.sw"
pcm_dst="$datadir/$this.out.wav"
pcm_ref="$datadir/$test_ref.ref.wav"
crcfile="$datadir/$this.crc"
target_crcfile="$target_datadir/$this.crc"

cleanfiles="$raw_dst $pcm_dst $crcfile"
trap 'rm -f -- $cleanfiles' EXIT

mkdir -p "$datadir"
mkdir -p "$outfile"
mkdir -p "$logdir"

(exec >&3) 2>/dev/null || exec 3>&2

[ "${V-0}" -gt 0 ] && echov=echov || echov=:
[ "${V-0}" -gt 1 ] || exec 2>$errfile

echov(){
    echo "$@" >&3
}

. $(dirname $0)/md5.sh

FFMPEG_OPTS="-v 0 -y"
COMMON_OPTS="-flags +bitexact -idct simple -sws_flags +accurate_rnd+bitexact"
DEC_OPTS="$COMMON_OPTS -threads $threads"
ENC_OPTS="$COMMON_OPTS -threads 1 -dct fastint"

run_ffmpeg()
{
    $echov $ffmpeg $FFMPEG_OPTS $*
    $ffmpeg $FFMPEG_OPTS $*
}

do_ffmpeg()
{
    f="$1"
    shift
    set -- $* ${target_path}/$f
    run_ffmpeg $*
    do_md5sum $f >> $logfile
    if [ $f = $raw_dst ] ; then
        $tiny_psnr $f $raw_ref >> $logfile
    elif [ $f = $pcm_dst ] ; then
        $tiny_psnr $f $pcm_ref 2 >> $logfile
    else
        wc -c $f >> $logfile
    fi
}

do_ffmpeg_nomd5()
{
    f="$1"
    shift
    set -- $* ${target_path}/$f
    run_ffmpeg $*
    if [ $f = $raw_dst ] ; then
        $tiny_psnr $f $raw_ref >> $logfile
    elif [ $f = $pcm_dst ] ; then
        $tiny_psnr $f $pcm_ref 2 >> $logfile
    else
        wc -c $f >> $logfile
    fi
}

do_ffmpeg_crc()
{
    f="$1"
    shift
    run_ffmpeg $* -f crc "$target_crcfile"
    echo "$f $(cat $crcfile)" >> $logfile
}

do_video_decoding()
{
    do_ffmpeg $raw_dst $DEC_OPTS $1 -i $target_path/$file -f rawvideo $ENC_OPTS -vsync 0 $2
}

do_video_encoding()
{
    file=${outfile}$1
    do_ffmpeg $file $DEC_OPTS -f image2 -vcodec pgmyuv -i $raw_src $ENC_OPTS $2
}

do_audio_encoding()
{
    file=${outfile}$1
    do_ffmpeg $file $DEC_OPTS -ac 2 -ar 44100 -f s16le -i $pcm_src -ab 128k $ENC_OPTS $2
}

do_audio_decoding()
{
    do_ffmpeg $pcm_dst $DEC_OPTS -i $target_path/$file -sample_fmt s16 -f wav
}
