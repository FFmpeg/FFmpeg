/*
 * various utility functions for use within FFmpeg
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
#include "internal.h"
#include "libavcodec/internal.h"
#include "libavcodec/raw.h"
#include "libavcodec/bytestream.h"
#include "libavutil/opt.h"
#include "libavutil/dict.h"
#include "libavutil/internal.h"
#include "libavutil/pixdesc.h"
#include "metadata.h"
#include "id3v2.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"
#include "riff.h"
#include "audiointerleave.h"
#include "url.h"
#include <stdarg.h>
#if CONFIG_NETWORK
#include "network.h"
#endif

#undef NDEBUG
#include <assert.h>

/**
 * @file
 * various utility functions for use within FFmpeg
 */

unsigned avformat_version(void)
{
    av_assert0(LIBAVFORMAT_VERSION_MICRO >= 100);
    return LIBAVFORMAT_VERSION_INT;
}

const char *avformat_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char *avformat_license(void)
{
#define LICENSE_PREFIX "libavformat license: "
    return LICENSE_PREFIX FFMPEG_LICENSE + sizeof(LICENSE_PREFIX) - 1;
}

#define RELATIVE_TS_BASE (INT64_MAX - (1LL<<48))

static int is_relative(int64_t ts) {
    return ts > (RELATIVE_TS_BASE - (1LL<<48));
}

/**
 * Wrap a given time stamp, if there is an indication for an overflow
 *
 * @param st stream
 * @param timestamp the time stamp to wrap
 * @return resulting time stamp
 */
static int64_t wrap_timestamp(AVStream *st, int64_t timestamp)
{
    if (st->pts_wrap_behavior != AV_PTS_WRAP_IGNORE &&
        st->pts_wrap_reference != AV_NOPTS_VALUE && timestamp != AV_NOPTS_VALUE) {
        if (st->pts_wrap_behavior == AV_PTS_WRAP_ADD_OFFSET &&
            timestamp < st->pts_wrap_reference)
            return timestamp + (1ULL<<st->pts_wrap_bits);
        else if (st->pts_wrap_behavior == AV_PTS_WRAP_SUB_OFFSET &&
            timestamp >= st->pts_wrap_reference)
            return timestamp - (1ULL<<st->pts_wrap_bits);
    }
    return timestamp;
}

MAKE_ACCESSORS(AVStream, stream, AVRational, r_frame_rate)
MAKE_ACCESSORS(AVFormatContext, format, AVCodec *, video_codec)
MAKE_ACCESSORS(AVFormatContext, format, AVCodec *, audio_codec)
MAKE_ACCESSORS(AVFormatContext, format, AVCodec *, subtitle_codec)

static AVCodec *find_decoder(AVFormatContext *s, AVStream *st, enum AVCodecID codec_id)
{
    if (st->codec->codec)
        return st->codec->codec;

    switch(st->codec->codec_type){
    case AVMEDIA_TYPE_VIDEO:
        if(s->video_codec)    return s->video_codec;
        break;
    case AVMEDIA_TYPE_AUDIO:
        if(s->audio_codec)    return s->audio_codec;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        if(s->subtitle_codec) return s->subtitle_codec;
        break;
    }

    return avcodec_find_decoder(codec_id);
}

int av_format_get_probe_score(const AVFormatContext *s)
{
    return s->probe_score;
}

/* an arbitrarily chosen "sane" max packet size -- 50M */
#define SANE_CHUNK_SIZE (50000000)

int ffio_limit(AVIOContext *s, int size)
{
    if(s->maxsize>=0){
        int64_t remaining= s->maxsize - avio_tell(s);
        if(remaining < size){
            int64_t newsize= avio_size(s);
            if(!s->maxsize || s->maxsize<newsize)
                s->maxsize= newsize - !newsize;
            remaining= s->maxsize - avio_tell(s);
            remaining= FFMAX(remaining, 0);
        }

        if(s->maxsize>=0 && remaining+1 < size){
            av_log(NULL, remaining ? AV_LOG_ERROR : AV_LOG_DEBUG, "Truncating packet of size %d to %"PRId64"\n", size, remaining+1);
            size= remaining+1;
        }
    }
    return size;
}

/*
 * Read the data in sane-sized chunks and append to pkt.
 * Return the number of bytes read or an error.
 */
static int append_packet_chunked(AVIOContext *s, AVPacket *pkt, int size)
{
    int64_t orig_pos   = pkt->pos; // av_grow_packet might reset pos
    int orig_size      = pkt->size;
    int ret;

    do {
        int prev_size = pkt->size;
        int read_size;

        /*
         * When the caller requests a lot of data, limit it to the amount left
         * in file or SANE_CHUNK_SIZE when it is not known
         */
        read_size = size;
        if (read_size > SANE_CHUNK_SIZE/10) {
            read_size = ffio_limit(s, read_size);
            // If filesize/maxsize is unknown, limit to SANE_CHUNK_SIZE
            if (s->maxsize < 0)
                read_size = FFMIN(read_size, SANE_CHUNK_SIZE);
        }

        ret = av_grow_packet(pkt, read_size);
        if (ret < 0)
            break;

        ret = avio_read(s, pkt->data + prev_size, read_size);
        if (ret != read_size) {
            av_shrink_packet(pkt, prev_size + FFMAX(ret, 0));
            break;
        }

        size -= read_size;
    } while (size > 0);
    if (size > 0)
        pkt->flags |= AV_PKT_FLAG_CORRUPT;

    pkt->pos = orig_pos;
    if (!pkt->size)
        av_free_packet(pkt);
    return pkt->size > orig_size ? pkt->size - orig_size : ret;
}

int av_get_packet(AVIOContext *s, AVPacket *pkt, int size)
{
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->pos  = avio_tell(s);

    return append_packet_chunked(s, pkt, size);
}

int av_append_packet(AVIOContext *s, AVPacket *pkt, int size)
{
    if (!pkt->size)
        return av_get_packet(s, pkt, size);
    return append_packet_chunked(s, pkt, size);
}


int av_filename_number_test(const char *filename)
{
    char buf[1024];
    return filename && (av_get_frame_filename(buf, sizeof(buf), filename, 1)>=0);
}

AVInputFormat *av_probe_input_format3(AVProbeData *pd, int is_opened, int *score_ret)
{
    AVProbeData lpd = *pd;
    AVInputFormat *fmt1 = NULL, *fmt;
    int score, nodat = 0, score_max=0;
    const static uint8_t zerobuffer[AVPROBE_PADDING_SIZE];

    if (!lpd.buf)
        lpd.buf = zerobuffer;

    if (lpd.buf_size > 10 && ff_id3v2_match(lpd.buf, ID3v2_DEFAULT_MAGIC)) {
        int id3len = ff_id3v2_tag_len(lpd.buf);
        if (lpd.buf_size > id3len + 16) {
            lpd.buf += id3len;
            lpd.buf_size -= id3len;
        }else
            nodat = 1;
    }

    fmt = NULL;
    while ((fmt1 = av_iformat_next(fmt1))) {
        if (!is_opened == !(fmt1->flags & AVFMT_NOFILE))
            continue;
        score = 0;
        if (fmt1->read_probe) {
            score = fmt1->read_probe(&lpd);
            if(fmt1->extensions && av_match_ext(lpd.filename, fmt1->extensions))
                score = FFMAX(score, nodat ? AVPROBE_SCORE_EXTENSION / 2 - 1 : 1);
        } else if (fmt1->extensions) {
            if (av_match_ext(lpd.filename, fmt1->extensions)) {
                score = AVPROBE_SCORE_EXTENSION;
            }
        }
        if (score > score_max) {
            score_max = score;
            fmt = fmt1;
        }else if (score == score_max)
            fmt = NULL;
    }
    if(nodat)
        score_max = FFMIN(AVPROBE_SCORE_EXTENSION / 2 - 1, score_max);
    *score_ret= score_max;

    return fmt;
}

AVInputFormat *av_probe_input_format2(AVProbeData *pd, int is_opened, int *score_max)
{
    int score_ret;
    AVInputFormat *fmt= av_probe_input_format3(pd, is_opened, &score_ret);
    if(score_ret > *score_max){
        *score_max= score_ret;
        return fmt;
    }else
        return NULL;
}

AVInputFormat *av_probe_input_format(AVProbeData *pd, int is_opened){
    int score=0;
    return av_probe_input_format2(pd, is_opened, &score);
}

static int set_codec_from_probe_data(AVFormatContext *s, AVStream *st, AVProbeData *pd)
{
    static const struct {
        const char *name; enum AVCodecID id; enum AVMediaType type;
    } fmt_id_type[] = {
        { "aac"      , AV_CODEC_ID_AAC       , AVMEDIA_TYPE_AUDIO },
        { "ac3"      , AV_CODEC_ID_AC3       , AVMEDIA_TYPE_AUDIO },
        { "dts"      , AV_CODEC_ID_DTS       , AVMEDIA_TYPE_AUDIO },
        { "eac3"     , AV_CODEC_ID_EAC3      , AVMEDIA_TYPE_AUDIO },
        { "h264"     , AV_CODEC_ID_H264      , AVMEDIA_TYPE_VIDEO },
        { "loas"     , AV_CODEC_ID_AAC_LATM  , AVMEDIA_TYPE_AUDIO },
        { "m4v"      , AV_CODEC_ID_MPEG4     , AVMEDIA_TYPE_VIDEO },
        { "mp3"      , AV_CODEC_ID_MP3       , AVMEDIA_TYPE_AUDIO },
        { "mpegvideo", AV_CODEC_ID_MPEG2VIDEO, AVMEDIA_TYPE_VIDEO },
        { 0 }
    };
    int score;
    AVInputFormat *fmt = av_probe_input_format3(pd, 1, &score);

    if (fmt && st->request_probe <= score) {
        int i;
        av_log(s, AV_LOG_DEBUG, "Probe with size=%d, packets=%d detected %s with score=%d\n",
               pd->buf_size, MAX_PROBE_PACKETS - st->probe_packets, fmt->name, score);
        for (i = 0; fmt_id_type[i].name; i++) {
            if (!strcmp(fmt->name, fmt_id_type[i].name)) {
                st->codec->codec_id   = fmt_id_type[i].id;
                st->codec->codec_type = fmt_id_type[i].type;
                break;
            }
        }
    }
    return score;
}

/************************************************************/
/* input media file */

int av_demuxer_open(AVFormatContext *ic){
    int err;

    if (ic->iformat->read_header) {
        err = ic->iformat->read_header(ic);
        if (err < 0)
            return err;
    }

    if (ic->pb && !ic->data_offset)
        ic->data_offset = avio_tell(ic->pb);

    return 0;
}


int av_probe_input_buffer2(AVIOContext *pb, AVInputFormat **fmt,
                          const char *filename, void *logctx,
                          unsigned int offset, unsigned int max_probe_size)
{
    AVProbeData pd = { filename ? filename : "", NULL, -offset };
    unsigned char *buf = NULL;
    uint8_t *mime_type;
    int ret = 0, probe_size, buf_offset = 0;
    int score = 0;

    if (!max_probe_size) {
        max_probe_size = PROBE_BUF_MAX;
    } else if (max_probe_size > PROBE_BUF_MAX) {
        max_probe_size = PROBE_BUF_MAX;
    } else if (max_probe_size < PROBE_BUF_MIN) {
        av_log(logctx, AV_LOG_ERROR,
               "Specified probe size value %u cannot be < %u\n", max_probe_size, PROBE_BUF_MIN);
        return AVERROR(EINVAL);
    }

    if (offset >= max_probe_size) {
        return AVERROR(EINVAL);
    }

    if (!*fmt && pb->av_class && av_opt_get(pb, "mime_type", AV_OPT_SEARCH_CHILDREN, &mime_type) >= 0 && mime_type) {
        if (!av_strcasecmp(mime_type, "audio/aacp")) {
            *fmt = av_find_input_format("aac");
        }
        av_freep(&mime_type);
    }

    for(probe_size= PROBE_BUF_MIN; probe_size<=max_probe_size && !*fmt;
        probe_size = FFMIN(probe_size<<1, FFMAX(max_probe_size, probe_size+1))) {

        if (probe_size < offset) {
            continue;
        }
        score = probe_size < max_probe_size ? AVPROBE_SCORE_RETRY : 0;

        /* read probe data */
        if ((ret = av_reallocp(&buf, probe_size + AVPROBE_PADDING_SIZE)) < 0)
            return ret;
        if ((ret = avio_read(pb, buf + buf_offset, probe_size - buf_offset)) < 0) {
            /* fail if error was not end of file, otherwise, lower score */
            if (ret != AVERROR_EOF) {
                av_free(buf);
                return ret;
            }
            score = 0;
            ret = 0;            /* error was end of file, nothing read */
        }
        pd.buf_size = buf_offset += ret;
        pd.buf = &buf[offset];

        memset(pd.buf + pd.buf_size, 0, AVPROBE_PADDING_SIZE);

        /* guess file format */
        *fmt = av_probe_input_format2(&pd, 1, &score);
        if(*fmt){
            if(score <= AVPROBE_SCORE_RETRY){ //this can only be true in the last iteration
                av_log(logctx, AV_LOG_WARNING, "Format %s detected only with low score of %d, misdetection possible!\n", (*fmt)->name, score);
            }else
                av_log(logctx, AV_LOG_DEBUG, "Format %s probed with size=%d and score=%d\n", (*fmt)->name, probe_size, score);
        }
    }

    if (!*fmt) {
        av_free(buf);
        return AVERROR_INVALIDDATA;
    }

    /* rewind. reuse probe buffer to avoid seeking */
    ret = ffio_rewind_with_probe_data(pb, &buf, pd.buf_size);

    return ret < 0 ? ret : score;
}

int av_probe_input_buffer(AVIOContext *pb, AVInputFormat **fmt,
                          const char *filename, void *logctx,
                          unsigned int offset, unsigned int max_probe_size)
{
    int ret = av_probe_input_buffer2(pb, fmt, filename, logctx, offset, max_probe_size);
    return ret < 0 ? ret : 0;
}


/* open input file and probe the format if necessary */
static int init_input(AVFormatContext *s, const char *filename, AVDictionary **options)
{
    int ret;
    AVProbeData pd = {filename, NULL, 0};
    int score = AVPROBE_SCORE_RETRY;

    if (s->pb) {
        s->flags |= AVFMT_FLAG_CUSTOM_IO;
        if (!s->iformat)
            return av_probe_input_buffer2(s->pb, &s->iformat, filename, s, 0, s->probesize);
        else if (s->iformat->flags & AVFMT_NOFILE)
            av_log(s, AV_LOG_WARNING, "Custom AVIOContext makes no sense and "
                                      "will be ignored with AVFMT_NOFILE format.\n");
        return 0;
    }

    if ( (s->iformat && s->iformat->flags & AVFMT_NOFILE) ||
        (!s->iformat && (s->iformat = av_probe_input_format2(&pd, 0, &score))))
        return score;

    if ((ret = avio_open2(&s->pb, filename, AVIO_FLAG_READ | s->avio_flags,
                          &s->interrupt_callback, options)) < 0)
        return ret;
    if (s->iformat)
        return 0;
    return av_probe_input_buffer2(s->pb, &s->iformat, filename, s, 0, s->probesize);
}

static AVPacket *add_to_pktbuf(AVPacketList **packet_buffer, AVPacket *pkt,
                               AVPacketList **plast_pktl){
    AVPacketList *pktl = av_mallocz(sizeof(AVPacketList));
    if (!pktl)
        return NULL;

    if (*packet_buffer)
        (*plast_pktl)->next = pktl;
    else
        *packet_buffer = pktl;

    /* add the packet in the buffered packet list */
    *plast_pktl = pktl;
    pktl->pkt= *pkt;
    return &pktl->pkt;
}

int avformat_queue_attached_pictures(AVFormatContext *s)
{
    int i;
    for (i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC &&
            s->streams[i]->discard < AVDISCARD_ALL) {
            AVPacket copy = s->streams[i]->attached_pic;
            copy.buf      = av_buffer_ref(copy.buf);
            if (!copy.buf)
                return AVERROR(ENOMEM);

            add_to_pktbuf(&s->raw_packet_buffer, &copy, &s->raw_packet_buffer_end);
        }
    return 0;
}

