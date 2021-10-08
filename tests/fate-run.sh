#! /bin/sh

export LC_ALL=C

base=$(dirname $0)
. "${base}/md5.sh"

base64=tests/base64${HOSTEXECSUF}

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
keep=${19:-0}

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
    psnr=$(tests/tiny_psnr${HOSTEXECSUF} "$1" "$2" $cmp_unit $cmp_shift 0) || return 1
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
    run ffprobe${PROGSUF}${EXECSUF} -show_entries format=format_name -print_format default=nw=1:nk=1 "$@"
}

probeaudiostream(){
    run ffprobe${PROGSUF}${EXECSUF} -show_entries stream=codec_name,codec_time_base,sample_fmt,channels,channel_layout:side_data "$@"
}

probetags(){
    run ffprobe${PROGSUF}${EXECSUF} -show_entries format_tags "$@"
}

runlocal(){
    test "${V:-0}" -gt 0 && echo ${base}/"$@" ${base} >&3
    ${base}/"$@" ${base}
}

probeframes(){
    run ffprobe${PROGSUF}${EXECSUF} -show_frames "$@"
}

probechapters(){
    run ffprobe${PROGSUF}${EXECSUF} -show_chapters "$@"
}

probegaplessinfo(){
    filename="$1"
    shift
    run ffprobe${PROGSUF}${EXECSUF} -bitexact -select_streams a -show_entries format=start_time,duration:stream=index,start_pts,duration_ts "$filename" "$@"
    pktfile1="${outdir}/${test}.pkts"
    framefile1="${outdir}/${test}.frames"
    cleanfiles="$cleanfiles $pktfile1 $framefile1"
    run ffprobe${PROGSUF}${EXECSUF} -bitexact -select_streams a -of compact -count_packets -show_entries packet=pts,dts,duration,flags:stream=nb_read_packets "$filename" "$@" > "$pktfile1"
    head -n 8 "$pktfile1"
    tail -n 9 "$pktfile1"
    run ffprobe${PROGSUF}${EXECSUF} -bitexact -select_streams a -of compact -count_frames -show_entries frame=pts,pkt_dts,best_effort_timestamp,pkt_duration,nb_samples:stream=nb_read_frames "$filename" "$@" > "$framefile1"
    head -n 8 "$framefile1"
    tail -n 9 "$framefile1"
}

ffmpeg(){
    dec_opts="-hwaccel $hwaccel -threads $threads -thread_type $thread_type"
    ffmpeg_args="-nostdin -nostats -noauto_conversion_filters -cpuflags $cpuflags"
    for arg in $@; do
        [ x${arg} = x-i ] && ffmpeg_args="${ffmpeg_args} ${dec_opts}"
        ffmpeg_args="${ffmpeg_args} ${arg}"
    done
    run ffmpeg${PROGSUF}${EXECSUF} ${ffmpeg_args}
}

ffprobe_demux(){
    filename=$1
    shift
    print_filename=$(basename "$filename")
    run ffprobe${PROGSUF}${EXECSUF} -print_filename "${print_filename}" \
        -of compact -bitexact -show_format -show_streams -show_packets  \
        -show_data_hash CRC32 "$filename" "$@"
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
    ffmpeg -y "$@" $(target_path $encfile) || return
    do_md5sum $encfile | awk '{print $1}'
}

pcm(){
    ffmpeg -auto_conversion_filters "$@" -vn -f s16le -
}

