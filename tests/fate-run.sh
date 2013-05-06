#! /bin/sh

export LC_ALL=C

base=$(dirname $0)
. "${base}/md5.sh"

base64=tests/base64

test="${1#fate-}"
samples=$2
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
    echo "scale=2; v = $1 - $2; if (v < 0) v = -v; if (v > $3) r = 1; r" | bc
}

do_tiny_psnr(){
    psnr=$(tests/tiny_psnr "$1" "$2" $cmp_unit $cmp_shift 0)
    val=$(expr "$psnr" : ".*$3: *\([0-9.]*\)")
    size1=$(expr "$psnr" : '.*bytes: *\([0-9]*\)')
    size2=$(expr "$psnr" : '.*bytes:[ 0-9]*/ *\([0-9]*\)')
    val_cmp=$(compare $val $cmp_target $fuzz)
    size_cmp=$(compare $size1 $size2 $size_tolerance)
    if [ "$val_cmp" != 0 ] || [ "$size_cmp" != 0 ]; then
        echo "$psnr"
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

probefmt(){
    run avprobe -show_format_entry format_name -v 0 "$@"
}

avconv(){
    dec_opts="-threads $threads -thread_type $thread_type"
    avconv_args="-nostats -cpuflags $cpuflags"
    for arg in $@; do
        [ x${arg} = x-i ] && avconv_args="${avconv_args} ${dec_opts}"
        avconv_args="${avconv_args} ${arg}"
    done
    run avconv ${avconv_args}
}

framecrc(){
    avconv "$@" -f framecrc -
}

framemd5(){
    avconv "$@" -f framemd5 -
}

crc(){
    avconv "$@" -f crc -
}

md5(){
    avconv "$@" md5:
}

pcm(){
    avconv "$@" -vn -f s16le -
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
    avconv -i $src_file "$@" -f $out_fmt -y ${encfile} || return
    avconv -f $out_fmt -i ${encfile} -c:a pcm_${pcm_fmt} -f ${dec_fmt} -
}

FLAGS="-flags +bitexact -sws_flags +accurate_rnd+bitexact"
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
    avconv -f $src_fmt $DEC_OPTS -i $tsrcfile $ENC_OPTS $enc_opt $FLAGS \
        -f $enc_fmt -y $tencfile || return
    do_md5sum $encfile
    echo $(wc -c $encfile)
    avconv $DEC_OPTS -i $tencfile $ENC_OPTS $dec_opt $FLAGS \
        -f $dec_fmt -y $tdecfile || return
    do_md5sum $decfile
    tests/tiny_psnr $srcfile $decfile $cmp_unit $cmp_shift
}

lavftest(){
    t="${test#lavf-}"
    ref=${base}/ref/lavf/$t
    ${base}/lavf-regression.sh $t lavf tests/vsynth1 "$target_exec" "$target_path" "$threads" "$thread_type" "$cpuflags"
}

video_filter(){
    filters=$1
    shift
    label=${test#filter-}
    raw_src="${target_path}/tests/vsynth1/%02d.pgm"
    printf '%-20s' $label
    avconv $DEC_OPTS -f image2 -vcodec pgmyuv -i $raw_src \
        $FLAGS $ENC_OPTS -vf "$filters" -vcodec rawvideo $* -f nut md5:
}

pixdesc(){
    pix_fmts="$(avconv -pix_fmts list 2>/dev/null | awk 'NR > 8 && /^IO/ { print $2 }' | sort)"
    for pix_fmt in $pix_fmts; do
        test=$pix_fmt
        video_filter "format=$pix_fmt,pixdesctest" -pix_fmt $pix_fmt
    done
}

pixfmts(){
    filter=${test#filter-pixfmts-}
    filter_args=$1

    showfiltfmts="$target_exec $target_path/libavfilter/filtfmts-test"
    exclude_fmts=${outfile}${filter}_exclude_fmts
    out_fmts=${outfile}${filter}_out_fmts

    # exclude pixel formats which are not supported as input
    avconv -pix_fmts list 2>/dev/null | awk 'NR > 8 && /^\..\./ { print $2 }' | sort >$exclude_fmts
    $showfiltfmts scale | awk -F '[ \r]' '/^OUTPUT/{ print $3 }' | sort | comm -23 - $exclude_fmts >$out_fmts

    pix_fmts=$($showfiltfmts $filter | awk -F '[ \r]' '/^INPUT/{ print $3 }' | sort | comm -12 - $out_fmts)
    for pix_fmt in $pix_fmts; do
        test=$pix_fmt
        video_filter "format=$pix_fmt,$filter=$filter_args" -pix_fmt $pix_fmt
    done

    rm $exclude_fmts $out_fmts
}

mkdir -p "$outdir"

exec 3>&2
eval $command >"$outfile" 2>$errfile
err=$?

if [ $err -gt 128 ]; then
    sig=$(kill -l $err 2>/dev/null)
    test "${sig}" = "${sig%[!A-Za-z]*}" || unset sig
fi

if test -e "$ref" || test $cmp = "oneline" ; then
    case $cmp in
        diff)   diff -u -b "$ref" "$outfile"            >$cmpfile ;;
        oneoff) oneoff     "$ref" "$outfile"            >$cmpfile ;;
        stddev) stddev     "$ref" "$outfile"            >$cmpfile ;;
        oneline)oneline    "$ref" "$outfile"            >$cmpfile ;;
        null)   cat               "$outfile"            >$cmpfile ;;
    esac
    cmperr=$?
    test $err = 0 && err=$cmperr
    test $err = 0 || cat $cmpfile
else
    echo "reference file '$ref' not found"
    err=1
fi

echo "${test}:${sig:-$err}:$($base64 <$cmpfile):$($base64 <$errfile)" >$repfile

if test $err != 0 && test $gen != "no" ; then
    echo "GEN     $ref"
    cp -f "$outfile" "$ref"
    err=$?
fi

test $err = 0 && rm -f $outfile $errfile $cmpfile $cleanfiles
exit $err