int avformat_open_input(AVFormatContext **ps, const char *filename, AVInputFormat *fmt, AVDictionary **options)
{
    AVFormatContext *s = *ps;
    int ret = 0;
    AVDictionary *tmp = NULL;
    ID3v2ExtraMeta *id3v2_extra_meta = NULL;

    if (!s && !(s = avformat_alloc_context()))
        return AVERROR(ENOMEM);
    if (!s->av_class){
        av_log(NULL, AV_LOG_ERROR, "Input context has not been properly allocated by avformat_alloc_context() and is not NULL either\n");
        return AVERROR(EINVAL);
    }
    if (fmt)
        s->iformat = fmt;

    if (options)
        av_dict_copy(&tmp, *options, 0);

    if ((ret = av_opt_set_dict(s, &tmp)) < 0)
        goto fail;

    if ((ret = init_input(s, filename, &tmp)) < 0)
        goto fail;
    s->probe_score = ret;
    avio_skip(s->pb, s->skip_initial_bytes);

    /* check filename in case an image number is expected */
    if (s->iformat->flags & AVFMT_NEEDNUMBER) {
        if (!av_filename_number_test(filename)) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
    }

    s->duration = s->start_time = AV_NOPTS_VALUE;
    av_strlcpy(s->filename, filename ? filename : "", sizeof(s->filename));

    /* allocate private data */
    if (s->iformat->priv_data_size > 0) {
        if (!(s->priv_data = av_mallocz(s->iformat->priv_data_size))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if (s->iformat->priv_class) {
            *(const AVClass**)s->priv_data = s->iformat->priv_class;
            av_opt_set_defaults(s->priv_data);
            if ((ret = av_opt_set_dict(s->priv_data, &tmp)) < 0)
                goto fail;
        }
    }

    /* e.g. AVFMT_NOFILE formats will not have a AVIOContext */
    if (s->pb)
        ff_id3v2_read(s, ID3v2_DEFAULT_MAGIC, &id3v2_extra_meta);

    if (!(s->flags&AVFMT_FLAG_PRIV_OPT) && s->iformat->read_header)
        if ((ret = s->iformat->read_header(s)) < 0)
            goto fail;

    if (id3v2_extra_meta) {
        if (!strcmp(s->iformat->name, "mp3") || !strcmp(s->iformat->name, "aac") ||
            !strcmp(s->iformat->name, "tta")) {
            if((ret = ff_id3v2_parse_apic(s, &id3v2_extra_meta)) < 0)
                goto fail;
        } else
            av_log(s, AV_LOG_DEBUG, "demuxer does not support additional id3 data, skipping\n");
    }
    ff_id3v2_free_extra_meta(&id3v2_extra_meta);

    if ((ret = avformat_queue_attached_pictures(s)) < 0)
        goto fail;

    if (!(s->flags&AVFMT_FLAG_PRIV_OPT) && s->pb && !s->data_offset)
        s->data_offset = avio_tell(s->pb);

    s->raw_packet_buffer_remaining_size = RAW_PACKET_BUFFER_SIZE;

    if (options) {
        av_dict_free(options);
        *options = tmp;
    }
    *ps = s;
    return 0;

fail:
    ff_id3v2_free_extra_meta(&id3v2_extra_meta);
    av_dict_free(&tmp);
    if (s->pb && !(s->flags & AVFMT_FLAG_CUSTOM_IO))
        avio_close(s->pb);
    avformat_free_context(s);
    *ps = NULL;
    return ret;
}

/*******************************************************/

static void force_codec_ids(AVFormatContext *s, AVStream *st)
{
    switch(st->codec->codec_type){
    case AVMEDIA_TYPE_VIDEO:
        if(s->video_codec_id)   st->codec->codec_id= s->video_codec_id;
        break;
    case AVMEDIA_TYPE_AUDIO:
        if(s->audio_codec_id)   st->codec->codec_id= s->audio_codec_id;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        if(s->subtitle_codec_id)st->codec->codec_id= s->subtitle_codec_id;
        break;
    }
}

static int probe_codec(AVFormatContext *s, AVStream *st, const AVPacket *pkt)
{
    if(st->request_probe>0){
        AVProbeData *pd = &st->probe_data;
        int end;
        av_log(s, AV_LOG_DEBUG, "probing stream %d pp:%d\n", st->index, st->probe_packets);
        --st->probe_packets;

        if (pkt) {
            uint8_t *new_buf = av_realloc(pd->buf, pd->buf_size+pkt->size+AVPROBE_PADDING_SIZE);
            if(!new_buf) {
                av_log(s, AV_LOG_WARNING,
                       "Failed to reallocate probe buffer for stream %d\n",
                       st->index);
                goto no_packet;
            }
            pd->buf = new_buf;
            memcpy(pd->buf+pd->buf_size, pkt->data, pkt->size);
            pd->buf_size += pkt->size;
            memset(pd->buf+pd->buf_size, 0, AVPROBE_PADDING_SIZE);
        } else {
no_packet:
            st->probe_packets = 0;
            if (!pd->buf_size) {
                av_log(s, AV_LOG_WARNING, "nothing to probe for stream %d\n",
                       st->index);
            }
        }

        end=    s->raw_packet_buffer_remaining_size <= 0
                || st->probe_packets<=0;

        if(end || av_log2(pd->buf_size) != av_log2(pd->buf_size - pkt->size)){
            int score= set_codec_from_probe_data(s, st, pd);
            if(    (st->codec->codec_id != AV_CODEC_ID_NONE && score > AVPROBE_SCORE_RETRY)
                || end){
                pd->buf_size=0;
                av_freep(&pd->buf);
                st->request_probe= -1;
                if(st->codec->codec_id != AV_CODEC_ID_NONE){
                    av_log(s, AV_LOG_DEBUG, "probed stream %d\n", st->index);
                }else
                    av_log(s, AV_LOG_WARNING, "probed stream %d failed\n", st->index);
            }
            force_codec_ids(s, st);
        }
    }
    return 0;
}

int ff_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, i, err;
    AVStream *st;

    for(;;){
        AVPacketList *pktl = s->raw_packet_buffer;

        if (pktl) {
            *pkt = pktl->pkt;
            st = s->streams[pkt->stream_index];
            if (s->raw_packet_buffer_remaining_size <= 0) {
                if ((err = probe_codec(s, st, NULL)) < 0)
                    return err;
            }
            if(st->request_probe <= 0){
                s->raw_packet_buffer = pktl->next;
                s->raw_packet_buffer_remaining_size += pkt->size;
                av_free(pktl);
                return 0;
            }
        }

        pkt->data = NULL;
        pkt->size = 0;
        av_init_packet(pkt);
        ret= s->iformat->read_packet(s, pkt);
        if (ret < 0) {
            if (!pktl || ret == AVERROR(EAGAIN))
                return ret;
            for (i = 0; i < s->nb_streams; i++) {
                st = s->streams[i];
                if (st->probe_packets) {
                    if ((err = probe_codec(s, st, NULL)) < 0)
                        return err;
                }
                av_assert0(st->request_probe <= 0);
            }
            continue;
        }

        if ((s->flags & AVFMT_FLAG_DISCARD_CORRUPT) &&
            (pkt->flags & AV_PKT_FLAG_CORRUPT)) {
            av_log(s, AV_LOG_WARNING,
                   "Dropped corrupted packet (stream = %d)\n",
                   pkt->stream_index);
            av_free_packet(pkt);
            continue;
        }

        if(!(s->flags & AVFMT_FLAG_KEEP_SIDE_DATA))
            av_packet_merge_side_data(pkt);

        if(pkt->stream_index >= (unsigned)s->nb_streams){
            av_log(s, AV_LOG_ERROR, "Invalid stream index %d\n", pkt->stream_index);
            continue;
        }

        st= s->streams[pkt->stream_index];
        pkt->dts = wrap_timestamp(st, pkt->dts);
        pkt->pts = wrap_timestamp(st, pkt->pts);

        force_codec_ids(s, st);

        /* TODO: audio: time filter; video: frame reordering (pts != dts) */
        if (s->use_wallclock_as_timestamps)
            pkt->dts = pkt->pts = av_rescale_q(av_gettime(), AV_TIME_BASE_Q, st->time_base);

        if(!pktl && st->request_probe <= 0)
            return ret;

        add_to_pktbuf(&s->raw_packet_buffer, pkt, &s->raw_packet_buffer_end);
        s->raw_packet_buffer_remaining_size -= pkt->size;

        if ((err = probe_codec(s, st, pkt)) < 0)
            return err;
    }
}

#if FF_API_READ_PACKET
int av_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    return ff_read_packet(s, pkt);
}
#endif


/**********************************************************/

static int determinable_frame_size(AVCodecContext *avctx)
{
    if (/*avctx->codec_id == AV_CODEC_ID_AAC ||*/
        avctx->codec_id == AV_CODEC_ID_MP1 ||
        avctx->codec_id == AV_CODEC_ID_MP2 ||
        avctx->codec_id == AV_CODEC_ID_MP3/* ||
        avctx->codec_id == AV_CODEC_ID_CELT*/)
        return 1;
    return 0;
}

/**
 * Get the number of samples of an audio frame. Return -1 on error.
 */
int ff_get_audio_frame_size(AVCodecContext *enc, int size, int mux)
{
    int frame_size;

    /* give frame_size priority if demuxing */
    if (!mux && enc->frame_size > 1)
        return enc->frame_size;

    if ((frame_size = av_get_audio_frame_duration(enc, size)) > 0)
        return frame_size;

    /* Fall back on using frame_size if muxing. */
    if (enc->frame_size > 1)
        return enc->frame_size;

    //For WMA we currently have no other means to calculate duration thus we
    //do it here by assuming CBR, which is true for all known cases.
    if(!mux && enc->bit_rate>0 && size>0 && enc->sample_rate>0 && enc->block_align>1) {
        if (enc->codec_id == AV_CODEC_ID_WMAV1 || enc->codec_id == AV_CODEC_ID_WMAV2)
            return  ((int64_t)size * 8 * enc->sample_rate) / enc->bit_rate;
    }

    return -1;
}


/**
 * Return the frame duration in seconds. Return 0 if not available.
 */
void ff_compute_frame_duration(int *pnum, int *pden, AVStream *st,
                               AVCodecParserContext *pc, AVPacket *pkt)
{
    int frame_size;

    *pnum = 0;
    *pden = 0;
    switch(st->codec->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (st->r_frame_rate.num && !pc) {
            *pnum = st->r_frame_rate.den;
            *pden = st->r_frame_rate.num;
        } else if(st->time_base.num*1000LL > st->time_base.den) {
            *pnum = st->time_base.num;
            *pden = st->time_base.den;
        }else if(st->codec->time_base.num*1000LL > st->codec->time_base.den){
            *pnum = st->codec->time_base.num;
            *pden = st->codec->time_base.den;
            if (pc && pc->repeat_pict) {
                if (*pnum > INT_MAX / (1 + pc->repeat_pict))
                    *pden /= 1 + pc->repeat_pict;
                else
                    *pnum *= 1 + pc->repeat_pict;
            }
            //If this codec can be interlaced or progressive then we need a parser to compute duration of a packet
            //Thus if we have no parser in such case leave duration undefined.
            if(st->codec->ticks_per_frame>1 && !pc){
                *pnum = *pden = 0;
            }
        }
        break;
    case AVMEDIA_TYPE_AUDIO:
        frame_size = ff_get_audio_frame_size(st->codec, pkt->size, 0);
        if (frame_size <= 0 || st->codec->sample_rate <= 0)
            break;
        *pnum = frame_size;
        *pden = st->codec->sample_rate;
        break;
    default:
        break;
    }
}

static int is_intra_only(AVCodecContext *enc){
    const AVCodecDescriptor *desc;

    if(enc->codec_type != AVMEDIA_TYPE_VIDEO)
        return 1;

    desc = av_codec_get_codec_descriptor(enc);
    if (!desc) {
        desc = avcodec_descriptor_get(enc->codec_id);
        av_codec_set_codec_descriptor(enc, desc);
    }
    if (desc)
        return !!(desc->props & AV_CODEC_PROP_INTRA_ONLY);
    return 0;
}

static int has_decode_delay_been_guessed(AVStream *st)
{
    if(st->codec->codec_id != AV_CODEC_ID_H264) return 1;
    if(!st->info) // if we have left find_stream_info then nb_decoded_frames won't increase anymore for stream copy
        return 1;
#if CONFIG_H264_DECODER
    if(st->codec->has_b_frames &&
       avpriv_h264_has_num_reorder_frames(st->codec) == st->codec->has_b_frames)
        return 1;
#endif
    if(st->codec->has_b_frames<3)
        return st->nb_decoded_frames >= 7;
    else if(st->codec->has_b_frames<4)
        return st->nb_decoded_frames >= 18;
    else
        return st->nb_decoded_frames >= 20;
}

static AVPacketList *get_next_pkt(AVFormatContext *s, AVStream *st, AVPacketList *pktl)
{
    if (pktl->next)
        return pktl->next;
    if (pktl == s->parse_queue_end)
        return s->packet_buffer;
    return NULL;
}

static int update_wrap_reference(AVFormatContext *s, AVStream *st, int stream_index)
{
    if (s->correct_ts_overflow && st->pts_wrap_bits < 63 &&
        st->pts_wrap_reference == AV_NOPTS_VALUE && st->first_dts != AV_NOPTS_VALUE) {
        int i;

        // reference time stamp should be 60 s before first time stamp
        int64_t pts_wrap_reference = st->first_dts - av_rescale(60, st->time_base.den, st->time_base.num);
        // if first time stamp is not more than 1/8 and 60s before the wrap point, subtract rather than add wrap offset
        int pts_wrap_behavior = (st->first_dts < (1LL<<st->pts_wrap_bits) - (1LL<<st->pts_wrap_bits-3)) ||
            (st->first_dts < (1LL<<st->pts_wrap_bits) - av_rescale(60, st->time_base.den, st->time_base.num)) ?
            AV_PTS_WRAP_ADD_OFFSET : AV_PTS_WRAP_SUB_OFFSET;

        AVProgram *first_program = av_find_program_from_stream(s, NULL, stream_index);

        if (!first_program) {
            int default_stream_index = av_find_default_stream_index(s);
            if (s->streams[default_stream_index]->pts_wrap_reference == AV_NOPTS_VALUE) {
                for (i=0; i<s->nb_streams; i++) {
                    s->streams[i]->pts_wrap_reference = pts_wrap_reference;
                    s->streams[i]->pts_wrap_behavior = pts_wrap_behavior;
                }
            }
            else {
                st->pts_wrap_reference = s->streams[default_stream_index]->pts_wrap_reference;
                st->pts_wrap_behavior = s->streams[default_stream_index]->pts_wrap_behavior;
            }
        }
        else {
            AVProgram *program = first_program;
            while (program) {
                if (program->pts_wrap_reference != AV_NOPTS_VALUE) {
                    pts_wrap_reference = program->pts_wrap_reference;
                    pts_wrap_behavior = program->pts_wrap_behavior;
                    break;
                }
                program = av_find_program_from_stream(s, program, stream_index);
            }

            // update every program with differing pts_wrap_reference
            program = first_program;
            while(program) {
                if (program->pts_wrap_reference != pts_wrap_reference) {
                    for (i=0; i<program->nb_stream_indexes; i++) {
                        s->streams[program->stream_index[i]]->pts_wrap_reference = pts_wrap_reference;
                        s->streams[program->stream_index[i]]->pts_wrap_behavior = pts_wrap_behavior;
                    }

                    program->pts_wrap_reference = pts_wrap_reference;
                    program->pts_wrap_behavior = pts_wrap_behavior;
                }
                program = av_find_program_from_stream(s, program, stream_index);
            }
        }
        return 1;
    }
    return 0;
}

static void update_initial_timestamps(AVFormatContext *s, int stream_index,
                                      int64_t dts, int64_t pts, AVPacket *pkt)
{
    AVStream *st= s->streams[stream_index];
    AVPacketList *pktl= s->parse_queue ? s->parse_queue : s->packet_buffer;
    int64_t pts_buffer[MAX_REORDER_DELAY+1];
    int64_t shift;
    int i, delay;

    if(st->first_dts != AV_NOPTS_VALUE || dts == AV_NOPTS_VALUE || st->cur_dts == AV_NOPTS_VALUE || is_relative(dts))
        return;

    delay = st->codec->has_b_frames;
    st->first_dts= dts - (st->cur_dts - RELATIVE_TS_BASE);
    st->cur_dts= dts;
    shift = st->first_dts - RELATIVE_TS_BASE;

    for (i=0; i<MAX_REORDER_DELAY+1; i++)
        pts_buffer[i] = AV_NOPTS_VALUE;

    if (is_relative(pts))
        pts += shift;

    for(; pktl; pktl= get_next_pkt(s, st, pktl)){
        if(pktl->pkt.stream_index != stream_index)
            continue;
        if(is_relative(pktl->pkt.pts))
            pktl->pkt.pts += shift;

        if(is_relative(pktl->pkt.dts))
            pktl->pkt.dts += shift;

        if(st->start_time == AV_NOPTS_VALUE && pktl->pkt.pts != AV_NOPTS_VALUE)
            st->start_time= pktl->pkt.pts;

        if(pktl->pkt.pts != AV_NOPTS_VALUE && delay <= MAX_REORDER_DELAY && has_decode_delay_been_guessed(st)){
            pts_buffer[0]= pktl->pkt.pts;
            for(i=0; i<delay && pts_buffer[i] > pts_buffer[i+1]; i++)
                FFSWAP(int64_t, pts_buffer[i], pts_buffer[i+1]);
            if(pktl->pkt.dts == AV_NOPTS_VALUE)
                pktl->pkt.dts= pts_buffer[0];
        }
    }

    if (update_wrap_reference(s, st, stream_index) && st->pts_wrap_behavior == AV_PTS_WRAP_SUB_OFFSET) {
        // correct first time stamps to negative values
        st->first_dts = wrap_timestamp(st, st->first_dts);
        st->cur_dts = wrap_timestamp(st, st->cur_dts);
        pkt->dts = wrap_timestamp(st, pkt->dts);
        pkt->pts = wrap_timestamp(st, pkt->pts);
        pts = wrap_timestamp(st, pts);
    }

    if (st->start_time == AV_NOPTS_VALUE)
        st->start_time = pts;
}

static void update_initial_durations(AVFormatContext *s, AVStream *st,
                                     int stream_index, int duration)
{
    AVPacketList *pktl= s->parse_queue ? s->parse_queue : s->packet_buffer;
    int64_t cur_dts= RELATIVE_TS_BASE;

    if(st->first_dts != AV_NOPTS_VALUE){
        cur_dts= st->first_dts;
        for(; pktl; pktl= get_next_pkt(s, st, pktl)){
            if(pktl->pkt.stream_index == stream_index){
                if(pktl->pkt.pts != pktl->pkt.dts || pktl->pkt.dts != AV_NOPTS_VALUE || pktl->pkt.duration)
                    break;
                cur_dts -= duration;
            }
        }
        if(pktl && pktl->pkt.dts != st->first_dts) {
            av_log(s, AV_LOG_DEBUG, "first_dts %s not matching first dts %s (pts %s, duration %d) in the queue\n",
                   av_ts2str(st->first_dts), av_ts2str(pktl->pkt.dts), av_ts2str(pktl->pkt.pts), pktl->pkt.duration);
            return;
        }
        if(!pktl) {
            av_log(s, AV_LOG_DEBUG, "first_dts %s but no packet with dts in the queue\n", av_ts2str(st->first_dts));
            return;
        }
        pktl= s->parse_queue ? s->parse_queue : s->packet_buffer;
        st->first_dts = cur_dts;
    }else if(st->cur_dts != RELATIVE_TS_BASE)
        return;

    for(; pktl; pktl= get_next_pkt(s, st, pktl)){
        if(pktl->pkt.stream_index != stream_index)
            continue;
        if(pktl->pkt.pts == pktl->pkt.dts && (pktl->pkt.dts == AV_NOPTS_VALUE || pktl->pkt.dts == st->first_dts)
           && !pktl->pkt.duration){
            pktl->pkt.dts= cur_dts;
            if(!st->codec->has_b_frames)
                pktl->pkt.pts= cur_dts;
//            if (st->codec->codec_type != AVMEDIA_TYPE_AUDIO)
                pktl->pkt.duration = duration;
        }else
            break;
        cur_dts = pktl->pkt.dts + pktl->pkt.duration;
    }
    if(!pktl)
        st->cur_dts= cur_dts;
}

