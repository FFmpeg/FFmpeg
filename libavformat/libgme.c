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

/**
* @file
* libgme demuxer
*/

#include <gme/gme.h>
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

typedef struct GMEContext {
    const AVClass *class;
    Music_Emu *music_emu;

    /* options */
    int track_index;
    int sample_rate;
    int64_t max_size;
} GMEContext;

#define OFFSET(x) offsetof(GMEContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define D AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    {"track_index", "set track that should be played",        OFFSET(track_index), AV_OPT_TYPE_INT,   {.i64 = 0},                0,    INT_MAX,  A|D},
    {"sample_rate", "set sample rate",                        OFFSET(sample_rate), AV_OPT_TYPE_INT,   {.i64 = 44100},            1000, 999999,   A|D},
    {"max_size",    "set max file size supported (in bytes)", OFFSET(max_size),    AV_OPT_TYPE_INT64, {.i64 = 50 * 1024 * 1024}, 0,    SIZE_MAX, A|D},
    {NULL}
};

static void add_meta(AVFormatContext *s, const char *name, const char *value)
{
    if (value && value[0])
        av_dict_set(&s->metadata, name, value, 0);
}

static int load_metadata(AVFormatContext *s, int64_t *duration)
{
    GMEContext *gme = s->priv_data;
    gme_info_t *info  = NULL;
    char buf[30];

    if (gme_track_info(gme->music_emu, &info, gme->track_index))
        return AVERROR_STREAM_NOT_FOUND;

    *duration = info->length;
    add_meta(s, "system",       info->system);
    add_meta(s, "game",         info->game);
    add_meta(s, "song",         info->song);
    add_meta(s, "author",       info->author);
    add_meta(s, "copyright",    info->copyright);
    add_meta(s, "comment",      info->comment);
    add_meta(s, "dumper",       info->dumper);

    snprintf(buf, sizeof(buf), "%d", (int)gme_track_count(gme->music_emu));
    add_meta(s, "tracks", buf);
    gme_free_info(info);

    return 0;
}

#define AUDIO_PKT_SIZE 512

static int read_close_gme(AVFormatContext *s)
{
    GMEContext *gme = s->priv_data;
    if (gme->music_emu)
        gme_delete(gme->music_emu);
    return 0;
}

static int read_header_gme(AVFormatContext *s)
{
    AVStream *st;
    AVIOContext *pb = s->pb;
    GMEContext *gme = s->priv_data;
    int64_t sz = avio_size(pb);
    int64_t duration;
    char *buf;
    char dummy;
    int ret;

    if (sz < 0) {
        av_log(s, AV_LOG_WARNING, "Could not determine file size\n");
        sz = gme->max_size;
    } else if (gme->max_size && sz > gme->max_size) {
        sz = gme->max_size;
    }

    buf = av_malloc(sz);
    if (!buf)
        return AVERROR(ENOMEM);
    sz = avio_read(pb, buf, sz);

    // Data left means our buffer (the max_size option) is too small
    if (avio_read(pb, &dummy, 1) == 1) {
        av_log(s, AV_LOG_ERROR, "File size is larger than max_size option "
               "value %"PRIi64", consider increasing the max_size option\n",
               gme->max_size);
        av_freep(&buf);
        return AVERROR_BUFFER_TOO_SMALL;
    }

    if (gme_open_data(buf, sz, &gme->music_emu, gme->sample_rate)) {
        gme->music_emu = NULL; /* Just for safety */
        av_freep(&buf);
        return AVERROR_INVALIDDATA;
    }
    av_freep(&buf);

    ret = load_metadata(s, &duration);
    if (ret < 0)
        return ret;
    if (gme_start_track(gme->music_emu, gme->track_index))
        return AVERROR_UNKNOWN;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000);
    if (duration > 0)
        st->duration = duration;
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE);
    st->codecpar->ch_layout.nb_channels = 2;
    st->codecpar->sample_rate = gme->sample_rate;

    return 0;
}

static int read_packet_gme(AVFormatContext *s, AVPacket *pkt)
{
    GMEContext *gme = s->priv_data;
    int n_samples = AUDIO_PKT_SIZE / 2;
    int ret;

    if (gme_track_ended(gme->music_emu))
        return AVERROR_EOF;

    if ((ret = av_new_packet(pkt, AUDIO_PKT_SIZE)) < 0)
        return ret;

    if (gme_play(gme->music_emu, n_samples, (short *)pkt->data))
        return AVERROR_EXTERNAL;

    return 0;
}

static int read_seek_gme(AVFormatContext *s, int stream_idx, int64_t ts, int flags)
{
    GMEContext *gme = s->priv_data;
    if (!gme_seek(gme->music_emu, (int)ts))
        return AVERROR_EXTERNAL;
    return 0;
}

static int probe_gme(const AVProbeData *p)
{
    // Reads 4 bytes - returns "" if unknown format.
    if (gme_identify_header(p->buf)[0]) {
        if (p->buf_size < 16384)
            return AVPROBE_SCORE_MAX / 4 ;
        else
            return AVPROBE_SCORE_MAX / 2;
    }
    return 0;
}

static const AVClass class_gme = {
    .class_name = "Game Music Emu demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFInputFormat ff_libgme_demuxer = {
    .p.name         = "libgme",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Game Music Emu demuxer"),
    .p.priv_class   = &class_gme,
    .priv_data_size = sizeof(GMEContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = probe_gme,
    .read_header    = read_header_gme,
    .read_packet    = read_packet_gme,
    .read_close     = read_close_gme,
    .read_seek      = read_seek_gme,
};
