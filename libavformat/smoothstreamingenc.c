/*
 * Live smooth streaming fragmenter
 * Copyright (c) 2012 Martin Storsjo
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

#include "config.h"
#include <float.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "avformat.h"
#include "internal.h"
#include "os_support.h"
#include "avc.h"
#include "url.h"
#include "isom.h"

#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/mathematics.h"
#include "libavutil/intreadwrite.h"

typedef struct {
    char file[1024];
    char infofile[1024];
    int64_t start_time, duration;
    int n;
    int64_t start_pos, size;
} Fragment;

typedef struct {
    AVFormatContext *ctx;
    int ctx_inited;
    char dirname[1024];
    uint8_t iobuf[32768];
    URLContext *out;  // Current output stream where all output is written
    URLContext *out2; // Auxiliary output stream where all output is also written
    URLContext *tail_out; // The actual main output stream, if we're currently seeked back to write elsewhere
    int64_t tail_pos, cur_pos, cur_start_pos;
    int packets_written;
    const char *stream_type_tag;
    int nb_fragments, fragments_size, fragment_index;
    Fragment **fragments;

    const char *fourcc;
    char *private_str;
    int packet_size;
    int audio_tag;
} OutputStream;

typedef struct {
    const AVClass *class;  /* Class for private options. */
    int window_size;
    int extra_window_size;
    int lookahead_count;
    int min_frag_duration;
    int remove_at_exit;
    OutputStream *streams;
    int has_video, has_audio;
    int nb_fragments;
} SmoothStreamingContext;

static int ism_write(void *opaque, uint8_t *buf, int buf_size)
{
    OutputStream *os = opaque;
    if (os->out)
        ffurl_write(os->out, buf, buf_size);
    if (os->out2)
        ffurl_write(os->out2, buf, buf_size);
    os->cur_pos += buf_size;
    if (os->cur_pos >= os->tail_pos)
        os->tail_pos = os->cur_pos;
    return buf_size;
}

static int64_t ism_seek(void *opaque, int64_t offset, int whence)
{
    OutputStream *os = opaque;
    int i;
    if (whence != SEEK_SET)
        return AVERROR(ENOSYS);
    if (os->tail_out) {
        if (os->out) {
            ffurl_close(os->out);
        }
        if (os->out2) {
            ffurl_close(os->out2);
        }
        os->out = os->tail_out;
        os->out2 = NULL;
        os->tail_out = NULL;
    }
    if (offset >= os->cur_start_pos) {
        if (os->out)
            ffurl_seek(os->out, offset - os->cur_start_pos, SEEK_SET);
        os->cur_pos = offset;
        return offset;
    }
    for (i = os->nb_fragments - 1; i >= 0; i--) {
        Fragment *frag = os->fragments[i];
        if (offset >= frag->start_pos && offset < frag->start_pos + frag->size) {
            int ret;
            AVDictionary *opts = NULL;
            os->tail_out = os->out;
            av_dict_set(&opts, "truncate", "0", 0);
            ret = ffurl_open(&os->out, frag->file, AVIO_FLAG_READ_WRITE, &os->ctx->interrupt_callback, &opts);
            av_dict_free(&opts);
            if (ret < 0) {
                os->out = os->tail_out;
                os->tail_out = NULL;
                return ret;
            }
            av_dict_set(&opts, "truncate", "0", 0);
            ffurl_open(&os->out2, frag->infofile, AVIO_FLAG_READ_WRITE, &os->ctx->interrupt_callback, &opts);
            av_dict_free(&opts);
            ffurl_seek(os->out, offset - frag->start_pos, SEEK_SET);
            if (os->out2)
                ffurl_seek(os->out2, offset - frag->start_pos, SEEK_SET);
            os->cur_pos = offset;
            return offset;
        }
    }
    return AVERROR(EIO);
}

static void get_private_data(OutputStream *os)
{
    AVCodecContext *codec = os->ctx->streams[0]->codec;
    uint8_t *ptr = codec->extradata;
    int size = codec->extradata_size;
    int i;
    if (codec->codec_id == AV_CODEC_ID_H264) {
        ff_avc_write_annexb_extradata(ptr, &ptr, &size);
        if (!ptr)
            ptr = codec->extradata;
    }
    if (!ptr)
        return;
    os->private_str = av_mallocz(2*size + 1);
    for (i = 0; i < size; i++)
        snprintf(&os->private_str[2*i], 3, "%02x", ptr[i]);
    if (ptr != codec->extradata)
        av_free(ptr);
}