static void compute_pkt_fields(AVFormatContext *s, AVStream *st,
                               AVCodecParserContext *pc, AVPacket *pkt)
{
    int num, den, presentation_delayed, delay, i;
    int64_t offset;

    if (s->flags & AVFMT_FLAG_NOFILLIN)
        return;

    if((s->flags & AVFMT_FLAG_IGNDTS) && pkt->pts != AV_NOPTS_VALUE)
        pkt->dts= AV_NOPTS_VALUE;

    if (pc && pc->pict_type == AV_PICTURE_TYPE_B
        && !st->codec->has_b_frames)
        //FIXME Set low_delay = 0 when has_b_frames = 1
        st->codec->has_b_frames = 1;

    /* do we have a video B-frame ? */
    delay= st->codec->has_b_frames;
    presentation_delayed = 0;

    /* XXX: need has_b_frame, but cannot get it if the codec is
        not initialized */
    if (delay &&
        pc && pc->pict_type != AV_PICTURE_TYPE_B)
        presentation_delayed = 1;

    if (pkt->pts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE &&
        st->pts_wrap_bits < 63 &&
        pkt->dts - (1LL << (st->pts_wrap_bits - 1)) > pkt->pts) {
        if(is_relative(st->cur_dts) || pkt->dts - (1LL<<(st->pts_wrap_bits-1)) > st->cur_dts) {
            pkt->dts -= 1LL<<st->pts_wrap_bits;
        } else
            pkt->pts += 1LL<<st->pts_wrap_bits;
    }

    // some mpeg2 in mpeg-ps lack dts (issue171 / input_file.mpg)
    // we take the conservative approach and discard both
    // Note, if this is misbehaving for a H.264 file then possibly presentation_delayed is not set correctly.
    if(delay==1 && pkt->dts == pkt->pts && pkt->dts != AV_NOPTS_VALUE && presentation_delayed){
        av_log(s, AV_LOG_DEBUG, "invalid dts/pts combination %"PRIi64"\n", pkt->dts);
        if(strcmp(s->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2")) // otherwise we discard correct timestamps for vc1-wmapro.ism
            pkt->dts= AV_NOPTS_VALUE;
    }

    if (pkt->duration == 0) {
        ff_compute_frame_duration(&num, &den, st, pc, pkt);
        if (den && num) {
            pkt->duration = av_rescale_rnd(1, num * (int64_t)st->time_base.den, den * (int64_t)st->time_base.num, AV_ROUND_DOWN);
        }
    }
    if(pkt->duration != 0 && (s->packet_buffer || s->parse_queue))
        update_initial_durations(s, st, pkt->stream_index, pkt->duration);

    /* correct timestamps with byte offset if demuxers only have timestamps
       on packet boundaries */
    if(pc && st->need_parsing == AVSTREAM_PARSE_TIMESTAMPS && pkt->size){
        /* this will estimate bitrate based on this frame's duration and size */
        offset = av_rescale(pc->offset, pkt->duration, pkt->size);
        if(pkt->pts != AV_NOPTS_VALUE)
            pkt->pts += offset;
        if(pkt->dts != AV_NOPTS_VALUE)
            pkt->dts += offset;
    }

    if (pc && pc->dts_sync_point >= 0) {
        // we have synchronization info from the parser
        int64_t den = st->codec->time_base.den * (int64_t) st->time_base.num;
        if (den > 0) {
            int64_t num = st->codec->time_base.num * (int64_t) st->time_base.den;
            if (pkt->dts != AV_NOPTS_VALUE) {
                // got DTS from the stream, update reference timestamp
                st->reference_dts = pkt->dts - pc->dts_ref_dts_delta * num / den;
            } else if (st->reference_dts != AV_NOPTS_VALUE) {
                // compute DTS based on reference timestamp
                pkt->dts = st->reference_dts + pc->dts_ref_dts_delta * num / den;
            }

            if (st->reference_dts != AV_NOPTS_VALUE && pkt->pts == AV_NOPTS_VALUE)
                pkt->pts = pkt->dts + pc->pts_dts_delta * num / den;

            if (pc->dts_sync_point > 0)
                st->reference_dts = pkt->dts; // new reference
        }
    }

    /* This may be redundant, but it should not hurt. */
    if(pkt->dts != AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE && pkt->pts > pkt->dts)
        presentation_delayed = 1;

    av_dlog(NULL, "IN delayed:%d pts:%s, dts:%s cur_dts:%s st:%d pc:%p duration:%d\n",
           presentation_delayed, av_ts2str(pkt->pts), av_ts2str(pkt->dts), av_ts2str(st->cur_dts), pkt->stream_index, pc, pkt->duration);
    /* interpolate PTS and DTS if they are not present */
    //We skip H264 currently because delay and has_b_frames are not reliably set
    if((delay==0 || (delay==1 && pc)) && st->codec->codec_id != AV_CODEC_ID_H264){
        if (presentation_delayed) {
            /* DTS = decompression timestamp */
            /* PTS = presentation timestamp */
            if (pkt->dts == AV_NOPTS_VALUE)
                pkt->dts = st->last_IP_pts;
            update_initial_timestamps(s, pkt->stream_index, pkt->dts, pkt->pts, pkt);
            if (pkt->dts == AV_NOPTS_VALUE)
                pkt->dts = st->cur_dts;

            /* this is tricky: the dts must be incremented by the duration
            of the frame we are displaying, i.e. the last I- or P-frame */
            if (st->last_IP_duration == 0)
                st->last_IP_duration = pkt->duration;
            if(pkt->dts != AV_NOPTS_VALUE)
                st->cur_dts = pkt->dts + st->last_IP_duration;
            st->last_IP_duration  = pkt->duration;
            st->last_IP_pts= pkt->pts;
            /* cannot compute PTS if not present (we can compute it only
            by knowing the future */
        } else if (pkt->pts != AV_NOPTS_VALUE ||
                   pkt->dts != AV_NOPTS_VALUE ||
                   pkt->duration                ) {
            int duration = pkt->duration;

            /* presentation is not delayed : PTS and DTS are the same */
            if (pkt->pts == AV_NOPTS_VALUE)
                pkt->pts = pkt->dts;
            update_initial_timestamps(s, pkt->stream_index, pkt->pts,
                                      pkt->pts, pkt);
            if (pkt->pts == AV_NOPTS_VALUE)
                pkt->pts = st->cur_dts;
            pkt->dts = pkt->pts;
            if (pkt->pts != AV_NOPTS_VALUE)
                st->cur_dts = pkt->pts + duration;
        }
    }

    if(pkt->pts != AV_NOPTS_VALUE && delay <= MAX_REORDER_DELAY && has_decode_delay_been_guessed(st)){
        st->pts_buffer[0]= pkt->pts;
        for(i=0; i<delay && st->pts_buffer[i] > st->pts_buffer[i+1]; i++)
            FFSWAP(int64_t, st->pts_buffer[i], st->pts_buffer[i+1]);
        if(pkt->dts == AV_NOPTS_VALUE)
            pkt->dts= st->pts_buffer[0];
    }
    if(st->codec->codec_id == AV_CODEC_ID_H264){ // we skipped it above so we try here
        update_initial_timestamps(s, pkt->stream_index, pkt->dts, pkt->pts, pkt); // this should happen on the first packet
    }
    if(pkt->dts > st->cur_dts)
        st->cur_dts = pkt->dts;

    av_dlog(NULL, "OUTdelayed:%d/%d pts:%s, dts:%s cur_dts:%s\n",
            presentation_delayed, delay, av_ts2str(pkt->pts), av_ts2str(pkt->dts), av_ts2str(st->cur_dts));

    /* update flags */
    if (is_intra_only(st->codec))
        pkt->flags |= AV_PKT_FLAG_KEY;
    if (pc)
        pkt->convergence_duration = pc->convergence_duration;
}

static void free_packet_buffer(AVPacketList **pkt_buf, AVPacketList **pkt_buf_end)
{
    while (*pkt_buf) {
        AVPacketList *pktl = *pkt_buf;
        *pkt_buf = pktl->next;
        av_free_packet(&pktl->pkt);
        av_freep(&pktl);
    }
    *pkt_buf_end = NULL;
}

/**
 * Parse a packet, add all split parts to parse_queue
 *
 * @param pkt packet to parse, NULL when flushing the parser at end of stream
 */
static int parse_packet(AVFormatContext *s, AVPacket *pkt, int stream_index)
{
    AVPacket out_pkt = { 0 }, flush_pkt = { 0 };
    AVStream     *st = s->streams[stream_index];
    uint8_t    *data = pkt ? pkt->data : NULL;
    int         size = pkt ? pkt->size : 0;
    int ret = 0, got_output = 0;

    if (!pkt) {
        av_init_packet(&flush_pkt);
        pkt = &flush_pkt;
        got_output = 1;
    } else if (!size && st->parser->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        // preserve 0-size sync packets
        compute_pkt_fields(s, st, st->parser, pkt);
    }

    while (size > 0 || (pkt == &flush_pkt && got_output)) {
        int len;

        av_init_packet(&out_pkt);
        len = av_parser_parse2(st->parser,  st->codec,
                               &out_pkt.data, &out_pkt.size, data, size,
                               pkt->pts, pkt->dts, pkt->pos);

        pkt->pts = pkt->dts = AV_NOPTS_VALUE;
        pkt->pos = -1;
        /* increment read pointer */
        data += len;
        size -= len;

        got_output = !!out_pkt.size;

        if (!out_pkt.size)
            continue;

        if (pkt->side_data) {
            out_pkt.side_data       = pkt->side_data;
            out_pkt.side_data_elems = pkt->side_data_elems;
            pkt->side_data       = NULL;
            pkt->side_data_elems = 0;
        }

        /* set the duration */
        out_pkt.duration = 0;
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (st->codec->sample_rate > 0) {
                out_pkt.duration = av_rescale_q_rnd(st->parser->duration,
                                                    (AVRational){ 1, st->codec->sample_rate },
                                                    st->time_base,
                                                    AV_ROUND_DOWN);
            }
        } else if (st->codec->time_base.num != 0 &&
                   st->codec->time_base.den != 0) {
            out_pkt.duration = av_rescale_q_rnd(st->parser->duration,
                                                st->codec->time_base,
                                                st->time_base,
                                                AV_ROUND_DOWN);
        }

        out_pkt.stream_index = st->index;
        out_pkt.pts = st->parser->pts;
        out_pkt.dts = st->parser->dts;
        out_pkt.pos = st->parser->pos;

        if(st->need_parsing == AVSTREAM_PARSE_FULL_RAW)
            out_pkt.pos = st->parser->frame_offset;

        if (st->parser->key_frame == 1 ||
            (st->parser->key_frame == -1 &&
             st->parser->pict_type == AV_PICTURE_TYPE_I))
            out_pkt.flags |= AV_PKT_FLAG_KEY;

        if(st->parser->key_frame == -1 && st->parser->pict_type==AV_PICTURE_TYPE_NONE && (pkt->flags&AV_PKT_FLAG_KEY))
            out_pkt.flags |= AV_PKT_FLAG_KEY;

        compute_pkt_fields(s, st, st->parser, &out_pkt);

        if (out_pkt.data == pkt->data && out_pkt.size == pkt->size) {
            out_pkt.buf   = pkt->buf;
            pkt->buf      = NULL;
#if FF_API_DESTRUCT_PACKET
FF_DISABLE_DEPRECATION_WARNINGS
            out_pkt.destruct = pkt->destruct;
            pkt->destruct = NULL;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        }
        if ((ret = av_dup_packet(&out_pkt)) < 0)
            goto fail;

        if (!add_to_pktbuf(&s->parse_queue, &out_pkt, &s->parse_queue_end)) {
            av_free_packet(&out_pkt);
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }


    /* end of the stream => close and free the parser */
    if (pkt == &flush_pkt) {
        av_parser_close(st->parser);
        st->parser = NULL;
    }

fail:
    av_free_packet(pkt);
    return ret;
}

static int read_from_packet_buffer(AVPacketList **pkt_buffer,
                                   AVPacketList **pkt_buffer_end,
                                   AVPacket      *pkt)
{
    AVPacketList *pktl;
    av_assert0(*pkt_buffer);
    pktl = *pkt_buffer;
    *pkt = pktl->pkt;
    *pkt_buffer = pktl->next;
    if (!pktl->next)
        *pkt_buffer_end = NULL;
    av_freep(&pktl);
    return 0;
}

static int read_frame_internal(AVFormatContext *s, AVPacket *pkt)
{
    int ret = 0, i, got_packet = 0;

    av_init_packet(pkt);

    while (!got_packet && !s->parse_queue) {
        AVStream *st;
        AVPacket cur_pkt;

        /* read next packet */
        ret = ff_read_packet(s, &cur_pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN))
                return ret;
            /* flush the parsers */
            for(i = 0; i < s->nb_streams; i++) {
                st = s->streams[i];
                if (st->parser && st->need_parsing)
                    parse_packet(s, NULL, st->index);
            }
            /* all remaining packets are now in parse_queue =>
             * really terminate parsing */
            break;
        }
        ret = 0;
        st  = s->streams[cur_pkt.stream_index];

        if (cur_pkt.pts != AV_NOPTS_VALUE &&
            cur_pkt.dts != AV_NOPTS_VALUE &&
            cur_pkt.pts < cur_pkt.dts) {
            av_log(s, AV_LOG_WARNING, "Invalid timestamps stream=%d, pts=%s, dts=%s, size=%d\n",
                   cur_pkt.stream_index,
                   av_ts2str(cur_pkt.pts),
                   av_ts2str(cur_pkt.dts),
                   cur_pkt.size);
        }
        if (s->debug & FF_FDEBUG_TS)
            av_log(s, AV_LOG_DEBUG, "ff_read_packet stream=%d, pts=%s, dts=%s, size=%d, duration=%d, flags=%d\n",
                   cur_pkt.stream_index,
                   av_ts2str(cur_pkt.pts),
                   av_ts2str(cur_pkt.dts),
                   cur_pkt.size,
                   cur_pkt.duration,
                   cur_pkt.flags);

        if (st->need_parsing && !st->parser && !(s->flags & AVFMT_FLAG_NOPARSE)) {
            st->parser = av_parser_init(st->codec->codec_id);
            if (!st->parser) {
                av_log(s, AV_LOG_VERBOSE, "parser not found for codec "
                       "%s, packets or times may be invalid.\n",
                       avcodec_get_name(st->codec->codec_id));
                /* no parser available: just output the raw packets */
                st->need_parsing = AVSTREAM_PARSE_NONE;
            } else if(st->need_parsing == AVSTREAM_PARSE_HEADERS) {
                st->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
            } else if(st->need_parsing == AVSTREAM_PARSE_FULL_ONCE) {
                st->parser->flags |= PARSER_FLAG_ONCE;
            } else if(st->need_parsing == AVSTREAM_PARSE_FULL_RAW) {
                st->parser->flags |= PARSER_FLAG_USE_CODEC_TS;
            }
        }

        if (!st->need_parsing || !st->parser) {
            /* no parsing needed: we just output the packet as is */
            *pkt = cur_pkt;
            compute_pkt_fields(s, st, NULL, pkt);
            if ((s->iformat->flags & AVFMT_GENERIC_INDEX) &&
                (pkt->flags & AV_PKT_FLAG_KEY) && pkt->dts != AV_NOPTS_VALUE) {
                ff_reduce_index(s, st->index);
                av_add_index_entry(st, pkt->pos, pkt->dts, 0, 0, AVINDEX_KEYFRAME);
            }
            got_packet = 1;
        } else if (st->discard < AVDISCARD_ALL) {
            if ((ret = parse_packet(s, &cur_pkt, cur_pkt.stream_index)) < 0)
                return ret;
        } else {
            /* free packet */
            av_free_packet(&cur_pkt);
        }
        if (pkt->flags & AV_PKT_FLAG_KEY)
            st->skip_to_keyframe = 0;
        if (st->skip_to_keyframe) {
            av_free_packet(&cur_pkt);
            if (got_packet) {
                *pkt = cur_pkt;
            }
            got_packet = 0;
        }
    }

    if (!got_packet && s->parse_queue)
        ret = read_from_packet_buffer(&s->parse_queue, &s->parse_queue_end, pkt);

    if(s->debug & FF_FDEBUG_TS)
        av_log(s, AV_LOG_DEBUG, "read_frame_internal stream=%d, pts=%s, dts=%s, size=%d, duration=%d, flags=%d\n",
            pkt->stream_index,
            av_ts2str(pkt->pts),
            av_ts2str(pkt->dts),
            pkt->size,
            pkt->duration,
            pkt->flags);

    return ret;
}

int av_read_frame(AVFormatContext *s, AVPacket *pkt)
{
    const int genpts = s->flags & AVFMT_FLAG_GENPTS;
    int          eof = 0;
    int ret;
    AVStream *st;

    if (!genpts) {
        ret = s->packet_buffer ?
            read_from_packet_buffer(&s->packet_buffer, &s->packet_buffer_end, pkt) :
            read_frame_internal(s, pkt);
        if (ret < 0)
            return ret;
        goto return_packet;
    }

    for (;;) {
        AVPacketList *pktl = s->packet_buffer;

        if (pktl) {
            AVPacket *next_pkt = &pktl->pkt;

            if (next_pkt->dts != AV_NOPTS_VALUE) {
                int wrap_bits = s->streams[next_pkt->stream_index]->pts_wrap_bits;
                // last dts seen for this stream. if any of packets following
                // current one had no dts, we will set this to AV_NOPTS_VALUE.
                int64_t last_dts = next_pkt->dts;
                while (pktl && next_pkt->pts == AV_NOPTS_VALUE) {
                    if (pktl->pkt.stream_index == next_pkt->stream_index &&
                        (av_compare_mod(next_pkt->dts, pktl->pkt.dts, 2LL << (wrap_bits - 1)) < 0)) {
                        if (av_compare_mod(pktl->pkt.pts, pktl->pkt.dts, 2LL << (wrap_bits - 1))) { //not b frame
                            next_pkt->pts = pktl->pkt.dts;
                        }
                        if (last_dts != AV_NOPTS_VALUE) {
                            // Once last dts was set to AV_NOPTS_VALUE, we don't change it.
                            last_dts = pktl->pkt.dts;
                        }
                    }
                    pktl = pktl->next;
                }
                if (eof && next_pkt->pts == AV_NOPTS_VALUE && last_dts != AV_NOPTS_VALUE) {
                    // Fixing the last reference frame had none pts issue (For MXF etc).
                    // We only do this when
                    // 1. eof.
                    // 2. we are not able to resolve a pts value for current packet.
                    // 3. the packets for this stream at the end of the files had valid dts.
                    next_pkt->pts = last_dts + next_pkt->duration;
                }
                pktl = s->packet_buffer;
            }

            /* read packet from packet buffer, if there is data */
            if (!(next_pkt->pts == AV_NOPTS_VALUE &&
                  next_pkt->dts != AV_NOPTS_VALUE && !eof)) {
                ret = read_from_packet_buffer(&s->packet_buffer,
                                               &s->packet_buffer_end, pkt);
                goto return_packet;
            }
        }

        ret = read_frame_internal(s, pkt);
        if (ret < 0) {
            if (pktl && ret != AVERROR(EAGAIN)) {
                eof = 1;
                continue;
            } else
                return ret;
        }

        if (av_dup_packet(add_to_pktbuf(&s->packet_buffer, pkt,
                          &s->packet_buffer_end)) < 0)
            return AVERROR(ENOMEM);
    }

return_packet:

    st = s->streams[pkt->stream_index];
    if (st->skip_samples) {
        uint8_t *p = av_packet_new_side_data(pkt, AV_PKT_DATA_SKIP_SAMPLES, 10);
        if (p) {
            AV_WL32(p, st->skip_samples);
            av_log(s, AV_LOG_DEBUG, "demuxer injecting skip %d\n", st->skip_samples);
        }
        st->skip_samples = 0;
    }

    if ((s->iformat->flags & AVFMT_GENERIC_INDEX) && pkt->flags & AV_PKT_FLAG_KEY) {
        ff_reduce_index(s, st->index);
        av_add_index_entry(st, pkt->pos, pkt->dts, 0, 0, AVINDEX_KEYFRAME);
    }

    if (is_relative(pkt->dts))
        pkt->dts -= RELATIVE_TS_BASE;
    if (is_relative(pkt->pts))
        pkt->pts -= RELATIVE_TS_BASE;

    return ret;
}

/* XXX: suppress the packet queue */
static void flush_packet_queue(AVFormatContext *s)
{
    free_packet_buffer(&s->parse_queue,       &s->parse_queue_end);
    free_packet_buffer(&s->packet_buffer,     &s->packet_buffer_end);
    free_packet_buffer(&s->raw_packet_buffer, &s->raw_packet_buffer_end);

    s->raw_packet_buffer_remaining_size = RAW_PACKET_BUFFER_SIZE;
}

/*******************************************************/
/* seek support */

int av_find_default_stream_index(AVFormatContext *s)
{
    int first_audio_index = -1;
    int i;
    AVStream *st;

    if (s->nb_streams <= 0)
        return -1;
    for(i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
            !(st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            return i;
        }
        if (first_audio_index < 0 && st->codec->codec_type == AVMEDIA_TYPE_AUDIO)
            first_audio_index = i;
    }
    return first_audio_index >= 0 ? first_audio_index : 0;
}

/**
 * Flush the frame reader.
 */
void ff_read_frame_flush(AVFormatContext *s)
{
    AVStream *st;
    int i, j;

    flush_packet_queue(s);

    /* for each stream, reset read state */
    for(i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];

        if (st->parser) {
            av_parser_close(st->parser);
            st->parser = NULL;
        }
        st->last_IP_pts = AV_NOPTS_VALUE;
        if(st->first_dts == AV_NOPTS_VALUE) st->cur_dts = RELATIVE_TS_BASE;
        else                                st->cur_dts = AV_NOPTS_VALUE; /* we set the current DTS to an unspecified origin */
        st->reference_dts = AV_NOPTS_VALUE;

        st->probe_packets = MAX_PROBE_PACKETS;

        for(j=0; j<MAX_REORDER_DELAY+1; j++)
            st->pts_buffer[j]= AV_NOPTS_VALUE;
    }
}

void ff_update_cur_dts(AVFormatContext *s, AVStream *ref_st, int64_t timestamp)
{
    int i;

    for(i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];

        st->cur_dts = av_rescale(timestamp,
                                 st->time_base.den * (int64_t)ref_st->time_base.num,
                                 st->time_base.num * (int64_t)ref_st->time_base.den);
    }
}

void ff_reduce_index(AVFormatContext *s, int stream_index)
{
    AVStream *st= s->streams[stream_index];
    unsigned int max_entries= s->max_index_size / sizeof(AVIndexEntry);

    if((unsigned)st->nb_index_entries >= max_entries){
        int i;
        for(i=0; 2*i<st->nb_index_entries; i++)
            st->index_entries[i]= st->index_entries[2*i];
        st->nb_index_entries= i;
    }
}

