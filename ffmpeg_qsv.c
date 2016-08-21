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

#include <mfx/mfxvideo.h>
#include <stdlib.h>

#include "libavutil/dict.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavcodec/qsv.h"

#include "ffmpeg.h"

typedef struct QSVContext {
    OutputStream *ost;

    mfxSession session;

    mfxExtOpaqueSurfaceAlloc opaque_alloc;
    AVBufferRef             *opaque_surfaces_buf;

    uint8_t           *surface_used;
    mfxFrameSurface1 **surface_ptrs;
    int nb_surfaces;

    mfxExtBuffer *ext_buffers[1];
} QSVContext;

static void buffer_release(void *opaque, uint8_t *data)
{
    *(uint8_t*)opaque = 0;
}

static int qsv_get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream *ist = s->opaque;
    QSVContext  *qsv = ist->hwaccel_ctx;
    int i;

    for (i = 0; i < qsv->nb_surfaces; i++) {
        if (qsv->surface_used[i])
            continue;

        frame->buf[0] = av_buffer_create((uint8_t*)qsv->surface_ptrs[i], sizeof(*qsv->surface_ptrs[i]),
                                         buffer_release, &qsv->surface_used[i], 0);
        if (!frame->buf[0])
            return AVERROR(ENOMEM);
        frame->data[3]       = (uint8_t*)qsv->surface_ptrs[i];
        qsv->surface_used[i] = 1;
        return 0;
    }

    return AVERROR(ENOMEM);
}

static int init_opaque_surf(QSVContext *qsv)
{
    AVQSVContext *hwctx_enc = qsv->ost->enc_ctx->hwaccel_context;
    mfxFrameSurface1 *surfaces;
    int i;

    qsv->nb_surfaces = hwctx_enc->nb_opaque_surfaces;

    qsv->opaque_surfaces_buf = av_buffer_ref(hwctx_enc->opaque_surfaces);
    qsv->surface_ptrs        = av_mallocz_array(qsv->nb_surfaces, sizeof(*qsv->surface_ptrs));
    qsv->surface_used        = av_mallocz_array(qsv->nb_surfaces, sizeof(*qsv->surface_used));
    if (!qsv->opaque_surfaces_buf || !qsv->surface_ptrs || !qsv->surface_used)
        return AVERROR(ENOMEM);

    surfaces = (mfxFrameSurface1*)qsv->opaque_surfaces_buf->data;
    for (i = 0; i < qsv->nb_surfaces; i++)
        qsv->surface_ptrs[i] = surfaces + i;

    qsv->opaque_alloc.Out.Surfaces   = qsv->surface_ptrs;
    qsv->opaque_alloc.Out.NumSurface = qsv->nb_surfaces;
    qsv->opaque_alloc.Out.Type       = hwctx_enc->opaque_alloc_type;

    qsv->opaque_alloc.Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
    qsv->opaque_alloc.Header.BufferSz = sizeof(qsv->opaque_alloc);
    qsv->ext_buffers[0]               = (mfxExtBuffer*)&qsv->opaque_alloc;

    return 0;
}

static void qsv_uninit(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    QSVContext  *qsv = ist->hwaccel_ctx;

    av_freep(&qsv->ost->enc_ctx->hwaccel_context);
    av_freep(&s->hwaccel_context);

    av_buffer_unref(&qsv->opaque_surfaces_buf);
    av_freep(&qsv->surface_used);
    av_freep(&qsv->surface_ptrs);

    av_freep(&qsv);
}

int qsv_init(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    QSVContext  *qsv = ist->hwaccel_ctx;
    AVQSVContext *hwctx_dec;
    int ret;

    if (!qsv) {
        av_log(NULL, AV_LOG_ERROR, "QSV transcoding is not initialized. "
               "-hwaccel qsv should only be used for one-to-one QSV transcoding "
               "with no filters.\n");
        return AVERROR_BUG;
    }

    ret = init_opaque_surf(qsv);
    if (ret < 0)
        return ret;

    hwctx_dec = av_qsv_alloc_context();
    if (!hwctx_dec)
        return AVERROR(ENOMEM);

    hwctx_dec->session        = qsv->session;
    hwctx_dec->iopattern      = MFX_IOPATTERN_OUT_OPAQUE_MEMORY;
    hwctx_dec->ext_buffers    = qsv->ext_buffers;
    hwctx_dec->nb_ext_buffers = FF_ARRAY_ELEMS(qsv->ext_buffers);

    av_freep(&s->hwaccel_context);
    s->hwaccel_context = hwctx_dec;

    ist->hwaccel_get_buffer = qsv_get_buffer;
    ist->hwaccel_uninit     = qsv_uninit;

    return 0;
}

