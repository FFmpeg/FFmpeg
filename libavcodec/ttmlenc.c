/*
 * TTML subtitle encoder
 * Copyright (c) 2020 24i
 *
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

/**
 * @file
 * TTML subtitle encoder
 * @see https://www.w3.org/TR/ttml1/
 * @see https://www.w3.org/TR/ttml2/
 * @see https://www.w3.org/TR/ttml-imsc/rec
 */

#include "avcodec.h"
#include "internal.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/internal.h"
#include "ass_split.h"
#include "ass.h"
#include "ttmlenc.h"

typedef struct {
    AVCodecContext *avctx;
    ASSSplitContext *ass_ctx;
    AVBPrint buffer;
} TTMLContext;

static void ttml_text_cb(void *priv, const char *text, int len)
{
    TTMLContext *s = priv;
    AVBPrint cur_line = { 0 };
    AVBPrint *buffer = &s->buffer;

    av_bprint_init(&cur_line, len, AV_BPRINT_SIZE_UNLIMITED);

    av_bprint_append_data(&cur_line, text, len);
    if (!av_bprint_is_complete(&cur_line)) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Failed to move the current subtitle dialog to AVBPrint!\n");
        av_bprint_finalize(&cur_line, NULL);
        return;
    }


    av_bprint_escape(buffer, cur_line.str, NULL, AV_ESCAPE_MODE_XML,
                     0);

    av_bprint_finalize(&cur_line, NULL);
}

static void ttml_new_line_cb(void *priv, int forced)
{
    TTMLContext *s = priv;

    av_bprintf(&s->buffer, "<br/>");
}

static const ASSCodesCallbacks ttml_callbacks = {
    .text             = ttml_text_cb,
    .new_line         = ttml_new_line_cb,
};

static int ttml_encode_frame(AVCodecContext *avctx, uint8_t *buf,
                             int bufsize, const AVSubtitle *sub)
{
    TTMLContext *s = avctx->priv_data;
    ASSDialog *dialog;
    int i;

    av_bprint_clear(&s->buffer);

    for (i=0; i<sub->num_rects; i++) {
        const char *ass = sub->rects[i]->ass;

        if (sub->rects[i]->type != SUBTITLE_ASS) {
            av_log(avctx, AV_LOG_ERROR, "Only SUBTITLE_ASS type supported.\n");
            return AVERROR(EINVAL);
        }

#if FF_API_ASS_TIMING
        if (!strncmp(ass, "Dialogue: ", 10)) {
            int num;
            dialog = ff_ass_split_dialog(s->ass_ctx, ass, 0, &num);

            for (; dialog && num--; dialog++) {
                int ret = ff_ass_split_override_codes(&ttml_callbacks, s,
                                                      dialog->text);
                int log_level = (ret != AVERROR_INVALIDDATA ||
                                 avctx->err_recognition & AV_EF_EXPLODE) ?
                                AV_LOG_ERROR : AV_LOG_WARNING;

                if (ret < 0) {
                    av_log(avctx, log_level,
                           "Splitting received ASS dialog failed: %s\n",
                           av_err2str(ret));

                    if (log_level == AV_LOG_ERROR)
                        return ret;
                }
            }
        } else {
#endif
            dialog = ff_ass_split_dialog2(s->ass_ctx, ass);
            if (!dialog)
                return AVERROR(ENOMEM);

            {
                int ret = ff_ass_split_override_codes(&ttml_callbacks, s,
                                                      dialog->text);
                int log_level = (ret != AVERROR_INVALIDDATA ||
                                 avctx->err_recognition & AV_EF_EXPLODE) ?
                                AV_LOG_ERROR : AV_LOG_WARNING;

                if (ret < 0) {
                    av_log(avctx, log_level,
                           "Splitting received ASS dialog text %s failed: %s\n",
                           dialog->text,
                           av_err2str(ret));

                    if (log_level == AV_LOG_ERROR) {
                        ff_ass_free_dialog(&dialog);
                        return ret;
                    }
                }

                ff_ass_free_dialog(&dialog);
            }
#if FF_API_ASS_TIMING
        }
#endif
    }

    if (!av_bprint_is_complete(&s->buffer))
        return AVERROR(ENOMEM);
    if (!s->buffer.len)
        return 0;

    // force null-termination, so in case our destination buffer is
    // too small, the return value is larger than bufsize minus null.
    if (av_strlcpy(buf, s->buffer.str, bufsize) > bufsize - 1) {
        av_log(avctx, AV_LOG_ERROR, "Buffer too small for TTML event.\n");
        return AVERROR_BUFFER_TOO_SMALL;
    }

    return s->buffer.len;
}

static av_cold int ttml_encode_close(AVCodecContext *avctx)
{
    TTMLContext *s = avctx->priv_data;

    ff_ass_split_free(s->ass_ctx);

    av_bprint_finalize(&s->buffer, NULL);

    return 0;
}

static av_cold int ttml_encode_init(AVCodecContext *avctx)
{
    TTMLContext *s = avctx->priv_data;

    s->avctx   = avctx;

    if (!(s->ass_ctx = ff_ass_split(avctx->subtitle_header))) {
        return AVERROR_INVALIDDATA;
    }

    if (!(avctx->extradata = av_mallocz(TTMLENC_EXTRADATA_SIGNATURE_SIZE +
                                        1 + AV_INPUT_BUFFER_PADDING_SIZE))) {
        return AVERROR(ENOMEM);
    }

    avctx->extradata_size = TTMLENC_EXTRADATA_SIGNATURE_SIZE;
    memcpy(avctx->extradata, TTMLENC_EXTRADATA_SIGNATURE,
           TTMLENC_EXTRADATA_SIGNATURE_SIZE);

    av_bprint_init(&s->buffer, 0, AV_BPRINT_SIZE_UNLIMITED);

    return 0;
}

AVCodec ff_ttml_encoder = {
    .name           = "ttml",
    .long_name      = NULL_IF_CONFIG_SMALL("TTML subtitle"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_TTML,
    .priv_data_size = sizeof(TTMLContext),
    .init           = ttml_encode_init,
    .encode_sub     = ttml_encode_frame,
    .close          = ttml_encode_close,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