int ff_add_index_entry(AVIndexEntry **index_entries,
                       int *nb_index_entries,
                       unsigned int *index_entries_allocated_size,
                       int64_t pos, int64_t timestamp, int size, int distance, int flags)
{
    AVIndexEntry *entries, *ie;
    int index;

    if((unsigned)*nb_index_entries + 1 >= UINT_MAX / sizeof(AVIndexEntry))
        return -1;

    if(timestamp == AV_NOPTS_VALUE)
        return AVERROR(EINVAL);

    if (size < 0 || size > 0x3FFFFFFF)
        return AVERROR(EINVAL);

    if (is_relative(timestamp)) //FIXME this maintains previous behavior but we should shift by the correct offset once known
        timestamp -= RELATIVE_TS_BASE;

    entries = av_fast_realloc(*index_entries,
                              index_entries_allocated_size,
                              (*nb_index_entries + 1) *
                              sizeof(AVIndexEntry));
    if(!entries)
        return -1;

    *index_entries= entries;

    index= ff_index_search_timestamp(*index_entries, *nb_index_entries, timestamp, AVSEEK_FLAG_ANY);

    if(index<0){
        index= (*nb_index_entries)++;
        ie= &entries[index];
        av_assert0(index==0 || ie[-1].timestamp < timestamp);
    }else{
        ie= &entries[index];
        if(ie->timestamp != timestamp){
            if(ie->timestamp <= timestamp)
                return -1;
            memmove(entries + index + 1, entries + index, sizeof(AVIndexEntry)*(*nb_index_entries - index));
            (*nb_index_entries)++;
        }else if(ie->pos == pos && distance < ie->min_distance) //do not reduce the distance
            distance= ie->min_distance;
    }

    ie->pos = pos;
    ie->timestamp = timestamp;
    ie->min_distance= distance;
    ie->size= size;
    ie->flags = flags;

    return index;
}

int av_add_index_entry(AVStream *st,
                       int64_t pos, int64_t timestamp, int size, int distance, int flags)
{
    timestamp = wrap_timestamp(st, timestamp);
    return ff_add_index_entry(&st->index_entries, &st->nb_index_entries,
                              &st->index_entries_allocated_size, pos,
                              timestamp, size, distance, flags);
}

int ff_index_search_timestamp(const AVIndexEntry *entries, int nb_entries,
                              int64_t wanted_timestamp, int flags)
{
    int a, b, m;
    int64_t timestamp;

    a = - 1;
    b = nb_entries;

    //optimize appending index entries at the end
    if(b && entries[b-1].timestamp < wanted_timestamp)
        a= b-1;

    while (b - a > 1) {
        m = (a + b) >> 1;
        timestamp = entries[m].timestamp;
        if(timestamp >= wanted_timestamp)
            b = m;
        if(timestamp <= wanted_timestamp)
            a = m;
    }
    m= (flags & AVSEEK_FLAG_BACKWARD) ? a : b;

    if(!(flags & AVSEEK_FLAG_ANY)){
        while(m>=0 && m<nb_entries && !(entries[m].flags & AVINDEX_KEYFRAME)){
            m += (flags & AVSEEK_FLAG_BACKWARD) ? -1 : 1;
        }
    }

    if(m == nb_entries)
        return -1;
    return  m;
}

int av_index_search_timestamp(AVStream *st, int64_t wanted_timestamp,
                              int flags)
{
    return ff_index_search_timestamp(st->index_entries, st->nb_index_entries,
                                     wanted_timestamp, flags);
}

static int64_t ff_read_timestamp(AVFormatContext *s, int stream_index, int64_t *ppos, int64_t pos_limit,
                                 int64_t (*read_timestamp)(struct AVFormatContext *, int , int64_t *, int64_t ))
{
    int64_t ts = read_timestamp(s, stream_index, ppos, pos_limit);
    if (stream_index >= 0)
        ts = wrap_timestamp(s->streams[stream_index], ts);
    return ts;
}

int ff_seek_frame_binary(AVFormatContext *s, int stream_index, int64_t target_ts, int flags)
{
    AVInputFormat *avif= s->iformat;
    int64_t av_uninit(pos_min), av_uninit(pos_max), pos, pos_limit;
    int64_t ts_min, ts_max, ts;
    int index;
    int64_t ret;
    AVStream *st;

    if (stream_index < 0)
        return -1;

    av_dlog(s, "read_seek: %d %s\n", stream_index, av_ts2str(target_ts));

    ts_max=
    ts_min= AV_NOPTS_VALUE;
    pos_limit= -1; //gcc falsely says it may be uninitialized

    st= s->streams[stream_index];
    if(st->index_entries){
        AVIndexEntry *e;

        index= av_index_search_timestamp(st, target_ts, flags | AVSEEK_FLAG_BACKWARD); //FIXME whole func must be checked for non-keyframe entries in index case, especially read_timestamp()
        index= FFMAX(index, 0);
        e= &st->index_entries[index];

        if(e->timestamp <= target_ts || e->pos == e->min_distance){
            pos_min= e->pos;
            ts_min= e->timestamp;
            av_dlog(s, "using cached pos_min=0x%"PRIx64" dts_min=%s\n",
                    pos_min, av_ts2str(ts_min));
        }else{
            av_assert1(index==0);
        }

        index= av_index_search_timestamp(st, target_ts, flags & ~AVSEEK_FLAG_BACKWARD);
        av_assert0(index < st->nb_index_entries);
        if(index >= 0){
            e= &st->index_entries[index];
            av_assert1(e->timestamp >= target_ts);
            pos_max= e->pos;
            ts_max= e->timestamp;
            pos_limit= pos_max - e->min_distance;
            av_dlog(s, "using cached pos_max=0x%"PRIx64" pos_limit=0x%"PRIx64" dts_max=%s\n",
                    pos_max, pos_limit, av_ts2str(ts_max));
        }
    }

    pos= ff_gen_search(s, stream_index, target_ts, pos_min, pos_max, pos_limit, ts_min, ts_max, flags, &ts, avif->read_timestamp);
    if(pos<0)
        return -1;

    /* do the seek */
    if ((ret = avio_seek(s->pb, pos, SEEK_SET)) < 0)
        return ret;

    ff_read_frame_flush(s);
    ff_update_cur_dts(s, st, ts);

    return 0;
}

int ff_find_last_ts(AVFormatContext *s, int stream_index, int64_t *ts, int64_t *pos,
                    int64_t (*read_timestamp)(struct AVFormatContext *, int , int64_t *, int64_t ))
{
    int64_t step= 1024;
    int64_t limit, ts_max;
    int64_t filesize = avio_size(s->pb);
    int64_t pos_max = filesize - 1;
    do{
        limit = pos_max;
        pos_max = FFMAX(0, (pos_max) - step);
        ts_max = ff_read_timestamp(s, stream_index, &pos_max, limit, read_timestamp);
        step += step;
    }while(ts_max == AV_NOPTS_VALUE && 2*limit > step);
    if (ts_max == AV_NOPTS_VALUE)
        return -1;

    for(;;){
        int64_t tmp_pos = pos_max + 1;
        int64_t tmp_ts = ff_read_timestamp(s, stream_index, &tmp_pos, INT64_MAX, read_timestamp);
        if(tmp_ts == AV_NOPTS_VALUE)
            break;
        av_assert0(tmp_pos > pos_max);
        ts_max  = tmp_ts;
        pos_max = tmp_pos;
        if(tmp_pos >= filesize)
            break;
    }

    if (ts)
        *ts = ts_max;
    if (pos)
        *pos = pos_max;

    return 0;
}

int64_t ff_gen_search(AVFormatContext *s, int stream_index, int64_t target_ts,
                      int64_t pos_min, int64_t pos_max, int64_t pos_limit,
                      int64_t ts_min, int64_t ts_max, int flags, int64_t *ts_ret,
                      int64_t (*read_timestamp)(struct AVFormatContext *, int , int64_t *, int64_t ))
{
    int64_t pos, ts;
    int64_t start_pos;
    int no_change;
    int ret;

    av_dlog(s, "gen_seek: %d %s\n", stream_index, av_ts2str(target_ts));

    if(ts_min == AV_NOPTS_VALUE){
        pos_min = s->data_offset;
        ts_min = ff_read_timestamp(s, stream_index, &pos_min, INT64_MAX, read_timestamp);
        if (ts_min == AV_NOPTS_VALUE)
            return -1;
    }

    if(ts_min >= target_ts){
        *ts_ret= ts_min;
        return pos_min;
    }

    if(ts_max == AV_NOPTS_VALUE){
        if ((ret = ff_find_last_ts(s, stream_index, &ts_max, &pos_max, read_timestamp)) < 0)
            return ret;
        pos_limit= pos_max;
    }

    if(ts_max <= target_ts){
        *ts_ret= ts_max;
        return pos_max;
    }

    if(ts_min > ts_max){
        return -1;
    }else if(ts_min == ts_max){
        pos_limit= pos_min;
    }

    no_change=0;
    while (pos_min < pos_limit) {
        av_dlog(s, "pos_min=0x%"PRIx64" pos_max=0x%"PRIx64" dts_min=%s dts_max=%s\n",
                pos_min, pos_max, av_ts2str(ts_min), av_ts2str(ts_max));
        assert(pos_limit <= pos_max);

        if(no_change==0){
            int64_t approximate_keyframe_distance= pos_max - pos_limit;
            // interpolate position (better than dichotomy)
            pos = av_rescale(target_ts - ts_min, pos_max - pos_min, ts_max - ts_min)
                + pos_min - approximate_keyframe_distance;
        }else if(no_change==1){
            // bisection, if interpolation failed to change min or max pos last time
            pos = (pos_min + pos_limit)>>1;
        }else{
            /* linear search if bisection failed, can only happen if there
               are very few or no keyframes between min/max */
            pos=pos_min;
        }
        if(pos <= pos_min)
            pos= pos_min + 1;
        else if(pos > pos_limit)
            pos= pos_limit;
        start_pos= pos;

        ts = ff_read_timestamp(s, stream_index, &pos, INT64_MAX, read_timestamp); //may pass pos_limit instead of -1
        if(pos == pos_max)
            no_change++;
        else
            no_change=0;
        av_dlog(s, "%"PRId64" %"PRId64" %"PRId64" / %s %s %s target:%s limit:%"PRId64" start:%"PRId64" noc:%d\n",
                pos_min, pos, pos_max,
                av_ts2str(ts_min), av_ts2str(ts), av_ts2str(ts_max), av_ts2str(target_ts),
                pos_limit, start_pos, no_change);
        if(ts == AV_NOPTS_VALUE){
            av_log(s, AV_LOG_ERROR, "read_timestamp() failed in the middle\n");
            return -1;
        }
        assert(ts != AV_NOPTS_VALUE);
        if (target_ts <= ts) {
            pos_limit = start_pos - 1;
            pos_max = pos;
            ts_max = ts;
        }
        if (target_ts >= ts) {
            pos_min = pos;
            ts_min = ts;
        }
    }

    pos = (flags & AVSEEK_FLAG_BACKWARD) ? pos_min : pos_max;
    ts  = (flags & AVSEEK_FLAG_BACKWARD) ?  ts_min :  ts_max;
#if 0
    pos_min = pos;
    ts_min = ff_read_timestamp(s, stream_index, &pos_min, INT64_MAX, read_timestamp);
    pos_min++;
    ts_max = ff_read_timestamp(s, stream_index, &pos_min, INT64_MAX, read_timestamp);
    av_dlog(s, "pos=0x%"PRIx64" %s<=%s<=%s\n",
            pos, av_ts2str(ts_min), av_ts2str(target_ts), av_ts2str(ts_max));
#endif
    *ts_ret= ts;
    return pos;
}

static int seek_frame_byte(AVFormatContext *s, int stream_index, int64_t pos, int flags){
    int64_t pos_min, pos_max;

    pos_min = s->data_offset;
    pos_max = avio_size(s->pb) - 1;

    if     (pos < pos_min) pos= pos_min;
    else if(pos > pos_max) pos= pos_max;

    avio_seek(s->pb, pos, SEEK_SET);

    s->io_repositioned = 1;

    return 0;
}

static int seek_frame_generic(AVFormatContext *s,
                                 int stream_index, int64_t timestamp, int flags)
{
    int index;
    int64_t ret;
    AVStream *st;
    AVIndexEntry *ie;

    st = s->streams[stream_index];

    index = av_index_search_timestamp(st, timestamp, flags);

    if(index < 0 && st->nb_index_entries && timestamp < st->index_entries[0].timestamp)
        return -1;

    if(index < 0 || index==st->nb_index_entries-1){
        AVPacket pkt;
        int nonkey=0;

        if(st->nb_index_entries){
            av_assert0(st->index_entries);
            ie= &st->index_entries[st->nb_index_entries-1];
            if ((ret = avio_seek(s->pb, ie->pos, SEEK_SET)) < 0)
                return ret;
            ff_update_cur_dts(s, st, ie->timestamp);
        }else{
            if ((ret = avio_seek(s->pb, s->data_offset, SEEK_SET)) < 0)
                return ret;
        }
        for (;;) {
            int read_status;
            do{
                read_status = av_read_frame(s, &pkt);
            } while (read_status == AVERROR(EAGAIN));
            if (read_status < 0)
                break;
            av_free_packet(&pkt);
            if(stream_index == pkt.stream_index && pkt.dts > timestamp){
                if(pkt.flags & AV_PKT_FLAG_KEY)
                    break;
                if(nonkey++ > 1000 && st->codec->codec_id != AV_CODEC_ID_CDGRAPHICS){
                    av_log(s, AV_LOG_ERROR,"seek_frame_generic failed as this stream seems to contain no keyframes after the target timestamp, %d non keyframes found\n", nonkey);
                    break;
                }
            }
        }
        index = av_index_search_timestamp(st, timestamp, flags);
    }
    if (index < 0)
        return -1;

    ff_read_frame_flush(s);
    if (s->iformat->read_seek){
        if(s->iformat->read_seek(s, stream_index, timestamp, flags) >= 0)
            return 0;
    }
    ie = &st->index_entries[index];
    if ((ret = avio_seek(s->pb, ie->pos, SEEK_SET)) < 0)
        return ret;
    ff_update_cur_dts(s, st, ie->timestamp);

    return 0;
}

static int seek_frame_internal(AVFormatContext *s, int stream_index,
                               int64_t timestamp, int flags)
{
    int ret;
    AVStream *st;

    if (flags & AVSEEK_FLAG_BYTE) {
        if (s->iformat->flags & AVFMT_NO_BYTE_SEEK)
            return -1;
        ff_read_frame_flush(s);
        return seek_frame_byte(s, stream_index, timestamp, flags);
    }

    if(stream_index < 0){
        stream_index= av_find_default_stream_index(s);
        if(stream_index < 0)
            return -1;

        st= s->streams[stream_index];
        /* timestamp for default must be expressed in AV_TIME_BASE units */
        timestamp = av_rescale(timestamp, st->time_base.den, AV_TIME_BASE * (int64_t)st->time_base.num);
    }

    /* first, we try the format specific seek */
    if (s->iformat->read_seek) {
        ff_read_frame_flush(s);
        ret = s->iformat->read_seek(s, stream_index, timestamp, flags);
    } else
        ret = -1;
    if (ret >= 0) {
        return 0;
    }

    if (s->iformat->read_timestamp && !(s->iformat->flags & AVFMT_NOBINSEARCH)) {
        ff_read_frame_flush(s);
        return ff_seek_frame_binary(s, stream_index, timestamp, flags);
    } else if (!(s->iformat->flags & AVFMT_NOGENSEARCH)) {
        ff_read_frame_flush(s);
        return seek_frame_generic(s, stream_index, timestamp, flags);
    }
    else
        return -1;
}

int av_seek_frame(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    int ret;

    if (s->iformat->read_seek2 && !s->iformat->read_seek) {
        int64_t min_ts = INT64_MIN, max_ts = INT64_MAX;
        if ((flags & AVSEEK_FLAG_BACKWARD))
            max_ts = timestamp;
        else
            min_ts = timestamp;
        return avformat_seek_file(s, stream_index, min_ts, timestamp, max_ts,
                                  flags & ~AVSEEK_FLAG_BACKWARD);
    }

    ret = seek_frame_internal(s, stream_index, timestamp, flags);

    if (ret >= 0)
        ret = avformat_queue_attached_pictures(s);

    return ret;
}

int avformat_seek_file(AVFormatContext *s, int stream_index, int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    if(min_ts > ts || max_ts < ts)
        return -1;
    if (stream_index < -1 || stream_index >= (int)s->nb_streams)
        return AVERROR(EINVAL);

    if(s->seek2any>0)
        flags |= AVSEEK_FLAG_ANY;
    flags &= ~AVSEEK_FLAG_BACKWARD;

    if (s->iformat->read_seek2) {
        int ret;
        ff_read_frame_flush(s);

        if (stream_index == -1 && s->nb_streams == 1) {
            AVRational time_base = s->streams[0]->time_base;
            ts = av_rescale_q(ts, AV_TIME_BASE_Q, time_base);
            min_ts = av_rescale_rnd(min_ts, time_base.den,
                                    time_base.num * (int64_t)AV_TIME_BASE,
                                    AV_ROUND_UP   | AV_ROUND_PASS_MINMAX);
            max_ts = av_rescale_rnd(max_ts, time_base.den,
                                    time_base.num * (int64_t)AV_TIME_BASE,
                                    AV_ROUND_DOWN | AV_ROUND_PASS_MINMAX);
        }

        ret = s->iformat->read_seek2(s, stream_index, min_ts, ts, max_ts, flags);

        if (ret >= 0)
            ret = avformat_queue_attached_pictures(s);
        return ret;
    }

    if(s->iformat->read_timestamp){
        //try to seek via read_timestamp()
    }

    // Fall back on old API if new is not implemented but old is.
    // Note the old API has somewhat different semantics.
    if (s->iformat->read_seek || 1) {
        int dir = (ts - (uint64_t)min_ts > (uint64_t)max_ts - ts ? AVSEEK_FLAG_BACKWARD : 0);
        int ret = av_seek_frame(s, stream_index, ts, flags | dir);
        if (ret<0 && ts != min_ts && max_ts != ts) {
            ret = av_seek_frame(s, stream_index, dir ? max_ts : min_ts, flags | dir);
            if (ret >= 0)
                ret = av_seek_frame(s, stream_index, ts, flags | (dir^AVSEEK_FLAG_BACKWARD));
        }
        return ret;
    }

    // try some generic seek like seek_frame_generic() but with new ts semantics
    return -1; //unreachable
}

/*******************************************************/

/**
 * Return TRUE if the stream has accurate duration in any stream.
 *
 * @return TRUE if the stream has accurate duration for at least one component.
 */
static int has_duration(AVFormatContext *ic)
{
    int i;
    AVStream *st;

    for(i = 0;i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->duration != AV_NOPTS_VALUE)
            return 1;
    }
    if (ic->duration != AV_NOPTS_VALUE)
        return 1;
    return 0;
}

/**
 * Estimate the stream timings from the one of each components.
 *
 * Also computes the global bitrate if possible.
 */
