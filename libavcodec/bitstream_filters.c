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

#include "config.h"

#include "libavutil/common.h"
#include "libavutil/log.h"

#include "avcodec.h"
#include "bsf.h"

extern const AVBitStreamFilter ff_aac_adtstoasc_bsf;
extern const AVBitStreamFilter ff_chomp_bsf;
extern const AVBitStreamFilter ff_dump_extradata_bsf;
extern const AVBitStreamFilter ff_dca_core_bsf;
extern const AVBitStreamFilter ff_extract_extradata_bsf;
extern const AVBitStreamFilter ff_h264_mp4toannexb_bsf;
extern const AVBitStreamFilter ff_hevc_mp4toannexb_bsf;
extern const AVBitStreamFilter ff_imx_dump_header_bsf;
extern const AVBitStreamFilter ff_mjpeg2jpeg_bsf;
extern const AVBitStreamFilter ff_mjpega_dump_header_bsf;
extern const AVBitStreamFilter ff_mp3_header_decompress_bsf;
extern const AVBitStreamFilter ff_mpeg4_unpack_bframes_bsf;
extern const AVBitStreamFilter ff_mov2textsub_bsf;
extern const AVBitStreamFilter ff_noise_bsf;
extern const AVBitStreamFilter ff_remove_extradata_bsf;
extern const AVBitStreamFilter ff_text2movsub_bsf;
extern const AVBitStreamFilter ff_vp9_superframe_bsf;

#include "libavcodec/bsf_list.c"

const AVBitStreamFilter *av_bsf_next(void **opaque)
{
    uintptr_t i = (uintptr_t)*opaque;
    const AVBitStreamFilter *f = bitstream_filters[i];

    if (f)
        *opaque = (void*)(i + 1);

    return f;
}

const AVBitStreamFilter *av_bsf_get_by_name(const char *name)
{
    int i;

    for (i = 0; bitstream_filters[i]; i++) {
        const AVBitStreamFilter *f = bitstream_filters[i];
        if (!strcmp(f->name, name))
            return f;
    }

    return NULL;
}

const AVClass *ff_bsf_child_class_next(const AVClass *prev)
{
    int i;

    /* find the filter that corresponds to prev */
    for (i = 0; prev && bitstream_filters[i]; i++) {
        if (bitstream_filters[i]->priv_class == prev) {
            i++;
            break;
        }
    }

    /* find next filter with priv options */
    for (; bitstream_filters[i]; i++)
        if (bitstream_filters[i]->priv_class)
            return bitstream_filters[i]->priv_class;
    return NULL;
}
