#!/bin/sh

# Copyright (c) 2011 Nicolas George
#
# This file is part of FFmpeg.
#
# FFmpeg is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# FFmpeg is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with FFmpeg; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

set -e

config="$1"
out="$2"
test -n "$out"

outval=""

add_line () {
  outval="$outval$*
"
}

parse_config_h () {
  while read define var value; do
    case "$define $var $value" in
      "#define CONFIG_"*_*" 1") eval "$var=1";;
    esac
  done
}

define_codecid () {
  id="$1"
  n=${1#CODEC_ID_}
  add_line "case ${id}:"
  eval "c=\${CONFIG_${n}_DECODER}:\${CONFIG_${n}_ENCODER}"
  case "$c" in
    1:*) s="decoder";;
    *:1) s="encoder";;
    *) s="";;
  esac
  case "$s" in
    "") add_line "    return \"$n\";" ;;
    *)
      add_line "    { extern AVCodec ff_${n}_${s};"
      add_line "      return ff_${n}_${s}.name; }"
      ;;
  esac
}

parse_enum_codecid () {
  while read line; do
    case "$line" in
      "};") break;;
      *CODEC_ID_FIRST*=*) ;;
      CODEC_ID_*) define_codecid ${line%%[=,]*};;
    esac
  done
}

parse_avcodec_h () {
  while read line; do
    case "$line" in
      "enum CodecID {") parse_enum_codecid; break;;
    esac
  done
}

parse_config_h  < "$config"
parse_avcodec_h # use stdin
sed -e '/case.*:/!y/ABCDEFGHIJKLMNOPQRSTUVWXYZ/abcdefghijklmnopqrstuvwxyz/' \
    -e 's/extern avcodec /extern AVCodec /' > "$out" <<EOF
$outval
EOF