static void update_stream_timings(AVFormatContext *ic)
{
    int64_t start_time, start_time1, start_time_text, end_time, end_time1;
    int64_t duration, duration1, filesize;
    int i;
    AVStream *st;
    AVProgram *p;

    start_time = INT64_MAX;
    start_time_text = INT64_MAX;
    end_time = INT64_MIN;
    duration = INT64_MIN;
    for(i = 0;i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time != AV_NOPTS_VALUE && st->time_base.den) {
            start_time1= av_rescale_q(st->start_time, st->time_base, AV_TIME_BASE_Q);
            if (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE || st->codec->codec_type == AVMEDIA_TYPE_DATA) {
                if (start_time1 < start_time_text)
                    start_time_text = start_time1;
            } else
                start_time = FFMIN(start_time, start_time1);
            end_time1 = AV_NOPTS_VALUE;
            if (st->duration != AV_NOPTS_VALUE) {
                end_time1 = start_time1
                          + av_rescale_q(st->duration, st->time_base, AV_TIME_BASE_Q);
                end_time = FFMAX(end_time, end_time1);
            }
            for(p = NULL; (p = av_find_program_from_stream(ic, p, i)); ){
                if(p->start_time == AV_NOPTS_VALUE || p->start_time > start_time1)
                    p->start_time = start_time1;
                if(p->end_time < end_time1)
                    p->end_time = end_time1;
            }
        }
        if (st->duration != AV_NOPTS_VALUE) {
            duration1 = av_rescale_q(st->duration, st->time_base, AV_TIME_BASE_Q);
            duration = FFMAX(duration, duration1);
        }
    }
    if (start_time == INT64_MAX || (start_time > start_time_text && start_time - start_time_text < AV_TIME_BASE))
        start_time = start_time_text;
    else if(start_time > start_time_text)
        av_log(ic, AV_LOG_VERBOSE, "Ignoring outlier non primary stream starttime %f\n", start_time_text / (float)AV_TIME_BASE);

    if (start_time != INT64_MAX) {
        ic->start_time = start_time;
        if (end_time != INT64_MIN) {
            if (ic->nb_programs) {
                for (i=0; i<ic->nb_programs; i++) {
                    p = ic->programs[i];
                    if(p->start_time != AV_NOPTS_VALUE && p->end_time > p->start_time)
                        duration = FFMAX(duration, p->end_time - p->start_time);
                }
            } else
                duration = FFMAX(duration, end_time - start_time);
        }
    }
    if (duration != INT64_MIN && duration > 0 && ic->duration == AV_NOPTS_VALUE) {
        ic->duration = duration;
    }
        if (ic->pb && (filesize = avio_size(ic->pb)) > 0 && ic->duration != AV_NOPTS_VALUE) {
            /* compute the bitrate */
            double bitrate = (double)filesize * 8.0 * AV_TIME_BASE /
                (double)ic->duration;
            if (bitrate >= 0 && bitrate <= INT_MAX)
                ic->bit_rate = bitrate;
        }
}

static void fill_all_stream_timings(AVFormatContext *ic)
{
    int i;
    AVStream *st;

    update_stream_timings(ic);
    for(i = 0;i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time == AV_NOPTS_VALUE) {
            if(ic->start_time != AV_NOPTS_VALUE)
                st->start_time = av_rescale_q(ic->start_time, AV_TIME_BASE_Q, st->time_base);
            if(ic->duration != AV_NOPTS_VALUE)
                st->duration = av_rescale_q(ic->duration, AV_TIME_BASE_Q, st->time_base);
        }
    }
}

static void estimate_timings_from_bit_rate(AVFormatContext *ic)
{
    int64_t filesize, duration;
    int i, show_warning = 0;
    AVStream *st;

    /* if bit_rate is already set, we believe it */
    if (ic->bit_rate <= 0) {
        int bit_rate = 0;
        for(i=0;i<ic->nb_streams;i++) {
            st = ic->streams[i];
            if (st->codec->bit_rate > 0) {
                if (INT_MAX - st->codec->bit_rate < bit_rate) {
                    bit_rate = 0;
                    break;
                }
                bit_rate += st->codec->bit_rate;
            }
        }
        ic->bit_rate = bit_rate;
    }

    /* if duration is already set, we believe it */
    if (ic->duration == AV_NOPTS_VALUE &&
        ic->bit_rate != 0) {
        filesize = ic->pb ? avio_size(ic->pb) : 0;
        if (filesize > 0) {
            for(i = 0; i < ic->nb_streams; i++) {
                st = ic->streams[i];
                if (   st->time_base.num <= INT64_MAX / ic->bit_rate
                    && st->duration == AV_NOPTS_VALUE) {
                    duration= av_rescale(8*filesize, st->time_base.den, ic->bit_rate*(int64_t)st->time_base.num);
                    st->duration = duration;
                    show_warning = 1;
                }
            }
        }
    }
    if (show_warning)
        av_log(ic, AV_LOG_WARNING, "Estimating duration from bitrate, this may be inaccurate\n");
}

#define DURATION_MAX_READ_SIZE 250000LL
#define DURATION_MAX_RETRY 4

/* only usable for MPEG-PS streams */
static void estimate_timings_from_pts(AVFormatContext *ic, int64_t old_offset)
{
    AVPacket pkt1, *pkt = &pkt1;
    AVStream *st;
    int read_size, i, ret;
    int64_t end_time;
    int64_t filesize, offset, duration;
    int retry=0;

    /* flush packet queue */
    flush_packet_queue(ic);

    for (i=0; i<ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time == AV_NOPTS_VALUE && st->first_dts == AV_NOPTS_VALUE)
            av_log(st->codec, AV_LOG_WARNING, "start time is not set in estimate_timings_from_pts\n");

        if (st->parser) {
            av_parser_close(st->parser);
            st->parser= NULL;
        }
    }

    /* estimate the end time (duration) */
    /* XXX: may need to support wrapping */
    filesize = ic->pb ? avio_size(ic->pb) : 0;
    end_time = AV_NOPTS_VALUE;
    do{
        offset = filesize - (DURATION_MAX_READ_SIZE<<retry);
        if (offset < 0)
            offset = 0;

        avio_seek(ic->pb, offset, SEEK_SET);
        read_size = 0;
        for(;;) {
            if (read_size >= DURATION_MAX_READ_SIZE<<(FFMAX(retry-1,0)))
                break;

            do {
                ret = ff_read_packet(ic, pkt);
            } while(ret == AVERROR(EAGAIN));
            if (ret != 0)
                break;
            read_size += pkt->size;
            st = ic->streams[pkt->stream_index];
            if (pkt->pts != AV_NOPTS_VALUE &&
                (st->start_time != AV_NOPTS_VALUE ||
                 st->first_dts  != AV_NOPTS_VALUE)) {
                duration = end_time = pkt->pts;
                if (st->start_time != AV_NOPTS_VALUE)
                    duration -= st->start_time;
                else
                    duration -= st->first_dts;
                if (duration > 0) {
                    if (st->duration == AV_NOPTS_VALUE || st->info->last_duration<=0 ||
                        (st->duration < duration && FFABS(duration - st->info->last_duration) < 60LL*st->time_base.den / st->time_base.num))
                        st->duration = duration;
                    st->info->last_duration = duration;
                }
            }
            av_free_packet(pkt);
        }
    }while(   end_time==AV_NOPTS_VALUE
           && filesize > (DURATION_MAX_READ_SIZE<<retry)
           && ++retry <= DURATION_MAX_RETRY);

    fill_all_stream_timings(ic);

    avio_seek(ic->pb, old_offset, SEEK_SET);
    for (i=0; i<ic->nb_streams; i++) {
        st= ic->streams[i];
        st->cur_dts= st->first_dts;
        st->last_IP_pts = AV_NOPTS_VALUE;
        st->reference_dts = AV_NOPTS_VALUE;
    }
}

static void estimate_timings(AVFormatContext *ic, int64_t old_offset)
{
    int64_t file_size;

    /* get the file size, if possible */
    if (ic->iformat->flags & AVFMT_NOFILE) {
        file_size = 0;
    } else {
        file_size = avio_size(ic->pb);
        file_size = FFMAX(0, file_size);
    }

    if ((!strcmp(ic->iformat->name, "mpeg") ||
         !strcmp(ic->iformat->name, "mpegts")) &&
        file_size && ic->pb->seekable) {
        /* get accurate estimate from the PTSes */
        estimate_timings_from_pts(ic, old_offset);
        ic->duration_estimation_method = AVFMT_DURATION_FROM_PTS;
    } else if (has_duration(ic)) {
        /* at least one component has timings - we use them for all
           the components */
        fill_all_stream_timings(ic);
        ic->duration_estimation_method = AVFMT_DURATION_FROM_STREAM;
    } else {
        /* less precise: use bitrate info */
        estimate_timings_from_bit_rate(ic);
        ic->duration_estimation_method = AVFMT_DURATION_FROM_BITRATE;
    }
    update_stream_timings(ic);

    {
        int i;
        AVStream av_unused *st;
        for(i = 0;i < ic->nb_streams; i++) {
            st = ic->streams[i];
            av_dlog(ic, "%d: start_time: %0.3f duration: %0.3f\n", i,
                    (double) st->start_time / AV_TIME_BASE,
                    (double) st->duration   / AV_TIME_BASE);
        }
        av_dlog(ic, "stream: start_time: %0.3f duration: %0.3f bitrate=%d kb/s\n",
                (double) ic->start_time / AV_TIME_BASE,
                (double) ic->duration   / AV_TIME_BASE,
                ic->bit_rate / 1000);
    }
}

static int has_codec_parameters(AVStream *st, const char **errmsg_ptr)
{
    AVCodecContext *avctx = st->codec;

#define FAIL(errmsg) do {                                         \
        if (errmsg_ptr)                                           \
            *errmsg_ptr = errmsg;                                 \
        return 0;                                                 \
    } while (0)

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        if (!avctx->frame_size && determinable_frame_size(avctx))
            FAIL("unspecified frame size");
        if (st->info->found_decoder >= 0 && avctx->sample_fmt == AV_SAMPLE_FMT_NONE)
            FAIL("unspecified sample format");
        if (!avctx->sample_rate)
            FAIL("unspecified sample rate");
        if (!avctx->channels)
            FAIL("unspecified number of channels");
        if (st->info->found_decoder >= 0 && !st->nb_decoded_frames && avctx->codec_id == AV_CODEC_ID_DTS)
            FAIL("no decodable DTS frames");
        break;
    case AVMEDIA_TYPE_VIDEO:
        if (!avctx->width)
            FAIL("unspecified size");
        if (st->info->found_decoder >= 0 && avctx->pix_fmt == AV_PIX_FMT_NONE)
            FAIL("unspecified pixel format");
        if (st->codec->codec_id == AV_CODEC_ID_RV30 || st->codec->codec_id == AV_CODEC_ID_RV40)
            if (!st->sample_aspect_ratio.num && !st->codec->sample_aspect_ratio.num && !st->codec_info_nb_frames)
                FAIL("no frame in rv30/40 and no sar");
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        if (avctx->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE && !avctx->width)
            FAIL("unspecified size");
        break;
    case AVMEDIA_TYPE_DATA:
        if(avctx->codec_id == AV_CODEC_ID_NONE) return 1;
    }

    if (avctx->codec_id == AV_CODEC_ID_NONE)
        FAIL("unknown codec");
    return 1;
}

/* returns 1 or 0 if or if not decoded data was returned, or a negative error */
static int try_decode_frame(AVFormatContext *s, AVStream *st, AVPacket *avpkt, AVDictionary **options)
{
    const AVCodec *codec;
    int got_picture = 1, ret = 0;
    AVFrame *frame = avcodec_alloc_frame();
    AVSubtitle subtitle;
    AVPacket pkt = *avpkt;

    if (!frame)
        return AVERROR(ENOMEM);

    if (!avcodec_is_open(st->codec) && !st->info->found_decoder) {
        AVDictionary *thread_opt = NULL;

        codec = find_decoder(s, st, st->codec->codec_id);

        if (!codec) {
            st->info->found_decoder = -1;
            ret = -1;
            goto fail;
        }

        /* force thread count to 1 since the h264 decoder will not extract SPS
         *  and PPS to extradata during multi-threaded decoding */
        av_dict_set(options ? options : &thread_opt, "threads", "1", 0);
        ret = avcodec_open2(st->codec, codec, options ? options : &thread_opt);
        if (!options)
            av_dict_free(&thread_opt);
        if (ret < 0) {
            st->info->found_decoder = -1;
            goto fail;
        }
        st->info->found_decoder = 1;
    } else if (!st->info->found_decoder)
        st->info->found_decoder = 1;

    if (st->info->found_decoder < 0) {
        ret = -1;
        goto fail;
    }

    while ((pkt.size > 0 || (!pkt.data && got_picture)) &&
           ret >= 0 &&
           (!has_codec_parameters(st, NULL)   ||
           !has_decode_delay_been_guessed(st) ||
           (!st->codec_info_nb_frames && st->codec->codec->capabilities & CODEC_CAP_CHANNEL_CONF))) {
        got_picture = 0;
        avcodec_get_frame_defaults(frame);
        switch(st->codec->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            ret = avcodec_decode_video2(st->codec, frame,
                                        &got_picture, &pkt);
            break;
        case AVMEDIA_TYPE_AUDIO:
            ret = avcodec_decode_audio4(st->codec, frame, &got_picture, &pkt);
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            ret = avcodec_decode_subtitle2(st->codec, &subtitle,
                                           &got_picture, &pkt);
            ret = pkt.size;
            break;
        default:
            break;
        }
        if (ret >= 0) {
            if (got_picture)
                st->nb_decoded_frames++;
            pkt.data += ret;
            pkt.size -= ret;
            ret       = got_picture;
        }
    }

    if(!pkt.data && !got_picture)
        ret = -1;

fail:
    avcodec_free_frame(&frame);
    return ret;
}

unsigned int ff_codec_get_tag(const AVCodecTag *tags, enum AVCodecID id)
{
    while (tags->id != AV_CODEC_ID_NONE) {
        if (tags->id == id)
            return tags->tag;
        tags++;
    }
    return 0;
}

enum AVCodecID ff_codec_get_id(const AVCodecTag *tags, unsigned int tag)
{
    int i;
    for(i=0; tags[i].id != AV_CODEC_ID_NONE;i++) {
        if(tag == tags[i].tag)
            return tags[i].id;
    }
    for(i=0; tags[i].id != AV_CODEC_ID_NONE; i++) {
        if (avpriv_toupper4(tag) == avpriv_toupper4(tags[i].tag))
            return tags[i].id;
    }
    return AV_CODEC_ID_NONE;
}

enum AVCodecID ff_get_pcm_codec_id(int bps, int flt, int be, int sflags)
{
    if (flt) {
        switch (bps) {
        case 32: return be ? AV_CODEC_ID_PCM_F32BE : AV_CODEC_ID_PCM_F32LE;
        case 64: return be ? AV_CODEC_ID_PCM_F64BE : AV_CODEC_ID_PCM_F64LE;
        default: return AV_CODEC_ID_NONE;
        }
    } else {
        bps  += 7;
        bps >>= 3;
        if (sflags & (1 << (bps - 1))) {
            switch (bps) {
            case 1:  return AV_CODEC_ID_PCM_S8;
            case 2:  return be ? AV_CODEC_ID_PCM_S16BE : AV_CODEC_ID_PCM_S16LE;
            case 3:  return be ? AV_CODEC_ID_PCM_S24BE : AV_CODEC_ID_PCM_S24LE;
            case 4:  return be ? AV_CODEC_ID_PCM_S32BE : AV_CODEC_ID_PCM_S32LE;
            default: return AV_CODEC_ID_NONE;
            }
        } else {
            switch (bps) {
            case 1:  return AV_CODEC_ID_PCM_U8;
            case 2:  return be ? AV_CODEC_ID_PCM_U16BE : AV_CODEC_ID_PCM_U16LE;
            case 3:  return be ? AV_CODEC_ID_PCM_U24BE : AV_CODEC_ID_PCM_U24LE;
            case 4:  return be ? AV_CODEC_ID_PCM_U32BE : AV_CODEC_ID_PCM_U32LE;
            default: return AV_CODEC_ID_NONE;
            }
        }
    }
}

unsigned int av_codec_get_tag(const AVCodecTag * const *tags, enum AVCodecID id)
{
    unsigned int tag;
    if (!av_codec_get_tag2(tags, id, &tag))
        return 0;
    return tag;
}

int av_codec_get_tag2(const AVCodecTag * const *tags, enum AVCodecID id,
                      unsigned int *tag)
{
    int i;
    for(i=0; tags && tags[i]; i++){
        const AVCodecTag *codec_tags = tags[i];
        while (codec_tags->id != AV_CODEC_ID_NONE) {
            if (codec_tags->id == id) {
                *tag = codec_tags->tag;
                return 1;
            }
            codec_tags++;
        }
    }
    return 0;
}

enum AVCodecID av_codec_get_id(const AVCodecTag * const *tags, unsigned int tag)
{
    int i;
    for(i=0; tags && tags[i]; i++){
        enum AVCodecID id= ff_codec_get_id(tags[i], tag);
        if(id!=AV_CODEC_ID_NONE) return id;
    }
    return AV_CODEC_ID_NONE;
}

static void compute_chapters_end(AVFormatContext *s)
{
    unsigned int i, j;
    int64_t max_time = s->duration + ((s->start_time == AV_NOPTS_VALUE) ? 0 : s->start_time);

    for (i = 0; i < s->nb_chapters; i++)
        if (s->chapters[i]->end == AV_NOPTS_VALUE) {
            AVChapter *ch = s->chapters[i];
            int64_t   end = max_time ? av_rescale_q(max_time, AV_TIME_BASE_Q, ch->time_base)
                                     : INT64_MAX;

            for (j = 0; j < s->nb_chapters; j++) {
                AVChapter *ch1 = s->chapters[j];
                int64_t next_start = av_rescale_q(ch1->start, ch1->time_base, ch->time_base);
                if (j != i && next_start > ch->start && next_start < end)
                    end = next_start;
            }
            ch->end = (end == INT64_MAX) ? ch->start : end;
        }
}

static int get_std_framerate(int i){
    if(i<60*12) return (i+1)*1001;
    else        return ((const int[]){24,30,60,12,15,48})[i-60*12]*1000*12;
}

/*
 * Is the time base unreliable.
 * This is a heuristic to balance between quick acceptance of the values in
 * the headers vs. some extra checks.
 * Old DivX and Xvid often have nonsense timebases like 1fps or 2fps.
 * MPEG-2 commonly misuses field repeat flags to store different framerates.
 * And there are "variable" fps files this needs to detect as well.
 */
static int tb_unreliable(AVCodecContext *c){
    if(   c->time_base.den >= 101L*c->time_base.num
       || c->time_base.den <    5L*c->time_base.num
/*       || c->codec_tag == AV_RL32("DIVX")
       || c->codec_tag == AV_RL32("XVID")*/
       || c->codec_tag == AV_RL32("mp4v")
       || c->codec_id == AV_CODEC_ID_MPEG2VIDEO
       || c->codec_id == AV_CODEC_ID_H264
       )
        return 1;
    return 0;
}

#if FF_API_FORMAT_PARAMETERS
int av_find_stream_info(AVFormatContext *ic)
{
    return avformat_find_stream_info(ic, NULL);
}
#endif

