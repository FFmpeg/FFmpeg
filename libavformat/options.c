/*
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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
#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/codec_par.h"

#include "libavutil/avassert.h"
#include "libavutil/iamf.h"
#include "libavutil/internal.h"
#include "libavutil/intmath.h"
#include "libavutil/opt.h"

/**
 * @file
 * Options definition for AVFormatContext.
 */

FF_DISABLE_DEPRECATION_WARNINGS
#include "options_table.h"
FF_ENABLE_DEPRECATION_WARNINGS

static const char* format_to_name(void* ptr)
{
    AVFormatContext* fc = (AVFormatContext*) ptr;
    if(fc->iformat) return fc->iformat->name;
    else if(fc->oformat) return fc->oformat->name;
    else return fc->av_class->class_name;
}

static void *format_child_next(void *obj, void *prev)
{
    AVFormatContext *s = obj;
    if (!prev && s->priv_data &&
        ((s->iformat && s->iformat->priv_class) ||
          s->oformat && s->oformat->priv_class))
        return s->priv_data;
    if (s->pb && s->pb->av_class && prev != s->pb)
        return s->pb;
    return NULL;
}

enum {
    CHILD_CLASS_ITER_AVIO = 0,
    CHILD_CLASS_ITER_MUX,
    CHILD_CLASS_ITER_DEMUX,
    CHILD_CLASS_ITER_DONE,

};

#define ITER_STATE_SHIFT 16

static const AVClass *format_child_class_iterate(void **iter)
{
    // we use the low 16 bits of iter as the value to be passed to
    // av_(de)muxer_iterate()
    void *val = (void*)(((uintptr_t)*iter) & ((1 << ITER_STATE_SHIFT) - 1));
    unsigned int state = ((uintptr_t)*iter) >> ITER_STATE_SHIFT;
    const AVClass *ret = NULL;

    if (state == CHILD_CLASS_ITER_AVIO) {
        ret = &ff_avio_class;
        state++;
        goto finish;
    }

    if (state == CHILD_CLASS_ITER_MUX) {
        const AVOutputFormat *ofmt;

        while ((ofmt = av_muxer_iterate(&val))) {
            ret = ofmt->priv_class;
            if (ret)
                goto finish;
        }

        val = NULL;
        state++;
    }

    if (state == CHILD_CLASS_ITER_DEMUX) {
        const AVInputFormat *ifmt;

        while ((ifmt = av_demuxer_iterate(&val))) {
            ret = ifmt->priv_class;
            if (ret)
                goto finish;
        }
        val = NULL;
        state++;
    }

finish:
    // make sure none av_(de)muxer_iterate does not set the high bits of val
    av_assert0(!((uintptr_t)val >> ITER_STATE_SHIFT));
    *iter = (void*)((uintptr_t)val | (state << ITER_STATE_SHIFT));
    return ret;
}

static AVClassCategory get_category(void *ptr)
{
    AVFormatContext* s = ptr;
    if(s->iformat) return AV_CLASS_CATEGORY_DEMUXER;
    else           return AV_CLASS_CATEGORY_MUXER;
}

static const AVClass av_format_context_class = {
    .class_name     = "AVFormatContext",
    .item_name      = format_to_name,
    .option         = avformat_options,
    .version        = LIBAVUTIL_VERSION_INT,
    .child_next     = format_child_next,
    .child_class_iterate = format_child_class_iterate,
    .category       = AV_CLASS_CATEGORY_MUXER,
    .get_category   = get_category,
};

static int io_open_default(AVFormatContext *s, AVIOContext **pb,
                           const char *url, int flags, AVDictionary **options)
{
    int loglevel;

    if (!strcmp(url, s->url) ||
        s->iformat && !strcmp(s->iformat->name, "image2") ||
        s->oformat && !strcmp(s->oformat->name, "image2")
    ) {
        loglevel = AV_LOG_DEBUG;
    } else
        loglevel = AV_LOG_INFO;

    av_log(s, loglevel, "Opening \'%s\' for %s\n", url, flags & AVIO_FLAG_WRITE ? "writing" : "reading");

    return ffio_open_whitelist(pb, url, flags, &s->interrupt_callback, options, s->protocol_whitelist, s->protocol_blacklist);
}

static int io_close2_default(AVFormatContext *s, AVIOContext *pb)
{
    return avio_close(pb);
}

