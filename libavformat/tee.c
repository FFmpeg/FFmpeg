/*
 * Tee pesudo-muxer
 * Copyright (c) 2012 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


#include "libavutil/avutil.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "avformat.h"

#define MAX_SLAVES 16

typedef struct TeeContext {
    const AVClass *class;
    unsigned nb_slaves;
    AVFormatContext *slaves[MAX_SLAVES];
} TeeContext;

static const char *const slave_delim     = "|";
static const char *const slave_opt_open  = "[";
static const char *const slave_opt_close = "]";
static const char *const slave_opt_delim = ":]"; /* must have the close too */

static const AVClass tee_muxer_class = {
    .class_name = "Tee muxer",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int parse_slave_options(void *log, char *slave,
                               AVDictionary **options, char **filename)
{
    const char *p;
    char *key, *val;
    int ret;

    if (!strspn(slave, slave_opt_open)) {
        *filename = slave;
        return 0;
    }
    p = slave + 1;
    if (strspn(p, slave_opt_close)) {
        *filename = (char *)p + 1;
        return 0;
    }
    while (1) {
        ret = av_opt_get_key_value(&p, "=", slave_opt_delim, 0, &key, &val);
        if (ret < 0) {
            av_log(log, AV_LOG_ERROR, "No option found near \"%s\"\n", p);
            goto fail;
        }
        ret = av_dict_set(options, key, val,
                          AV_DICT_DONT_STRDUP_KEY | AV_DICT_DONT_STRDUP_VAL);
        if (ret < 0)
            goto fail;
        if (strspn(p, slave_opt_close))
            break;
        p++;
    }
    *filename = (char *)p + 1;
    return 0;

fail:
    av_dict_free(options);
    return ret;
}

static int open_slave(AVFormatContext *avf, char *slave, AVFormatContext **ravf)
{
    int i, ret;
    AVDictionary *options = NULL;
    AVDictionaryEntry *entry;
    char *filename;
    char *format = NULL;
    AVFormatContext *avf2 = NULL;
    AVStream *st, *st2;

    if ((ret = parse_slave_options(avf, slave, &options, &filename)) < 0)
        return ret;
    if ((entry = av_dict_get(options, "f", NULL, 0))) {
        format = entry->value;
        entry->value = NULL; /* prevent it from being freed */
        av_dict_set(&options, "f", NULL, 0);
    }

    ret = avformat_alloc_output_context2(&avf2, NULL, format, filename);
    if (ret < 0)
        goto fail;
    av_free(format);

    for (i = 0; i < avf->nb_streams; i++) {
        st = avf->streams[i];
        if (!(st2 = avformat_new_stream(avf2, NULL))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        st2->id = st->id;
        st2->r_frame_rate        = st->r_frame_rate;
        st2->time_base           = st->time_base;
        st2->start_time          = st->start_time;
        st2->duration            = st->duration;
        st2->nb_frames           = st->nb_frames;
        st2->disposition         = st->disposition;
        st2->sample_aspect_ratio = st->sample_aspect_ratio;
        st2->avg_frame_rate      = st->avg_frame_rate;
        av_dict_copy(&st2->metadata, st->metadata, 0);
        if ((ret = avcodec_copy_context(st2->codec, st->codec)) < 0)
            goto fail;
    }

    if (!(avf2->oformat->flags & AVFMT_NOFILE)) {
        if ((ret = avio_open(&avf2->pb, filename, AVIO_FLAG_WRITE)) < 0) {
            av_log(avf, AV_LOG_ERROR, "Slave '%s': error opening: %s\n",
                   slave, av_err2str(ret));
            goto fail;
        }
    }

    if ((ret = avformat_write_header(avf2, &options)) < 0) {
        av_log(avf, AV_LOG_ERROR, "Slave '%s': error writing header: %s\n",
               slave, av_err2str(ret));
        goto fail;
    }
    if (options) {
        entry = NULL;
        while ((entry = av_dict_get(options, "", entry, AV_DICT_IGNORE_SUFFIX)))
            av_log(avf2, AV_LOG_ERROR, "Unknown option '%s'\n", entry->key);
        ret = AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }

    *ravf = avf2;
    return 0;

fail:
    av_dict_free(&options);
    return ret;
}

static void close_slaves(AVFormatContext *avf)
{
    TeeContext *tee = avf->priv_data;
    AVFormatContext *avf2;
    unsigned i;

    for (i = 0; i < tee->nb_slaves; i++) {
        avf2 = tee->slaves[i];
        avio_close(avf2->pb);
        avf2->pb = NULL;
        avformat_free_context(avf2);
        tee->slaves[i] = NULL;
    }
}

static int tee_write_header(AVFormatContext *avf)
{
    TeeContext *tee = avf->priv_data;
    unsigned nb_slaves = 0, i;
    const char *filename = avf->filename;
    char *slaves[MAX_SLAVES];
    int ret;

    while (*filename) {
        if (nb_slaves == MAX_SLAVES) {
            av_log(avf, AV_LOG_ERROR, "Maximum %d slave muxers reached.\n",
                   MAX_SLAVES);
            ret = AVERROR_PATCHWELCOME;
            goto fail;
        }
        if (!(slaves[nb_slaves++] = av_get_token(&filename, slave_delim))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if (strspn(filename, slave_delim))
            filename++;
    }

    for (i = 0; i < nb_slaves; i++) {
        if ((ret = open_slave(avf, slaves[i], &tee->slaves[i])) < 0)
            goto fail;
        av_freep(&slaves[i]);
    }

    tee->nb_slaves = nb_slaves;
    return 0;

fail:
    for (i = 0; i < nb_slaves; i++)
        av_freep(&slaves[i]);
    close_slaves(avf);
    return ret;
}

static int tee_write_trailer(AVFormatContext *avf)
{
    TeeContext *tee = avf->priv_data;
    AVFormatContext *avf2;
    int ret_all = 0, ret;
    unsigned i;

    for (i = 0; i < tee->nb_slaves; i++) {
        avf2 = tee->slaves[i];
        if ((ret = av_write_trailer(avf2)) < 0)
            if (!ret_all)
                ret_all = ret;
        if (!(avf2->oformat->flags & AVFMT_NOFILE)) {
            if ((ret = avio_close(avf2->pb)) < 0)
                if (!ret_all)
                    ret_all = ret;
            avf2->pb = NULL;
        }
    }
    close_slaves(avf);
    return ret_all;
}

static int tee_write_packet(AVFormatContext *avf, AVPacket *pkt)
{
    TeeContext *tee = avf->priv_data;
    AVFormatContext *avf2;
    AVPacket pkt2;
    int ret_all = 0, ret;
    unsigned i, s;
    AVRational tb, tb2;

    for (i = 0; i < tee->nb_slaves; i++) {
        avf2 = tee->slaves[i];
        s = pkt->stream_index;
        if (s >= avf2->nb_streams) {
            if (!ret_all)
                ret_all = AVERROR(EINVAL);
            continue;
        }
        if ((ret = av_copy_packet(&pkt2, pkt)) < 0 ||
            (ret = av_dup_packet(&pkt2))< 0)
            if (!ret_all) {
                ret = ret_all;
                continue;
            }
        tb  = avf ->streams[s]->time_base;
        tb2 = avf2->streams[s]->time_base;
        pkt2.pts      = av_rescale_q(pkt->pts,      tb, tb2);
        pkt2.dts      = av_rescale_q(pkt->dts,      tb, tb2);
        pkt2.duration = av_rescale_q(pkt->duration, tb, tb2);
        if ((ret = av_interleaved_write_frame(avf2, &pkt2)) < 0)
            if (!ret_all)
                ret_all = ret;
    }
    return ret_all;
}

AVOutputFormat ff_tee_muxer = {
    .name              = "tee",
    .long_name         = NULL_IF_CONFIG_SMALL("Multiple muxer tee"),
    .priv_data_size    = sizeof(TeeContext),
    .write_header      = tee_write_header,
    .write_trailer     = tee_write_trailer,
    .write_packet      = tee_write_packet,
    .priv_class        = &tee_muxer_class,
    .flags             = AVFMT_NOFILE,
};
