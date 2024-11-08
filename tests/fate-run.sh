#!/bin/sh

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

case $threads in
    random*)
        threads_max=${threads#random}
        [ -z "$threads_max" ] && threads_max=16
        threads=$(awk "BEGIN { srand(); print 1+int(rand() * $threads_max) }" < /dev/null)
        ;;
esac


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
    run ffprobe${PROGSUF}${EXECSUF} -bitexact -show_entries format=format_name -print_format default=nw=1:nk=1 "$@"
}

probeaudiostream(){
    run ffprobe${PROGSUF}${EXECSUF} -bitexact -show_entries stream=codec_name,codec_time_base,sample_fmt,channels,channel_layout:side_data "$@"
}

probetags(){
    run ffprobe${PROGSUF}${EXECSUF} -bitexact -show_entries format_tags "$@"
}

runlocal(){
    test "${V:-0}" -gt 0 && echo ${base}/"$@" ${base} >&3
    ${base}/"$@" ${base}
}

probeframes(){
    run ffprobe${PROGSUF}${EXECSUF} -bitexact -show_frames "$@"
}

probechapters(){
    run ffprobe${PROGSUF}${EXECSUF} -bitexact -show_chapters "$@"
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

SCALE_FLAGS="+accurate_rnd+bitexact"
FLAGS="-flags +bitexact -sws_flags $SCALE_FLAGS -fflags +bitexact"
DEC_OPTS="-threads $threads -thread_type $thread_type -idct simple $FLAGS"
ENC_OPTS="-threads 1        -idct simple -dct fastint"

enc_dec(){
    enc_fmt_in=$1
    srcfile=$2
    enc_fmt_out=$3
    enc_opt_out=$4
    dec_fmt_out=$5
    dec_opt_out=$6
    dec_opt_in=$7
    ffprobe_opts=$8
    twopass=$9
    encfile="${outdir}/${test}.${enc_fmt_out}"
    decfile="${outdir}/${test}.out.${dec_fmt_out}"
    cleanfiles="$cleanfiles $decfile"
    test "$keep" -ge 1 || cleanfiles="$cleanfiles $encfile"
    tsrcfile=$(target_path $srcfile)
    tencfile=$(target_path $encfile)
    tdecfile=$(target_path $decfile)

    if [ -n "$twopass" ]; then
        logfile_prefix="${outdir}/${test}.pass1"
        cleanfiles="$cleanfiles ${logfile_prefix}-0.log"
        tlogfile_prefix=$(target_path $logfile_prefix)
        ffmpeg -auto_conversion_filters -f $enc_fmt_in $DEC_OPTS -i $tsrcfile  \
            $ENC_OPTS $enc_opt_out $FLAGS -pass 1 -passlogfile $tlogfile_prefix \
            -f $enc_fmt_out -y $tencfile || return
        enc_opt_out="$enc_opt_out -pass 2 -passlogfile $tlogfile_prefix"
    fi

    ffmpeg -auto_conversion_filters -f $enc_fmt_in $DEC_OPTS -i $tsrcfile $ENC_OPTS $enc_opt_out $FLAGS \
        -f $enc_fmt_out -y $tencfile || return
    do_md5sum $encfile
    echo $(wc -c $encfile)
    ffmpeg -auto_conversion_filters $dec_opt_in $DEC_OPTS -i $tencfile $ENC_OPTS $dec_opt_out $FLAGS \
        -f $dec_fmt_out -y $tdecfile || return
    do_md5sum $decfile
    tests/tiny_psnr${HOSTEXECSUF} $srcfile $decfile $cmp_unit $cmp_shift
    test -z "$ffprobe_opts" || \
        run ffprobe${PROGSUF}${EXECSUF} -bitexact $ffprobe_opts $tencfile || return
}

transcode(){
    src_fmt=$1
    srcfile=$2
    enc_fmt=$3
    enc_opt=$4
    final_encode=$5
    ffprobe_opts=$6
    additional_input=$7
    final_decode=$8
    enc_opt_in=$9
    test -z "$additional_input" || additional_input="$DEC_OPTS $additional_input"
    encfile="${outdir}/${test}.${enc_fmt}"
    test $keep -ge 1 || cleanfiles="$cleanfiles $encfile"
    tsrcfile=$(target_path $srcfile)
    tencfile=$(target_path $encfile)
    ffmpeg -f $src_fmt $DEC_OPTS $enc_opt_in -i $tsrcfile $additional_input \
           $ENC_OPTS $enc_opt $FLAGS -f $enc_fmt -y $tencfile || return
    do_md5sum $encfile
    echo $(wc -c $encfile)
    ffmpeg $DEC_OPTS $final_decode -i $tencfile $ENC_OPTS $FLAGS $final_encode \
        -f framecrc - || return
    test -z "$ffprobe_opts" || \
        run ffprobe${PROGSUF}${EXECSUF} -bitexact $ffprobe_opts $tencfile || return
}

stream_demux(){
    src_fmt=$1
    srcfile=$2
    src_opts=$3
    enc_opts=$4
    ffprobe_opts=$5
    tsrcfile=$(target_path $srcfile)
    ffmpeg $DEC_OPTS -f $src_fmt $src_opts -i $tsrcfile $ENC_OPTS $FLAGS $enc_opts \
        -f framecrc - || return
    test -z "$ffprobe_opts" || \
        run ffprobe${PROGSUF}${EXECSUF} -bitexact $ffprobe_opts $tsrcfile || return
}

stream_remux(){
    src_fmt=$1
    srcfile=$2
    src_opts=$3
    enc_fmt=$4
    stream_maps=$5
    final_decode=$6
    final_encode=$7
    ffprobe_opts=$8
    encfile="${outdir}/${test}.${enc_fmt}"
    test $keep -ge 1 || cleanfiles="$cleanfiles $encfile"
    tsrcfile=$(target_path $srcfile)
    tencfile=$(target_path $encfile)
    ffmpeg -f $src_fmt $src_opts -i $tsrcfile $stream_maps -codec copy $FLAGS \
        -f $enc_fmt -y $tencfile || return
    ffmpeg $DEC_OPTS $final_decode -i $tencfile $ENC_OPTS $FLAGS $final_encode \
        -f framecrc - || return
    test -z "$ffprobe_opts" || \
        run ffprobe${PROGSUF}${EXECSUF} -bitexact $ffprobe_opts $tencfile || return
}

# this function is for testing external encoders,
# where the precise output is not controlled by us
# we can still test e.g. that the output can be decoded correctly
enc_external(){
    srcfile=$1
    enc_fmt=$2
    enc_opt=$3
    probe_opt=$4

    srcfile=$(target_path $srcfile)
    encfile=$(target_path "${outdir}/${test}.${enc_fmt}")

    ffmpeg -i $srcfile $enc_opt -f $enc_fmt -y $encfile || return
    run ffprobe${PROGSUF}${EXECSUF} -bitexact $probe_opt $encfile || return
}

# FIXME: There is a certain duplication between the avconv-related helper
# functions above and below that should be refactored.
ffmpeg2="$target_exec ${target_path}/ffmpeg${PROGSUF}${EXECSUF}"
raw_src="${target_path}/tests/vsynth1/%02d.pgm"
pcm_src="${target_path}/tests/data/asynth1.sw"

[ "${V-0}" -gt 0 ] && echov=echov || echov=:

echov(){
    echo "$@" >&3
}

AVCONV_OPTS="-nostdin -nostats -noauto_conversion_filters -y -cpuflags $cpuflags -filter_threads $threads"
COMMON_OPTS="-flags +bitexact -idct simple -sws_flags $SCALE_FLAGS -fflags +bitexact"
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
    run_avconv $* || return
    do_md5sum $f
    echo $(wc -c $f)
}