AVFormatContext *avformat_alloc_context(void)
{
    FFFormatContext *const si = av_mallocz(sizeof(*si));
    AVFormatContext *s;

    if (!si)
        return NULL;

    s = &si->pub;
    s->av_class = &av_format_context_class;
    s->io_open  = io_open_default;
    s->io_close2= io_close2_default;

    av_opt_set_defaults(s);

    si->pkt = av_packet_alloc();
    si->parse_pkt = av_packet_alloc();
    if (!si->pkt || !si->parse_pkt) {
        avformat_free_context(s);
        return NULL;
    }

#if FF_API_LAVF_SHORTEST
    si->shortest_end = AV_NOPTS_VALUE;
#endif

    return s;
}

#if FF_API_GET_DUR_ESTIMATE_METHOD
enum AVDurationEstimationMethod av_fmt_ctx_get_duration_estimation_method(const AVFormatContext* ctx)
{
    return ctx->duration_estimation_method;
}
#endif

const AVClass *avformat_get_class(void)
{
    return &av_format_context_class;
}

#define DISPOSITION_OPT(ctx)                                                                                                        \
    { "disposition", NULL, offsetof(ctx, disposition), AV_OPT_TYPE_FLAGS, { .i64 = 0 },                                             \
        .flags = AV_OPT_FLAG_ENCODING_PARAM, .unit = "disposition" },                                                               \
        { "default",            .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DEFAULT           },    .unit = "disposition" }, \
        { "dub",                .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DUB               },    .unit = "disposition" }, \
        { "original",           .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_ORIGINAL          },    .unit = "disposition" }, \
        { "comment",            .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_COMMENT           },    .unit = "disposition" }, \
        { "lyrics",             .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_LYRICS            },    .unit = "disposition" }, \
        { "karaoke",            .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_KARAOKE           },    .unit = "disposition" }, \
        { "forced",             .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_FORCED            },    .unit = "disposition" }, \
        { "hearing_impaired",   .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_HEARING_IMPAIRED  },    .unit = "disposition" }, \
        { "visual_impaired",    .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_VISUAL_IMPAIRED   },    .unit = "disposition" }, \
        { "clean_effects",      .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_CLEAN_EFFECTS     },    .unit = "disposition" }, \
        { "attached_pic",       .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_ATTACHED_PIC      },    .unit = "disposition" }, \
        { "timed_thumbnails",   .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_TIMED_THUMBNAILS  },    .unit = "disposition" }, \
        { "non_diegetic",       .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_NON_DIEGETIC      },    .unit = "disposition" }, \
        { "captions",           .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_CAPTIONS          },    .unit = "disposition" }, \
        { "descriptions",       .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DESCRIPTIONS      },    .unit = "disposition" }, \
        { "metadata",           .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_METADATA          },    .unit = "disposition" }, \
        { "dependent",          .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DEPENDENT         },    .unit = "disposition" }, \
        { "still_image",        .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_STILL_IMAGE       },    .unit = "disposition" }

static const AVOption stream_options[] = {
    DISPOSITION_OPT(AVStream),
    { "discard", NULL, offsetof(AVStream, discard), AV_OPT_TYPE_INT, { .i64 = AVDISCARD_DEFAULT }, INT_MIN, INT_MAX,
        .flags = AV_OPT_FLAG_DECODING_PARAM, .unit = "avdiscard" },
        { "none",               .type = AV_OPT_TYPE_CONST, {.i64 = AVDISCARD_NONE     }, .unit = "avdiscard" },
        { "default",            .type = AV_OPT_TYPE_CONST, {.i64 = AVDISCARD_DEFAULT  }, .unit = "avdiscard" },
        { "noref",              .type = AV_OPT_TYPE_CONST, {.i64 = AVDISCARD_NONREF   }, .unit = "avdiscard" },
        { "bidir",              .type = AV_OPT_TYPE_CONST, {.i64 = AVDISCARD_BIDIR    }, .unit = "avdiscard" },
        { "nointra",            .type = AV_OPT_TYPE_CONST, {.i64 = AVDISCARD_NONINTRA }, .unit = "avdiscard" },
        { "nokey",              .type = AV_OPT_TYPE_CONST, {.i64 = AVDISCARD_NONKEY   }, .unit = "avdiscard" },
        { "all",                .type = AV_OPT_TYPE_CONST, {.i64 = AVDISCARD_ALL      }, .unit = "avdiscard" },
    { NULL }
};

static const AVClass stream_class = {
    .class_name     = "AVStream",
    .item_name      = av_default_item_name,
    .version        = LIBAVUTIL_VERSION_INT,
    .option         = stream_options,
};

const AVClass *av_stream_get_class(void)
{
    return &stream_class;
}