fmtstdout(){
    fmt=$1
    shift 1
    ffmpeg -bitexact "$@" -bitexact -f $fmt -
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
    ffmpeg -auto_conversion_filters -i $src_file "$@" -f $out_fmt -y ${encfile} || return
    ffmpeg -auto_conversion_filters -bitexact -i ${encfile} -c:a pcm_${pcm_fmt} -fflags +bitexact -f ${dec_fmt} -
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
    ffprobe_opts=$9
    encfile="${outdir}/${test}.${enc_fmt}"
    decfile="${outdir}/${test}.out.${dec_fmt}"
    cleanfiles="$cleanfiles $decfile"
    test "$7" = -keep || cleanfiles="$cleanfiles $encfile"
    tsrcfile=$(target_path $srcfile)
    tencfile=$(target_path $encfile)
    tdecfile=$(target_path $decfile)
    ffmpeg -auto_conversion_filters -f $src_fmt $DEC_OPTS -i $tsrcfile $ENC_OPTS $enc_opt $FLAGS \
        -f $enc_fmt -y $tencfile || return
    do_md5sum $encfile
    echo $(wc -c $encfile)
    ffmpeg -auto_conversion_filters $8 $DEC_OPTS -i $tencfile $ENC_OPTS $dec_opt $FLAGS \
        -f $dec_fmt -y $tdecfile || return
    do_md5sum $decfile
    tests/tiny_psnr${HOSTEXECSUF} $srcfile $decfile $cmp_unit $cmp_shift
    test -z $ffprobe_opts || \
        run ffprobe${PROGSUF}${EXECSUF} $ffprobe_opts $tencfile || return
}

transcode(){
    src_fmt=$1
    srcfile=$2
    enc_fmt=$3
    enc_opt=$4
    final_decode=$5
    ffprobe_opts=$7
    additional_input=$8
    test -z "$additional_input" || additional_input="$DEC_OPTS $additional_input"
    encfile="${outdir}/${test}.${enc_fmt}"
    test "$6" = -keep || cleanfiles="$cleanfiles $encfile"
    tsrcfile=$(target_path $srcfile)
    tencfile=$(target_path $encfile)
    ffmpeg -f $src_fmt $DEC_OPTS -i $tsrcfile $additional_input \
           $ENC_OPTS $enc_opt $FLAGS -f $enc_fmt -y $tencfile || return
    do_md5sum $encfile
    echo $(wc -c $encfile)
    ffmpeg $DEC_OPTS -i $tencfile $ENC_OPTS $FLAGS $final_decode \
        -f framecrc - || return
    test -z $ffprobe_opts || \
        run ffprobe${PROGSUF}${EXECSUF} $ffprobe_opts $tencfile || return
}

stream_remux(){
    src_fmt=$1
    srcfile=$2
    enc_fmt=$3
    stream_maps=$4
    final_decode=$5
    ffprobe_opts=$7
    encfile="${outdir}/${test}.${enc_fmt}"
    test "$6" = -keep || cleanfiles="$cleanfiles $encfile"
    tsrcfile=$(target_path $srcfile)
    tencfile=$(target_path $encfile)
    ffmpeg -f $src_fmt -i $tsrcfile $stream_maps -codec copy $FLAGS \
        -f $enc_fmt -y $tencfile || return
    ffmpeg $DEC_OPTS -i $tencfile $ENC_OPTS $FLAGS $final_decode \
        -f framecrc - || return
    test -z $ffprobe_opts || \
        run ffprobe${PROGSUF}${EXECSUF} $ffprobe_opts $tencfile || return
}

# FIXME: There is a certain duplication between the avconv-related helper
# functions above and below that should be refactored.
ffmpeg2="$target_exec ${target_path}/ffmpeg${PROGSUF}${EXECSUF}"
raw_src="${target_path}/tests/vsynth1/%02d.pgm"
pcm_src="${target_path}/tests/data/asynth1.sw"
crcfile="tests/data/$test.lavf.crc"
target_crcfile="${target_path}/$crcfile"

[ "${V-0}" -gt 0 ] && echov=echov || echov=:

echov(){
    echo "$@" >&3
}

AVCONV_OPTS="-nostdin -nostats -noauto_conversion_filters -y -cpuflags $cpuflags -filter_threads $threads"
COMMON_OPTS="-flags +bitexact -idct simple -sws_flags +accurate_rnd+bitexact -fflags +bitexact"
DEC_OPTS="$COMMON_OPTS -threads $threads"
ENC_OPTS="$COMMON_OPTS -threads 1 -dct fastint"

run_avconv(){
    $echov $ffmpeg2 $AVCONV_OPTS $*
    $ffmpeg2 $AVCONV_OPTS $*
}

do_avconv(){
    f="$1"
    shift
    set -- $* ${target_path}/$f
    run_avconv $*
    do_md5sum $f
    echo $(wc -c $f)
}

