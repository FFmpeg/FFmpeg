/*
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * Motion Compensation Deinterlacer
 * Ported from MPlayer libmpcodecs/vf_mcdeint.c.
 *
 * Known Issues:
 *
 * The motion estimation is somewhat at the mercy of the input, if the
 * input frames are created purely based on spatial interpolation then
 * for example a thin black line or another random and not
 * interpolateable pattern will cause problems.
 * Note: completely ignoring the "unavailable" lines during motion
 * estimation did not look any better, so the most obvious solution
 * would be to improve tfields or penalize problematic motion vectors.
 *
 * If non iterative ME is used then snow currently ignores the OBMC
 * window and as a result sometimes creates artifacts.
 *
 * Only past frames are used, we should ideally use future frames too,
 * something like filtering the whole movie in forward and then
 * backward direction seems like a interesting idea but the current
 * filter framework is FAR from supporting such things.
 *
 * Combining the motion compensated image with the input image also is
 * not as trivial as it seems, simple blindly taking even lines from
 * one and odd ones from the other does not work at all as ME/MC
 * sometimes has nothing in the previous frames which matches the
 * current. The current algorithm has been found by trial and error
 * and almost certainly can be improved...
 */

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/avcodec.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

enum MCDeintMode {
    MODE_FAST = 0,
    MODE_MEDIUM,
    MODE_SLOW,
    MODE_EXTRA_SLOW,
    MODE_NB,
};

enum MCDeintParity {
    PARITY_TFF  =  0, ///< top field first
    PARITY_BFF  =  1, ///< bottom field first
};

typedef struct {
    const AVClass *class;
    int mode;           ///< MCDeintMode
    int parity;         ///< MCDeintParity
    int qp;
    AVCodecContext *enc_ctx;
} MCDeintContext;

#define OFFSET(x) offsetof(MCDeintContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define CONST(name, help, val, unit) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, INT_MIN, INT_MAX, FLAGS, unit }