do_avconv_crc(){
    f="$1"
    shift
    printf "%s " "$f"
    run_avconv $* -f crc -
}

lavf_audio(){
    t="${test#lavf-}"
    outdir="tests/data/lavf"
    file=${outdir}/lavf.$t
    test "$keep" -ge 1 || cleanfiles="$cleanfiles $file"
    do_avconv $file -auto_conversion_filters $DEC_OPTS $1 -ar 44100 -f s16le -i $pcm_src \
              "$ENC_OPTS -metadata title=lavftest" -t 1 -qscale 10 $2 || return
    test "$4" = "disable_crc" ||
        do_avconv_crc $file -auto_conversion_filters $DEC_OPTS $3 -i $target_path/$file
}

lavf_container(){
    t="${test#lavf-}"
    outdir="tests/data/lavf"
    file=${outdir}/lavf.$t
    test "$keep" -ge 1 || cleanfiles="$cleanfiles $file"
    do_avconv $file -auto_conversion_filters $DEC_OPTS -f image2 -c:v pgmyuv -i $raw_src $DEC_OPTS \
              -ar 44100 -f s16le $1 -i $pcm_src "$ENC_OPTS -metadata title=lavftest" -b:a 64k -t 1 -qscale:v 10 $2 || return
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
    cleanfiles="$cleanfiles $file"
    input="${target_samples}/$1"
    do_avconv $file -auto_conversion_filters $DEC_OPTS $2 -i "$input" \
              "$ENC_OPTS -metadata title=lavftest" -vcodec copy -acodec copy || return
    do_avconv_crc $file -auto_conversion_filters $DEC_OPTS -i $target_path/$file $3
}