int ff_alloc_extradata(AVCodecContext *avctx, int size)
{
    int ret;

    if (size < 0 || size >= INT32_MAX - FF_INPUT_BUFFER_PADDING_SIZE) {
        avctx->extradata_size = 0;
        return AVERROR(EINVAL);
    }
    avctx->extradata = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (avctx->extradata) {
        memset(avctx->extradata + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
        avctx->extradata_size = size;
        ret = 0;
    } else {
        avctx->extradata_size = 0;
        ret = AVERROR(ENOMEM);
    }
    return ret;
}

int avformat_find_stream_info(AVFormatContext *ic, AVDictionary **options)
{
    int i, count, ret = 0, j;
    int64_t read_size;
    AVStream *st;
    AVPacket pkt1, *pkt;
    int64_t old_offset = avio_tell(ic->pb);
    int orig_nb_streams = ic->nb_streams;        // new streams might appear, no options for those
    int flush_codecs = ic->probesize > 0;

    if(ic->pb)
        av_log(ic, AV_LOG_DEBUG, "File position before avformat_find_stream_info() is %"PRId64"\n", avio_tell(ic->pb));

    for(i=0;i<ic->nb_streams;i++) {
        const AVCodec *codec;
        AVDictionary *thread_opt = NULL;
        st = ic->streams[i];

        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO ||
            st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
/*            if(!st->time_base.num)
                st->time_base= */
            if(!st->codec->time_base.num)
                st->codec->time_base= st->time_base;
        }
        //only for the split stuff
        if (!st->parser && !(ic->flags & AVFMT_FLAG_NOPARSE)) {
            st->parser = av_parser_init(st->codec->codec_id);
            if(st->parser){
                if(st->need_parsing == AVSTREAM_PARSE_HEADERS){
                    st->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
                } else if(st->need_parsing == AVSTREAM_PARSE_FULL_RAW) {
                    st->parser->flags |= PARSER_FLAG_USE_CODEC_TS;
                }
            } else if (st->need_parsing) {
                av_log(ic, AV_LOG_VERBOSE, "parser not found for codec "
                       "%s, packets or times may be invalid.\n",
                       avcodec_get_name(st->codec->codec_id));
            }
        }
        codec = find_decoder(ic, st, st->codec->codec_id);

        /* force thread count to 1 since the h264 decoder will not extract SPS
         *  and PPS to extradata during multi-threaded decoding */
        av_dict_set(options ? &options[i] : &thread_opt, "threads", "1", 0);

        /* Ensure that subtitle_header is properly set. */
        if (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE
            && codec && !st->codec->codec)
            avcodec_open2(st->codec, codec, options ? &options[i]
                              : &thread_opt);

        //try to just open decoders, in case this is enough to get parameters
        if (!has_codec_parameters(st, NULL) && st->request_probe <= 0) {
            if (codec && !st->codec->codec)
                avcodec_open2(st->codec, codec, options ? &options[i]
                              : &thread_opt);
        }
        if (!options)
            av_dict_free(&thread_opt);
    }

    for (i=0; i<ic->nb_streams; i++) {
#if FF_API_R_FRAME_RATE
        ic->streams[i]->info->last_dts = AV_NOPTS_VALUE;
#endif
        ic->streams[i]->info->fps_first_dts = AV_NOPTS_VALUE;
        ic->streams[i]->info->fps_last_dts  = AV_NOPTS_VALUE;
    }

    count = 0;
    read_size = 0;
    for(;;) {
        if (ff_check_interrupt(&ic->interrupt_callback)){
            ret= AVERROR_EXIT;
            av_log(ic, AV_LOG_DEBUG, "interrupted\n");
            break;
        }

        /* check if one codec still needs to be handled */
        for(i=0;i<ic->nb_streams;i++) {
            int fps_analyze_framecount = 20;

            st = ic->streams[i];
            if (!has_codec_parameters(st, NULL))
                break;
            /* if the timebase is coarse (like the usual millisecond precision
               of mkv), we need to analyze more frames to reliably arrive at
               the correct fps */
            if (av_q2d(st->time_base) > 0.0005)
                fps_analyze_framecount *= 2;
            if (ic->fps_probe_size >= 0)
                fps_analyze_framecount = ic->fps_probe_size;
            if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
                fps_analyze_framecount = 0;
            /* variable fps and no guess at the real fps */
            if(   tb_unreliable(st->codec) && !(st->r_frame_rate.num && st->avg_frame_rate.num)
               && st->info->duration_count < fps_analyze_framecount
               && st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
                break;
            if(st->parser && st->parser->parser->split && !st->codec->extradata)
                break;
            if (st->first_dts == AV_NOPTS_VALUE &&
                (st->codec->codec_type == AVMEDIA_TYPE_VIDEO ||
                 st->codec->codec_type == AVMEDIA_TYPE_AUDIO))
                break;
        }
        if (i == ic->nb_streams) {
            /* NOTE: if the format has no header, then we need to read
               some packets to get most of the streams, so we cannot
               stop here */
            if (!(ic->ctx_flags & AVFMTCTX_NOHEADER)) {
                /* if we found the info for all the codecs, we can stop */
                ret = count;
                av_log(ic, AV_LOG_DEBUG, "All info found\n");
                flush_codecs = 0;
                break;
            }
        }
        /* we did not get all the codec info, but we read too much data */
        if (read_size >= ic->probesize) {
            ret = count;
            av_log(ic, AV_LOG_DEBUG, "Probe buffer size limit of %d bytes reached\n", ic->probesize);
            for (i = 0; i < ic->nb_streams; i++)
                if (!ic->streams[i]->r_frame_rate.num &&
                    ic->streams[i]->info->duration_count <= 1 &&
                    strcmp(ic->iformat->name, "image2"))
                    av_log(ic, AV_LOG_WARNING,
                           "Stream #%d: not enough frames to estimate rate; "
                           "consider increasing probesize\n", i);
            break;
        }

        /* NOTE: a new stream can be added there if no header in file
           (AVFMTCTX_NOHEADER) */
        ret = read_frame_internal(ic, &pkt1);
        if (ret == AVERROR(EAGAIN))
            continue;

        if (ret < 0) {
            /* EOF or error*/
            break;
        }

        if (ic->flags & AVFMT_FLAG_NOBUFFER)
            free_packet_buffer(&ic->packet_buffer, &ic->packet_buffer_end);
        {
            pkt = add_to_pktbuf(&ic->packet_buffer, &pkt1,
                                &ic->packet_buffer_end);
            if (!pkt) {
                ret = AVERROR(ENOMEM);
                goto find_stream_info_err;
            }
            if ((ret = av_dup_packet(pkt)) < 0)
                goto find_stream_info_err;
        }

        st = ic->streams[pkt->stream_index];
        if (!(st->disposition & AV_DISPOSITION_ATTACHED_PIC))
            read_size += pkt->size;

        if (pkt->dts != AV_NOPTS_VALUE && st->codec_info_nb_frames > 1) {
            /* check for non-increasing dts */
            if (st->info->fps_last_dts != AV_NOPTS_VALUE &&
                st->info->fps_last_dts >= pkt->dts) {
                av_log(ic, AV_LOG_DEBUG, "Non-increasing DTS in stream %d: "
                       "packet %d with DTS %"PRId64", packet %d with DTS "
                       "%"PRId64"\n", st->index, st->info->fps_last_dts_idx,
                       st->info->fps_last_dts, st->codec_info_nb_frames, pkt->dts);
                st->info->fps_first_dts = st->info->fps_last_dts = AV_NOPTS_VALUE;
            }
            /* check for a discontinuity in dts - if the difference in dts
             * is more than 1000 times the average packet duration in the sequence,
             * we treat it as a discontinuity */
            if (st->info->fps_last_dts != AV_NOPTS_VALUE &&
                st->info->fps_last_dts_idx > st->info->fps_first_dts_idx &&
                (pkt->dts - st->info->fps_last_dts) / 1000 >
                (st->info->fps_last_dts - st->info->fps_first_dts) / (st->info->fps_last_dts_idx - st->info->fps_first_dts_idx)) {
                av_log(ic, AV_LOG_WARNING, "DTS discontinuity in stream %d: "
                       "packet %d with DTS %"PRId64", packet %d with DTS "
                       "%"PRId64"\n", st->index, st->info->fps_last_dts_idx,
                       st->info->fps_last_dts, st->codec_info_nb_frames, pkt->dts);
                st->info->fps_first_dts = st->info->fps_last_dts = AV_NOPTS_VALUE;
            }

            /* update stored dts values */
            if (st->info->fps_first_dts == AV_NOPTS_VALUE) {
                st->info->fps_first_dts     = pkt->dts;
                st->info->fps_first_dts_idx = st->codec_info_nb_frames;
            }
            st->info->fps_last_dts = pkt->dts;
            st->info->fps_last_dts_idx = st->codec_info_nb_frames;
        }
        if (st->codec_info_nb_frames>1) {
            int64_t t=0;
            if (st->time_base.den > 0)
                t = av_rescale_q(st->info->codec_info_duration, st->time_base, AV_TIME_BASE_Q);
            if (st->avg_frame_rate.num > 0)
                t = FFMAX(t, av_rescale_q(st->codec_info_nb_frames, av_inv_q(st->avg_frame_rate), AV_TIME_BASE_Q));

            if (   t==0
                && st->codec_info_nb_frames>30
                && st->info->fps_first_dts != AV_NOPTS_VALUE
                && st->info->fps_last_dts  != AV_NOPTS_VALUE)
                t = FFMAX(t, av_rescale_q(st->info->fps_last_dts - st->info->fps_first_dts, st->time_base, AV_TIME_BASE_Q));

            if (t >= ic->max_analyze_duration) {
                av_log(ic, AV_LOG_VERBOSE, "max_analyze_duration %d reached at %"PRId64" microseconds\n", ic->max_analyze_duration, t);
                break;
            }
            if (pkt->duration) {
                st->info->codec_info_duration        += pkt->duration;
                st->info->codec_info_duration_fields += st->parser && st->need_parsing && st->codec->ticks_per_frame==2 ? st->parser->repeat_pict + 1 : 2;
            }
        }
#if FF_API_R_FRAME_RATE
        {
            int64_t last = st->info->last_dts;

            if(   pkt->dts != AV_NOPTS_VALUE && last != AV_NOPTS_VALUE && pkt->dts > last
               && pkt->dts - (uint64_t)last < INT64_MAX){
                double dts= (is_relative(pkt->dts) ?  pkt->dts - RELATIVE_TS_BASE : pkt->dts) * av_q2d(st->time_base);
                int64_t duration= pkt->dts - last;

                if (!st->info->duration_error)
                    st->info->duration_error = av_mallocz(sizeof(st->info->duration_error[0])*2);
                if (!st->info->duration_error)
                    return AVERROR(ENOMEM);

//                 if(st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
//                     av_log(NULL, AV_LOG_ERROR, "%f\n", dts);
                for (i=0; i<MAX_STD_TIMEBASES; i++) {
                    int framerate= get_std_framerate(i);
                    double sdts= dts*framerate/(1001*12);
                    for(j=0; j<2; j++){
                        int64_t ticks= llrint(sdts+j*0.5);
                        double error= sdts - ticks + j*0.5;
                        st->info->duration_error[j][0][i] += error;
                        st->info->duration_error[j][1][i] += error*error;
                    }
                }
                st->info->duration_count++;
                // ignore the first 4 values, they might have some random jitter
                if (st->info->duration_count > 3 && is_relative(pkt->dts) == is_relative(last))
                    st->info->duration_gcd = av_gcd(st->info->duration_gcd, duration);
            }
            if (pkt->dts != AV_NOPTS_VALUE)
                st->info->last_dts = pkt->dts;
        }
#endif
        if(st->parser && st->parser->parser->split && !st->codec->extradata){
            int i= st->parser->parser->split(st->codec, pkt->data, pkt->size);
            if (i > 0 && i < FF_MAX_EXTRADATA_SIZE) {
                if (ff_alloc_extradata(st->codec, i))
                    return AVERROR(ENOMEM);
                memcpy(st->codec->extradata, pkt->data, st->codec->extradata_size);
            }
        }

        /* if still no information, we try to open the codec and to
           decompress the frame. We try to avoid that in most cases as
           it takes longer and uses more memory. For MPEG-4, we need to
           decompress for QuickTime.

           If CODEC_CAP_CHANNEL_CONF is set this will force decoding of at
           least one frame of codec data, this makes sure the codec initializes
           the channel configuration and does not only trust the values from the container.
        */
        try_decode_frame(ic, st, pkt, (options && i < orig_nb_streams ) ? &options[i] : NULL);

        st->codec_info_nb_frames++;
        count++;
    }

    if (flush_codecs) {
        AVPacket empty_pkt = { 0 };
        int err = 0;
        av_init_packet(&empty_pkt);

        for(i=0;i<ic->nb_streams;i++) {

            st = ic->streams[i];

            /* flush the decoders */
            if (st->info->found_decoder == 1) {
                do {
                    err = try_decode_frame(ic, st, &empty_pkt,
                                            (options && i < orig_nb_streams) ?
                                            &options[i] : NULL);
                } while (err > 0 && !has_codec_parameters(st, NULL));

                if (err < 0) {
                    av_log(ic, AV_LOG_INFO,
                        "decoding for stream %d failed\n", st->index);
                }
            }
        }
    }

    // close codecs which were opened in try_decode_frame()
    for(i=0;i<ic->nb_streams;i++) {
        st = ic->streams[i];
        avcodec_close(st->codec);
    }
    for(i=0;i<ic->nb_streams;i++) {
        st = ic->streams[i];
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            if(st->codec->codec_id == AV_CODEC_ID_RAWVIDEO && !st->codec->codec_tag && !st->codec->bits_per_coded_sample){
                uint32_t tag= avcodec_pix_fmt_to_codec_tag(st->codec->pix_fmt);
                if (avpriv_find_pix_fmt(ff_raw_pix_fmt_tags, tag) == st->codec->pix_fmt)
                    st->codec->codec_tag= tag;
            }

            /* estimate average framerate if not set by demuxer */
            if (st->info->codec_info_duration_fields && !st->avg_frame_rate.num && st->info->codec_info_duration) {
                int      best_fps = 0;
                double best_error = 0.01;

                if (st->info->codec_info_duration        >= INT64_MAX / st->time_base.num / 2||
                    st->info->codec_info_duration_fields >= INT64_MAX / st->time_base.den ||
                    st->info->codec_info_duration        < 0)
                    continue;
                av_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
                          st->info->codec_info_duration_fields*(int64_t)st->time_base.den,
                          st->info->codec_info_duration*2*(int64_t)st->time_base.num, 60000);

                /* round guessed framerate to a "standard" framerate if it's
                 * within 1% of the original estimate*/
                for (j = 1; j < MAX_STD_TIMEBASES; j++) {
                    AVRational std_fps = { get_std_framerate(j), 12*1001 };
                    double error = fabs(av_q2d(st->avg_frame_rate) / av_q2d(std_fps) - 1);

                    if (error < best_error) {
                        best_error = error;
                        best_fps   = std_fps.num;
                    }
                }
                if (best_fps) {
                    av_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
                              best_fps, 12*1001, INT_MAX);
                }
            }
            // the check for tb_unreliable() is not completely correct, since this is not about handling
            // a unreliable/inexact time base, but a time base that is finer than necessary, as e.g.
            // ipmovie.c produces.
            if (tb_unreliable(st->codec) && st->info->duration_count > 15 && st->info->duration_gcd > FFMAX(1, st->time_base.den/(500LL*st->time_base.num)) && !st->r_frame_rate.num)
                av_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den, st->time_base.den, st->time_base.num * st->info->duration_gcd, INT_MAX);
            if (st->info->duration_count>1 && !st->r_frame_rate.num
                && tb_unreliable(st->codec)) {
                int num = 0;
                double best_error= 0.01;

                for (j=0; j<MAX_STD_TIMEBASES; j++) {
                    int k;

                    if(st->info->codec_info_duration && st->info->codec_info_duration*av_q2d(st->time_base) < (1001*12.0)/get_std_framerate(j))
                        continue;
                    if(!st->info->codec_info_duration && 1.0 < (1001*12.0)/get_std_framerate(j))
                        continue;
                    for(k=0; k<2; k++){
                        int n= st->info->duration_count;
                        double a= st->info->duration_error[k][0][j] / n;
                        double error= st->info->duration_error[k][1][j]/n - a*a;

                        if(error < best_error && best_error> 0.000000001){
                            best_error= error;
                            num = get_std_framerate(j);
                        }
                        if(error < 0.02)
                            av_log(NULL, AV_LOG_DEBUG, "rfps: %f %f\n", get_std_framerate(j) / 12.0/1001, error);
                    }
                }
                // do not increase frame rate by more than 1 % in order to match a standard rate.
                if (num && (!st->r_frame_rate.num || (double)num/(12*1001) < 1.01 * av_q2d(st->r_frame_rate)))
                    av_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den, num, 12*1001, INT_MAX);
            }

            if (!st->r_frame_rate.num){
                if(    st->codec->time_base.den * (int64_t)st->time_base.num
                    <= st->codec->time_base.num * st->codec->ticks_per_frame * (int64_t)st->time_base.den){
                    st->r_frame_rate.num = st->codec->time_base.den;
                    st->r_frame_rate.den = st->codec->time_base.num * st->codec->ticks_per_frame;
                }else{
                    st->r_frame_rate.num = st->time_base.den;
                    st->r_frame_rate.den = st->time_base.num;
                }
            }
        }else if(st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            if(!st->codec->bits_per_coded_sample)
                st->codec->bits_per_coded_sample= av_get_bits_per_sample(st->codec->codec_id);
            // set stream disposition based on audio service type
            switch (st->codec->audio_service_type) {
            case AV_AUDIO_SERVICE_TYPE_EFFECTS:
                st->disposition = AV_DISPOSITION_CLEAN_EFFECTS;    break;
            case AV_AUDIO_SERVICE_TYPE_VISUALLY_IMPAIRED:
                st->disposition = AV_DISPOSITION_VISUAL_IMPAIRED;  break;
            case AV_AUDIO_SERVICE_TYPE_HEARING_IMPAIRED:
                st->disposition = AV_DISPOSITION_HEARING_IMPAIRED; break;
            case AV_AUDIO_SERVICE_TYPE_COMMENTARY:
                st->disposition = AV_DISPOSITION_COMMENT;          break;
            case AV_AUDIO_SERVICE_TYPE_KARAOKE:
                st->disposition = AV_DISPOSITION_KARAOKE;          break;
            }
        }
    }

    if(ic->probesize)
    estimate_timings(ic, old_offset);

    if (ret >= 0 && ic->nb_streams)
        ret = -1; /* we could not have all the codec parameters before EOF */
    for(i=0;i<ic->nb_streams;i++) {
        const char *errmsg;
        st = ic->streams[i];
        if (!has_codec_parameters(st, &errmsg)) {
            char buf[256];
            avcodec_string(buf, sizeof(buf), st->codec, 0);
            av_log(ic, AV_LOG_WARNING,
                   "Could not find codec parameters for stream %d (%s): %s\n"
                   "Consider increasing the value for the 'analyzeduration' and 'probesize' options\n",
                   i, buf, errmsg);
        } else {
            ret = 0;
        }
    }

    compute_chapters_end(ic);

 find_stream_info_err:
    for (i=0; i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (ic->streams[i]->codec && ic->streams[i]->codec->codec_type != AVMEDIA_TYPE_AUDIO)
            ic->streams[i]->codec->thread_count = 0;
        if (st->info)
            av_freep(&st->info->duration_error);
        av_freep(&ic->streams[i]->info);
    }
    if(ic->pb)
        av_log(ic, AV_LOG_DEBUG, "File position after avformat_find_stream_info() is %"PRId64"\n", avio_tell(ic->pb));
    return ret;
}

AVProgram *av_find_program_from_stream(AVFormatContext *ic, AVProgram *last, int s)
{
    int i, j;

    for (i = 0; i < ic->nb_programs; i++) {
        if (ic->programs[i] == last) {
            last = NULL;
        } else {
            if (!last)
                for (j = 0; j < ic->programs[i]->nb_stream_indexes; j++)
                    if (ic->programs[i]->stream_index[j] == s)
                        return ic->programs[i];
        }
    }
    return NULL;
}

int av_find_best_stream(AVFormatContext *ic,
                        enum AVMediaType type,
                        int wanted_stream_nb,
                        int related_stream,
                        AVCodec **decoder_ret,
                        int flags)
{
    int i, nb_streams = ic->nb_streams;
    int ret = AVERROR_STREAM_NOT_FOUND, best_count = -1, best_bitrate = -1, best_multiframe = -1, count, bitrate, multiframe;
    unsigned *program = NULL;
    AVCodec *decoder = NULL, *best_decoder = NULL;

    if (related_stream >= 0 && wanted_stream_nb < 0) {
        AVProgram *p = av_find_program_from_stream(ic, NULL, related_stream);
        if (p) {
            program = p->stream_index;
            nb_streams = p->nb_stream_indexes;
        }
    }
    for (i = 0; i < nb_streams; i++) {
        int real_stream_index = program ? program[i] : i;
        AVStream *st = ic->streams[real_stream_index];
        AVCodecContext *avctx = st->codec;
        if (avctx->codec_type != type)
            continue;
        if (wanted_stream_nb >= 0 && real_stream_index != wanted_stream_nb)
            continue;
        if (st->disposition & (AV_DISPOSITION_HEARING_IMPAIRED|AV_DISPOSITION_VISUAL_IMPAIRED))
            continue;
        if (decoder_ret) {
            decoder = find_decoder(ic, st, st->codec->codec_id);
            if (!decoder) {
                if (ret < 0)
                    ret = AVERROR_DECODER_NOT_FOUND;
                continue;
            }
        }
        count = st->codec_info_nb_frames;
        bitrate = avctx->bit_rate;
        multiframe = FFMIN(5, count);
        if ((best_multiframe >  multiframe) ||
            (best_multiframe == multiframe && best_bitrate >  bitrate) ||
            (best_multiframe == multiframe && best_bitrate == bitrate && best_count >= count))
            continue;
        best_count = count;
        best_bitrate = bitrate;
        best_multiframe = multiframe;
        ret = real_stream_index;
        best_decoder = decoder;
        if (program && i == nb_streams - 1 && ret < 0) {
            program = NULL;
            nb_streams = ic->nb_streams;
            i = 0; /* no related stream found, try again with everything */
        }
    }
    if (decoder_ret)
        *decoder_ret = best_decoder;
    return ret;
}