do_avconv_crc(){
    f="$1"
    shift
    run_avconv $* -f crc "$target_crcfile"
    echo "$f $(cat $crcfile)"
}

lavf_audio(){
    t="${test#lavf-}"
    outdir="tests/data/lavf"
    file=${outdir}/lavf.$t
    do_avconv $file -auto_conversion_filters $DEC_OPTS $1 -ar 44100 -f s16le -i $pcm_src "$ENC_OPTS -metadata title=lavftest" -t 1 -qscale 10 $2
    do_avconv_crc $file -auto_conversion_filters $DEC_OPTS $3 -i $target_path/$file
}

lavf_container(){
    t="${test#lavf-}"
    outdir="tests/data/lavf"
    file=${outdir}/lavf.$t
    do_avconv $file -auto_conversion_filters $DEC_OPTS -f image2 -c:v pgmyuv -i $raw_src $DEC_OPTS -ar 44100 -f s16le $1 -i $pcm_src "$ENC_OPTS -metadata title=lavftest" -b:a 64k -t 1 -qscale:v 10 $2
    test "$3" = "disable_crc" ||
        do_avconv_crc $file -auto_conversion_filters $DEC_OPTS -i $target_path/$file $3
}

lavf_container_attach() {          lavf_container "" "$1 -attach ${raw_src%/*}/00.pgm -metadata:s:t mimetype=image/x-portable-greymap"; }
lavf_container_timecode_nodrop() { lavf_container "" "$1 -timecode 02:56:14:13"; }
lavf_container_timecode_drop()   { lavf_container "" "$1 -timecode 02:56:14.13 -r 30000/1001"; }

lavf_container_timecode()
{
    lavf_container_timecode_nodrop "$@"
    lavf_container_timecode_drop "$@"
    lavf_container "" "$1"
}

lavf_container_fate()
{
    t="${test#lavf-fate-}"
    outdir="tests/data/lavf-fate"
    file=${outdir}/lavf.$t
    input="${target_samples}/$1"
    do_avconv $file -auto_conversion_filters $DEC_OPTS $2 -i "$input" "$ENC_OPTS -metadata title=lavftest" -vcodec copy -acodec copy
    do_avconv_crc $file -auto_conversion_filters $DEC_OPTS -i $target_path/$file $3
}

lavf_image(){
    t="${test#lavf-}"
    outdir="tests/data/images/$t"
    mkdir -p "$outdir"
    file=${outdir}/%02d.$t
    run_avconv $DEC_OPTS -f image2 -c:v pgmyuv -i $raw_src $1 "$ENC_OPTS -metadata title=lavftest" -vf scale -frames 13 -y -qscale 10 $target_path/$file
    do_md5sum ${outdir}/02.$t
    do_avconv_crc $file -auto_conversion_filters $DEC_OPTS $2 -i $target_path/$file $2
    echo $(wc -c ${outdir}/02.$t)
}

lavf_image2pipe(){
    t="${test#lavf-}"
    t="${t%pipe}"
    outdir="tests/data/lavf"
    file=${outdir}/${t}pipe.$t
    do_avconv $file -auto_conversion_filters $DEC_OPTS -f image2 -c:v pgmyuv -i $raw_src -f image2pipe "$ENC_OPTS -metadata title=lavftest" -t 1 -qscale 10
    do_avconv_crc $file -auto_conversion_filters $DEC_OPTS -f image2pipe -i $target_path/$file
}

lavf_video(){
    t="${test#lavf-}"
    outdir="tests/data/lavf"
    file=${outdir}/lavf.$t
    do_avconv $file -auto_conversion_filters $DEC_OPTS -f image2 -c:v pgmyuv -i $raw_src "$ENC_OPTS -metadata title=lavftest" -t 1 -qscale 10 $1 $2
    do_avconv_crc $file -auto_conversion_filters $DEC_OPTS -i $target_path/$file $1
}

