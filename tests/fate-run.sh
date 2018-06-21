#! /bin/sh

export LC_ALL=C

base=$(dirname $0)
. "${base}/md5.sh"

base64=tests/base64

test="${1#fate-}"
target_samples=$2
target_exec=$3
target_path=$4
command=$5
cmp=${6:-diff}
ref=${7:-"${base}/ref/fate/${test}"}
fuzz=${8:-1}
threads=${9:-1}
thread_type=${10:-frame+slice}
cpuflags=${11:-all}
cmp_shift=${12:-0}
cmp_target=${13:-0}
size_tolerance=${14:-0}
cmp_unit=${15:-2}
gen=${16:-no}
hwaccel=${17:-none}
report_type=${18:-standard}

outdir="tests/data/fate"
outfile="${outdir}/${test}"
errfile="${outdir}/${test}.err"
cmpfile="${outdir}/${test}.diff"
repfile="${outdir}/${test}.rep"

target_path(){
    test ${1} = ${1#/} && p=${target_path}/
    echo ${p}${1}
}

# $1=value1, $2=value2, $3=threshold
# prints 0 if absolute difference between value1 and value2 is <= threshold
compare(){
    awk "BEGIN { v = $1 - $2; printf ((v < 0 ? -v : v) > $3) }"
}

do_tiny_psnr(){
    psnr=$(tests/tiny_psnr "$1" "$2" $cmp_unit $cmp_shift 0) || return 1
    val=$(expr "$psnr" : ".*$3: *\([0-9.]*\)")
    size1=$(expr "$psnr" : '.*bytes: *\([0-9]*\)')
    size2=$(expr "$psnr" : '.*bytes:[ 0-9]*/ *\([0-9]*\)')
    val_cmp=$(compare $val $cmp_target $fuzz)
    size_cmp=$(compare $size1 $size2 $size_tolerance)
    if [ "$val_cmp" != 0 ] || [ "$size_cmp" != 0 ]; then
        echo "$psnr"
        if [ "$val_cmp" != 0 ]; then
            echo "$3: |$val - $cmp_target| >= $fuzz"
        fi
        if [ "$size_cmp" != 0 ]; then
            echo "size: |$size1 - $size2| >= $size_tolerance"
        fi
        return 1
    fi
}

oneoff(){
    do_tiny_psnr "$1" "$2" MAXDIFF
}

stddev(){
    do_tiny_psnr "$1" "$2" stddev
}

oneline(){
    printf '%s\n' "$1" | diff -u -b - "$2"
}

run(){
    test "${V:-0}" -gt 0 && echo "$target_exec" $target_path/"$@" >&3
    $target_exec $target_path/"$@"
}

runecho(){
    test "${V:-0}" -gt 0 && echo "$target_exec" $target_path/"$@" >&3
    $target_exec $target_path/"$@" >&3
}

probefmt(){
    run ffprobe${PROGSUF} -show_entries format=format_name -print_format default=nw=1:nk=1 -v 0 "$@"
}

probetags(){
    run ffprobe${PROGSUF} -show_entries format_tags -v 0 "$@"
}

runlocal(){
    test "${V:-0}" -gt 0 && echo ${base}/"$@" ${base} >&3
    ${base}/"$@" ${base}
}

probeframes(){
    run ffprobe${PROGSUF} -show_frames -v 0 "$@"
}

probechapters(){
    run ffprobe${PROGSUF} -show_chapters -v 0 "$@"
}

probegaplessinfo(){
    filename="$1"
    shift
    run ffprobe${PROGSUF} -bitexact -select_streams a -show_entries format=start_time,duration:stream=index,start_pts,duration_ts -v 0 "$filename" "$@"
    pktfile1="${outdir}/${test}.pkts"
    framefile1="${outdir}/${test}.frames"
    cleanfiles="$cleanfiles $pktfile1 $framefile1"
    run ffprobe${PROGSUF} -bitexact -select_streams a -of compact -count_packets -show_entries packet=pts,dts,duration,flags:stream=nb_read_packets -v 0 "$filename" "$@" > "$pktfile1"
    head -n 8 "$pktfile1"
    tail -n 9 "$pktfile1"
    run ffprobe${PROGSUF} -bitexact -select_streams a -of compact -count_frames -show_entries frame=pkt_pts,pkt_dts,best_effort_timestamp,pkt_duration,nb_samples:stream=nb_read_frames -v 0 "$filename" "$@" > "$framefile1"
    head -n 8 "$framefile1"
    tail -n 9 "$framefile1"
}

ffmpeg(){
    dec_opts="-hwaccel $hwaccel -threads $threads -thread_type $thread_type"
    ffmpeg_args="-nostdin -nostats -cpuflags $cpuflags"
    for arg in $@; do
        [ x${arg} = x-i ] && ffmpeg_args="${ffmpeg_args} ${dec_opts}"
        ffmpeg_args="${ffmpeg_args} ${arg}"
    done
    run ffmpeg${PROGSUF} ${ffmpeg_args}
}

framecrc(){
    ffmpeg "$@" -bitexact -f framecrc -
}

ffmetadata(){
    ffmpeg "$@" -bitexact -f ffmetadata -
}

framemd5(){
    ffmpeg "$@" -bitexact -f framemd5 -
}

crc(){
    ffmpeg "$@" -f crc -
}

md5pipe(){
    ffmpeg "$@" md5:
}

md5(){
    encfile="${outdir}/${test}.out"
    cleanfiles="$cleanfiles $encfile"
    ffmpeg "$@" $encfile
    do_md5sum $encfile | awk '{print $1}'
}

pcm(){
    ffmpeg "$@" -vn -f s16le -
}

fmtstdout(){
    fmt=$1
    shift 1
    ffmpeg -bitexact "$@" -f $fmt -
}

enc_dec_pcm(){
    out_fmt=$1
    dec_fmt=$2
    pcm_fmt=$3
    src_file=$(target_path $4)
    shift 4
    encfile="${outdir}/${test}.${out_fmt}"
    cleanfiles=$encfile
    encfile=$(target_path ${encfile})
    ffmpeg -i $src_file "$@" -f $out_fmt -y ${encfile} || return
    ffmpeg -bitexact -i ${encfile} -c:a pcm_${pcm_fmt} -fflags +bitexact -f ${dec_fmt} -
}

FLAGS="-flags +bitexact -sws_flags +accurate_rnd+bitexact -fflags +bitexact"
DEC_OPTS="-threads $threads -idct simple $FLAGS"
ENC_OPTS="-threads 1        -idct simple -dct fastint"

enc_dec(){
    src_fmt=$1
    srcfile=$2
    enc_fmt=$3
    enc_opt=$4
    dec_fmt=$5
    dec_opt=$6
    encfile="${outdir}/${test}.${enc_fmt}"
    decfile="${outdir}/${test}.out.${dec_fmt}"
    cleanfiles="$cleanfiles $decfile"
    test "$7" = -keep || cleanfiles="$cleanfiles $encfile"
    tsrcfile=$(target_path $srcfile)
    tencfile=$(target_path $encfile)
    tdecfile=$(target_path $decfile)
    ffmpeg -f $src_fmt $DEC_OPTS -i $tsrcfile $ENC_OPTS $enc_opt $FLAGS \
        -f $enc_fmt -y $tencfile || return
    do_md5sum $encfile
    echo $(wc -c $encfile)
    ffmpeg $8 $DEC_OPTS -i $tencfile $ENC_OPTS $dec_opt $FLAGS \
        -f $dec_fmt -y $tdecfile || return
    do_md5sum $decfile
    tests/tiny_psnr $srcfile $decfile $cmp_unit $cmp_shift
}

transcode(){
    src_fmt=$1
    srcfile=$2
    enc_fmt=$3
    enc_opt=$4
    final_decode=$5
    encfile="${outdir}/${test}.${enc_fmt}"
    test "$7" = -keep || cleanfiles="$cleanfiles $encfile"
    tsrcfile=$(target_path $srcfile)
    tencfile=$(target_path $encfile)
    ffmpeg -f $src_fmt $DEC_OPTS -i $tsrcfile $ENC_OPTS $enc_opt $FLAGS \
        -f $enc_fmt -y $tencfile || return
    do_md5sum $encfile
    echo $(wc -c $encfile)
    ffmpeg $DEC_OPTS -i $encfile $ENC_OPTS $FLAGS $final_decode \
        -f framecrc - || return
}

stream_remux(){
    src_fmt=$1
    srcfile=$2
    enc_fmt=$3
    stream_maps=$4
    final_decode=$5
    encfile="${outdir}/${test}.${enc_fmt}"
    test "$7" = -keep || cleanfiles="$cleanfiles $encfile"
    tsrcfile=$(target_path $srcfile)
    tencfile=$(target_path $encfile)
    ffmpeg -f $src_fmt -i $tsrcfile $stream_maps -codec copy $FLAGS \
        -f $enc_fmt -y $tencfile || return
    ffmpeg $DEC_OPTS -i $encfile $ENC_OPTS $FLAGS $final_decode \
        -f framecrc - || return
}

lavffatetest(){
    t="${test#lavf-fate-}"
    ref=${base}/ref/lavf-fate/$t
    ${base}/lavf-regression.sh $t lavf-fate tests/vsynth1 "$target_exec" "$target_path" "$threads" "$thread_type" "$cpuflags" "$target_samples"
}

lavftest(){
    t="${test#lavf-}"
    ref=${base}/ref/lavf/$t
    ${base}/lavf-regression.sh $t lavf tests/vsynth1 "$target_exec" "$target_path" "$threads" "$thread_type" "$cpuflags" "$target_samples"
}

refcmp_metadata(){
    refcmp=$1
    pixfmt=$2
    fuzz=${3:-0.001}
    ffmpeg $FLAGS $ENC_OPTS \
        -lavfi "testsrc2=size=300x200:rate=1:duration=5,format=${pixfmt},split[ref][tmp];[tmp]avgblur=4[enc];[enc][ref]${refcmp},metadata=print:file=-" \
        -f null /dev/null | awk -v ref=${ref} -v fuzz=${fuzz} -f ${base}/refcmp-metadata.awk -
}

video_filter(){
    filters=$1
    shift
    label=${test#filter-}
    raw_src="${target_path}/tests/vsynth1/%02d.pgm"
    printf '%-20s' $label
    ffmpeg $DEC_OPTS -f image2 -vcodec pgmyuv -i $raw_src \
        $FLAGS $ENC_OPTS -vf "$filters" -vcodec rawvideo -frames:v 5 $* -f nut md5:
}

pixfmts(){
    filter=${test#filter-pixfmts-}
    filter=${filter%_*}
    filter_args=$1
    prefilter_chain=$2
    nframes=${3:-1}

    showfiltfmts="$target_exec $target_path/libavfilter/tests/filtfmts"
    scale_exclude_fmts=${outfile}_scale_exclude_fmts
    scale_in_fmts=${outfile}_scale_in_fmts
    scale_out_fmts=${outfile}_scale_out_fmts
    in_fmts=${outfile}_in_fmts

    # exclude pixel formats which are not supported as input
    $showfiltfmts scale | awk -F '[ \r]' '/^INPUT/{ fmt=substr($3, 5); print fmt }' | sort >$scale_in_fmts
    $showfiltfmts scale | awk -F '[ \r]' '/^OUTPUT/{ fmt=substr($3, 5); print fmt }' | sort >$scale_out_fmts
    comm -12 $scale_in_fmts $scale_out_fmts >$scale_exclude_fmts

    $showfiltfmts $filter | awk -F '[ \r]' '/^INPUT/{ fmt=substr($3, 5); print fmt }' | sort >$in_fmts
    pix_fmts=$(comm -12 $scale_exclude_fmts $in_fmts)

    outertest=$test
    for pix_fmt in $pix_fmts; do
        test=$pix_fmt
        video_filter "${prefilter_chain}format=$pix_fmt,$filter=$filter_args" -pix_fmt $pix_fmt -frames:v $nframes
    done

    rm $in_fmts $scale_in_fmts $scale_out_fmts $scale_exclude_fmts
    test=$outertest
}

gapless(){
    sample=$(target_path $1)
    extra_args=$2

    decfile1="${outdir}/${test}.out-1"
    decfile2="${outdir}/${test}.out-2"
    decfile3="${outdir}/${test}.out-3"
    cleanfiles="$cleanfiles $decfile1 $decfile2 $decfile3"

    # test packet data
    ffmpeg $extra_args -i "$sample" -bitexact -c:a copy -f framecrc -y $decfile1
    do_md5sum $decfile1
    # test decoded (and cut) data
    ffmpeg $extra_args -i "$sample" -bitexact -f wav md5:
    # the same as above again, with seeking to the start
    ffmpeg $extra_args -ss 0 -seek_timestamp 1 -i "$sample" -bitexact -c:a copy -f framecrc -y $decfile2
    do_md5sum $decfile2
    ffmpeg $extra_args -ss 0 -seek_timestamp 1 -i "$sample" -bitexact -f wav md5:
    # test packet data, with seeking to a specific position
    ffmpeg $extra_args -ss 5 -seek_timestamp 1 -i "$sample" -bitexact -c:a copy -f framecrc -y $decfile3
    do_md5sum $decfile3
}

gaplessenc(){
    sample=$(target_path $1)
    format=$2
    codec=$3

    file1="${outdir}/${test}.out-1"
    cleanfiles="$cleanfiles $file1"

    # test data after reencoding
    ffmpeg -i "$sample" -bitexact -map 0:a -c:a $codec -f $format -y "$file1"
    probegaplessinfo "$file1"
}

audio_match(){
    sample=$(target_path $1)
    trefile=$(target_path $2)
    extra_args=$3

    decfile="${outdir}/${test}.wav"
    cleanfiles="$cleanfiles $decfile"

    ffmpeg -i "$sample" -bitexact $extra_args -y $decfile
    tests/audiomatch $decfile $trefile
}

concat(){
    template=$1
    sample=$2
    mode=$3
    extra_args=$4

    concatfile="${outdir}/${test}.ffconcat"
    packetfile="${outdir}/${test}.ffprobe"
    cleanfiles="$concatfile $packetfile"

    awk "{gsub(/%SRCFILE%/, \"$sample\"); print}" $template > $concatfile

    if [ "$mode" = "md5" ]; then
        run ffprobe${PROGSUF} -bitexact -show_streams -show_packets -v 0 -fflags keepside -safe 0 $extra_args $concatfile | tr -d '\r' > $packetfile
        do_md5sum $packetfile
    else
        run ffprobe${PROGSUF} -bitexact -show_streams -show_packets -v 0 -of compact=p=0:nk=1 -fflags keepside -safe 0 $extra_args $concatfile
    fi
}

null(){
    :
}

mkdir -p "$outdir"

# Disable globbing: command arguments may contain globbing characters and
# must be kept verbatim
set -f

exec 3>&2
eval $command >"$outfile" 2>$errfile
err=$?

if [ $err -gt 128 ]; then
    sig=$(kill -l $err 2>/dev/null)
    test "${sig}" = "${sig%[!A-Za-z]*}" || unset sig
fi

if test -e "$ref" || test $cmp = "oneline" || test $cmp = "null" || test $cmp = "grep" ; then
    case $cmp in
        diff)   diff -u -b "$ref" "$outfile"            >$cmpfile ;;
        rawdiff)diff -u    "$ref" "$outfile"            >$cmpfile ;;
        oneoff) oneoff     "$ref" "$outfile"            >$cmpfile ;;
        stddev) stddev     "$ref" "$outfile"            >$cmpfile ;;
        oneline)oneline    "$ref" "$outfile"            >$cmpfile ;;
        grep)   grep       "$ref" "$errfile"            >$cmpfile ;;
        null)   cat               "$outfile"            >$cmpfile ;;
    esac
    cmperr=$?
    test $err = 0 && err=$cmperr
    if [ "$report_type" = "ignore" ]; then
        test $err = 0 || echo "IGNORE\t${test}" && err=0 && unset sig
    else
        test $err = 0 || cat $cmpfile
    fi
else
    echo "reference file '$ref' not found"
    err=1
fi

if [ $err -eq 0 ] && test $report_type = "standard" ; then
    unset cmpo erro
else
    cmpo="$($base64 <$cmpfile)"
    erro="$($base64 <$errfile)"
fi
echo "${test}:${sig:-$err}:$cmpo:$erro" >$repfile

if test $err != 0 && test $gen != "no" ; then
    echo "GEN     $ref"
    cp -f "$outfile" "$ref"
    err=$?
fi

if test $err = 0; then
    rm -f $outfile $errfile $cmpfile $cleanfiles
elif test $gen = "no"; then
    echo "Test $test failed. Look at $errfile for details."
    test "${V:-0}" -gt 0 && cat $errfile
else
    echo "Updating reference failed, possibly no output file was generated."
fi
exit $err