lavf_image(){
    no_file_checksums="$3"
    nb_frames=13
    t="${test#lavf-}"
    outdir="tests/data/images/$t"
    mkdir -p "$outdir"
    file=${outdir}/%02d.$t
    if [ "$keep" -lt 1 ]; then
        for i in `seq $nb_frames`; do
            filename=`printf "$file" $i`
            cleanfiles="$cleanfiles $filename"
        done
    fi
    run_avconv $DEC_OPTS -f image2 -c:v pgmyuv -i $raw_src $1 \
              "$ENC_OPTS -metadata title=lavftest" -vf scale -frames $nb_frames \
              -y -qscale 10 $target_path/$file || return
    if [ -z "$no_file_checksums" ]; then
        do_md5sum ${outdir}/02.$t
        echo $(wc -c ${outdir}/02.$t)
    fi
    do_avconv_crc $file -auto_conversion_filters $DEC_OPTS $2 -i $target_path/$file $2
}

lavf_image2pipe(){
    t="${test#lavf-}"
    t="${t%pipe}"
    outdir="tests/data/lavf"
    file=${outdir}/${t}pipe.$t
    do_avconv $file -auto_conversion_filters $DEC_OPTS -f image2 -c:v pgmyuv -i $raw_src \
              -f image2pipe "$ENC_OPTS -metadata title=lavftest" -t 1 -qscale 10 || return
    do_avconv_crc $file -auto_conversion_filters $DEC_OPTS -f image2pipe -i $target_path/$file
}

lavf_video(){
    t="${test#lavf-}"
    outdir="tests/data/lavf"
    file=${outdir}/lavf.$t
    test "$keep" -ge 1 || cleanfiles="$cleanfiles $file"
    do_avconv $file -auto_conversion_filters $DEC_OPTS -f image2 -c:v pgmyuv -i $raw_src \
              "$ENC_OPTS -metadata title=lavftest" -t 1 -qscale 10 $1 $2 || return
    do_avconv_crc $file -auto_conversion_filters $DEC_OPTS -i $target_path/$file $1
}

refcmp_metadata(){
    refcmp=$1
    pixfmt=$2
    fuzz=${3:-0.001}
    ffmpeg -auto_conversion_filters $FLAGS $ENC_OPTS \
        -lavfi "testsrc2=size=300x200:rate=1:duration=5,format=${pixfmt},split[ref][tmp];[tmp]avgblur=4[enc];[enc][ref]${refcmp},metadata=print:file=-" \
        -f null /dev/null | awk -v ref=${ref} -v fuzz=${fuzz} -f ${base}/refcmp-metadata.awk -
}

cmp_metadata(){
    refcmp=$1
    pixfmt=$2
    fuzz=${3:-0.001}
    ffmpeg $FLAGS $ENC_OPTS \
        -lavfi "testsrc2=size=300x200:rate=1:duration=5,format=${pixfmt},${refcmp},metadata=print:file=-" \
        -f null /dev/null | awk -v ref=${ref} -v fuzz=${fuzz} -f ${base}/refcmp-metadata.awk -
}

refcmp_metadata_files(){
    file1=$1
    file2=$2
    refcmp=$3
    pixfmt=$4
    fuzz=${5:-0.001}
    ffmpeg -auto_conversion_filters $FLAGS -i $file1 $FLAGS -i $file2 $ENC_OPTS \
        -lavfi "[0:v]format=${pixfmt}[v0];[1:v]format=${pixfmt}[v1];[v0][v1]${refcmp},metadata=print:file=-" \
        -f null /dev/null | awk -v ref=${ref} -v fuzz=${fuzz} -f ${base}/refcmp-metadata.awk -
}

refcmp_metadata_transcode(){
    srcfile=$1
    enc_opt=$2
    enc_fmt=$3
    enc_ext=$4
    shift 4
    encfile="${outdir}/${test}.${enc_ext}"
    cleanfiles="$cleanfiles $encfile"
    tsrcfile=$(target_path $srcfile)
    tencfile=$(target_path $encfile)
    ffmpeg $DEC_OPTS -i $tsrcfile $ENC_OPTS $enc_opt $FLAGS -y -f $enc_fmt $tencfile || return
    refcmp_metadata_files $tencfile $tsrcfile "$@"
}

pixfmt_conversion(){
    conversion="${test#pixfmt-}"
    outdir="tests/data/pixfmt"
    raw_dst="$outdir/$conversion.out.yuv"
    file=${outdir}/${conversion}.yuv
    cleanfiles="$cleanfiles $raw_dst $file"
    run_avconv $DEC_OPTS -r 1 -f image2 -c:v pgmyuv -i $raw_src \
               $ENC_OPTS -f rawvideo -t 1 -s 352x288 -pix_fmt $conversion $target_path/$raw_dst || return
    do_avconv $file $DEC_OPTS -f rawvideo -s 352x288 -pix_fmt $conversion -i $target_path/$raw_dst \
              $ENC_OPTS -f rawvideo -s 352x288 -pix_fmt yuv444p -color_range mpeg
}

