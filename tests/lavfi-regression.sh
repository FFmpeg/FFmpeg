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

do_lavfi() {
    test_name=$1
    eval test=\$do_$test_name
    vfilters="slicify=random,$2"

    if [ -n "$test" ] ; then
        do_video_encoding ${test_name}.nut "" "-vcodec rawvideo -vfilters $vfilters"
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

# TODO: add tests for
# direct rendering,
# slices
# chains with feedback loops

rm -f "$bench" "$bench2"
