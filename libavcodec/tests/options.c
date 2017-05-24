/*
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavcodec/options.c"

static int dummy_init(AVCodecContext *ctx)
{
    //TODO: this code should set every possible pointer that could be set by codec and is not an option;
    ctx->extradata_size = 8;
    ctx->extradata = av_malloc(ctx->extradata_size);
    return 0;
}

static int dummy_close(AVCodecContext *ctx)
{
    av_freep(&ctx->extradata);
    ctx->extradata_size = 0;
    return 0;
}

static int dummy_encode(AVCodecContext *ctx, AVPacket *pkt, const AVFrame *frame, int *got_packet)
{
    return AVERROR(ENOSYS);
}

typedef struct Dummy12Context {
    AVClass  *av_class;
    int      num;
    char*    str;
} Dummy12Context;

typedef struct Dummy3Context {
    void     *fake_av_class;
    int      num;
    char*    str;
} Dummy3Context;

#define OFFSET(x) offsetof(Dummy12Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption dummy_options[] = {
    { "str", "set str", OFFSET(str), AV_OPT_TYPE_STRING, { .str = "i'm src default value" }, 0, 0, VE},
    { "num", "set num", OFFSET(num), AV_OPT_TYPE_INT,    { .i64 = 1500100900 },    0, INT_MAX, VE},
    { NULL },
};

static const AVClass dummy_v1_class = {
    .class_name = "dummy_v1_class",
    .item_name  = av_default_item_name,
    .option     = dummy_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVClass dummy_v2_class = {
    .class_name = "dummy_v2_class",
    .item_name  = av_default_item_name,
    .option     = dummy_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

/* codec with options */
static AVCodec dummy_v1_encoder = {
    .name             = "dummy_v1_codec",
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_NONE - 1,
    .encode2          = dummy_encode,
    .init             = dummy_init,
    .close            = dummy_close,
    .priv_class       = &dummy_v1_class,
    .priv_data_size   = sizeof(Dummy12Context),
};

/* codec with options, different class */
static AVCodec dummy_v2_encoder = {
    .name             = "dummy_v2_codec",
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_NONE - 2,
    .encode2          = dummy_encode,
    .init             = dummy_init,
    .close            = dummy_close,
    .priv_class       = &dummy_v2_class,
    .priv_data_size   = sizeof(Dummy12Context),
};

/* codec with priv data, but no class */
static AVCodec dummy_v3_encoder = {
    .name             = "dummy_v3_codec",
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_NONE - 3,
    .encode2          = dummy_encode,
    .init             = dummy_init,
    .close            = dummy_close,
    .priv_data_size   = sizeof(Dummy3Context),
};

/* codec without priv data */
static AVCodec dummy_v4_encoder = {
    .name             = "dummy_v4_codec",
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_NONE - 4,
    .encode2          = dummy_encode,
    .init             = dummy_init,
    .close            = dummy_close,
};

static void test_copy_print_codec(const AVCodecContext *ctx)
{
    printf("%-14s: %dx%d prv: %s",
           ctx->codec ? ctx->codec->name : "NULL",
           ctx->width, ctx->height,
           ctx->priv_data ? "set" : "null");
    if (ctx->codec && ctx->codec->priv_class && ctx->codec->priv_data_size) {
        int64_t i64;
        char *str = NULL;
        av_opt_get_int(ctx->priv_data, "num", 0, &i64);
        av_opt_get(ctx->priv_data, "str", 0, (uint8_t**)&str);
        printf(" opts: %"PRId64" %s", i64, str);
        av_free(str);
    }
    printf("\n");
}

static void test_copy(const AVCodec *c1, const AVCodec *c2)
{
    AVCodecContext *ctx1, *ctx2;
    printf("%s -> %s\nclosed:\n", c1 ? c1->name : "NULL", c2 ? c2->name : "NULL");
    ctx1 = avcodec_alloc_context3(c1);
    ctx2 = avcodec_alloc_context3(c2);
    ctx1->width = ctx1->height = 128;
    ctx1->time_base = (AVRational){12,34};
    if (ctx2->codec && ctx2->codec->priv_class && ctx2->codec->priv_data_size) {
        av_opt_set(ctx2->priv_data, "num", "667", 0);
        av_opt_set(ctx2->priv_data, "str", "i'm dest value before copy", 0);
    }
    avcodec_copy_context(ctx2, ctx1);
    test_copy_print_codec(ctx1);
    test_copy_print_codec(ctx2);
    if (ctx1->codec) {
        int ret;
        printf("opened:\n");
        ret = avcodec_open2(ctx1, ctx1->codec, NULL);
        if (ret < 0) {
            fprintf(stderr, "avcodec_open2 failed\n");
            exit(1);
        }
        if (ctx2->codec && ctx2->codec->priv_class && ctx2->codec->priv_data_size) {
            av_opt_set(ctx2->priv_data, "num", "667", 0);
            av_opt_set(ctx2->priv_data, "str", "i'm dest value before copy", 0);
        }
        avcodec_copy_context(ctx2, ctx1);
        test_copy_print_codec(ctx1);
        test_copy_print_codec(ctx2);
        avcodec_close(ctx1);
    }
    avcodec_free_context(&ctx1);
    avcodec_free_context(&ctx2);
}

int main(void)
{
    AVCodec *dummy_codec[] = {
        &dummy_v1_encoder,
        &dummy_v2_encoder,
        &dummy_v3_encoder,
        &dummy_v4_encoder,
        NULL,
    };
    int i, j;

    for (i = 0; dummy_codec[i]; i++)
        avcodec_register(dummy_codec[i]);

    printf("testing avcodec_copy_context()\n");
    for (i = 0; i < FF_ARRAY_ELEMS(dummy_codec); i++)
        for (j = 0; j < FF_ARRAY_ELEMS(dummy_codec); j++)
            test_copy(dummy_codec[i], dummy_codec[j]);
    return 0;
}
