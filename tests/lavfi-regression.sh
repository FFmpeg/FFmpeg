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
    filters=$2
    shift 2
    printf '%-20s' $label
    run_avconv $DEC_OPTS -f image2 -vcodec pgmyuv -i $raw_src    \
        $ENC_OPTS -vf "$filters" -vcodec rawvideo $* -f nut md5:
}

do_lavfi() {
    vfilters="slicify=random,$2"

    if [ $test = $1 ] ; then
        do_video_filter $test "$vfilters"
    fi
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
do_lavfi "fade"               "fade=in:5:15,fade=out:30:15"
do_lavfi "null"               "null"
do_lavfi "overlay"            "split[m],scale=88:72,pad=96:80:4:4[o2];[m]fifo[o1],[o1][o2]overlay=240:16"
do_lavfi "pad"                "pad=iw*1.5:ih*1.5:iw*0.3:ih*0.2"
do_lavfi "pp"                 "mp=pp=be/de/tn/l5/al"
do_lavfi "pp2"                "mp=pp=be/fq:16/fa/lb"
do_lavfi "pp3"                "mp=pp=be/fq:8/ac/li"
do_lavfi "pp4"                "mp=pp=be/ci"
do_lavfi "pp5"                "mp=pp=md"
do_lavfi "pp6"                "mp=pp=be/fd"
do_lavfi "scale200"           "scale=200:200"
do_lavfi "scale500"           "scale=500:500"
do_lavfi "select"             "select=not(eq(mod(n\,2)\,0)+eq(mod(n\,3)\,0))"
do_lavfi "setdar"             "setdar=16/9"
do_lavfi "setsar"             "setsar=16/11"
do_lavfi "thumbnail"          "thumbnail=10"
do_lavfi "transpose"          "transpose"
do_lavfi "unsharp"            "unsharp=10:10:-1.5:10:10:-1.5"
do_lavfi "vflip"              "vflip"
do_lavfi "vflip_crop"         "vflip,crop=iw-100:ih-100:100:100"
do_lavfi "vflip_vflip"        "vflip,vflip"

do_lavfi_colormatrix "colormatrix" bt709 fcc bt601 smpte240m

do_lavfi_pixfmts(){
    test ${test%_[bl]e} = pixfmts_$1 || return 0
    filter=$1
    filter_args=$2

    showfiltfmts="$target_exec $target_path/tools/lavfi-showfiltfmts"
    exclude_fmts=${outfile}${1}_exclude_fmts
    out_fmts=${outfile}${1}_out_fmts

    # exclude pixel formats which are not supported as input
    $avconv -pix_fmts list 2>/dev/null | sed -ne '9,$p' | grep '^\..\.' | cut -d' ' -f2 | sort >$exclude_fmts
    $showfiltfmts scale | awk -F '[ \r]' '/^OUTPUT/{ fmt=substr($3, 5); print fmt }' | sort | comm -23 - $exclude_fmts >$out_fmts

    pix_fmts=$($showfiltfmts $filter $filter_args | awk -F '[ \r]' '/^INPUT/{ fmt=substr($3, 5); print fmt }' | sort | comm -12 - $out_fmts)
    for pix_fmt in $pix_fmts; do
        do_video_filter $pix_fmt "slicify=random,format=$pix_fmt,$filter=$filter_args" -pix_fmt $pix_fmt
    done

    rm $exclude_fmts $out_fmts
}

# all these filters have exactly one input and exactly one output
do_lavfi_pixfmts "copy"    ""
do_lavfi_pixfmts "crop"    "100:100:100:100"
do_lavfi_pixfmts "hflip"   ""
do_lavfi_pixfmts "null"    ""
do_lavfi_pixfmts "pad"     "500:400:20:20"
do_lavfi_pixfmts "scale"   "200:100"
do_lavfi_pixfmts "vflip"   ""

if [ -n "$do_pixdesc" ]; then
    pix_fmts="$($avconv -pix_fmts list 2>/dev/null | sed -ne '9,$p' | grep '^IO' | cut -d' ' -f2 | sort)"
    for pix_fmt in $pix_fmts; do
        do_video_filter $pix_fmt "slicify=random,format=$pix_fmt,pixdesctest" -pix_fmt $pix_fmt
    done
fi

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

# TODO: add tests for
# direct rendering,
# chains with feedback loops
