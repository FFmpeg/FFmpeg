/*
 * Copyright (c) 2011 Stefano Sabatini
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
 * filter for showing textual audio frame information
 */

#include <inttypes.h>
#include <stddef.h>

#include "libavutil/adler32.h"
#include "libavutil/attributes.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/downmix_info.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/replaygain.h"
#include "libavutil/timestamp.h"
#include "libavutil/samplefmt.h"

#include "libavcodec/avcodec.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct AShowInfoContext {
    /**
     * Scratch space for individual plane checksums for planar audio
     */
    uint32_t *plane_checksums;
} AShowInfoContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    AShowInfoContext *s = ctx->priv;
    av_freep(&s->plane_checksums);
}

static void dump_matrixenc(AVFilterContext *ctx, AVFrameSideData *sd)
{
    enum AVMatrixEncoding enc;

    av_log(ctx, AV_LOG_INFO, "matrix encoding: ");

    if (sd->size < sizeof(enum AVMatrixEncoding)) {
        av_log(ctx, AV_LOG_INFO, "invalid data");
        return;
    }

    enc = *(enum AVMatrixEncoding *)sd->data;
    switch (enc) {
    case AV_MATRIX_ENCODING_NONE:           av_log(ctx, AV_LOG_INFO, "none");                break;
    case AV_MATRIX_ENCODING_DOLBY:          av_log(ctx, AV_LOG_INFO, "Dolby Surround");      break;
    case AV_MATRIX_ENCODING_DPLII:          av_log(ctx, AV_LOG_INFO, "Dolby Pro Logic II");  break;
    case AV_MATRIX_ENCODING_DPLIIX:         av_log(ctx, AV_LOG_INFO, "Dolby Pro Logic IIx"); break;
    case AV_MATRIX_ENCODING_DPLIIZ:         av_log(ctx, AV_LOG_INFO, "Dolby Pro Logic IIz"); break;
    case AV_MATRIX_ENCODING_DOLBYEX:        av_log(ctx, AV_LOG_INFO, "Dolby EX");            break;
    case AV_MATRIX_ENCODING_DOLBYHEADPHONE: av_log(ctx, AV_LOG_INFO, "Dolby Headphone");     break;
    default:                                av_log(ctx, AV_LOG_WARNING, "unknown");          break;
    }
}

static void dump_downmix(AVFilterContext *ctx, AVFrameSideData *sd)
{
    AVDownmixInfo *di;

    av_log(ctx, AV_LOG_INFO, "downmix: ");
    if (sd->size < sizeof(*di)) {
        av_log(ctx, AV_LOG_INFO, "invalid data");
        return;
    }

    di = (AVDownmixInfo *)sd->data;

    av_log(ctx, AV_LOG_INFO, "preferred downmix type - ");
    switch (di->preferred_downmix_type) {
    case AV_DOWNMIX_TYPE_LORO:    av_log(ctx, AV_LOG_INFO, "Lo/Ro");              break;
    case AV_DOWNMIX_TYPE_LTRT:    av_log(ctx, AV_LOG_INFO, "Lt/Rt");              break;
    case AV_DOWNMIX_TYPE_DPLII:   av_log(ctx, AV_LOG_INFO, "Dolby Pro Logic II"); break;
    default:                      av_log(ctx, AV_LOG_WARNING, "unknown");         break;
    }

    av_log(ctx, AV_LOG_INFO, " Mix levels: center %f (%f ltrt) - "
           "surround %f (%f ltrt) - lfe %f",
           di->center_mix_level, di->center_mix_level_ltrt,
           di->surround_mix_level, di->surround_mix_level_ltrt,
           di->lfe_mix_level);
}

static void print_gain(AVFilterContext *ctx, const char *str, int32_t gain)
{
    av_log(ctx, AV_LOG_INFO, "%s - ", str);
    if (gain == INT32_MIN)
        av_log(ctx, AV_LOG_INFO, "unknown");
    else
        av_log(ctx, AV_LOG_INFO, "%f", gain / 100000.0f);
    av_log(ctx, AV_LOG_INFO, ", ");
}

static void print_peak(AVFilterContext *ctx, const char *str, uint32_t peak)
{
    av_log(ctx, AV_LOG_INFO, "%s - ", str);
    if (!peak)
        av_log(ctx, AV_LOG_INFO, "unknown");
    else
        av_log(ctx, AV_LOG_INFO, "%f", (float)peak / UINT32_MAX);
    av_log(ctx, AV_LOG_INFO, ", ");
}

static void dump_replaygain(AVFilterContext *ctx, AVFrameSideData *sd)
{
    AVReplayGain *rg;

    av_log(ctx, AV_LOG_INFO, "replaygain: ");
    if (sd->size < sizeof(*rg)) {
        av_log(ctx, AV_LOG_INFO, "invalid data");
        return;
    }
    rg = (AVReplayGain*)sd->data;

    print_gain(ctx, "track gain", rg->track_gain);
    print_peak(ctx, "track peak", rg->track_peak);
    print_gain(ctx, "album gain", rg->album_gain);
    print_peak(ctx, "album peak", rg->album_peak);
}

