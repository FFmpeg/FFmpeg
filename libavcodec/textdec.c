/*
 * Copyright (c) 2012 Clément Bœsch
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
 * Raw subtitles decoder
 */

#include "avcodec.h"
#include "ass.h"
#include "libavutil/bprint.h"
#include "libavutil/opt.h"

typedef struct {
    AVClass *class;
    const char *linebreaks;
    int keep_ass_markup;
    int readorder;
} TextContext;

#define OFFSET(x) offsetof(TextContext, x)
#define SD AV_OPT_FLAG_SUBTITLE_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "keep_ass_markup", "Set if ASS tags must be escaped", OFFSET(keep_ass_markup), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, .flags=SD },
    { NULL }
};

static int text_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_sub_ptr, AVPacket *avpkt)
{
    int ret = 0;
    AVBPrint buf;
    AVSubtitle *sub = data;
    const char *ptr = avpkt->data;
    TextContext *text = avctx->priv_data;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    if (ptr && avpkt->size > 0 && *ptr) {
        ff_ass_bprint_text_event(&buf, ptr, avpkt->size, text->linebreaks, text->keep_ass_markup);
        ret = ff_ass_add_rect(sub, buf.str, text->readorder++, 0, NULL, NULL);
    }
    av_bprint_finalize(&buf, NULL);
    if (ret < 0)
        return ret;
    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

static void text_flush(AVCodecContext *avctx)
{
    TextContext *text = avctx->priv_data;
    if (!(avctx->flags2 & AV_CODEC_FLAG2_RO_FLUSH_NOOP))
        text->readorder = 0;
}

#define DECLARE_CLASS(decname) static const AVClass decname ## _decoder_class = {   \
    .class_name = #decname " decoder",      \
    .item_name  = av_default_item_name,     \
    .option     = decname ## _options,      \
    .version    = LIBAVUTIL_VERSION_INT,    \
}

#if CONFIG_TEXT_DECODER
#define text_options options
DECLARE_CLASS(text);

AVCodec ff_text_decoder = {
    .name           = "text",
    .long_name      = NULL_IF_CONFIG_SMALL("Raw text subtitle"),
    .priv_data_size = sizeof(TextContext),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_TEXT,
    .decode         = text_decode_frame,
    .init           = ff_ass_subtitle_header_default,
    .priv_class     = &text_decoder_class,
    .flush          = text_flush,
};
#endif

#if CONFIG_VPLAYER_DECODER || CONFIG_PJS_DECODER || CONFIG_SUBVIEWER1_DECODER || CONFIG_STL_DECODER

static int linebreak_init(AVCodecContext *avctx)
{
    TextContext *text = avctx->priv_data;
    text->linebreaks = "|";
    return ff_ass_subtitle_header_default(avctx);
}

#if CONFIG_VPLAYER_DECODER
#define vplayer_options options
DECLARE_CLASS(vplayer);

AVCodec ff_vplayer_decoder = {
    .name           = "vplayer",
    .long_name      = NULL_IF_CONFIG_SMALL("VPlayer subtitle"),
    .priv_data_size = sizeof(TextContext),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_VPLAYER,
    .decode         = text_decode_frame,
    .init           = linebreak_init,
    .priv_class     = &vplayer_decoder_class,
    .flush          = text_flush,
};
#endif

#if CONFIG_STL_DECODER
#define stl_options options
DECLARE_CLASS(stl);

AVCodec ff_stl_decoder = {
    .name           = "stl",
    .long_name      = NULL_IF_CONFIG_SMALL("Spruce subtitle format"),
    .priv_data_size = sizeof(TextContext),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_STL,
    .decode         = text_decode_frame,
    .init           = linebreak_init,
    .priv_class     = &stl_decoder_class,
    .flush          = text_flush,
};
#endif

#if CONFIG_PJS_DECODER
#define pjs_options options
DECLARE_CLASS(pjs);

AVCodec ff_pjs_decoder = {
    .name           = "pjs",
    .long_name      = NULL_IF_CONFIG_SMALL("PJS subtitle"),
    .priv_data_size = sizeof(TextContext),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_PJS,
    .decode         = text_decode_frame,
    .init           = linebreak_init,
    .priv_class     = &pjs_decoder_class,
    .flush          = text_flush,
};
#endif

#if CONFIG_SUBVIEWER1_DECODER
#define subviewer1_options options
DECLARE_CLASS(subviewer1);

AVCodec ff_subviewer1_decoder = {
    .name           = "subviewer1",
    .long_name      = NULL_IF_CONFIG_SMALL("SubViewer1 subtitle"),
    .priv_data_size = sizeof(TextContext),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_SUBVIEWER1,
    .decode         = text_decode_frame,
    .init           = linebreak_init,
    .priv_class     = &subviewer1_decoder_class,
    .flush          = text_flush,
};
#endif

#endif /* text subtitles with '|' line break */
