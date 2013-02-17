#!/bin/sh
#
# automatic regression test for libavfilter
#
#
#set -x

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

do_lavfi_plain() {
    vfilters="$2"

    if [ $test = $1 ] ; then
        do_video_filter $test "$2"
    fi
}

do_lavfi() {
    do_lavfi_plain $1 "$2"
}

do_lavfi_colormatrix() {
    do_lavfi "${1}1" "$1=$4:$5,$1=$5:$3,$1=$3:$4,$1=$4:$3,$1=$3:$5,$1=$5:$2"
    do_lavfi "${1}2" "$1=$2:$3,$1=$3:$2,$1=$2:$4,$1=$4:$2,$1=$2:$5,$1=$5:$4"
}

do_lavfi "crop"               "crop=iw-100:ih-100:100:100"
do_lavfi "crop_scale"         "crop=iw-100:ih-100:100:100,scale=400:-1"
do_lavfi "crop_scale_vflip"   "null,null,crop=iw-200:ih-200:200:200,crop=iw-20:ih-20:20:20,scale=200:200,scale=250:250,vflip,vflip,null,scale=200:200,crop=iw-100:ih-100:100:100,vflip,scale=200:200,null,vflip,crop=iw-100:ih-100:100:100,null"
do_lavfi "crop_vflip"         "crop=iw-100:ih-100:100:100,vflip"
do_lavfi "drawbox"            "drawbox=224:24:88:72:#FF8010@0.5"
do_lavfi "edgedetect"         "edgedetect"
do_lavfi "fade"               "fade=in:5:15,fade=out:30:15"
do_lavfi "hue"                "hue=s=sin(2*PI*t)+1"
do_lavfi "idet"               "idet"
do_lavfi "null"               "null"
do_lavfi "overlay"            "split[m],scale=88:72,pad=96:80:4:4[o2];[m]fifo[o1],[o1][o2]overlay=240:16"
do_lavfi "pad"                "pad=iw*1.5:ih*1.5:iw*0.3:ih*0.2"
do_lavfi "pp"                 "pp=be/hb/vb/tn/l5/al"
do_lavfi "pp2"                "pp=be/fq:16/h1/v1/lb"
do_lavfi "pp3"                "pp=be/fq:8/ha:128:7/va/li"
do_lavfi "pp4"                "pp=be/ci"
do_lavfi "pp5"                "pp=md"
do_lavfi "pp6"                "pp=be/fd"
do_lavfi "scale200"           "scale=200:200"
do_lavfi "scale500"           "scale=500:500"
do_lavfi "select"             "select=not(eq(mod(n\,2)\,0)+eq(mod(n\,3)\,0))"
do_lavfi "setdar"             "setdar=16/9"
do_lavfi "setsar"             "setsar=16/11"
do_lavfi "thumbnail"          "thumbnail=10"
do_lavfi "tile"               "tile=3x3:nb_frames=5:padding=7:margin=2"
do_lavfi "transpose"          "transpose"
do_lavfi "unsharp"            "unsharp=11:11:-1.5:11:11:-1.5"
do_lavfi "vflip"              "vflip"
do_lavfi "vflip_crop"         "vflip,crop=iw-100:ih-100:100:100"
do_lavfi "vflip_vflip"        "vflip,vflip"

do_lavfi_plain "alphamerge_rgb"     "[in]format=bgra,split,alphamerge[out]"
do_lavfi_plain "alphamerge_yuv"     "[in]format=yuv420p,split,alphamerge[out]"
do_lavfi_plain "alphaextract_rgb"   "[in]format=bgra,split,alphamerge,split[o3][o4];[o4]alphaextract[alpha];[o3][alpha]alphamerge[out]"
do_lavfi_plain "alphaextract_yuv"   "[in]format=yuv420p,split,alphamerge,split[o3][o4];[o4]alphaextract[alpha];[o3][alpha]alphamerge[out]"

do_lavfi_colormatrix "colormatrix" bt709 fcc bt601 smpte240m

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
do_lavfi_pixfmts "pixfmts_copy"        "copy"    ""
do_lavfi_pixfmts "pixfmts_crop"        "crop"    "100:100:100:100"
do_lavfi_pixfmts "pixfmts_hflip"       "hflip"   ""
do_lavfi_pixfmts "pixfmts_null"        "null"    ""
do_lavfi_pixfmts "pixfmts_pad"         "pad"     "500:400:20:20"
do_lavfi_pixfmts "pixfmts_pixdesctest" "pixdesctest"
do_lavfi_pixfmts "pixfmts_scale"       "scale"   "200:100"
do_lavfi_pixfmts "pixfmts_super2xsai"  "super2xsai"
do_lavfi_pixfmts "pixfmts_vflip"       "vflip"
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