AVStream *avformat_new_stream(AVFormatContext *s, const AVCodec *c)
{
    FFFormatContext *const si = ffformatcontext(s);
    FFStream *sti;
    AVStream *st;
    AVStream **streams;

    if (s->nb_streams >= s->max_streams) {
        av_log(s, AV_LOG_ERROR, "Number of streams exceeds max_streams parameter"
               " (%d), see the documentation if you wish to increase it\n",
               s->max_streams);
        return NULL;
    }
    streams = av_realloc_array(s->streams, s->nb_streams + 1, sizeof(*streams));
    if (!streams)
        return NULL;
    s->streams = streams;

    sti = av_mallocz(sizeof(*sti));
    if (!sti)
        return NULL;
    st = &sti->pub;

    st->av_class = &stream_class;
    st->codecpar = avcodec_parameters_alloc();
    if (!st->codecpar)
        goto fail;

    sti->fmtctx = s;

    if (s->iformat) {
        sti->avctx = avcodec_alloc_context3(NULL);
        if (!sti->avctx)
            goto fail;

        sti->info = av_mallocz(sizeof(*sti->info));
        if (!sti->info)
            goto fail;

#if FF_API_R_FRAME_RATE
        sti->info->last_dts      = AV_NOPTS_VALUE;
#endif
        sti->info->fps_first_dts = AV_NOPTS_VALUE;
        sti->info->fps_last_dts  = AV_NOPTS_VALUE;

        /* default pts setting is MPEG-like */
        avpriv_set_pts_info(st, 33, 1, 90000);
        /* we set the current DTS to 0 so that formats without any timestamps
         * but durations get some timestamps, formats with some unknown
         * timestamps have their first few packets buffered and the
         * timestamps corrected before they are returned to the user */
        sti->cur_dts = RELATIVE_TS_BASE;
    } else {
        sti->cur_dts = AV_NOPTS_VALUE;
    }

    st->index      = s->nb_streams;
    st->start_time = AV_NOPTS_VALUE;
    st->duration   = AV_NOPTS_VALUE;
    sti->first_dts     = AV_NOPTS_VALUE;
    sti->probe_packets = s->max_probe_packets;
    sti->pts_wrap_reference = AV_NOPTS_VALUE;
    sti->pts_wrap_behavior  = AV_PTS_WRAP_IGNORE;

    sti->last_IP_pts = AV_NOPTS_VALUE;
    sti->last_dts_for_order_check = AV_NOPTS_VALUE;
    for (int i = 0; i < MAX_REORDER_DELAY + 1; i++)
        sti->pts_buffer[i] = AV_NOPTS_VALUE;

    st->sample_aspect_ratio = (AVRational) { 0, 1 };
    sti->transferred_mux_tb = (AVRational) { 0, 1 };;

#if FF_API_AVSTREAM_SIDE_DATA
    sti->inject_global_side_data = si->inject_global_side_data;
#endif

    sti->need_context_update = 1;

    s->streams[s->nb_streams++] = st;
    return st;
fail:
    ff_free_stream(&st);
    return NULL;
}

