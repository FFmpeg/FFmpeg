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

#include "libavutil/mem.h"

#include "vda.h"
#include "vda_internal.h"

#if CONFIG_H264_VDA_HWACCEL
AVVDAContext *av_vda_alloc_context(void)
{
    AVVDAContext *ret = av_mallocz(sizeof(*ret));

    if (ret)
        ret->output_callback = ff_vda_output_callback;

    return ret;
}

int av_vda_default_init(AVCodecContext *avctx)
{
    avctx->hwaccel_context = av_vda_alloc_context();
    if (!avctx->hwaccel_context)
        return AVERROR(ENOMEM);
    return ff_vda_default_init(avctx);
}

void av_vda_default_free(AVCodecContext *avctx)
{
    ff_vda_default_free(avctx);
    av_freep(&avctx->hwaccel_context);
}

void ff_vda_default_free(AVCodecContext *avctx)
{
    AVVDAContext *vda = avctx->hwaccel_context;
    if (vda && vda->decoder)
        VDADecoderDestroy(vda->decoder);
}

#else
AVVDAContext *av_vda_alloc_context(void)
{
    return NULL;
}

int av_vda_default_init(AVCodecContext *avctx)
{
    return AVERROR(ENOSYS);
}

void av_vda_default_free(AVCodecContext *ctx)
{
}
#endif
