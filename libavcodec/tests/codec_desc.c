/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavcodec/codec_desc.h"

int main(int argc, char **argv)
{
    const AVCodecDescriptor *old_desc = NULL, *desc;

    while (desc = avcodec_descriptor_next(old_desc)) {
        if (old_desc && old_desc->id >= desc->id) {
            av_log(NULL, AV_LOG_FATAL, "Unsorted codec_descriptors '%s' and '%s'.\n", old_desc->name, desc->name);
            return 1;
        }

        if (avcodec_descriptor_get(desc->id) != desc) {
            av_log(NULL, AV_LOG_FATAL, "avcodec_descriptor_get() failed with '%s'.\n", desc->name);
            return 1;
        }

        if (avcodec_descriptor_get_by_name(desc->name) != desc) {
            av_log(NULL, AV_LOG_FATAL, "avcodec_descriptor_get_by_name() failed with '%s'.\n", desc->name);
            return 1;
        }

        old_desc = desc;
    }

    return 0;
}