#define FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
#define OFFSET(x) offsetof(AVStreamGroupTileGrid, x)
static const AVOption tile_grid_options[] = {
    { "grid_size", "size of the output canvas", OFFSET(coded_width),
        AV_OPT_TYPE_IMAGE_SIZE, { .str = NULL }, 0, INT_MAX, FLAGS },
    { "output_size", "size of valid pixels in output image meant for presentation", OFFSET(width),
        AV_OPT_TYPE_IMAGE_SIZE, { .str = NULL }, 0, INT_MAX, FLAGS },
    { "background_color", "set a background color for unused pixels",
        OFFSET(background), AV_OPT_TYPE_COLOR, { .str = "black"}, 0, 0, FLAGS },
    { "horizontal_offset", NULL, OFFSET(horizontal_offset), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "vertical_offset",   NULL, OFFSET(vertical_offset),   AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { NULL },
};
#undef FLAGS
#undef OFFSET

static const AVClass tile_grid_class = {
    .class_name = "AVStreamGroupTileGrid",
    .version    = LIBAVUTIL_VERSION_INT,
    .option     = tile_grid_options,
};

static void *stream_group_child_next(void *obj, void *prev)
{
    AVStreamGroup *stg = obj;
    if (!prev) {
        switch(stg->type) {
        case AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT:
            return stg->params.iamf_audio_element;
        case AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION:
            return stg->params.iamf_mix_presentation;
        case AV_STREAM_GROUP_PARAMS_TILE_GRID:
            return stg->params.tile_grid;
        default:
            break;
        }
    }
    return NULL;
}

static const AVClass *stream_group_child_iterate(void **opaque)
{
    uintptr_t i = (uintptr_t)*opaque;
    const AVClass *ret = NULL;

    switch(i) {
    case AV_STREAM_GROUP_PARAMS_NONE:
        i++;
    // fall-through
    case AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT:
        ret = av_iamf_audio_element_get_class();
        break;
    case AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION:
        ret = av_iamf_mix_presentation_get_class();
        break;
    case AV_STREAM_GROUP_PARAMS_TILE_GRID:
        ret = &tile_grid_class;
        break;
    default:
        break;
    }

    if (ret)
        *opaque = (void*)(i + 1);
    return ret;
}

static const AVOption stream_group_options[] = {
    DISPOSITION_OPT(AVStreamGroup),
    {"id", "Set group id", offsetof(AVStreamGroup, id), AV_OPT_TYPE_INT64, {.i64 = 0}, 0, INT64_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};

static const AVClass stream_group_class = {
    .class_name     = "AVStreamGroup",
    .item_name      = av_default_item_name,
    .version        = LIBAVUTIL_VERSION_INT,
    .option         = stream_group_options,
    .child_next     = stream_group_child_next,
    .child_class_iterate = stream_group_child_iterate,
};

const AVClass *av_stream_group_get_class(void)
{
    return &stream_group_class;
}

AVStreamGroup *avformat_stream_group_create(AVFormatContext *s,
                                            enum AVStreamGroupParamsType type,
                                            AVDictionary **options)
{
    AVStreamGroup **stream_groups;
    AVStreamGroup *stg;
    FFStreamGroup *stgi;

    stream_groups = av_realloc_array(s->stream_groups, s->nb_stream_groups + 1,
                                     sizeof(*stream_groups));
    if (!stream_groups)
        return NULL;
    s->stream_groups = stream_groups;

    stgi = av_mallocz(sizeof(*stgi));
    if (!stgi)
        return NULL;
    stg = &stgi->pub;

    stg->av_class = &stream_group_class;
    av_opt_set_defaults(stg);
    stg->type = type;
    switch (type) {
    case AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT:
        stg->params.iamf_audio_element = av_iamf_audio_element_alloc();
        if (!stg->params.iamf_audio_element)
            goto fail;
        break;
    case AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION:
        stg->params.iamf_mix_presentation = av_iamf_mix_presentation_alloc();
        if (!stg->params.iamf_mix_presentation)
            goto fail;
        break;
    case AV_STREAM_GROUP_PARAMS_TILE_GRID:
        stg->params.tile_grid = av_mallocz(sizeof(*stg->params.tile_grid));
        if (!stg->params.tile_grid)
            goto fail;
        stg->params.tile_grid->av_class = &tile_grid_class;
        av_opt_set_defaults(stg->params.tile_grid);
        break;
    default:
        goto fail;
    }

    if (options) {
        if (av_opt_set_dict2(stg, options, AV_OPT_SEARCH_CHILDREN))
            goto fail;
    }

    stgi->fmtctx = s;
    stg->index   = s->nb_stream_groups;

    s->stream_groups[s->nb_stream_groups++] = stg;

    return stg;
fail:
    ff_free_stream_group(&stg);
    return NULL;
}

static int stream_group_add_stream(AVStreamGroup *stg, AVStream *st)
{
    AVStream **streams = av_realloc_array(stg->streams, stg->nb_streams + 1,
                                          sizeof(*stg->streams));
    if (!streams)
        return AVERROR(ENOMEM);

    stg->streams = streams;
    stg->streams[stg->nb_streams++] = st;

    return 0;
}

int avformat_stream_group_add_stream(AVStreamGroup *stg, AVStream *st)
{
    const FFStreamGroup *stgi = cffstreamgroup(stg);
    const FFStream *sti = cffstream(st);

    if (stgi->fmtctx != sti->fmtctx)
        return AVERROR(EINVAL);

    for (int i = 0; i < stg->nb_streams; i++)
        if (stg->streams[i]->index == st->index)
            return AVERROR(EEXIST);

    return stream_group_add_stream(stg, st);
}

static int option_is_disposition(const AVOption *opt)
{
    return opt->type == AV_OPT_TYPE_CONST &&
           opt->unit && !strcmp(opt->unit, "disposition");
}

int av_disposition_from_string(const char *disp)
{
    for (const AVOption *opt = stream_options; opt->name; opt++)
        if (option_is_disposition(opt) && !strcmp(disp, opt->name))
            return opt->default_val.i64;
    return AVERROR(EINVAL);
}

const char *av_disposition_to_string(int disposition)
{
    int val;

    if (disposition <= 0)
        return NULL;

    val = 1 << ff_ctz(disposition);
    for (const AVOption *opt = stream_options; opt->name; opt++)
        if (option_is_disposition(opt) && opt->default_val.i64 == val)
            return opt->name;

    return NULL;
}
