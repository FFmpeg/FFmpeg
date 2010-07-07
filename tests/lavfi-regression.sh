#!/bin/sh
#
# automatic regression test for libavfilter
#
#
#set -x

set -e

. $(dirname $0)/regression-funcs.sh

eval do_$test=y

rm -f "$logfile"
rm -f "$benchfile"

get_common_elements() (
    for elt1 in $1; do
        for elt2 in $2; do
            [ $elt1 = $elt2 ] && res="$res $elt1 "
        done
    done

    echo $res
)

# Returns the elements present in $1 but not in $2.
get_exclusive_elements() (
    for elt1 in $1; do
        common=""
        for elt2 in $2; do
            [ $elt1 = $elt2 ] && common="true" && break;
        done
        [ -z "$common" ] && res="$res $elt1"
    done

    echo $res
)

do_lavfi() {
    test_name=$1
    eval test=\$do_$test_name
    vfilters="slicify=random,$2"

    if [ -n "$test" ] ; then
        do_video_encoding ${test_name}.nut "" "-vcodec rawvideo -vf $vfilters"
    fi
}

do_lavfi "crop"               "crop=100:100"
do_lavfi "crop_scale"         "crop=100:100,scale=400:-1"
do_lavfi "crop_scale_vflip"   "null,null,crop=200:200,crop=20:20,scale=200:200,scale=250:250,vflip,vflip,null,scale=200:200,crop=100:100,vflip,scale=200:200,null,vflip,crop=100:100,null"
do_lavfi "crop_vflip"         "crop=100:100,vflip"
do_lavfi "null"               "null"
do_lavfi "scale200"           "scale=200:200"
do_lavfi "scale500"           "scale=500:500"
do_lavfi "vflip"              "vflip"
do_lavfi "vflip_crop"         "vflip,crop=100:100"
do_lavfi "vflip_vflip"        "vflip,vflip"

# all these filters have exactly one input and exactly one output
filters_args="
crop=100:100:100:100
null
pad=500:400:20:20
scale=200:100
vflip
"

if [ -n "$do_lavfi_pix_fmts" ]; then
    # exclude pixel formats which are not supported as input
    excluded_pix_fmts="$(ffmpeg -pix_fmts list 2>/dev/null | sed -ne '9,$p' | grep '^\..\.' | cut -d' ' -f2)"

    scale_out_pix_fmts=$(tools/lavfi-showfiltfmts scale | grep "^OUTPUT" | cut -d: -f2)
    scale_out_pix_fmts=$(get_exclusive_elements "$scale_out_pix_fmts" "$excluded_pix_fmts")

    for filter_args in $filters_args; do
        filter=$(echo $filter_args | sed -e 's/\([^=]\+\)=.*/\1/')
        in_pix_fmts=$(tools/lavfi-showfiltfmts $filter | grep "^INPUT" | cut -d: -f2)
        pix_fmts=$(get_common_elements "$in_pix_fmts" "$scale_out_pix_fmts")

        for pix_fmt in $pix_fmts; do
            do_video_encoding "${pix_fmt}-${filter}.nut" "" \
                "-vf slicify=random,format=$pix_fmt,$filter_args -vcodec rawvideo -pix_fmt $pix_fmt"
        done
    done
fi

if [ -n "$do_lavfi_pixdesc" ]; then
    pix_fmts="$($ffmpeg -pix_fmts list 2>/dev/null | sed -ne '9,$p' | grep '^IO' | cut -d' ' -f2)"

    ref_file=tests/ref/lavfi/lavfi_pixdesc
    rm -f $ref_file
    res_file=$logfile

    for pix_fmt in $pix_fmts; do
        # print to the reference logfile
        logfile=$ref_file
        do_video_encoding "lavfi_pixdesc-${pix_fmt}.nut" "" \
            "-vf slicify=random,format=$pix_fmt -vcodec rawvideo -pix_fmt $pix_fmt"

        # print to the result logfile
        logfile=$res_file
        do_video_encoding "lavfi_pixdesc-${pix_fmt}.nut" "" \
            "-vf slicify=random,format=$pix_fmt,pixdesctest -vcodec rawvideo -pix_fmt $pix_fmt"
    done
fi

# TODO: add tests for
# direct rendering,
# chains with feedback loops

rm -f "$bench" "$bench2"