static void ism_free(AVFormatContext *s)
{
    SmoothStreamingContext *c = s->priv_data;
    int i, j;
    if (!c->streams)
        return;
    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        ffurl_close(os->out);
        ffurl_close(os->out2);
        ffurl_close(os->tail_out);
        os->out = os->out2 = os->tail_out = NULL;
        if (os->ctx && os->ctx_inited)
            av_write_trailer(os->ctx);
        if (os->ctx && os->ctx->pb)
            av_free(os->ctx->pb);
        if (os->ctx)
            avformat_free_context(os->ctx);
        av_free(os->private_str);
        for (j = 0; j < os->nb_fragments; j++)
            av_free(os->fragments[j]);
        av_free(os->fragments);
    }
    av_freep(&c->streams);
}

static void output_chunk_list(OutputStream *os, AVIOContext *out, int final, int skip, int window_size)
{
    int removed = 0, i, start = 0;
    if (os->nb_fragments <= 0)
        return;
    if (os->fragments[0]->n > 0)
        removed = 1;
    if (final)
        skip = 0;
    if (window_size)
        start = FFMAX(os->nb_fragments - skip - window_size, 0);
    for (i = start; i < os->nb_fragments - skip; i++) {
        Fragment *frag = os->fragments[i];
        if (!final || removed)
            avio_printf(out, "<c t=\"%"PRIu64"\" d=\"%"PRIu64"\" />\n", frag->start_time, frag->duration);
        else
            avio_printf(out, "<c n=\"%d\" d=\"%"PRIu64"\" />\n", frag->n, frag->duration);
    }
}

static int write_manifest(AVFormatContext *s, int final)
{
    SmoothStreamingContext *c = s->priv_data;
    AVIOContext *out;
    char filename[1024];
    int ret, i, video_chunks = 0, audio_chunks = 0, video_streams = 0, audio_streams = 0;
    int64_t duration = 0;

    snprintf(filename, sizeof(filename), "%s/Manifest", s->filename);
    ret = avio_open2(&out, filename, AVIO_FLAG_WRITE, &s->interrupt_callback, NULL);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Unable to open %s for writing\n", filename);
        return ret;
    }
    avio_printf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        if (os->nb_fragments > 0) {
            Fragment *last = os->fragments[os->nb_fragments - 1];
            duration = last->start_time + last->duration;
        }
        if (s->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_chunks = os->nb_fragments;
            video_streams++;
        } else {
            audio_chunks = os->nb_fragments;
            audio_streams++;
        }
    }
    if (!final) {
        duration = 0;
        video_chunks = audio_chunks = 0;
    }
    if (c->window_size) {
        video_chunks = FFMIN(video_chunks, c->window_size);
        audio_chunks = FFMIN(audio_chunks, c->window_size);
    }
    avio_printf(out, "<SmoothStreamingMedia MajorVersion=\"2\" MinorVersion=\"0\" Duration=\"%"PRIu64"\"", duration);
    if (!final)
        avio_printf(out, " IsLive=\"true\" LookAheadFragmentCount=\"%d\" DVRWindowLength=\"0\"", c->lookahead_count);
    avio_printf(out, ">\n");
    if (c->has_video) {
        int last = -1, index = 0;
        avio_printf(out, "<StreamIndex Type=\"video\" QualityLevels=\"%d\" Chunks=\"%d\" Url=\"QualityLevels({bitrate})/Fragments(video={start time})\">\n", video_streams, video_chunks);
        for (i = 0; i < s->nb_streams; i++) {
            OutputStream *os = &c->streams[i];
            if (s->streams[i]->codec->codec_type != AVMEDIA_TYPE_VIDEO)
                continue;
            last = i;
            avio_printf(out, "<QualityLevel Index=\"%d\" Bitrate=\"%d\" FourCC=\"%s\" MaxWidth=\"%d\" MaxHeight=\"%d\" CodecPrivateData=\"%s\" />\n", index, s->streams[i]->codec->bit_rate, os->fourcc, s->streams[i]->codec->width, s->streams[i]->codec->height, os->private_str);
            index++;
        }
        output_chunk_list(&c->streams[last], out, final, c->lookahead_count, c->window_size);
        avio_printf(out, "</StreamIndex>\n");
    }
    if (c->has_audio) {
        int last = -1, index = 0;
        avio_printf(out, "<StreamIndex Type=\"audio\" QualityLevels=\"%d\" Chunks=\"%d\" Url=\"QualityLevels({bitrate})/Fragments(audio={start time})\">\n", audio_streams, audio_chunks);
        for (i = 0; i < s->nb_streams; i++) {
            OutputStream *os = &c->streams[i];
            if (s->streams[i]->codec->codec_type != AVMEDIA_TYPE_AUDIO)
                continue;
            last = i;
            avio_printf(out, "<QualityLevel Index=\"%d\" Bitrate=\"%d\" FourCC=\"%s\" SamplingRate=\"%d\" Channels=\"%d\" BitsPerSample=\"16\" PacketSize=\"%d\" AudioTag=\"%d\" CodecPrivateData=\"%s\" />\n", index, s->streams[i]->codec->bit_rate, os->fourcc, s->streams[i]->codec->sample_rate, s->streams[i]->codec->channels, os->packet_size, os->audio_tag, os->private_str);
            index++;
        }
        output_chunk_list(&c->streams[last], out, final, c->lookahead_count, c->window_size);
        avio_printf(out, "</StreamIndex>\n");
    }
    avio_printf(out, "</SmoothStreamingMedia>\n");
    avio_flush(out);
    avio_close(out);
    return 0;
}