/*******************************************************/

int av_read_play(AVFormatContext *s)
{
    if (s->iformat->read_play)
        return s->iformat->read_play(s);
    if (s->pb)
        return avio_pause(s->pb, 0);
    return AVERROR(ENOSYS);
}

int av_read_pause(AVFormatContext *s)
{
    if (s->iformat->read_pause)
        return s->iformat->read_pause(s);
    if (s->pb)
        return avio_pause(s->pb, 1);
    return AVERROR(ENOSYS);
}

void ff_free_stream(AVFormatContext *s, AVStream *st){
    av_assert0(s->nb_streams>0);
    av_assert0(s->streams[ s->nb_streams-1 ] == st);

    if (st->parser) {
        av_parser_close(st->parser);
    }
    if (st->attached_pic.data)
        av_free_packet(&st->attached_pic);
    av_dict_free(&st->metadata);
    av_freep(&st->probe_data.buf);
    av_freep(&st->index_entries);
    av_freep(&st->codec->extradata);
    av_freep(&st->codec->subtitle_header);
    av_freep(&st->codec);
    av_freep(&st->priv_data);
    if (st->info)
        av_freep(&st->info->duration_error);
    av_freep(&st->info);
    av_freep(&s->streams[ --s->nb_streams ]);
}

void avformat_free_context(AVFormatContext *s)
{
    int i;

    if (!s)
        return;

    av_opt_free(s);
    if (s->iformat && s->iformat->priv_class && s->priv_data)
        av_opt_free(s->priv_data);

    for(i=s->nb_streams-1; i>=0; i--) {
        ff_free_stream(s, s->streams[i]);
    }
    for(i=s->nb_programs-1; i>=0; i--) {
        av_dict_free(&s->programs[i]->metadata);
        av_freep(&s->programs[i]->stream_index);
        av_freep(&s->programs[i]);
    }
    av_freep(&s->programs);
    av_freep(&s->priv_data);
    while(s->nb_chapters--) {
        av_dict_free(&s->chapters[s->nb_chapters]->metadata);
        av_freep(&s->chapters[s->nb_chapters]);
    }
    av_freep(&s->chapters);
    av_dict_free(&s->metadata);
    av_freep(&s->streams);
    av_free(s);
}

#if FF_API_CLOSE_INPUT_FILE
void av_close_input_file(AVFormatContext *s)
{
    avformat_close_input(&s);
}
#endif

void avformat_close_input(AVFormatContext **ps)
{
    AVFormatContext *s;
    AVIOContext *pb;

    if (!ps || !*ps)
        return;

    s = *ps;
    pb = s->pb;

    if ((s->iformat && s->iformat->flags & AVFMT_NOFILE) ||
        (s->flags & AVFMT_FLAG_CUSTOM_IO))
        pb = NULL;

    flush_packet_queue(s);

    if (s->iformat) {
        if (s->iformat->read_close)
            s->iformat->read_close(s);
    }

    avformat_free_context(s);

    *ps = NULL;

    avio_close(pb);
}

#if FF_API_NEW_STREAM
AVStream *av_new_stream(AVFormatContext *s, int id)
{
    AVStream *st = avformat_new_stream(s, NULL);
    if (st)
        st->id = id;
    return st;
}
#endif

AVStream *avformat_new_stream(AVFormatContext *s, const AVCodec *c)
{
    AVStream *st;
    int i;
    AVStream **streams;

    if (s->nb_streams >= INT_MAX/sizeof(*streams))
        return NULL;
    streams = av_realloc_array(s->streams, s->nb_streams + 1, sizeof(*streams));
    if (!streams)
        return NULL;
    s->streams = streams;

    st = av_mallocz(sizeof(AVStream));
    if (!st)
        return NULL;
    if (!(st->info = av_mallocz(sizeof(*st->info)))) {
        av_free(st);
        return NULL;
    }
    st->info->last_dts = AV_NOPTS_VALUE;

    st->codec = avcodec_alloc_context3(c);
    if (s->iformat) {
        /* no default bitrate if decoding */
        st->codec->bit_rate = 0;
    }
    st->index = s->nb_streams;
    st->start_time = AV_NOPTS_VALUE;
    st->duration = AV_NOPTS_VALUE;
        /* we set the current DTS to 0 so that formats without any timestamps
           but durations get some timestamps, formats with some unknown
           timestamps have their first few packets buffered and the
           timestamps corrected before they are returned to the user */
    st->cur_dts = s->iformat ? RELATIVE_TS_BASE : 0;
    st->first_dts = AV_NOPTS_VALUE;
    st->probe_packets = MAX_PROBE_PACKETS;
    st->pts_wrap_reference = AV_NOPTS_VALUE;
    st->pts_wrap_behavior = AV_PTS_WRAP_IGNORE;

    /* default pts setting is MPEG-like */
    avpriv_set_pts_info(st, 33, 1, 90000);
    st->last_IP_pts = AV_NOPTS_VALUE;
    for(i=0; i<MAX_REORDER_DELAY+1; i++)
        st->pts_buffer[i]= AV_NOPTS_VALUE;
    st->reference_dts = AV_NOPTS_VALUE;

    st->sample_aspect_ratio = (AVRational){0,1};

#if FF_API_R_FRAME_RATE
    st->info->last_dts      = AV_NOPTS_VALUE;
#endif
    st->info->fps_first_dts = AV_NOPTS_VALUE;
    st->info->fps_last_dts  = AV_NOPTS_VALUE;

    s->streams[s->nb_streams++] = st;
    return st;
}

AVProgram *av_new_program(AVFormatContext *ac, int id)
{
    AVProgram *program=NULL;
    int i;

    av_dlog(ac, "new_program: id=0x%04x\n", id);

    for(i=0; i<ac->nb_programs; i++)
        if(ac->programs[i]->id == id)
            program = ac->programs[i];

    if(!program){
        program = av_mallocz(sizeof(AVProgram));
        if (!program)
            return NULL;
        dynarray_add(&ac->programs, &ac->nb_programs, program);
        program->discard = AVDISCARD_NONE;
    }
    program->id = id;
    program->pts_wrap_reference = AV_NOPTS_VALUE;
    program->pts_wrap_behavior = AV_PTS_WRAP_IGNORE;

    program->start_time =
    program->end_time   = AV_NOPTS_VALUE;

    return program;
}

AVChapter *avpriv_new_chapter(AVFormatContext *s, int id, AVRational time_base, int64_t start, int64_t end, const char *title)
{
    AVChapter *chapter = NULL;
    int i;

    for(i=0; i<s->nb_chapters; i++)
        if(s->chapters[i]->id == id)
            chapter = s->chapters[i];

    if(!chapter){
        chapter= av_mallocz(sizeof(AVChapter));
        if(!chapter)
            return NULL;
        dynarray_add(&s->chapters, &s->nb_chapters, chapter);
    }
    av_dict_set(&chapter->metadata, "title", title, 0);
    chapter->id    = id;
    chapter->time_base= time_base;
    chapter->start = start;
    chapter->end   = end;

    return chapter;
}

void ff_program_add_stream_index(AVFormatContext *ac, int progid, unsigned int idx)
{
    int i, j;
    AVProgram *program=NULL;
    void *tmp;

    if (idx >= ac->nb_streams) {
        av_log(ac, AV_LOG_ERROR, "stream index %d is not valid\n", idx);
        return;
    }

    for(i=0; i<ac->nb_programs; i++){
        if(ac->programs[i]->id != progid)
            continue;
        program = ac->programs[i];
        for(j=0; j<program->nb_stream_indexes; j++)
            if(program->stream_index[j] == idx)
                return;

        tmp = av_realloc_array(program->stream_index, program->nb_stream_indexes+1, sizeof(unsigned int));
        if(!tmp)
            return;
        program->stream_index = tmp;
        program->stream_index[program->nb_stream_indexes++] = idx;
        return;
    }
}

static void print_fps(double d, const char *postfix){
    uint64_t v= lrintf(d*100);
    if     (v% 100      ) av_log(NULL, AV_LOG_INFO, ", %3.2f %s", d, postfix);
    else if(v%(100*1000)) av_log(NULL, AV_LOG_INFO, ", %1.0f %s", d, postfix);
    else                  av_log(NULL, AV_LOG_INFO, ", %1.0fk %s", d/1000, postfix);
}

static void dump_metadata(void *ctx, AVDictionary *m, const char *indent)
{
    if(m && !(av_dict_count(m) == 1 && av_dict_get(m, "language", NULL, 0))){
        AVDictionaryEntry *tag=NULL;

        av_log(ctx, AV_LOG_INFO, "%sMetadata:\n", indent);
        while((tag=av_dict_get(m, "", tag, AV_DICT_IGNORE_SUFFIX))) {
            if(strcmp("language", tag->key)){
                const char *p = tag->value;
                av_log(ctx, AV_LOG_INFO, "%s  %-16s: ", indent, tag->key);
                while(*p) {
                    char tmp[256];
                    size_t len = strcspn(p, "\x8\xa\xb\xc\xd");
                    av_strlcpy(tmp, p, FFMIN(sizeof(tmp), len+1));
                    av_log(ctx, AV_LOG_INFO, "%s", tmp);
                    p += len;
                    if (*p == 0xd) av_log(ctx, AV_LOG_INFO, " ");
                    if (*p == 0xa) av_log(ctx, AV_LOG_INFO, "\n%s  %-16s: ", indent, "");
                    if (*p) p++;
                }
                av_log(ctx, AV_LOG_INFO, "\n");
            }
        }
    }
}

/* "user interface" functions */
static void dump_stream_format(AVFormatContext *ic, int i, int index, int is_output)
{
    char buf[256];
    int flags = (is_output ? ic->oformat->flags : ic->iformat->flags);
    AVStream *st = ic->streams[i];
    int g = av_gcd(st->time_base.num, st->time_base.den);
    AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL, 0);
    avcodec_string(buf, sizeof(buf), st->codec, is_output);
    av_log(NULL, AV_LOG_INFO, "    Stream #%d:%d", index, i);
    /* the pid is an important information, so we display it */
    /* XXX: add a generic system */
    if (flags & AVFMT_SHOW_IDS)
        av_log(NULL, AV_LOG_INFO, "[0x%x]", st->id);
    if (lang)
        av_log(NULL, AV_LOG_INFO, "(%s)", lang->value);
    av_log(NULL, AV_LOG_DEBUG, ", %d, %d/%d", st->codec_info_nb_frames, st->time_base.num/g, st->time_base.den/g);
    av_log(NULL, AV_LOG_INFO, ": %s", buf);
    if (st->sample_aspect_ratio.num && // default
        av_cmp_q(st->sample_aspect_ratio, st->codec->sample_aspect_ratio)) {
        AVRational display_aspect_ratio;
        av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                  st->codec->width*st->sample_aspect_ratio.num,
                  st->codec->height*st->sample_aspect_ratio.den,
                  1024*1024);
        av_log(NULL, AV_LOG_INFO, ", SAR %d:%d DAR %d:%d",
                 st->sample_aspect_ratio.num, st->sample_aspect_ratio.den,
                 display_aspect_ratio.num, display_aspect_ratio.den);
    }
    if(st->codec->codec_type == AVMEDIA_TYPE_VIDEO){
        if(st->avg_frame_rate.den && st->avg_frame_rate.num)
            print_fps(av_q2d(st->avg_frame_rate), "fps");
#if FF_API_R_FRAME_RATE
        if(st->r_frame_rate.den && st->r_frame_rate.num)
            print_fps(av_q2d(st->r_frame_rate), "tbr");
#endif
        if(st->time_base.den && st->time_base.num)
            print_fps(1/av_q2d(st->time_base), "tbn");
        if(st->codec->time_base.den && st->codec->time_base.num)
            print_fps(1/av_q2d(st->codec->time_base), "tbc");
    }
    if (st->disposition & AV_DISPOSITION_DEFAULT)
        av_log(NULL, AV_LOG_INFO, " (default)");
    if (st->disposition & AV_DISPOSITION_DUB)
        av_log(NULL, AV_LOG_INFO, " (dub)");
    if (st->disposition & AV_DISPOSITION_ORIGINAL)
        av_log(NULL, AV_LOG_INFO, " (original)");
    if (st->disposition & AV_DISPOSITION_COMMENT)
        av_log(NULL, AV_LOG_INFO, " (comment)");
    if (st->disposition & AV_DISPOSITION_LYRICS)
        av_log(NULL, AV_LOG_INFO, " (lyrics)");
    if (st->disposition & AV_DISPOSITION_KARAOKE)
        av_log(NULL, AV_LOG_INFO, " (karaoke)");
    if (st->disposition & AV_DISPOSITION_FORCED)
        av_log(NULL, AV_LOG_INFO, " (forced)");
    if (st->disposition & AV_DISPOSITION_HEARING_IMPAIRED)
        av_log(NULL, AV_LOG_INFO, " (hearing impaired)");
    if (st->disposition & AV_DISPOSITION_VISUAL_IMPAIRED)
        av_log(NULL, AV_LOG_INFO, " (visual impaired)");
    if (st->disposition & AV_DISPOSITION_CLEAN_EFFECTS)
        av_log(NULL, AV_LOG_INFO, " (clean effects)");
    av_log(NULL, AV_LOG_INFO, "\n");
    dump_metadata(NULL, st->metadata, "    ");
}

void av_dump_format(AVFormatContext *ic,
                    int index,
                    const char *url,
                    int is_output)
{
    int i;
    uint8_t *printed = ic->nb_streams ? av_mallocz(ic->nb_streams) : NULL;
    if (ic->nb_streams && !printed)
        return;

    av_log(NULL, AV_LOG_INFO, "%s #%d, %s, %s '%s':\n",
            is_output ? "Output" : "Input",
            index,
            is_output ? ic->oformat->name : ic->iformat->name,
            is_output ? "to" : "from", url);
    dump_metadata(NULL, ic->metadata, "  ");
    if (!is_output) {
        av_log(NULL, AV_LOG_INFO, "  Duration: ");
        if (ic->duration != AV_NOPTS_VALUE) {
            int hours, mins, secs, us;
            int64_t duration = ic->duration + 5000;
            secs = duration / AV_TIME_BASE;
            us = duration % AV_TIME_BASE;
            mins = secs / 60;
            secs %= 60;
            hours = mins / 60;
            mins %= 60;
            av_log(NULL, AV_LOG_INFO, "%02d:%02d:%02d.%02d", hours, mins, secs,
                   (100 * us) / AV_TIME_BASE);
        } else {
            av_log(NULL, AV_LOG_INFO, "N/A");
        }
        if (ic->start_time != AV_NOPTS_VALUE) {
            int secs, us;
            av_log(NULL, AV_LOG_INFO, ", start: ");
            secs = ic->start_time / AV_TIME_BASE;
            us = abs(ic->start_time % AV_TIME_BASE);
            av_log(NULL, AV_LOG_INFO, "%d.%06d",
                   secs, (int)av_rescale(us, 1000000, AV_TIME_BASE));
        }
        av_log(NULL, AV_LOG_INFO, ", bitrate: ");
        if (ic->bit_rate) {
            av_log(NULL, AV_LOG_INFO,"%d kb/s", ic->bit_rate / 1000);
        } else {
            av_log(NULL, AV_LOG_INFO, "N/A");
        }
        av_log(NULL, AV_LOG_INFO, "\n");
    }
    for (i = 0; i < ic->nb_chapters; i++) {
        AVChapter *ch = ic->chapters[i];
        av_log(NULL, AV_LOG_INFO, "    Chapter #%d.%d: ", index, i);
        av_log(NULL, AV_LOG_INFO, "start %f, ", ch->start * av_q2d(ch->time_base));
        av_log(NULL, AV_LOG_INFO, "end %f\n",   ch->end   * av_q2d(ch->time_base));

        dump_metadata(NULL, ch->metadata, "    ");
    }
    if(ic->nb_programs) {
        int j, k, total = 0;
        for(j=0; j<ic->nb_programs; j++) {
            AVDictionaryEntry *name = av_dict_get(ic->programs[j]->metadata,
                                                  "name", NULL, 0);
            av_log(NULL, AV_LOG_INFO, "  Program %d %s\n", ic->programs[j]->id,
                   name ? name->value : "");
            dump_metadata(NULL, ic->programs[j]->metadata, "    ");
            for(k=0; k<ic->programs[j]->nb_stream_indexes; k++) {
                dump_stream_format(ic, ic->programs[j]->stream_index[k], index, is_output);
                printed[ic->programs[j]->stream_index[k]] = 1;
            }
            total += ic->programs[j]->nb_stream_indexes;
        }
        if (total < ic->nb_streams)
            av_log(NULL, AV_LOG_INFO, "  No Program\n");
    }
    for(i=0;i<ic->nb_streams;i++)
        if (!printed[i])
            dump_stream_format(ic, i, index, is_output);

    av_free(printed);
}

uint64_t ff_ntp_time(void)
{
  return (av_gettime() / 1000) * 1000 + NTP_OFFSET_US;
}

int av_get_frame_filename(char *buf, int buf_size,
                          const char *path, int number)
{
    const char *p;
    char *q, buf1[20], c;
    int nd, len, percentd_found;

    q = buf;
    p = path;
    percentd_found = 0;
    for(;;) {
        c = *p++;
        if (c == '\0')
            break;
        if (c == '%') {
            do {
                nd = 0;
                while (av_isdigit(*p)) {
                    nd = nd * 10 + *p++ - '0';
                }
                c = *p++;
            } while (av_isdigit(c));

            switch(c) {
            case '%':
                goto addchar;
            case 'd':
                if (percentd_found)
                    goto fail;
                percentd_found = 1;
                snprintf(buf1, sizeof(buf1), "%0*d", nd, number);
                len = strlen(buf1);
                if ((q - buf + len) > buf_size - 1)
                    goto fail;
                memcpy(q, buf1, len);
                q += len;
                break;
            default:
                goto fail;
            }
        } else {
        addchar:
            if ((q - buf) < buf_size - 1)
                *q++ = c;
        }
    }
    if (!percentd_found)
        goto fail;
    *q = '\0';
    return 0;
 fail:
    *q = '\0';
    return -1;
}

static void hex_dump_internal(void *avcl, FILE *f, int level,
                              const uint8_t *buf, int size)
{
    int len, i, j, c;
#define PRINT(...) do { if (!f) av_log(avcl, level, __VA_ARGS__); else fprintf(f, __VA_ARGS__); } while(0)

    for(i=0;i<size;i+=16) {
        len = size - i;
        if (len > 16)
            len = 16;
        PRINT("%08x ", i);
        for(j=0;j<16;j++) {
            if (j < len)
                PRINT(" %02x", buf[i+j]);
            else
                PRINT("   ");
        }
        PRINT(" ");
        for(j=0;j<len;j++) {
            c = buf[i+j];
            if (c < ' ' || c > '~')
                c = '.';
            PRINT("%c", c);
        }
        PRINT("\n");
    }
#undef PRINT
}

void av_hex_dump(FILE *f, const uint8_t *buf, int size)
{
    hex_dump_internal(NULL, f, 0, buf, size);
}

void av_hex_dump_log(void *avcl, int level, const uint8_t *buf, int size)
{
    hex_dump_internal(avcl, NULL, level, buf, size);
}