static const AVOption mcdeint_options[] = {
    { "mode", "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=MODE_FAST}, 0, MODE_NB-1, FLAGS, .unit="mode" },
    CONST("fast",       NULL, MODE_FAST,       "mode"),
    CONST("medium",     NULL, MODE_MEDIUM,     "mode"),
    CONST("slow",       NULL, MODE_SLOW,       "mode"),
    CONST("extra_slow", NULL, MODE_EXTRA_SLOW, "mode"),

    { "parity", "set the assumed picture field parity", OFFSET(parity), AV_OPT_TYPE_INT, {.i64=PARITY_BFF}, -1, 1, FLAGS, "parity" },
    CONST("tff", "assume top field first",    PARITY_TFF, "parity"),
    CONST("bff", "assume bottom field first", PARITY_BFF, "parity"),

    { "qp", "set qp", OFFSET(qp), AV_OPT_TYPE_INT, {.i64=1}, INT_MIN, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(mcdeint);

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    MCDeintContext *mcdeint = ctx->priv;
    AVCodec *enc;
    AVCodecContext *enc_ctx;
    AVDictionary *opts = NULL;
    int ret;

    if (!(enc = avcodec_find_encoder(AV_CODEC_ID_SNOW))) {
        av_log(ctx, AV_LOG_ERROR, "Snow encoder is not enabled in libavcodec\n");
        return AVERROR(EINVAL);
    }

    mcdeint->enc_ctx = avcodec_alloc_context3(enc);
    if (!mcdeint->enc_ctx)
        return AVERROR(ENOMEM);
    enc_ctx = mcdeint->enc_ctx;
    enc_ctx->width  = inlink->w;
    enc_ctx->height = inlink->h;
    enc_ctx->time_base = (AVRational){1,25};  // meaningless
    enc_ctx->gop_size = INT_MAX;
    enc_ctx->max_b_frames = 0;
    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    enc_ctx->flags = AV_CODEC_FLAG_QSCALE | CODEC_FLAG_LOW_DELAY;
    enc_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    enc_ctx->global_quality = 1;
    enc_ctx->me_cmp = enc_ctx->me_sub_cmp = FF_CMP_SAD;
    enc_ctx->mb_cmp = FF_CMP_SSE;
    av_dict_set(&opts, "memc_only", "1", 0);
    av_dict_set(&opts, "no_bitstream", "1", 0);

    switch (mcdeint->mode) {
    case MODE_EXTRA_SLOW:
        enc_ctx->refs = 3;
    case MODE_SLOW:
        enc_ctx->me_method = ME_ITER;
    case MODE_MEDIUM:
        enc_ctx->flags |= AV_CODEC_FLAG_4MV;
        enc_ctx->dia_size = 2;
    case MODE_FAST:
        enc_ctx->flags |= AV_CODEC_FLAG_QPEL;
    }

    ret = avcodec_open2(enc_ctx, enc, &opts);
    av_dict_free(&opts);
    if (ret < 0)
        return ret;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MCDeintContext *mcdeint = ctx->priv;

    if (mcdeint->enc_ctx) {
        avcodec_close(mcdeint->enc_ctx);
        av_freep(&mcdeint->enc_ctx);
    }
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpic)
{
    MCDeintContext *mcdeint = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *outpic, *frame_dec;
    AVPacket pkt = {0};
    int x, y, i, ret, got_frame = 0;

    outpic = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!outpic) {
        av_frame_free(&inpic);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(outpic, inpic);
    inpic->quality = mcdeint->qp * FF_QP2LAMBDA;

    av_init_packet(&pkt);

    ret = avcodec_encode_video2(mcdeint->enc_ctx, &pkt, inpic, &got_frame);
    if (ret < 0)
        goto end;

    frame_dec = mcdeint->enc_ctx->coded_frame;

    for (i = 0; i < 3; i++) {
        int is_chroma = !!i;
        int w = FF_CEIL_RSHIFT(inlink->w, is_chroma);
        int h = FF_CEIL_RSHIFT(inlink->h, is_chroma);
        int fils = frame_dec->linesize[i];
        int srcs = inpic    ->linesize[i];
        int dsts = outpic   ->linesize[i];

        for (y = 0; y < h; y++) {
            if ((y ^ mcdeint->parity) & 1) {
                for (x = 0; x < w; x++) {
                    uint8_t *filp = &frame_dec->data[i][x + y*fils];
                    uint8_t *srcp = &inpic    ->data[i][x + y*srcs];
                    uint8_t *dstp = &outpic   ->data[i][x + y*dsts];

                    if (y > 0 && y < h-1){
                        int is_edge = x < 3 || x > w-4;
                        int diff0 = filp[-fils] - srcp[-srcs];
                        int diff1 = filp[+fils] - srcp[+srcs];
                        int temp = filp[0];

#define DELTA(j) av_clip(j, -x, w-1-x)

#define GET_SCORE_EDGE(j)\
   FFABS(srcp[-srcs+DELTA(-1+(j))] - srcp[+srcs+DELTA(-1-(j))])+\
   FFABS(srcp[-srcs+DELTA(j)     ] - srcp[+srcs+DELTA(  -(j))])+\
   FFABS(srcp[-srcs+DELTA(1+(j)) ] - srcp[+srcs+DELTA( 1-(j))])

#define GET_SCORE(j)\
   FFABS(srcp[-srcs-1+(j)] - srcp[+srcs-1-(j)])+\
   FFABS(srcp[-srcs  +(j)] - srcp[+srcs  -(j)])+\
   FFABS(srcp[-srcs+1+(j)] - srcp[+srcs+1-(j)])

#define CHECK_EDGE(j)\
    {   int score = GET_SCORE_EDGE(j);\
        if (score < spatial_score){\
            spatial_score = score;\
            diff0 = filp[-fils+DELTA(j)]    - srcp[-srcs+DELTA(j)];\
            diff1 = filp[+fils+DELTA(-(j))] - srcp[+srcs+DELTA(-(j))];\

#define CHECK(j)\
    {   int score = GET_SCORE(j);\
        if (score < spatial_score){\
            spatial_score= score;\
            diff0 = filp[-fils+(j)] - srcp[-srcs+(j)];\
            diff1 = filp[+fils-(j)] - srcp[+srcs-(j)];\

                        if (is_edge) {
                            int spatial_score = GET_SCORE_EDGE(0) - 1;
                            CHECK_EDGE(-1) CHECK_EDGE(-2) }} }}
                            CHECK_EDGE( 1) CHECK_EDGE( 2) }} }}
                        } else {
                            int spatial_score = GET_SCORE(0) - 1;
                            CHECK(-1) CHECK(-2) }} }}
                            CHECK( 1) CHECK( 2) }} }}
                        }


                        if (diff0 + diff1 > 0)
                            temp -= (diff0 + diff1 - FFABS(FFABS(diff0) - FFABS(diff1)) / 2) / 2;
                        else
                            temp -= (diff0 + diff1 + FFABS(FFABS(diff0) - FFABS(diff1)) / 2) / 2;
                        *filp = *dstp = temp > 255U ? ~(temp>>31) : temp;
                    } else {
                        *dstp = *filp;
                    }
                }
            }
        }

        for (y = 0; y < h; y++) {
            if (!((y ^ mcdeint->parity) & 1)) {
                for (x = 0; x < w; x++) {
                    frame_dec->data[i][x + y*fils] =
                    outpic   ->data[i][x + y*dsts] = inpic->data[i][x + y*srcs];
                }
            }
        }
    }
    mcdeint->parity ^= 1;

end:
    av_packet_unref(&pkt);
    av_frame_free(&inpic);
    if (ret < 0) {
        av_frame_free(&outpic);
        return ret;
    }
    return ff_filter_frame(outlink, outpic);
}

static const AVFilterPad mcdeint_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
    { NULL }
};

static const AVFilterPad mcdeint_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_mcdeint = {
    .name          = "mcdeint",
    .description   = NULL_IF_CONFIG_SMALL("Apply motion compensating deinterlacing."),
    .priv_size     = sizeof(MCDeintContext),
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = mcdeint_inputs,
    .outputs       = mcdeint_outputs,
    .priv_class    = &mcdeint_class,
};
