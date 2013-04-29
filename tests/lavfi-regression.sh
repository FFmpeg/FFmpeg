#!/bin/sh
#
# automatic regression test for libavfilter
#
#
#set -x

#FIXME the whole file should be removed

set -e

. $(dirname $0)/regression-funcs.sh

eval do_$test=y

do_video_filter() {
    label=$1
    filters="$2"
    shift 2
    printf '%-20s' $label
    run_avconv $DEC_OPTS -f image2 -vcodec pgmyuv -i $raw_src    \
        $ENC_OPTS -vf "$filters" -vcodec rawvideo $* -f nut md5:
}

do_lavfi_pixfmts(){
    testname=$1;
    test ${test%_[bl]e} = $testname || return 0
    filter=$2
    filter_args=$3
    prefilter_chain=$4

    showfiltfmts="$target_exec $target_path/libavfilter/filtfmts-test"
    scale_exclude_fmts=${outfile}${testname}_scale_exclude_fmts
    scale_in_fmts=${outfile}${testname}_scale_in_fmts
    scale_out_fmts=${outfile}${testname}_scale_out_fmts
    in_fmts=${outfile}${testname}_in_fmts

    # exclude pixel formats which are not supported as input
    $showfiltfmts scale | awk -F '[ \r]' '/^INPUT/{ fmt=substr($3, 5); print fmt }' | sort >$scale_in_fmts
    $showfiltfmts scale | awk -F '[ \r]' '/^OUTPUT/{ fmt=substr($3, 5); print fmt }' | sort >$scale_out_fmts
    comm -12 $scale_in_fmts $scale_out_fmts >$scale_exclude_fmts

    $showfiltfmts $filter | awk -F '[ \r]' '/^INPUT/{ fmt=substr($3, 5); print fmt }' | sort >$in_fmts
    pix_fmts=$(comm -12 $scale_exclude_fmts $in_fmts)

    for pix_fmt in $pix_fmts; do
        do_video_filter $pix_fmt "${prefilter_chain}format=$pix_fmt,$filter=$filter_args" -pix_fmt $pix_fmt
    done

    rm $in_fmts $scale_in_fmts $scale_out_fmts $scale_exclude_fmts
}

# all these filters have exactly one input and exactly one output
do_lavfi_pixfmts "field"               "field"   "bottom"
do_lavfi_pixfmts "histeq"              "histeq"  "antibanding=strong"
do_lavfi_pixfmts "il"                  "il"      "luma_mode=d:chroma_mode=d:alpha_mode=d"
do_lavfi_pixfmts "kerndeint"           "kerndeint" "" "tinterlace=interleave_top,"
do_lavfi_pixfmts "pixfmts_super2xsai"  "super2xsai"
do_lavfi_pixfmts "tinterlace_merge"    "tinterlace" "merge"
do_lavfi_pixfmts "tinterlace_pad"      "tinterlace" "pad"

do_lavfi_lavd() {
    label=$1
    graph=$2
    shift 2
    [ $test = $label ] || return 0
    printf '%-20s' $label
    run_avconv $DEC_OPTS -f lavfi -i $graph \
        $ENC_OPTS -vcodec rawvideo $* -f nut md5:
}

do_lavfi_lavd "life"                 "life=s=40x40:r=5:seed=42:mold=64" -t 2
do_lavfi_lavd "testsrc"              "testsrc=r=7:n=2:d=10"
do_lavfi_lavd "scalenorm"            "sws_flags=+accurate_rnd+bitexact;testsrc=s=128x96:d=1:r=5,format=yuv420p[a];testsrc=s=160x120:d=1:r=5[b];[a][b]concat=unsafe=1,scale=flags=+accurate_rnd+bitexact"

# TODO: add tests for
# direct rendering,
# chains with feedback loops