static int ism_write_header(AVFormatContext *s)
{
    SmoothStreamingContext *c = s->priv_data;
    int ret = 0, i;
    AVOutputFormat *oformat;

    if (mkdir(s->filename, 0777) < 0) {
        av_log(s, AV_LOG_ERROR, "mkdir failed\n");
        ret = AVERROR(errno);
        goto fail;
    }

    oformat = av_guess_format("ismv", NULL, NULL);
    if (!oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        goto fail;
    }

    c->streams = av_mallocz(sizeof(*c->streams) * s->nb_streams);
    if (!c->streams) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        AVFormatContext *ctx;
        AVStream *st;
        AVDictionary *opts = NULL;
        char buf[10];

        if (!s->streams[i]->codec->bit_rate) {
            av_log(s, AV_LOG_ERROR, "No bit rate set for stream %d\n", i);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        snprintf(os->dirname, sizeof(os->dirname), "%s/QualityLevels(%d)", s->filename, s->streams[i]->codec->bit_rate);
        if (mkdir(os->dirname, 0777) < 0) {
            ret = AVERROR(errno);
            av_log(s, AV_LOG_ERROR, "mkdir failed\n");
            goto fail;
        }

        ctx = avformat_alloc_context();
        if (!ctx) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        os->ctx = ctx;
        ctx->oformat = oformat;
        ctx->interrupt_callback = s->interrupt_callback;

        if (!(st = avformat_new_stream(ctx, NULL))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        avcodec_copy_context(st->codec, s->streams[i]->codec);
        st->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;

        ctx->pb = avio_alloc_context(os->iobuf, sizeof(os->iobuf), AVIO_FLAG_WRITE, os, NULL, ism_write, ism_seek);
        if (!ctx->pb) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        snprintf(buf, sizeof(buf), "%d", c->lookahead_count);
        av_dict_set(&opts, "ism_lookahead", buf, 0);
        av_dict_set(&opts, "movflags", "frag_custom", 0);
        if ((ret = avformat_write_header(ctx, &opts)) < 0) {
             goto fail;
        }
        os->ctx_inited = 1;
        avio_flush(ctx->pb);
        av_dict_free(&opts);
        s->streams[i]->time_base = st->time_base;
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            c->has_video = 1;
            os->stream_type_tag = "video";
            if (st->codec->codec_id == AV_CODEC_ID_H264) {
                os->fourcc = "H264";
            } else if (st->codec->codec_id == AV_CODEC_ID_VC1) {
                os->fourcc = "WVC1";
            } else {
                av_log(s, AV_LOG_ERROR, "Unsupported video codec\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
        } else {
            c->has_audio = 1;
            os->stream_type_tag = "audio";
            if (st->codec->codec_id == AV_CODEC_ID_AAC) {
                os->fourcc = "AACL";
                os->audio_tag = 0xff;
            } else if (st->codec->codec_id == AV_CODEC_ID_WMAPRO) {
                os->fourcc = "WMAP";
                os->audio_tag = 0x0162;
            } else {
                av_log(s, AV_LOG_ERROR, "Unsupported audio codec\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
            os->packet_size = st->codec->block_align ? st->codec->block_align : 4;
        }
        get_private_data(os);
    }

    if (!c->has_video && c->min_frag_duration <= 0) {
        av_log(s, AV_LOG_WARNING, "no video stream and no min frag duration set\n");
        ret = AVERROR(EINVAL);
    }
    ret = write_manifest(s, 0);

fail:
    if (ret)
        ism_free(s);
    return ret;
}

static int parse_fragment(AVFormatContext *s, const char *filename, int64_t *start_ts, int64_t *duration, int64_t *moof_size, int64_t size)
{
    AVIOContext *in;
    int ret;
    uint32_t len;
    if ((ret = avio_open2(&in, filename, AVIO_FLAG_READ, &s->interrupt_callback, NULL)) < 0)
        return ret;
    ret = AVERROR(EIO);
    *moof_size = avio_rb32(in);
    if (*moof_size < 8 || *moof_size > size)
        goto fail;
    if (avio_rl32(in) != MKTAG('m','o','o','f'))
        goto fail;
    len = avio_rb32(in);
    if (len > *moof_size)
        goto fail;
    if (avio_rl32(in) != MKTAG('m','f','h','d'))
        goto fail;
    avio_seek(in, len - 8, SEEK_CUR);
    avio_rb32(in); /* traf size */
    if (avio_rl32(in) != MKTAG('t','r','a','f'))
        goto fail;
    while (avio_tell(in) < *moof_size) {
        uint32_t len = avio_rb32(in);
        uint32_t tag = avio_rl32(in);
        int64_t end = avio_tell(in) + len - 8;
        if (len < 8 || len >= *moof_size)
            goto fail;
        if (tag == MKTAG('u','u','i','d')) {
            const uint8_t tfxd[] = {
                0x6d, 0x1d, 0x9b, 0x05, 0x42, 0xd5, 0x44, 0xe6,
                0x80, 0xe2, 0x14, 0x1d, 0xaf, 0xf7, 0x57, 0xb2
            };
            uint8_t uuid[16];
            avio_read(in, uuid, 16);
            if (!memcmp(uuid, tfxd, 16) && len >= 8 + 16 + 4 + 16) {
                avio_seek(in, 4, SEEK_CUR);
                *start_ts = avio_rb64(in);
                *duration = avio_rb64(in);
                ret = 0;
                break;
            }
        }
        avio_seek(in, end, SEEK_SET);
    }
fail:
    avio_close(in);
    return ret;
}

static int add_fragment(OutputStream *os, const char *file, const char *infofile, int64_t start_time, int64_t duration, int64_t start_pos, int64_t size)
{
    Fragment *frag;
    if (os->nb_fragments >= os->fragments_size) {
        os->fragments_size = (os->fragments_size + 1) * 2;
        os->fragments = av_realloc(os->fragments, sizeof(*os->fragments)*os->fragments_size);
        if (!os->fragments)
            return AVERROR(ENOMEM);
    }
    frag = av_mallocz(sizeof(*frag));
    if (!frag)
        return AVERROR(ENOMEM);
    av_strlcpy(frag->file, file, sizeof(frag->file));
    av_strlcpy(frag->infofile, infofile, sizeof(frag->infofile));
    frag->start_time = start_time;
    frag->duration = duration;
    frag->start_pos = start_pos;
    frag->size = size;
    frag->n = os->fragment_index;
    os->fragments[os->nb_fragments++] = frag;
    os->fragment_index++;
    return 0;
}

static int copy_moof(AVFormatContext *s, const char* infile, const char *outfile, int64_t size)
{
    AVIOContext *in, *out;
    int ret = 0;
    if ((ret = avio_open2(&in, infile, AVIO_FLAG_READ, &s->interrupt_callback, NULL)) < 0)
        return ret;
    if ((ret = avio_open2(&out, outfile, AVIO_FLAG_WRITE, &s->interrupt_callback, NULL)) < 0) {
        avio_close(in);
        return ret;
    }
    while (size > 0) {
        uint8_t buf[8192];
        int n = FFMIN(size, sizeof(buf));
        n = avio_read(in, buf, n);
        if (n <= 0) {
            ret = AVERROR(EIO);
            break;
        }
        avio_write(out, buf, n);
        size -= n;
    }
    avio_flush(out);
    avio_close(out);
    avio_close(in);
    return ret;
}

static int ism_flush(AVFormatContext *s, int final)
{
    SmoothStreamingContext *c = s->priv_data;
    int i, ret = 0;

    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        char filename[1024], target_filename[1024], header_filename[1024];
        int64_t start_pos = os->tail_pos, size;
        int64_t start_ts, duration, moof_size;
        if (!os->packets_written)
            continue;

        snprintf(filename, sizeof(filename), "%s/temp", os->dirname);
        ret = ffurl_open(&os->out, filename, AVIO_FLAG_WRITE, &s->interrupt_callback, NULL);
        if (ret < 0)
            break;
        os->cur_start_pos = os->tail_pos;
        av_write_frame(os->ctx, NULL);
        avio_flush(os->ctx->pb);
        os->packets_written = 0;
        if (!os->out || os->tail_out)
            return AVERROR(EIO);

        ffurl_close(os->out);
        os->out = NULL;
        size = os->tail_pos - start_pos;
        if ((ret = parse_fragment(s, filename, &start_ts, &duration, &moof_size, size)) < 0)
            break;
        snprintf(header_filename, sizeof(header_filename), "%s/FragmentInfo(%s=%"PRIu64")", os->dirname, os->stream_type_tag, start_ts);
        snprintf(target_filename, sizeof(target_filename), "%s/Fragments(%s=%"PRIu64")", os->dirname, os->stream_type_tag, start_ts);
        copy_moof(s, filename, header_filename, moof_size);
        rename(filename, target_filename);
        add_fragment(os, target_filename, header_filename, start_ts, duration, start_pos, size);
    }

    if (c->window_size || (final && c->remove_at_exit)) {
        for (i = 0; i < s->nb_streams; i++) {
            OutputStream *os = &c->streams[i];
            int j;
            int remove = os->nb_fragments - c->window_size - c->extra_window_size - c->lookahead_count;
            if (final && c->remove_at_exit)
                remove = os->nb_fragments;
            if (remove > 0) {
                for (j = 0; j < remove; j++) {
                    unlink(os->fragments[j]->file);
                    unlink(os->fragments[j]->infofile);
                    av_free(os->fragments[j]);
                }
                os->nb_fragments -= remove;
                memmove(os->fragments, os->fragments + remove, os->nb_fragments * sizeof(*os->fragments));
            }
            if (final && c->remove_at_exit)
                rmdir(os->dirname);
        }
    }

    if (ret >= 0)
        ret = write_manifest(s, final);
    return ret;
}

static int ism_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SmoothStreamingContext *c = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    OutputStream *os = &c->streams[pkt->stream_index];
    int64_t end_dts = (c->nb_fragments + 1LL) * c->min_frag_duration;
    int ret;

    if (st->first_dts == AV_NOPTS_VALUE)
        st->first_dts = pkt->dts;

    if ((!c->has_video || st->codec->codec_type == AVMEDIA_TYPE_VIDEO) &&
        av_compare_ts(pkt->dts - st->first_dts, st->time_base,
                      end_dts, AV_TIME_BASE_Q) >= 0 &&
        pkt->flags & AV_PKT_FLAG_KEY && os->packets_written) {

        if ((ret = ism_flush(s, 0)) < 0)
            return ret;
        c->nb_fragments++;
    }

    os->packets_written++;
    return ff_write_chained(os->ctx, 0, pkt, s);
}

static int ism_write_trailer(AVFormatContext *s)
{
    SmoothStreamingContext *c = s->priv_data;
    ism_flush(s, 1);

    if (c->remove_at_exit) {
        char filename[1024];
        snprintf(filename, sizeof(filename), "%s/Manifest", s->filename);
        unlink(filename);
        rmdir(s->filename);
    }

    ism_free(s);
    return 0;
}

#define OFFSET(x) offsetof(SmoothStreamingContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "window_size", "number of fragments kept in the manifest", OFFSET(window_size), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, E },
    { "extra_window_size", "number of fragments kept outside of the manifest before removing from disk", OFFSET(extra_window_size), AV_OPT_TYPE_INT, { .i64 = 5 }, 0, INT_MAX, E },
    { "lookahead_count", "number of lookahead fragments", OFFSET(lookahead_count), AV_OPT_TYPE_INT, { .i64 = 2 }, 0, INT_MAX, E },
    { "min_frag_duration", "minimum fragment duration (in microseconds)", OFFSET(min_frag_duration), AV_OPT_TYPE_INT64, { .i64 = 5000000 }, 0, INT_MAX, E },
    { "remove_at_exit", "remove all fragments when finished", OFFSET(remove_at_exit), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, E },
    { NULL },
};

static const AVClass ism_class = {
    .class_name = "smooth streaming muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};


AVOutputFormat ff_smoothstreaming_muxer = {
    .name           = "smoothstreaming",
    .long_name      = NULL_IF_CONFIG_SMALL("Smooth Streaming Muxer"),
    .priv_data_size = sizeof(SmoothStreamingContext),
    .audio_codec    = AV_CODEC_ID_AAC,
    .video_codec    = AV_CODEC_ID_H264,
    .flags          = AVFMT_GLOBALHEADER | AVFMT_NOFILE,
    .write_header   = ism_write_header,
    .write_packet   = ism_write_packet,
    .write_trailer  = ism_write_trailer,
    .codec_tag      = (const AVCodecTag* const []){ ff_mp4_obj_type, 0 },
    .priv_class     = &ism_class,
};
