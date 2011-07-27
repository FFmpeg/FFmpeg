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

do_lavfi "crop"               "crop=iw-100:ih-100:100:100"
do_lavfi "crop_scale"         "crop=iw-100:ih-100:100:100,scale=400:-1"
do_lavfi "crop_scale_vflip"   "null,null,crop=iw-200:ih-200:200:200,crop=iw-20:ih-20:20:20,scale=200:200,scale=250:250,vflip,vflip,null,scale=200:200,crop=iw-100:ih-100:100:100,vflip,scale=200:200,null,vflip,crop=iw-100:ih-100:100:100,null"
do_lavfi "crop_vflip"         "crop=iw-100:ih-100:100:100,vflip"
do_lavfi "null"               "null"
do_lavfi "scale200"           "scale=200:200"
do_lavfi "scale500"           "scale=500:500"
do_lavfi "vflip"              "vflip"
do_lavfi "vflip_crop"         "vflip,crop=iw-100:ih-100:100:100"
do_lavfi "vflip_vflip"        "vflip,vflip"

do_lavfi_pixfmts(){
    test ${test%_[bl]e} = pixfmts_$1 || return 0
    filter=$1
    filter_args=$2

    showfiltfmts="$target_exec $target_path/tools/lavfi-showfiltfmts"
    exclude_fmts=${outfile}${1}_exclude_fmts
    out_fmts=${outfile}${1}_out_fmts

    # exclude pixel formats which are not supported as input
    $avconv -pix_fmts list 2>/dev/null | sed -ne '9,$p' | grep '^\..\.' | cut -d' ' -f2 | sort >$exclude_fmts
    $showfiltfmts scale | awk -F '[ \r]' '/^OUTPUT/{ print $3 }' | sort | comm -23 - $exclude_fmts >$out_fmts

    pix_fmts=$($showfiltfmts $filter | awk -F '[ \r]' '/^INPUT/{ print $3 }' | sort | comm -12 - $out_fmts)
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

# TODO: add tests for
# direct rendering,
# chains with feedback loops