pixfmt_conversion_ext(){
    prefix=$1
    suffix=$2
    color_range="${test#pixfmt-}"
    color_range=${color_range%-*}
    conversion="${test#pixfmt-$color_range-}"
    outdir="tests/data/pixfmt"
    file=${outdir}/${color_range}-${conversion}.yuv
    cleanfiles="$cleanfiles $file"
    do_avconv $file $DEC_OPTS -lavfi ${prefix}testsrc=s=352x288,format=${color_range},scale=flags=$SCALE_FLAGS:sws_dither=none,format=$conversion \
              $ENC_OPTS -t 1 -f rawvideo -s 352x288 -pix_fmt ${color_range}${suffix} -color_range mpeg
}

pixdesc(){
    pix_fmt=${test#filter-pixdesc-}
    label=${test#filter-}
    raw_src="${target_path}/tests/vsynth1/%02d.pgm"

    md5file1="${outdir}/${test}-1.md5"
    md5file2="${outdir}/${test}-2.md5"
    cleanfiles="$cleanfiles $md5file1 $md5file2"

    ffmpeg $DEC_OPTS -f image2 -vcodec pgmyuv -i $raw_src \
        $FLAGS $ENC_OPTS -vf "scale,format=$pix_fmt" -vcodec rawvideo -frames:v 5 \
        "-pix_fmt $pix_fmt" -f nut md5:$md5file1
    ffmpeg $DEC_OPTS -f image2 -vcodec pgmyuv -i $raw_src \
        $FLAGS $ENC_OPTS -vf "scale,format=$pix_fmt,pixdesctest" -vcodec rawvideo -frames:v 5 \
        "-pix_fmt $pix_fmt" -f nut md5:$md5file2

    diff -q $md5file1 $md5file2 || return
    printf '%-20s' $label
    cat $md5file1
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
        video_filter "${prefilter_chain}scale,format=$pix_fmt,$filter=$filter_args" -pix_fmt $pix_fmt -frames:v $nframes || return
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
    ffmpeg -auto_conversion_filters $extra_args -i "$sample" \
           -bitexact -c:a copy -f framecrc -y $(target_path $decfile1) || return
    do_md5sum $decfile1
    # test decoded (and cut) data
    ffmpeg -auto_conversion_filters $extra_args -i "$sample" -bitexact -f wav md5: || return
    # the same as above again, with seeking to the start
    ffmpeg -auto_conversion_filters $extra_args -ss 0 -seek_timestamp 1 -i "$sample" \
           -bitexact -c:a copy -f framecrc -y $(target_path $decfile2) || return
    do_md5sum $decfile2
    ffmpeg -auto_conversion_filters $extra_args -ss 0 -seek_timestamp 1 -i "$sample" \
           -bitexact -f wav md5: || return
    # test packet data, with seeking to a specific position
    ffmpeg -auto_conversion_filters $extra_args -ss 5 -seek_timestamp 1 -i "$sample" \
           -bitexact -c:a copy -f framecrc -y $(target_path $decfile3) || return
    do_md5sum $decfile3
}

gaplessenc(){
    sample=$(target_path $1)
    format=$2
    codec=$3

    file1="${outdir}/${test}.out-1"
    cleanfiles="$cleanfiles $file1"

    # test data after reencoding
    ffmpeg -i "$sample" -bitexact -map 0:a -c:a $codec -af aresample \
           -f $format -y "$(target_path "$file1")" || return
    probegaplessinfo "$(target_path "$file1")"
}

audio_match(){
    sample=$(target_path $1)
    trefile=$2
    extra_args=$3

    decfile="${outdir}/${test}.wav"
    cleanfiles="$cleanfiles $decfile"

    ffmpeg -auto_conversion_filters -i "$sample" -bitexact $extra_args -y $(target_path $decfile) || return
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
    echo "threads=$threads" >> "$errfile"
    cmpo="$($base64 <$cmpfile)"
    erro="$($base64 <$errfile)"
fi
echo "${test}:${sig:-$err}:$cmpo:$erro" >$repfile

if test $err != 0 && test $gen != "no" && test "${ref#tests/data/}" = "$ref" ; then
    echo "GEN     $ref"
    cp -f "$outfile" "$ref"
    err=$?
fi

if test $err = 0; then
    if test $keep -lt 2; then
        rm -f $outfile $errfile $cmpfile $cleanfiles
    fi
elif test $gen = "no"; then
    echo "Test $test failed. Look at $errfile for details."
    test "${V:-0}" -gt 0 && cat $errfile
else
    echo "Updating reference failed, possibly no output file was generated."
fi
exit $err
