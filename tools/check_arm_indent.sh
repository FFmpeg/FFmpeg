#!/bin/sh
#
# Copyright (c) 2025 Martin Storsjo
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


cd $(dirname $0)/..

if [ "$1" = "--apply" ]; then
    apply=1
fi

ret=0

for i in */aarch64/*.S */aarch64/*/*.S */arm/*.S; do
    case $i in
    libavcodec/aarch64/h264idct_neon.S|libavcodec/aarch64/h26x/epel_neon.S|libavcodec/aarch64/h26x/qpel_neon.S|libavcodec/aarch64/vc1dsp_neon.S)
        # Skip files with known (and tolerated) deviations from the tool.
        continue
        ;;
    libavcodec/arm/jrevdct_arm.S)
        # This file has a large copyright header that gets reindented like code.
        continue
        ;;
    libavcodec/arm/mlpdsp_armv5te.S|libavcodec/arm/mlpdsp_armv6.S)
        # These files use a bit more gas directives than most files, and the
        # reindenter script would need a lot of manual fixups to make this
        # look good, so keep it as is.
        continue
        ;;
    libavcodec/arm/vc1dsp_neon.S|libavutil/arm/float_dsp_vfp.S)
        # These files use different indentation levels to signify different
        # levels in unrolling.
        continue
        ;;
    libavcodec/arm/simple_idct_arm.S|libavcodec/arm/simple_idct_armv5te.S|libavcodec/arm/simple_idct_armv6.S)
        # These files use defines for constants, like "W26", that get mistaken
        # as register names and get lowercased by the script.
        continue
        ;;
    esac
    ./tools/indent_arm_assembly.pl < "$i" > tmp.S || ret=$?
    if ! git diff --quiet --no-index "$i" tmp.S; then
        if [ -n "$apply" ]; then
            mv tmp.S "$i"
        else
            git --no-pager diff --no-index "$i" tmp.S
        fi
        ret=1
    fi
done

rm -f tmp.S

exit $ret
