#!/bin/sh
#
# * Copyright (C) 2018 Michael Niedermayer (michaelni@gmx.at)
# *
# * This file is part of FFmpeg.
# *
# * FFmpeg is free software; you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation; either version 2 of the License, or
# * (at your option) any later version.
# *
# * FFmpeg is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with FFmpeg; if not, write to the Free Software
# * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

set -e

LC_ALL=C
export LC_ALL

LIST=target_dec_fate.list

show_help(){
    cat <<EOF
Usage: ./target_dec_fate.sh <directory> [<test to run>]

directory       the directory into which sample files will be downloaded
test to run     the number of the issue to test
Note, some test samples may not yet be available to the public, also this
script will not download samples which are already in the directory. So you
may want to preserve its content between runs.
EOF
    exit 0
}

test -z "$1"  && show_help
test ! -d "$1"  && echo $1 is not an accessable directory && show_help
test ! -f target_dec_fate.sh && echo $0 Must be run from its location && show_help
grep 'CONFIG_OSSFUZZ 0' ../config.h && echo not configured for ossfuzz && show_help

#Download testcases
while read -r LINE; do
    ISSUE_NUM=`echo $LINE | sed 's#/.*##'`
    FILE_ID=`echo $LINE | sed 's#.*/clusterfuzz-testcase[a-zA-Z0-9_-]*-\([0-9]*\).*#\1#'`
    FILE=`echo $LINE | sed 's# .*##'`
    if test -f "$1/$FILE" ; then
        echo exists       $FILE
    elif echo "$ISSUE_NUM" | grep '#' >/dev/null ; then
        echo disabled     $FILE
    else
        echo downloading  $FILE
        mkdir -p "$1/$ISSUE_NUM"
        wget -O "$1/$FILE" "https://oss-fuzz.com/download?testcase_id=$FILE_ID" || rm "$1/$FILE"
    fi
done < "$LIST"

#Find which fuzzers we need to build
TOOLS=
while read -r LINE; do
    TOOL_ID=`echo $LINE | sed 's#[^ ]* ##'`
    TOOLS="$TOOLS tools/$TOOL_ID"
done < "$LIST"

cd ..
#Build fuzzers
make -j4 $TOOLS

#Run testcases
while read -r LINE; do
    TOOL_ID=`echo $LINE | sed 's#[^ ]* ##'`
    FILE=`echo $LINE | sed 's# .*##'`
    if ! test -f "$1/$FILE" ; then
        continue
    fi
    tools/$TOOL_ID $1/$FILE
done < "tools/$LIST"

echo OK
