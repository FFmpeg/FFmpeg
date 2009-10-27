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
    vfilters=$2

    if [ -n "$test" ] ; then
        do_video_encoding ${test_name}.avi "" "-vcodec rawvideo -vfilters $vfilters"
    fi
}

# example tests:
# do_lavfi "crop" "crop=100:100:-1:-1"
# do_lavfi "crop_scale" "crop=100:100,scale=200:-1"
# do_lavfi "scale" "scale=200:200"

# TODO: add tests for
# direct rendering,
# slices
# chains with feedback loops

rm -f "$bench" "$bench2"
