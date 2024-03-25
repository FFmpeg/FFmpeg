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
#include "codec_internal.h"
#include "libavutil/bprint.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "ass_split.h"
#include "ttmlenc.h"

typedef struct {
    AVCodecContext *avctx;
    ASSSplitContext *ass_ctx;
    AVBPrint buffer;
} TTMLContext;

static void ttml_text_cb(void *priv, const char *text, int len)
{
    TTMLContext *s = priv;
    AVBPrint cur_line;
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

    av_bprint_init_for_buffer(&s->buffer, buf, bufsize);

    for (i=0; i<sub->num_rects; i++) {
        const char *ass = sub->rects[i]->ass;
        int ret;

        if (sub->rects[i]->type != SUBTITLE_ASS) {
            av_log(avctx, AV_LOG_ERROR, "Only SUBTITLE_ASS type supported.\n");
            return AVERROR(EINVAL);
        }

        dialog = ff_ass_split_dialog(s->ass_ctx, ass);
        if (!dialog)
            return AVERROR(ENOMEM);

        if (dialog->style) {
            av_bprintf(&s->buffer, "<span region=\"");
            av_bprint_escape(&s->buffer, dialog->style, NULL,
                             AV_ESCAPE_MODE_XML,
                             AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
            av_bprintf(&s->buffer, "\">");
        }

        ret = ff_ass_split_override_codes(&ttml_callbacks, s, dialog->text);
        if (ret < 0) {
            int log_level = (ret != AVERROR_INVALIDDATA ||
                             avctx->err_recognition & AV_EF_EXPLODE) ?
                            AV_LOG_ERROR : AV_LOG_WARNING;
            av_log(avctx, log_level,
                   "Splitting received ASS dialog text %s failed: %s\n",
                   dialog->text,
                   av_err2str(ret));

            if (log_level == AV_LOG_ERROR) {
                ff_ass_free_dialog(&dialog);
                return ret;
            }
        }

        if (dialog->style)
            av_bprintf(&s->buffer, "</span>");

        ff_ass_free_dialog(&dialog);
    }

    if (!s->buffer.len)
        return 0;
    if (!av_bprint_is_complete(&s->buffer)) {
        av_log(avctx, AV_LOG_ERROR, "Buffer too small for TTML event.\n");
        return AVERROR_BUFFER_TOO_SMALL;
    }

    return s->buffer.len;
}

static av_cold int ttml_encode_close(AVCodecContext *avctx)
{
    TTMLContext *s = avctx->priv_data;

    ff_ass_split_free(s->ass_ctx);

    return 0;
}

static const char *ttml_get_display_alignment(int alignment)
{
    switch (alignment) {
    case 1:
    case 2:
    case 3:
        return "after";
    case 4:
    case 5:
    case 6:
        return "center";
    case 7:
    case 8:
    case 9:
        return "before";
    default:
        return NULL;
    }
}

static const char *ttml_get_text_alignment(int alignment)
{
    switch (alignment) {
    case 1:
    case 4:
    case 7:
        return "left";
    case 2:
    case 5:
    case 8:
        return "center";
    case 3:
    case 6:
    case 9:
        return "right";
    default:
        return NULL;
    }
}

static void ttml_get_origin(ASSScriptInfo script_info, ASSStyle style,
                           int *origin_left, int *origin_top)
{
    *origin_left = av_rescale(style.margin_l, 100, script_info.play_res_x);
    *origin_top  =
        av_rescale((style.alignment >= 7) ? style.margin_v : 0,
                   100, script_info.play_res_y);
}

static void ttml_get_extent(ASSScriptInfo script_info, ASSStyle style,
                           int *width, int *height)
{
    *width  = av_rescale(script_info.play_res_x - style.margin_r,
                         100, script_info.play_res_x);
    *height = av_rescale((style.alignment <= 3) ?
                         script_info.play_res_y - style.margin_v :
                         script_info.play_res_y,
                         100, script_info.play_res_y);
}

static int ttml_write_region(AVCodecContext *avctx, AVBPrint *buf,
                             ASSScriptInfo script_info, ASSStyle style)
{
    const char *display_alignment = NULL;
    const char *text_alignment = NULL;
    int origin_left = 0;
    int origin_top  = 0;
    int width = 0;
    int height = 0;

    if (!style.name) {
        av_log(avctx, AV_LOG_ERROR, "Subtitle style name not set!\n");
        return AVERROR_INVALIDDATA;
    }

    if (style.font_size < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid font size for TTML: %d!\n",
               style.font_size);
        return AVERROR_INVALIDDATA;
    }

    if (style.margin_l < 0 || style.margin_r < 0 || style.margin_v < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "One or more negative margin values in subtitle style: "
               "left: %d, right: %d, vertical: %d!\n",
               style.margin_l, style.margin_r, style.margin_v);
        return AVERROR_INVALIDDATA;
    }

    display_alignment = ttml_get_display_alignment(style.alignment);
    text_alignment = ttml_get_text_alignment(style.alignment);
    if (!display_alignment || !text_alignment) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to convert ASS style alignment %d of style %s to "
               "TTML display and text alignment!\n",
               style.alignment,
               style.name);
        return AVERROR_INVALIDDATA;
    }

    ttml_get_origin(script_info, style, &origin_left, &origin_top);
    ttml_get_extent(script_info, style, &width, &height);

    av_bprintf(buf, "      <region xml:id=\"");
    av_bprint_escape(buf, style.name, NULL, AV_ESCAPE_MODE_XML,
                     AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
    av_bprintf(buf, "\"\n");

    av_bprintf(buf, "        tts:origin=\"%d%% %d%%\"\n",
               origin_left, origin_top);
    av_bprintf(buf, "        tts:extent=\"%d%% %d%%\"\n",
               width, height);

    av_bprintf(buf, "        tts:displayAlign=\"");
    av_bprint_escape(buf, display_alignment, NULL, AV_ESCAPE_MODE_XML,
                     AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
    av_bprintf(buf, "\"\n");

    av_bprintf(buf, "        tts:textAlign=\"");
    av_bprint_escape(buf, text_alignment, NULL, AV_ESCAPE_MODE_XML,
                     AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
    av_bprintf(buf, "\"\n");

    // if we set cell resolution to our script reference resolution,
    // then a single line is a single "point" on our canvas. Thus, by setting
    // our font size to font size in cells, we should gain a similar enough
    // scale without resorting to explicit pixel based font sizing, which is
    // frowned upon in the TTML community.
    av_bprintf(buf, "        tts:fontSize=\"%dc\"\n",
               style.font_size);

    if (style.font_name) {
        av_bprintf(buf, "        tts:fontFamily=\"");
        av_bprint_escape(buf, style.font_name, NULL, AV_ESCAPE_MODE_XML,
                         AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
        av_bprintf(buf, "\"\n");
    }

    av_bprintf(buf, "        tts:overflow=\"visible\" />\n");

    return 0;
}

static int ttml_write_header_content(AVCodecContext *avctx)
{
    TTMLContext *s = avctx->priv_data;
    ASS *ass = (ASS *)s->ass_ctx;
    ASSScriptInfo script_info = ass->script_info;
    const size_t base_extradata_size = TTMLENC_EXTRADATA_SIGNATURE_SIZE + 1 +
                                       AV_INPUT_BUFFER_PADDING_SIZE;
    size_t additional_extradata_size = 0;
    int ret;

    if (script_info.play_res_x <= 0 || script_info.play_res_y <= 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid subtitle reference resolution %dx%d!\n",
               script_info.play_res_x, script_info.play_res_y);
        return AVERROR_INVALIDDATA;
    }

    av_bprint_init(&s->buffer, 0, INT_MAX - base_extradata_size);

    // write the first string in extradata, attributes in the base "tt" element.
    av_bprintf(&s->buffer, TTML_DEFAULT_NAMESPACING);
    // the cell resolution is in character cells, so not exactly 1:1 against
    // a pixel based resolution, but as the tts:extent in the root
    // "tt" element is frowned upon (and disallowed in the EBU-TT profile),
    // we mimic the reference resolution by setting it as the cell resolution.
    av_bprintf(&s->buffer, "  ttp:cellResolution=\"%d %d\"\n",
               script_info.play_res_x, script_info.play_res_y);
    av_bprint_chars(&s->buffer, '\0', 1);

    // write the second string in extradata, head element containing the styles
    av_bprintf(&s->buffer, "  <head>\n");
    av_bprintf(&s->buffer, "    <layout>\n");

    for (int i = 0; i < ass->styles_count; i++) {
        ret = ttml_write_region(avctx, &s->buffer, script_info,
                                ass->styles[i]);
        if (ret < 0)
            goto fail;
    }

    av_bprintf(&s->buffer, "    </layout>\n");
    av_bprintf(&s->buffer, "  </head>\n");
    av_bprint_chars(&s->buffer, '\0', 1);

    if (!av_bprint_is_complete(&s->buffer)) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    additional_extradata_size = s->buffer.len;

    if (!(avctx->extradata =
            av_mallocz(base_extradata_size + additional_extradata_size))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    avctx->extradata_size =
        TTMLENC_EXTRADATA_SIGNATURE_SIZE + additional_extradata_size;
    memcpy(avctx->extradata, TTMLENC_EXTRADATA_SIGNATURE,
           TTMLENC_EXTRADATA_SIGNATURE_SIZE);

    memcpy(avctx->extradata + TTMLENC_EXTRADATA_SIGNATURE_SIZE,
           s->buffer.str, additional_extradata_size);

    ret = 0;
fail:
    av_bprint_finalize(&s->buffer, NULL);

    return ret;
}

static av_cold int ttml_encode_init(AVCodecContext *avctx)
{
    TTMLContext *s = avctx->priv_data;
    int ret = AVERROR_BUG;
    s->avctx   = avctx;

    if (!(s->ass_ctx = ff_ass_split(avctx->subtitle_header))) {
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ttml_write_header_content(avctx)) < 0) {
        return ret;
    }

    return 0;
}

const FFCodec ff_ttml_encoder = {
    .p.name         = "ttml",
    CODEC_LONG_NAME("TTML subtitle"),
    .p.type         = AVMEDIA_TYPE_SUBTITLE,
    .p.id           = AV_CODEC_ID_TTML,
    .priv_data_size = sizeof(TTMLContext),
    .init           = ttml_encode_init,
    FF_CODEC_ENCODE_SUB_CB(ttml_encode_frame),
    .close          = ttml_encode_close,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
