#!/bin/sh
# Convert a source file into a C source file containing the
# source code as a C string.

# This file is part of FFmpeg.
#
# FFmpeg is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# FFmpeg is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

input="$1"
output="$2"

name=$(basename "$input" | sed 's/\./_/')

cat >$output <<EOF
// Generated from $input
const char *ff_source_$name =
EOF

# Convert \ to \\ and " to \", then add " to the start and end of the line.
cat "$input" | sed 's/\\/\\\\/g;s/\"/\\\"/g;s/^/\"/;s/$/\\n\"/' >>$output

echo ";" >>$output