refcmp_metadata(){
    refcmp=$1
    pixfmt=$2
    fuzz=${3:-0.001}
    ffmpeg $FLAGS $ENC_OPTS \
        -lavfi "testsrc2=size=300x200:rate=1:duration=5,format=${pixfmt},split[ref][tmp];[tmp]avgblur=4[enc];[enc][ref]${refcmp},metadata=print:file=-" \
        -f null /dev/null | awk -v ref=${ref} -v fuzz=${fuzz} -f ${base}/refcmp-metadata.awk -
}

pixfmt_conversion(){
    conversion="${test#pixfmt-}"
    outdir="tests/data/pixfmt"
    raw_dst="$outdir/$conversion.out.yuv"
    file=${outdir}/${conversion}.yuv
    run_avconv $DEC_OPTS -r 1 -f image2 -c:v pgmyuv -i $raw_src \
               $ENC_OPTS -f rawvideo -t 1 -s 352x288 -pix_fmt $conversion $target_path/$raw_dst
    do_avconv $file $DEC_OPTS -f rawvideo -s 352x288 -pix_fmt $conversion -i $target_path/$raw_dst \
              $ENC_OPTS -f rawvideo -s 352x288 -pix_fmt yuv444p
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

    showfiltfmts="$target_exec $target_path/libavfilter/tests/filtfmts${EXECSUF}"
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
        video_filter "${prefilter_chain}scale,format=$pix_fmt,$filter=$filter_args" -pix_fmt $pix_fmt -frames:v $nframes
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
    ffmpeg -auto_conversion_filters $extra_args -i "$sample" -bitexact -c:a copy -f framecrc -y $(target_path $decfile1)
    do_md5sum $decfile1
    # test decoded (and cut) data
    ffmpeg -auto_conversion_filters $extra_args -i "$sample" -bitexact -f wav md5:
    # the same as above again, with seeking to the start
    ffmpeg -auto_conversion_filters $extra_args -ss 0 -seek_timestamp 1 -i "$sample" -bitexact -c:a copy -f framecrc -y $(target_path $decfile2)
    do_md5sum $decfile2
    ffmpeg -auto_conversion_filters $extra_args -ss 0 -seek_timestamp 1 -i "$sample" -bitexact -f wav md5:
    # test packet data, with seeking to a specific position
    ffmpeg -auto_conversion_filters $extra_args -ss 5 -seek_timestamp 1 -i "$sample" -bitexact -c:a copy -f framecrc -y $(target_path $decfile3)
    do_md5sum $decfile3
}

gaplessenc(){
    sample=$(target_path $1)
    format=$2
    codec=$3

    file1="${outdir}/${test}.out-1"
    cleanfiles="$cleanfiles $file1"

    # test data after reencoding
    ffmpeg -i "$sample" -bitexact -map 0:a -c:a $codec -af aresample -f $format -y "$(target_path "$file1")"
    probegaplessinfo "$(target_path "$file1")"
}

audio_match(){
    sample=$(target_path $1)
    trefile=$2
    extra_args=$3

    decfile="${outdir}/${test}.wav"
    cleanfiles="$cleanfiles $decfile"

    ffmpeg -auto_conversion_filters -i "$sample" -bitexact $extra_args -y $(target_path $decfile)
    tests/audiomatch${HOSTEXECSUF} $decfile $trefile
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
        run ffprobe${PROGSUF}${EXECSUF} -bitexact -show_streams -show_packets -safe 0 $extra_args $(target_path $concatfile) | tr -d '\r' > $packetfile
        do_md5sum $packetfile
    else
        run ffprobe${PROGSUF}${EXECSUF} -bitexact -show_streams -show_packets -of compact=p=0:nk=1 -safe 0 $extra_args $(target_path $concatfile)
    fi
}

venc_data(){
    file=$1
    stream=$2
    frames=$3
    run tools/venc_data_dump${EXECSUF} ${file} ${stream} ${frames} ${threads} ${thread_type}
}

null(){
    :
}

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
    if test $keep = 0; then
        rm -f $outfile $errfile $cmpfile $cleanfiles
    fi
elif test $gen = "no"; then
    echo "Test $test failed. Look at $errfile for details."
    test "${V:-0}" -gt 0 && cat $errfile
else
    echo "Updating reference failed, possibly no output file was generated."
fi
exit $err
