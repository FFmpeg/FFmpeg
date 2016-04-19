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

static const AVBitStreamFilter *bitstream_filters[] = {
#if CONFIG_AAC_ADTSTOASC_BSF
    &ff_aac_adtstoasc_bsf,
#endif
#if CONFIG_CHOMP_BSF
    &ff_chomp_bsf,
#endif
#if CONFIG_DUMP_EXTRADATA_BSF
    &ff_dump_extradata_bsf,
#endif
#if CONFIG_DCA_CORE_BSF
    &ff_dca_core_bsf,
#endif
#if CONFIG_H264_MP4TOANNEXB_BSF
    &ff_h264_mp4toannexb_bsf,
#endif
#if CONFIG_HEVC_MP4TOANNEXB_BSF
    &ff_hevc_mp4toannexb_bsf,
#endif
#if CONFIG_IMX_DUMP_HEADER_BSF
    &ff_imx_dump_header_bsf,
#endif
#if CONFIG_MJPEG2JPEG_BSF
    &ff_mjpeg2jpeg_bsf,
#endif
#if CONFIG_MJPEGA_DUMP_HEADER_BSF
    &ff_mjpega_dump_header_bsf,
#endif
#if CONFIG_MP3_HEADER_DECOMPRESS_BSF
    &ff_mp3_header_decompress_bsf,
#endif
#if CONFIG_MPEG4_UNPACK_BFRAMES_BSF
    &ff_mpeg4_unpack_bframes_bsf,
#endif
#if CONFIG_MOV2TEXTSUB_BSF
    &ff_mov2textsub_bsf,
#endif
#if CONFIG_NOISE_BSF
    &ff_noise_bsf,
#endif
#if CONFIG_REMOVE_EXTRADATA_BSF
    &ff_remove_extradata_bsf,
#endif
#if CONFIG_TEXT2MOVSUB_BSF
    &ff_text2movsub_bsf,
#endif
#if CONFIG_VP9_SUPERFRAME_BSF
    &ff_vp9_superframe_bsf,
#endif
    NULL,
};

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