static void pkt_dump_internal(void *avcl, FILE *f, int level, AVPacket *pkt, int dump_payload, AVRational time_base)
{
#define PRINT(...) do { if (!f) av_log(avcl, level, __VA_ARGS__); else fprintf(f, __VA_ARGS__); } while(0)
    PRINT("stream #%d:\n", pkt->stream_index);
    PRINT("  keyframe=%d\n", ((pkt->flags & AV_PKT_FLAG_KEY) != 0));
    PRINT("  duration=%0.3f\n", pkt->duration * av_q2d(time_base));
    /* DTS is _always_ valid after av_read_frame() */
    PRINT("  dts=");
    if (pkt->dts == AV_NOPTS_VALUE)
        PRINT("N/A");
    else
        PRINT("%0.3f", pkt->dts * av_q2d(time_base));
    /* PTS may not be known if B-frames are present. */
    PRINT("  pts=");
    if (pkt->pts == AV_NOPTS_VALUE)
        PRINT("N/A");
    else
        PRINT("%0.3f", pkt->pts * av_q2d(time_base));
    PRINT("\n");
    PRINT("  size=%d\n", pkt->size);
#undef PRINT
    if (dump_payload)
        av_hex_dump(f, pkt->data, pkt->size);
}

void av_pkt_dump2(FILE *f, AVPacket *pkt, int dump_payload, AVStream *st)
{
    pkt_dump_internal(NULL, f, 0, pkt, dump_payload, st->time_base);
}

void av_pkt_dump_log2(void *avcl, int level, AVPacket *pkt, int dump_payload,
                      AVStream *st)
{
    pkt_dump_internal(avcl, NULL, level, pkt, dump_payload, st->time_base);
}

void av_url_split(char *proto, int proto_size,
                  char *authorization, int authorization_size,
                  char *hostname, int hostname_size,
                  int *port_ptr,
                  char *path, int path_size,
                  const char *url)
{
    const char *p, *ls, *ls2, *at, *at2, *col, *brk;

    if (port_ptr)               *port_ptr = -1;
    if (proto_size > 0)         proto[0] = 0;
    if (authorization_size > 0) authorization[0] = 0;
    if (hostname_size > 0)      hostname[0] = 0;
    if (path_size > 0)          path[0] = 0;

    /* parse protocol */
    if ((p = strchr(url, ':'))) {
        av_strlcpy(proto, url, FFMIN(proto_size, p + 1 - url));
        p++; /* skip ':' */
        if (*p == '/') p++;
        if (*p == '/') p++;
    } else {
        /* no protocol means plain filename */
        av_strlcpy(path, url, path_size);
        return;
    }

    /* separate path from hostname */
    ls = strchr(p, '/');
    ls2 = strchr(p, '?');
    if(!ls)
        ls = ls2;
    else if (ls && ls2)
        ls = FFMIN(ls, ls2);
    if(ls)
        av_strlcpy(path, ls, path_size);
    else
        ls = &p[strlen(p)]; // XXX

    /* the rest is hostname, use that to parse auth/port */
    if (ls != p) {
        /* authorization (user[:pass]@hostname) */
        at2 = p;
        while ((at = strchr(p, '@')) && at < ls) {
            av_strlcpy(authorization, at2,
                       FFMIN(authorization_size, at + 1 - at2));
            p = at + 1; /* skip '@' */
        }

        if (*p == '[' && (brk = strchr(p, ']')) && brk < ls) {
            /* [host]:port */
            av_strlcpy(hostname, p + 1,
                       FFMIN(hostname_size, brk - p));
            if (brk[1] == ':' && port_ptr)
                *port_ptr = atoi(brk + 2);
        } else if ((col = strchr(p, ':')) && col < ls) {
            av_strlcpy(hostname, p,
                       FFMIN(col + 1 - p, hostname_size));
            if (port_ptr) *port_ptr = atoi(col + 1);
        } else
            av_strlcpy(hostname, p,
                       FFMIN(ls + 1 - p, hostname_size));
    }
}

char *ff_data_to_hex(char *buff, const uint8_t *src, int s, int lowercase)
{
    int i;
    static const char hex_table_uc[16] = { '0', '1', '2', '3',
                                           '4', '5', '6', '7',
                                           '8', '9', 'A', 'B',
                                           'C', 'D', 'E', 'F' };
    static const char hex_table_lc[16] = { '0', '1', '2', '3',
                                           '4', '5', '6', '7',
                                           '8', '9', 'a', 'b',
                                           'c', 'd', 'e', 'f' };
    const char *hex_table = lowercase ? hex_table_lc : hex_table_uc;

    for(i = 0; i < s; i++) {
        buff[i * 2]     = hex_table[src[i] >> 4];
        buff[i * 2 + 1] = hex_table[src[i] & 0xF];
    }

    return buff;
}

int ff_hex_to_data(uint8_t *data, const char *p)
{
    int c, len, v;

    len = 0;
    v = 1;
    for (;;) {
        p += strspn(p, SPACE_CHARS);
        if (*p == '\0')
            break;
        c = av_toupper((unsigned char) *p++);
        if (c >= '0' && c <= '9')
            c = c - '0';
        else if (c >= 'A' && c <= 'F')
            c = c - 'A' + 10;
        else
            break;
        v = (v << 4) | c;
        if (v & 0x100) {
            if (data)
                data[len] = v;
            len++;
            v = 1;
        }
    }
    return len;
}

#if FF_API_SET_PTS_INFO
void av_set_pts_info(AVStream *s, int pts_wrap_bits,
                     unsigned int pts_num, unsigned int pts_den)
{
    avpriv_set_pts_info(s, pts_wrap_bits, pts_num, pts_den);
}
#endif

void avpriv_set_pts_info(AVStream *s, int pts_wrap_bits,
                         unsigned int pts_num, unsigned int pts_den)
{
    AVRational new_tb;
    if(av_reduce(&new_tb.num, &new_tb.den, pts_num, pts_den, INT_MAX)){
        if(new_tb.num != pts_num)
            av_log(NULL, AV_LOG_DEBUG, "st:%d removing common factor %d from timebase\n", s->index, pts_num/new_tb.num);
    }else
        av_log(NULL, AV_LOG_WARNING, "st:%d has too large timebase, reducing\n", s->index);

    if(new_tb.num <= 0 || new_tb.den <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Ignoring attempt to set invalid timebase %d/%d for st:%d\n", new_tb.num, new_tb.den, s->index);
        return;
    }
    s->time_base = new_tb;
    av_codec_set_pkt_timebase(s->codec, new_tb);
    s->pts_wrap_bits = pts_wrap_bits;
}

void ff_parse_key_value(const char *str, ff_parse_key_val_cb callback_get_buf,
                        void *context)
{
    const char *ptr = str;

    /* Parse key=value pairs. */
    for (;;) {
        const char *key;
        char *dest = NULL, *dest_end;
        int key_len, dest_len = 0;

        /* Skip whitespace and potential commas. */
        while (*ptr && (av_isspace(*ptr) || *ptr == ','))
            ptr++;
        if (!*ptr)
            break;

        key = ptr;

        if (!(ptr = strchr(key, '=')))
            break;
        ptr++;
        key_len = ptr - key;

        callback_get_buf(context, key, key_len, &dest, &dest_len);
        dest_end = dest + dest_len - 1;

        if (*ptr == '\"') {
            ptr++;
            while (*ptr && *ptr != '\"') {
                if (*ptr == '\\') {
                    if (!ptr[1])
                        break;
                    if (dest && dest < dest_end)
                        *dest++ = ptr[1];
                    ptr += 2;
                } else {
                    if (dest && dest < dest_end)
                        *dest++ = *ptr;
                    ptr++;
                }
            }
            if (*ptr == '\"')
                ptr++;
        } else {
            for (; *ptr && !(av_isspace(*ptr) || *ptr == ','); ptr++)
                if (dest && dest < dest_end)
                    *dest++ = *ptr;
        }
        if (dest)
            *dest = 0;
    }
}

int ff_find_stream_index(AVFormatContext *s, int id)
{
    int i;
    for (i = 0; i < s->nb_streams; i++) {
        if (s->streams[i]->id == id)
            return i;
    }
    return -1;
}

int64_t ff_iso8601_to_unix_time(const char *datestr)
{
    struct tm time1 = {0}, time2 = {0};
    char *ret1, *ret2;
    ret1 = av_small_strptime(datestr, "%Y - %m - %d %H:%M:%S", &time1);
    ret2 = av_small_strptime(datestr, "%Y - %m - %dT%H:%M:%S", &time2);
    if (ret2 && !ret1)
        return av_timegm(&time2);
    else
        return av_timegm(&time1);
}

int avformat_query_codec(AVOutputFormat *ofmt, enum AVCodecID codec_id, int std_compliance)
{
    if (ofmt) {
        if (ofmt->query_codec)
            return ofmt->query_codec(codec_id, std_compliance);
        else if (ofmt->codec_tag)
            return !!av_codec_get_tag(ofmt->codec_tag, codec_id);
        else if (codec_id == ofmt->video_codec || codec_id == ofmt->audio_codec ||
                 codec_id == ofmt->subtitle_codec)
            return 1;
    }
    return AVERROR_PATCHWELCOME;
}

int avformat_network_init(void)
{
#if CONFIG_NETWORK
    int ret;
    ff_network_inited_globally = 1;
    if ((ret = ff_network_init()) < 0)
        return ret;
    ff_tls_init();
#endif
    return 0;
}

int avformat_network_deinit(void)
{
#if CONFIG_NETWORK
    ff_network_close();
    ff_tls_deinit();
#endif
    return 0;
}

int ff_add_param_change(AVPacket *pkt, int32_t channels,
                        uint64_t channel_layout, int32_t sample_rate,
                        int32_t width, int32_t height)
{
    uint32_t flags = 0;
    int size = 4;
    uint8_t *data;
    if (!pkt)
        return AVERROR(EINVAL);
    if (channels) {
        size += 4;
        flags |= AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_COUNT;
    }
    if (channel_layout) {
        size += 8;
        flags |= AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_LAYOUT;
    }
    if (sample_rate) {
        size += 4;
        flags |= AV_SIDE_DATA_PARAM_CHANGE_SAMPLE_RATE;
    }
    if (width || height) {
        size += 8;
        flags |= AV_SIDE_DATA_PARAM_CHANGE_DIMENSIONS;
    }
    data = av_packet_new_side_data(pkt, AV_PKT_DATA_PARAM_CHANGE, size);
    if (!data)
        return AVERROR(ENOMEM);
    bytestream_put_le32(&data, flags);
    if (channels)
        bytestream_put_le32(&data, channels);
    if (channel_layout)
        bytestream_put_le64(&data, channel_layout);
    if (sample_rate)
        bytestream_put_le32(&data, sample_rate);
    if (width || height) {
        bytestream_put_le32(&data, width);
        bytestream_put_le32(&data, height);
    }
    return 0;
}

AVRational av_guess_sample_aspect_ratio(AVFormatContext *format, AVStream *stream, AVFrame *frame)
{
    AVRational undef = {0, 1};
    AVRational stream_sample_aspect_ratio = stream ? stream->sample_aspect_ratio : undef;
    AVRational codec_sample_aspect_ratio  = stream && stream->codec ? stream->codec->sample_aspect_ratio : undef;
    AVRational frame_sample_aspect_ratio  = frame  ? frame->sample_aspect_ratio  : codec_sample_aspect_ratio;

    av_reduce(&stream_sample_aspect_ratio.num, &stream_sample_aspect_ratio.den,
               stream_sample_aspect_ratio.num,  stream_sample_aspect_ratio.den, INT_MAX);
    if (stream_sample_aspect_ratio.num <= 0 || stream_sample_aspect_ratio.den <= 0)
        stream_sample_aspect_ratio = undef;

    av_reduce(&frame_sample_aspect_ratio.num, &frame_sample_aspect_ratio.den,
               frame_sample_aspect_ratio.num,  frame_sample_aspect_ratio.den, INT_MAX);
    if (frame_sample_aspect_ratio.num <= 0 || frame_sample_aspect_ratio.den <= 0)
        frame_sample_aspect_ratio = undef;

    if (stream_sample_aspect_ratio.num)
        return stream_sample_aspect_ratio;
    else
        return frame_sample_aspect_ratio;
}

AVRational av_guess_frame_rate(AVFormatContext *format, AVStream *st, AVFrame *frame)
{
    AVRational fr = st->r_frame_rate;

    if (st->codec->ticks_per_frame > 1) {
        AVRational codec_fr = av_inv_q(st->codec->time_base);
        AVRational   avg_fr = st->avg_frame_rate;
        codec_fr.den *= st->codec->ticks_per_frame;
        if (   codec_fr.num > 0 && codec_fr.den > 0 && av_q2d(codec_fr) < av_q2d(fr)*0.7
            && fabs(1.0 - av_q2d(av_div_q(avg_fr, fr))) > 0.1)
            fr = codec_fr;
    }

    return fr;
}

int avformat_match_stream_specifier(AVFormatContext *s, AVStream *st,
                                    const char *spec)
{
    if (*spec <= '9' && *spec >= '0') /* opt:index */
        return strtol(spec, NULL, 0) == st->index;
    else if (*spec == 'v' || *spec == 'a' || *spec == 's' || *spec == 'd' ||
             *spec == 't') { /* opt:[vasdt] */
        enum AVMediaType type;

        switch (*spec++) {
        case 'v': type = AVMEDIA_TYPE_VIDEO;      break;
        case 'a': type = AVMEDIA_TYPE_AUDIO;      break;
        case 's': type = AVMEDIA_TYPE_SUBTITLE;   break;
        case 'd': type = AVMEDIA_TYPE_DATA;       break;
        case 't': type = AVMEDIA_TYPE_ATTACHMENT; break;
        default:  av_assert0(0);
        }
        if (type != st->codec->codec_type)
            return 0;
        if (*spec++ == ':') { /* possibly followed by :index */
            int i, index = strtol(spec, NULL, 0);
            for (i = 0; i < s->nb_streams; i++)
                if (s->streams[i]->codec->codec_type == type && index-- == 0)
                   return i == st->index;
            return 0;
        }
        return 1;
    } else if (*spec == 'p' && *(spec + 1) == ':') {
        int prog_id, i, j;
        char *endptr;
        spec += 2;
        prog_id = strtol(spec, &endptr, 0);
        for (i = 0; i < s->nb_programs; i++) {
            if (s->programs[i]->id != prog_id)
                continue;

            if (*endptr++ == ':') {
                int stream_idx = strtol(endptr, NULL, 0);
                return stream_idx >= 0 &&
                    stream_idx < s->programs[i]->nb_stream_indexes &&
                    st->index == s->programs[i]->stream_index[stream_idx];
            }

            for (j = 0; j < s->programs[i]->nb_stream_indexes; j++)
                if (st->index == s->programs[i]->stream_index[j])
                    return 1;
        }
        return 0;
    } else if (*spec == '#') {
        int sid;
        char *endptr;
        sid = strtol(spec + 1, &endptr, 0);
        if (!*endptr)
            return st->id == sid;
    } else if (!*spec) /* empty specifier, matches everything */
        return 1;

    av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
    return AVERROR(EINVAL);
}

void ff_generate_avci_extradata(AVStream *st)
{
    static const uint8_t avci100_1080p_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x7a, 0x10, 0x29,
        0xb6, 0xd4, 0x20, 0x22, 0x33, 0x19, 0xc6, 0x63,
        0x23, 0x21, 0x01, 0x11, 0x98, 0xce, 0x33, 0x19,
        0x18, 0x21, 0x02, 0x56, 0xb9, 0x3d, 0x7d, 0x7e,
        0x4f, 0xe3, 0x3f, 0x11, 0xf1, 0x9e, 0x08, 0xb8,
        0x8c, 0x54, 0x43, 0xc0, 0x78, 0x02, 0x27, 0xe2,
        0x70, 0x1e, 0x30, 0x10, 0x10, 0x14, 0x00, 0x00,
        0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xca,
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x33, 0x48,
        0xd0
    };
    static const uint8_t avci100_1080i_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x7a, 0x10, 0x29,
        0xb6, 0xd4, 0x20, 0x22, 0x33, 0x19, 0xc6, 0x63,
        0x23, 0x21, 0x01, 0x11, 0x98, 0xce, 0x33, 0x19,
        0x18, 0x21, 0x03, 0x3a, 0x46, 0x65, 0x6a, 0x65,
        0x24, 0xad, 0xe9, 0x12, 0x32, 0x14, 0x1a, 0x26,
        0x34, 0xad, 0xa4, 0x41, 0x82, 0x23, 0x01, 0x50,
        0x2b, 0x1a, 0x24, 0x69, 0x48, 0x30, 0x40, 0x2e,
        0x11, 0x12, 0x08, 0xc6, 0x8c, 0x04, 0x41, 0x28,
        0x4c, 0x34, 0xf0, 0x1e, 0x01, 0x13, 0xf2, 0xe0,
        0x3c, 0x60, 0x20, 0x20, 0x28, 0x00, 0x00, 0x03,
        0x00, 0x08, 0x00, 0x00, 0x03, 0x01, 0x94, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x33, 0x48,
        0xd0
    };
    static const uint8_t avci50_1080i_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x6e, 0x10, 0x28,
        0xa6, 0xd4, 0x20, 0x32, 0x33, 0x0c, 0x71, 0x18,
        0x88, 0x62, 0x10, 0x19, 0x19, 0x86, 0x38, 0x8c,
        0x44, 0x30, 0x21, 0x02, 0x56, 0x4e, 0x6e, 0x61,
        0x87, 0x3e, 0x73, 0x4d, 0x98, 0x0c, 0x03, 0x06,
        0x9c, 0x0b, 0x73, 0xe6, 0xc0, 0xb5, 0x18, 0x63,
        0x0d, 0x39, 0xe0, 0x5b, 0x02, 0xd4, 0xc6, 0x19,
        0x1a, 0x79, 0x8c, 0x32, 0x34, 0x24, 0xf0, 0x16,
        0x81, 0x13, 0xf7, 0xff, 0x80, 0x02, 0x00, 0x01,
        0xf1, 0x80, 0x80, 0x80, 0xa0, 0x00, 0x00, 0x03,
        0x00, 0x20, 0x00, 0x00, 0x06, 0x50, 0x80, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x31, 0x12,
        0x11
    };
    static const uint8_t avci100_720p_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x7a, 0x10, 0x29,
        0xb6, 0xd4, 0x20, 0x2a, 0x33, 0x1d, 0xc7, 0x62,
        0xa1, 0x08, 0x40, 0x54, 0x66, 0x3b, 0x8e, 0xc5,
        0x42, 0x02, 0x10, 0x25, 0x64, 0x2c, 0x89, 0xe8,
        0x85, 0xe4, 0x21, 0x4b, 0x90, 0x83, 0x06, 0x95,
        0xd1, 0x06, 0x46, 0x97, 0x20, 0xc8, 0xd7, 0x43,
        0x08, 0x11, 0xc2, 0x1e, 0x4c, 0x91, 0x0f, 0x01,
        0x40, 0x16, 0xec, 0x07, 0x8c, 0x04, 0x04, 0x05,
        0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03,
        0x00, 0x64, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x31, 0x12,
        0x11
    };
    int size = 0;
    const uint8_t *data = 0;
    if (st->codec->width == 1920) {
        if (st->codec->field_order == AV_FIELD_PROGRESSIVE) {
            data = avci100_1080p_extradata;
            size = sizeof(avci100_1080p_extradata);
        } else {
            data = avci100_1080i_extradata;
            size = sizeof(avci100_1080i_extradata);
        }
    } else if (st->codec->width == 1440) {
        data = avci50_1080i_extradata;
        size = sizeof(avci50_1080i_extradata);
    } else if (st->codec->width == 1280) {
        data = avci100_720p_extradata;
        size = sizeof(avci100_720p_extradata);
    }
    if (!size)
        return;
    av_freep(&st->codec->extradata);
    if (ff_alloc_extradata(st->codec, size))
        return;
    memcpy(st->codec->extradata, data, size);
}