static mfxIMPL choose_implementation(const InputStream *ist)
{
    static const struct {
        const char *name;
        mfxIMPL     impl;
    } impl_map[] = {
        { "auto",     MFX_IMPL_AUTO         },
        { "sw",       MFX_IMPL_SOFTWARE     },
        { "hw",       MFX_IMPL_HARDWARE     },
        { "auto_any", MFX_IMPL_AUTO_ANY     },
        { "hw_any",   MFX_IMPL_HARDWARE_ANY },
        { "hw2",      MFX_IMPL_HARDWARE2    },
        { "hw3",      MFX_IMPL_HARDWARE3    },
        { "hw4",      MFX_IMPL_HARDWARE4    },
    };

    mfxIMPL impl = MFX_IMPL_AUTO_ANY;
    int i;

    if (ist->hwaccel_device) {
        for (i = 0; i < FF_ARRAY_ELEMS(impl_map); i++)
            if (!strcmp(ist->hwaccel_device, impl_map[i].name)) {
                impl = impl_map[i].impl;
                break;
            }
        if (i == FF_ARRAY_ELEMS(impl_map))
            impl = strtol(ist->hwaccel_device, NULL, 0);
    }

    return impl;
}

int qsv_transcode_init(OutputStream *ost)
{
    InputStream *ist;
    const enum AVPixelFormat *pix_fmt;

    AVDictionaryEntry *e;
    const AVOption *opt;
    int flags = 0;

    int err, i;

    QSVContext *qsv = NULL;
    AVQSVContext *hwctx = NULL;
    mfxIMPL impl;
    mfxVersion ver = { { 3, 1 } };

    /* check if the encoder supports QSV */
    if (!ost->enc->pix_fmts)
        return 0;
    for (pix_fmt = ost->enc->pix_fmts; *pix_fmt != AV_PIX_FMT_NONE; pix_fmt++)
        if (*pix_fmt == AV_PIX_FMT_QSV)
            break;
    if (*pix_fmt == AV_PIX_FMT_NONE)
        return 0;

    if (strcmp(ost->avfilter, "null") || ost->source_index < 0)
        return 0;

    /* check if the decoder supports QSV and the output only goes to this stream */
    ist = input_streams[ost->source_index];
    if (ist->hwaccel_id != HWACCEL_QSV || !ist->dec || !ist->dec->pix_fmts)
        return 0;
    for (pix_fmt = ist->dec->pix_fmts; *pix_fmt != AV_PIX_FMT_NONE; pix_fmt++)
        if (*pix_fmt == AV_PIX_FMT_QSV)
            break;
    if (*pix_fmt == AV_PIX_FMT_NONE)
        return 0;

    for (i = 0; i < nb_output_streams; i++)
        if (output_streams[i] != ost &&
            output_streams[i]->source_index == ost->source_index)
            return 0;

    av_log(NULL, AV_LOG_VERBOSE, "Setting up QSV transcoding\n");

    qsv   = av_mallocz(sizeof(*qsv));
    hwctx = av_qsv_alloc_context();
    if (!qsv || !hwctx)
        goto fail;

    impl = choose_implementation(ist);

    err = MFXInit(impl, &ver, &qsv->session);
    if (err != MFX_ERR_NONE) {
        av_log(NULL, AV_LOG_ERROR, "Error initializing an MFX session: %d\n", err);
        goto fail;
    }

    e = av_dict_get(ost->encoder_opts, "flags", NULL, 0);
    opt = av_opt_find(ost->enc_ctx, "flags", NULL, 0, 0);
    if (e && opt)
        av_opt_eval_flags(ost->enc_ctx, opt, e->value, &flags);

    qsv->ost = ost;

    hwctx->session                = qsv->session;
    hwctx->iopattern              = MFX_IOPATTERN_IN_OPAQUE_MEMORY;
    hwctx->opaque_alloc           = 1;
    hwctx->nb_opaque_surfaces     = 16;

    ost->hwaccel_ctx              = qsv;
    ost->enc_ctx->hwaccel_context = hwctx;
    ost->enc_ctx->pix_fmt         = AV_PIX_FMT_QSV;

    ist->hwaccel_ctx              = qsv;
    ist->dec_ctx->pix_fmt         = AV_PIX_FMT_QSV;
    ist->resample_pix_fmt         = AV_PIX_FMT_QSV;

    return 0;

fail:
    av_freep(&hwctx);
    av_freep(&qsv);
    return AVERROR_UNKNOWN;
}