static void dump_audio_service_type(AVFilterContext *ctx, AVFrameSideData *sd)
{
    enum AVAudioServiceType *ast;

    av_log(ctx, AV_LOG_INFO, "audio service type: ");
    if (sd->size < sizeof(*ast)) {
        av_log(ctx, AV_LOG_INFO, "invalid data");
        return;
    }
    ast = (enum AVAudioServiceType*)sd->data;
    switch (*ast) {
    case AV_AUDIO_SERVICE_TYPE_MAIN:              av_log(ctx, AV_LOG_INFO, "Main Audio Service"); break;
    case AV_AUDIO_SERVICE_TYPE_EFFECTS:           av_log(ctx, AV_LOG_INFO, "Effects");            break;
    case AV_AUDIO_SERVICE_TYPE_VISUALLY_IMPAIRED: av_log(ctx, AV_LOG_INFO, "Visually Impaired");  break;
    case AV_AUDIO_SERVICE_TYPE_HEARING_IMPAIRED:  av_log(ctx, AV_LOG_INFO, "Hearing Impaired");   break;
    case AV_AUDIO_SERVICE_TYPE_DIALOGUE:          av_log(ctx, AV_LOG_INFO, "Dialogue");           break;
    case AV_AUDIO_SERVICE_TYPE_COMMENTARY:        av_log(ctx, AV_LOG_INFO, "Commentary");         break;
    case AV_AUDIO_SERVICE_TYPE_EMERGENCY:         av_log(ctx, AV_LOG_INFO, "Emergency");          break;
    case AV_AUDIO_SERVICE_TYPE_VOICE_OVER:        av_log(ctx, AV_LOG_INFO, "Voice Over");         break;
    case AV_AUDIO_SERVICE_TYPE_KARAOKE:           av_log(ctx, AV_LOG_INFO, "Karaoke");            break;
    default:                                      av_log(ctx, AV_LOG_INFO, "unknown");            break;
    }
}

static void dump_unknown(AVFilterContext *ctx, AVFrameSideData *sd)
{
    av_log(ctx, AV_LOG_INFO, "unknown side data type: %d, size %d bytes", sd->type, sd->size);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext *ctx = inlink->dst;
    AShowInfoContext *s  = ctx->priv;
    char chlayout_str[128];
    uint32_t checksum = 0;
    int channels    = inlink->channels;
    int planar      = av_sample_fmt_is_planar(buf->format);
    int block_align = av_get_bytes_per_sample(buf->format) * (planar ? 1 : channels);
    int data_size   = buf->nb_samples * block_align;
    int planes      = planar ? channels : 1;
    int i;
    void *tmp_ptr = av_realloc_array(s->plane_checksums, channels, sizeof(*s->plane_checksums));

    if (!tmp_ptr)
        return AVERROR(ENOMEM);
    s->plane_checksums = tmp_ptr;

    for (i = 0; i < planes; i++) {
        uint8_t *data = buf->extended_data[i];

        s->plane_checksums[i] = av_adler32_update(0, data, data_size);
        checksum = i ? av_adler32_update(checksum, data, data_size) :
                       s->plane_checksums[0];
    }

    av_get_channel_layout_string(chlayout_str, sizeof(chlayout_str), av_frame_get_channels(buf),
                                 buf->channel_layout);

    av_log(ctx, AV_LOG_INFO,
           "n:%"PRId64" pts:%s pts_time:%s pos:%"PRId64" "
           "fmt:%s channels:%d chlayout:%s rate:%d nb_samples:%d "
           "checksum:%08"PRIX32" ",
           inlink->frame_count_out,
           av_ts2str(buf->pts), av_ts2timestr(buf->pts, &inlink->time_base),
           av_frame_get_pkt_pos(buf),
           av_get_sample_fmt_name(buf->format), av_frame_get_channels(buf), chlayout_str,
           buf->sample_rate, buf->nb_samples,
           checksum);

    av_log(ctx, AV_LOG_INFO, "plane_checksums: [ ");
    for (i = 0; i < planes; i++)
        av_log(ctx, AV_LOG_INFO, "%08"PRIX32" ", s->plane_checksums[i]);
    av_log(ctx, AV_LOG_INFO, "]\n");

    for (i = 0; i < buf->nb_side_data; i++) {
        AVFrameSideData *sd = buf->side_data[i];

        av_log(ctx, AV_LOG_INFO, "  side data - ");
        switch (sd->type) {
        case AV_FRAME_DATA_MATRIXENCODING: dump_matrixenc (ctx, sd); break;
        case AV_FRAME_DATA_DOWNMIX_INFO:   dump_downmix   (ctx, sd); break;
        case AV_FRAME_DATA_REPLAYGAIN:     dump_replaygain(ctx, sd); break;
        case AV_FRAME_DATA_AUDIO_SERVICE_TYPE: dump_audio_service_type(ctx, sd); break;
        default:                           dump_unknown   (ctx, sd); break;
        }

        av_log(ctx, AV_LOG_INFO, "\n");
    }

    return ff_filter_frame(inlink->dst->outputs[0], buf);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_ashowinfo = {
    .name        = "ashowinfo",
    .description = NULL_IF_CONFIG_SMALL("Show textual information for each audio frame."),
    .priv_size   = sizeof(AShowInfoContext),
    .uninit      = uninit,
    .inputs      = inputs,
    .outputs     = outputs,
};
