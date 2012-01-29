/*
 * various utility functions for use within Libav
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* #define DEBUG */

#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "libavcodec/internal.h"
#include "libavcodec/bytestream.h"
#include "libavutil/opt.h"
#include "libavutil/dict.h"
#include "libavutil/pixdesc.h"
#include "metadata.h"
#include "id3v2.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "riff.h"
#include "audiointerleave.h"
#include "url.h"
#include <sys/time.h>
#include <time.h>
#include <stdarg.h>
#if CONFIG_NETWORK
#include "network.h"
#endif

#undef NDEBUG
#include <assert.h>

/**
 * @file
 * various utility functions for use within Libav
 */

unsigned avformat_version(void)
{
    return LIBAVFORMAT_VERSION_INT;
}

const char *avformat_configuration(void)
{
    return LIBAV_CONFIGURATION;
}

const char *avformat_license(void)
{
#define LICENSE_PREFIX "libavformat license: "
    return LICENSE_PREFIX LIBAV_LICENSE + sizeof(LICENSE_PREFIX) - 1;
}

/* fraction handling */

/**
 * f = val + (num / den) + 0.5.
 *
 * 'num' is normalized so that it is such as 0 <= num < den.
 *
 * @param f fractional number
 * @param val integer value
 * @param num must be >= 0
 * @param den must be >= 1
 */
static void frac_init(AVFrac *f, int64_t val, int64_t num, int64_t den)
{
    num += (den >> 1);
    if (num >= den) {
        val += num / den;
        num = num % den;
    }
    f->val = val;
    f->num = num;
    f->den = den;
}

/**
 * Fractional addition to f: f = f + (incr / f->den).
 *
 * @param f fractional number
 * @param incr increment, can be positive or negative
 */
static void frac_add(AVFrac *f, int64_t incr)
{
    int64_t num, den;

    num = f->num + incr;
    den = f->den;
    if (num < 0) {
        f->val += num / den;
        num = num % den;
        if (num < 0) {
            num += den;
            f->val--;
        }
    } else if (num >= den) {
        f->val += num / den;
        num = num % den;
    }
    f->num = num;
}

/** head of registered input format linked list */
static AVInputFormat *first_iformat = NULL;
/** head of registered output format linked list */
static AVOutputFormat *first_oformat = NULL;

AVInputFormat  *av_iformat_next(AVInputFormat  *f)
{
    if(f) return f->next;
    else  return first_iformat;
}

AVOutputFormat *av_oformat_next(AVOutputFormat *f)
{
    if(f) return f->next;
    else  return first_oformat;
}

void av_register_input_format(AVInputFormat *format)
{
    AVInputFormat **p;
    p = &first_iformat;
    while (*p != NULL) p = &(*p)->next;
    *p = format;
    format->next = NULL;
}

void av_register_output_format(AVOutputFormat *format)
{
    AVOutputFormat **p;
    p = &first_oformat;
    while (*p != NULL) p = &(*p)->next;
    *p = format;
    format->next = NULL;
}

int av_match_ext(const char *filename, const char *extensions)
{
    const char *ext, *p;
    char ext1[32], *q;

    if(!filename)
        return 0;

    ext = strrchr(filename, '.');
    if (ext) {
        ext++;
        p = extensions;
        for(;;) {
            q = ext1;
            while (*p != '\0' && *p != ',' && q-ext1<sizeof(ext1)-1)
                *q++ = *p++;
            *q = '\0';
            if (!av_strcasecmp(ext1, ext))
                return 1;
            if (*p == '\0')
                break;
            p++;
        }
    }
    return 0;
}

static int match_format(const char *name, const char *names)
{
    const char *p;
    int len, namelen;

    if (!name || !names)
        return 0;

    namelen = strlen(name);
    while ((p = strchr(names, ','))) {
        len = FFMAX(p - names, namelen);
        if (!av_strncasecmp(name, names, len))
            return 1;
        names = p+1;
    }
    return !av_strcasecmp(name, names);
}

AVOutputFormat *av_guess_format(const char *short_name, const char *filename,
                                const char *mime_type)
{
    AVOutputFormat *fmt = NULL, *fmt_found;
    int score_max, score;

    /* specific test for image sequences */
#if CONFIG_IMAGE2_MUXER
    if (!short_name && filename &&
        av_filename_number_test(filename) &&
        ff_guess_image2_codec(filename) != CODEC_ID_NONE) {
        return av_guess_format("image2", NULL, NULL);
    }
#endif
    /* Find the proper file type. */
    fmt_found = NULL;
    score_max = 0;
    while ((fmt = av_oformat_next(fmt))) {
        score = 0;
        if (fmt->name && short_name && !strcmp(fmt->name, short_name))
            score += 100;
        if (fmt->mime_type && mime_type && !strcmp(fmt->mime_type, mime_type))
            score += 10;
        if (filename && fmt->extensions &&
            av_match_ext(filename, fmt->extensions)) {
            score += 5;
        }
        if (score > score_max) {
            score_max = score;
            fmt_found = fmt;
        }
    }
    return fmt_found;
}

enum CodecID av_guess_codec(AVOutputFormat *fmt, const char *short_name,
                            const char *filename, const char *mime_type, enum AVMediaType type){
    if(type == AVMEDIA_TYPE_VIDEO){
        enum CodecID codec_id= CODEC_ID_NONE;

#if CONFIG_IMAGE2_MUXER
        if(!strcmp(fmt->name, "image2") || !strcmp(fmt->name, "image2pipe")){
            codec_id= ff_guess_image2_codec(filename);
        }
#endif
        if(codec_id == CODEC_ID_NONE)
            codec_id= fmt->video_codec;
        return codec_id;
    }else if(type == AVMEDIA_TYPE_AUDIO)
        return fmt->audio_codec;
    else if (type == AVMEDIA_TYPE_SUBTITLE)
        return fmt->subtitle_codec;
    else
        return CODEC_ID_NONE;
}

AVInputFormat *av_find_input_format(const char *short_name)
{
    AVInputFormat *fmt = NULL;
    while ((fmt = av_iformat_next(fmt))) {
        if (match_format(short_name, fmt->name))
            return fmt;
    }
    return NULL;
}


int av_get_packet(AVIOContext *s, AVPacket *pkt, int size)
{
    int ret= av_new_packet(pkt, size);

    if(ret<0)
        return ret;

    pkt->pos= avio_tell(s);

    ret= avio_read(s, pkt->data, size);
    if(ret<=0)
        av_free_packet(pkt);
    else
        av_shrink_packet(pkt, ret);

    return ret;
}

int av_append_packet(AVIOContext *s, AVPacket *pkt, int size)
{
    int ret;
    int old_size;
    if (!pkt->size)
        return av_get_packet(s, pkt, size);
    old_size = pkt->size;
    ret = av_grow_packet(pkt, size);
    if (ret < 0)
        return ret;
    ret = avio_read(s, pkt->data + old_size, size);
    av_shrink_packet(pkt, old_size + FFMAX(ret, 0));
    return ret;
}


int av_filename_number_test(const char *filename)
{
    char buf[1024];
    return filename && (av_get_frame_filename(buf, sizeof(buf), filename, 1)>=0);
}

AVInputFormat *av_probe_input_format2(AVProbeData *pd, int is_opened, int *score_max)
{
    AVProbeData lpd = *pd;
    AVInputFormat *fmt1 = NULL, *fmt;
    int score, id3 = 0;

    if (lpd.buf_size > 10 && ff_id3v2_match(lpd.buf, ID3v2_DEFAULT_MAGIC)) {
        int id3len = ff_id3v2_tag_len(lpd.buf);
        if (lpd.buf_size > id3len + 16) {
            lpd.buf += id3len;
            lpd.buf_size -= id3len;
        }
        id3 = 1;
    }

    fmt = NULL;
    while ((fmt1 = av_iformat_next(fmt1))) {
        if (!is_opened == !(fmt1->flags & AVFMT_NOFILE))
            continue;
        score = 0;
        if (fmt1->read_probe) {
            score = fmt1->read_probe(&lpd);
        } else if (fmt1->extensions) {
            if (av_match_ext(lpd.filename, fmt1->extensions)) {
                score = 50;
            }
        }
        if (score > *score_max) {
            *score_max = score;
            fmt = fmt1;
        }else if (score == *score_max)
            fmt = NULL;
    }

    /* a hack for files with huge id3v2 tags -- try to guess by file extension. */
    if (!fmt && is_opened && *score_max < AVPROBE_SCORE_MAX/4) {
        while ((fmt = av_iformat_next(fmt)))
            if (fmt->extensions && av_match_ext(lpd.filename, fmt->extensions)) {
                *score_max = AVPROBE_SCORE_MAX/4;
                break;
            }
    }

    if (!fmt && id3 && *score_max < AVPROBE_SCORE_MAX/4-1) {
        while ((fmt = av_iformat_next(fmt)))
            if (fmt->extensions && av_match_ext("mp3", fmt->extensions)) {
                *score_max = AVPROBE_SCORE_MAX/4-1;
                break;
            }
    }

    return fmt;
}

AVInputFormat *av_probe_input_format(AVProbeData *pd, int is_opened){
    int score=0;
    return av_probe_input_format2(pd, is_opened, &score);
}

static int set_codec_from_probe_data(AVFormatContext *s, AVStream *st, AVProbeData *pd, int score)
{
    static const struct {
        const char *name; enum CodecID id; enum AVMediaType type;
    } fmt_id_type[] = {
        { "aac"      , CODEC_ID_AAC       , AVMEDIA_TYPE_AUDIO },
        { "ac3"      , CODEC_ID_AC3       , AVMEDIA_TYPE_AUDIO },
        { "dts"      , CODEC_ID_DTS       , AVMEDIA_TYPE_AUDIO },
        { "eac3"     , CODEC_ID_EAC3      , AVMEDIA_TYPE_AUDIO },
        { "h264"     , CODEC_ID_H264      , AVMEDIA_TYPE_VIDEO },
        { "m4v"      , CODEC_ID_MPEG4     , AVMEDIA_TYPE_VIDEO },
        { "mp3"      , CODEC_ID_MP3       , AVMEDIA_TYPE_AUDIO },
        { "mpegvideo", CODEC_ID_MPEG2VIDEO, AVMEDIA_TYPE_VIDEO },
        { 0 }
    };
    AVInputFormat *fmt = av_probe_input_format2(pd, 1, &score);

    if (fmt) {
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
    return !!fmt;
}

/************************************************************/
/* input media file */

/** size of probe buffer, for guessing file type from file contents */
#define PROBE_BUF_MIN 2048
#define PROBE_BUF_MAX (1<<20)

int av_probe_input_buffer(AVIOContext *pb, AVInputFormat **fmt,
                          const char *filename, void *logctx,
                          unsigned int offset, unsigned int max_probe_size)
{
    AVProbeData pd = { filename ? filename : "", NULL, -offset };
    unsigned char *buf = NULL;
    int ret = 0, probe_size;

    if (!max_probe_size) {
        max_probe_size = PROBE_BUF_MAX;
    } else if (max_probe_size > PROBE_BUF_MAX) {
        max_probe_size = PROBE_BUF_MAX;
    } else if (max_probe_size < PROBE_BUF_MIN) {
        return AVERROR(EINVAL);
    }

    if (offset >= max_probe_size) {
        return AVERROR(EINVAL);
    }

    for(probe_size= PROBE_BUF_MIN; probe_size<=max_probe_size && !*fmt;
        probe_size = FFMIN(probe_size<<1, FFMAX(max_probe_size, probe_size+1))) {
        int score = probe_size < max_probe_size ? AVPROBE_SCORE_MAX/4 : 0;
        int buf_offset = (probe_size == PROBE_BUF_MIN) ? 0 : probe_size>>1;

        if (probe_size < offset) {
            continue;
        }

        /* read probe data */
        buf = av_realloc(buf, probe_size + AVPROBE_PADDING_SIZE);
        if ((ret = avio_read(pb, buf + buf_offset, probe_size - buf_offset)) < 0) {
            /* fail if error was not end of file, otherwise, lower score */
            if (ret != AVERROR_EOF) {
                av_free(buf);
                return ret;
            }
            score = 0;
            ret = 0;            /* error was end of file, nothing read */
        }
        pd.buf_size += ret;
        pd.buf = &buf[offset];

        memset(pd.buf + pd.buf_size, 0, AVPROBE_PADDING_SIZE);

        /* guess file format */
        *fmt = av_probe_input_format2(&pd, 1, &score);
        if(*fmt){
            if(score <= AVPROBE_SCORE_MAX/4){ //this can only be true in the last iteration
                av_log(logctx, AV_LOG_WARNING, "Format detected only with low score of %d, misdetection possible!\n", score);
            }else
                av_log(logctx, AV_LOG_DEBUG, "Probed with size=%d and score=%d\n", probe_size, score);
        }
    }

    if (!*fmt) {
        av_free(buf);
        return AVERROR_INVALIDDATA;
    }

    /* rewind. reuse probe buffer to avoid seeking */
    if ((ret = ffio_rewind_with_probe_data(pb, buf, pd.buf_size)) < 0)
        av_free(buf);

    return ret;
}

/* open input file and probe the format if necessary */
static int init_input(AVFormatContext *s, const char *filename, AVDictionary **options)
{
    int ret;
    AVProbeData pd = {filename, NULL, 0};

    if (s->pb) {
        s->flags |= AVFMT_FLAG_CUSTOM_IO;
        if (!s->iformat)
            return av_probe_input_buffer(s->pb, &s->iformat, filename, s, 0, 0);
        else if (s->iformat->flags & AVFMT_NOFILE)
            return AVERROR(EINVAL);
        return 0;
    }

    if ( (s->iformat && s->iformat->flags & AVFMT_NOFILE) ||
        (!s->iformat && (s->iformat = av_probe_input_format(&pd, 0))))
        return 0;

    if ((ret = avio_open2(&s->pb, filename, AVIO_FLAG_READ,
                          &s->interrupt_callback, options)) < 0)
        return ret;
    if (s->iformat)
        return 0;
    return av_probe_input_buffer(s->pb, &s->iformat, filename, s, 0, 0);
}

int avformat_open_input(AVFormatContext **ps, const char *filename, AVInputFormat *fmt, AVDictionary **options)
{
    AVFormatContext *s = *ps;
    int ret = 0;
    AVDictionary *tmp = NULL;

    if (!s && !(s = avformat_alloc_context()))
        return AVERROR(ENOMEM);
    if (fmt)
        s->iformat = fmt;

    if (options)
        av_dict_copy(&tmp, *options, 0);

    if ((ret = av_opt_set_dict(s, &tmp)) < 0)
        goto fail;

    if ((ret = init_input(s, filename, &tmp)) < 0)
        goto fail;

    /* check filename in case an image number is expected */
    if (s->iformat->flags & AVFMT_NEEDNUMBER) {
        if (!av_filename_number_test(filename)) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
    }

    s->duration = s->start_time = AV_NOPTS_VALUE;
    av_strlcpy(s->filename, filename, sizeof(s->filename));

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
        ff_id3v2_read(s, ID3v2_DEFAULT_MAGIC);

    if (s->iformat->read_header)
        if ((ret = s->iformat->read_header(s)) < 0)
            goto fail;

    if (s->pb && !s->data_offset)
        s->data_offset = avio_tell(s->pb);

    s->raw_packet_buffer_remaining_size = RAW_PACKET_BUFFER_SIZE;

    if (options) {
        av_dict_free(options);
        *options = tmp;
    }
    *ps = s;
    return 0;

fail:
    av_dict_free(&tmp);
    if (s->pb && !(s->flags & AVFMT_FLAG_CUSTOM_IO))
        avio_close(s->pb);
    avformat_free_context(s);
    *ps = NULL;
    return ret;
}

/*******************************************************/

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

int av_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, i;
    AVStream *st;

    for(;;){
        AVPacketList *pktl = s->raw_packet_buffer;

        if (pktl) {
            *pkt = pktl->pkt;
            if(s->streams[pkt->stream_index]->codec->codec_id != CODEC_ID_PROBE ||
               !s->streams[pkt->stream_index]->probe_packets ||
               s->raw_packet_buffer_remaining_size < pkt->size){
                AVProbeData *pd = &s->streams[pkt->stream_index]->probe_data;
                av_freep(&pd->buf);
                pd->buf_size = 0;
                s->raw_packet_buffer = pktl->next;
                s->raw_packet_buffer_remaining_size += pkt->size;
                av_free(pktl);
                return 0;
            }
        }

        av_init_packet(pkt);
        ret= s->iformat->read_packet(s, pkt);
        if (ret < 0) {
            if (!pktl || ret == AVERROR(EAGAIN))
                return ret;
            for (i = 0; i < s->nb_streams; i++)
                s->streams[i]->probe_packets = 0;
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

        st= s->streams[pkt->stream_index];

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

        if(!pktl && (st->codec->codec_id != CODEC_ID_PROBE ||
                     !st->probe_packets))
            return ret;

        add_to_pktbuf(&s->raw_packet_buffer, pkt, &s->raw_packet_buffer_end);
        s->raw_packet_buffer_remaining_size -= pkt->size;

        if(st->codec->codec_id == CODEC_ID_PROBE){
            AVProbeData *pd = &st->probe_data;
            av_log(s, AV_LOG_DEBUG, "probing stream %d\n", st->index);
            --st->probe_packets;

            pd->buf = av_realloc(pd->buf, pd->buf_size+pkt->size+AVPROBE_PADDING_SIZE);
            memcpy(pd->buf+pd->buf_size, pkt->data, pkt->size);
            pd->buf_size += pkt->size;
            memset(pd->buf+pd->buf_size, 0, AVPROBE_PADDING_SIZE);

            if(av_log2(pd->buf_size) != av_log2(pd->buf_size - pkt->size)){
                //FIXME we do not reduce score to 0 for the case of running out of buffer space in bytes
                set_codec_from_probe_data(s, st, pd, st->probe_packets > 0 ? AVPROBE_SCORE_MAX/4 : 0);
                if(st->codec->codec_id != CODEC_ID_PROBE){
                    pd->buf_size=0;
                    av_freep(&pd->buf);
                    av_log(s, AV_LOG_DEBUG, "probed stream %d\n", st->index);
                }
            }
        }
    }
}

/**********************************************************/

/**
 * Get the number of samples of an audio frame. Return -1 on error.
 */
static int get_audio_frame_size(AVCodecContext *enc, int size)
{
    int frame_size;

    if(enc->codec_id == CODEC_ID_VORBIS)
        return -1;

    if (enc->frame_size <= 1) {
        int bits_per_sample = av_get_bits_per_sample(enc->codec_id);

        if (bits_per_sample) {
            if (enc->channels == 0)
                return -1;
            frame_size = (size << 3) / (bits_per_sample * enc->channels);
        } else {
            /* used for example by ADPCM codecs */
            if (enc->bit_rate == 0)
                return -1;
            frame_size = ((int64_t)size * 8 * enc->sample_rate) / enc->bit_rate;
        }
    } else {
        frame_size = enc->frame_size;
    }
    return frame_size;
}


/**
 * Return the frame duration in seconds. Return 0 if not available.
 */
static void compute_frame_duration(int *pnum, int *pden, AVStream *st,
                                   AVCodecParserContext *pc, AVPacket *pkt)
{
    int frame_size;

    *pnum = 0;
    *pden = 0;
    switch(st->codec->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (st->r_frame_rate.num) {
            *pnum = st->r_frame_rate.den;
            *pden = st->r_frame_rate.num;
        } else if(st->time_base.num*1000LL > st->time_base.den) {
            *pnum = st->time_base.num;
            *pden = st->time_base.den;
        }else if(st->codec->time_base.num*1000LL > st->codec->time_base.den){
            *pnum = st->codec->time_base.num;
            *pden = st->codec->time_base.den;
            if (pc && pc->repeat_pict) {
                *pnum = (*pnum) * (1 + pc->repeat_pict);
            }
            //If this codec can be interlaced or progressive then we need a parser to compute duration of a packet
            //Thus if we have no parser in such case leave duration undefined.
            if(st->codec->ticks_per_frame>1 && !pc){
                *pnum = *pden = 0;
            }
        }
        break;
    case AVMEDIA_TYPE_AUDIO:
        frame_size = get_audio_frame_size(st->codec, pkt->size);
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
    if(enc->codec_type == AVMEDIA_TYPE_AUDIO){
        return 1;
    }else if(enc->codec_type == AVMEDIA_TYPE_VIDEO){
        switch(enc->codec_id){
        case CODEC_ID_MJPEG:
        case CODEC_ID_MJPEGB:
        case CODEC_ID_LJPEG:
        case CODEC_ID_PRORES:
        case CODEC_ID_RAWVIDEO:
        case CODEC_ID_DVVIDEO:
        case CODEC_ID_HUFFYUV:
        case CODEC_ID_FFVHUFF:
        case CODEC_ID_ASV1:
        case CODEC_ID_ASV2:
        case CODEC_ID_VCR1:
        case CODEC_ID_DNXHD:
        case CODEC_ID_JPEG2000:
            return 1;
        default: break;
        }
    }
    return 0;
}

static void update_initial_timestamps(AVFormatContext *s, int stream_index,
                                      int64_t dts, int64_t pts)
{
    AVStream *st= s->streams[stream_index];
    AVPacketList *pktl= s->packet_buffer;

    if(st->first_dts != AV_NOPTS_VALUE || dts == AV_NOPTS_VALUE || st->cur_dts == AV_NOPTS_VALUE)
        return;

    st->first_dts= dts - st->cur_dts;
    st->cur_dts= dts;

    for(; pktl; pktl= pktl->next){
        if(pktl->pkt.stream_index != stream_index)
            continue;
        //FIXME think more about this check
        if(pktl->pkt.pts != AV_NOPTS_VALUE && pktl->pkt.pts == pktl->pkt.dts)
            pktl->pkt.pts += st->first_dts;

        if(pktl->pkt.dts != AV_NOPTS_VALUE)
            pktl->pkt.dts += st->first_dts;

        if(st->start_time == AV_NOPTS_VALUE && pktl->pkt.pts != AV_NOPTS_VALUE)
            st->start_time= pktl->pkt.pts;
    }
    if (st->start_time == AV_NOPTS_VALUE)
        st->start_time = pts;
}

static void update_initial_durations(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    AVPacketList *pktl= s->packet_buffer;
    int64_t cur_dts= 0;

    if(st->first_dts != AV_NOPTS_VALUE){
        cur_dts= st->first_dts;
        for(; pktl; pktl= pktl->next){
            if(pktl->pkt.stream_index == pkt->stream_index){
                if(pktl->pkt.pts != pktl->pkt.dts || pktl->pkt.dts != AV_NOPTS_VALUE || pktl->pkt.duration)
                    break;
                cur_dts -= pkt->duration;
            }
        }
        pktl= s->packet_buffer;
        st->first_dts = cur_dts;
    }else if(st->cur_dts)
        return;

    for(; pktl; pktl= pktl->next){
        if(pktl->pkt.stream_index != pkt->stream_index)
            continue;
        if(pktl->pkt.pts == pktl->pkt.dts && pktl->pkt.dts == AV_NOPTS_VALUE
           && !pktl->pkt.duration){
            pktl->pkt.dts= cur_dts;
            if(!st->codec->has_b_frames)
                pktl->pkt.pts= cur_dts;
            cur_dts += pkt->duration;
            pktl->pkt.duration= pkt->duration;
        }else
            break;
    }
    if(st->first_dts == AV_NOPTS_VALUE)
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

    if (st->codec->codec_id != CODEC_ID_H264 && pc && pc->pict_type == AV_PICTURE_TYPE_B)
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

    if(pkt->pts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE && pkt->dts > pkt->pts && st->pts_wrap_bits<63
       /*&& pkt->dts-(1LL<<st->pts_wrap_bits) < pkt->pts*/){
        pkt->dts -= 1LL<<st->pts_wrap_bits;
    }

    // some mpeg2 in mpeg-ps lack dts (issue171 / input_file.mpg)
    // we take the conservative approach and discard both
    // Note, if this is misbehaving for a H.264 file then possibly presentation_delayed is not set correctly.
    if(delay==1 && pkt->dts == pkt->pts && pkt->dts != AV_NOPTS_VALUE && presentation_delayed){
        av_log(s, AV_LOG_DEBUG, "invalid dts/pts combination\n");
        pkt->dts= pkt->pts= AV_NOPTS_VALUE;
    }

    if (pkt->duration == 0) {
        compute_frame_duration(&num, &den, st, pc, pkt);
        if (den && num) {
            pkt->duration = av_rescale_rnd(1, num * (int64_t)st->time_base.den, den * (int64_t)st->time_base.num, AV_ROUND_DOWN);

            if(pkt->duration != 0 && s->packet_buffer)
                update_initial_durations(s, st, pkt);
        }
    }

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
                pkt->pts = pkt->dts + pc->pts_dts_delta * num / den;
            } else if (st->reference_dts != AV_NOPTS_VALUE) {
                // compute DTS based on reference timestamp
                pkt->dts = st->reference_dts + pc->dts_ref_dts_delta * num / den;
                pkt->pts = pkt->dts + pc->pts_dts_delta * num / den;
            }
            if (pc->dts_sync_point > 0)
                st->reference_dts = pkt->dts; // new reference
        }
    }

    /* This may be redundant, but it should not hurt. */
    if(pkt->dts != AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE && pkt->pts > pkt->dts)
        presentation_delayed = 1;

//    av_log(NULL, AV_LOG_DEBUG, "IN delayed:%d pts:%"PRId64", dts:%"PRId64" cur_dts:%"PRId64" st:%d pc:%p\n", presentation_delayed, pkt->pts, pkt->dts, st->cur_dts, pkt->stream_index, pc);
    /* interpolate PTS and DTS if they are not present */
    //We skip H264 currently because delay and has_b_frames are not reliably set
    if((delay==0 || (delay==1 && pc)) && st->codec->codec_id != CODEC_ID_H264){
        if (presentation_delayed) {
            /* DTS = decompression timestamp */
            /* PTS = presentation timestamp */
            if (pkt->dts == AV_NOPTS_VALUE)
                pkt->dts = st->last_IP_pts;
            update_initial_timestamps(s, pkt->stream_index, pkt->dts, pkt->pts);
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
        } else if(pkt->pts != AV_NOPTS_VALUE || pkt->dts != AV_NOPTS_VALUE || pkt->duration){
            if(pkt->pts != AV_NOPTS_VALUE && pkt->duration){
                int64_t old_diff= FFABS(st->cur_dts - pkt->duration - pkt->pts);
                int64_t new_diff= FFABS(st->cur_dts - pkt->pts);
                if(old_diff < new_diff && old_diff < (pkt->duration>>3)){
                    pkt->pts += pkt->duration;
    //                av_log(NULL, AV_LOG_DEBUG, "id:%d old:%"PRId64" new:%"PRId64" dur:%d cur:%"PRId64" size:%d\n", pkt->stream_index, old_diff, new_diff, pkt->duration, st->cur_dts, pkt->size);
                }
            }

            /* presentation is not delayed : PTS and DTS are the same */
            if(pkt->pts == AV_NOPTS_VALUE)
                pkt->pts = pkt->dts;
            update_initial_timestamps(s, pkt->stream_index, pkt->pts, pkt->pts);
            if(pkt->pts == AV_NOPTS_VALUE)
                pkt->pts = st->cur_dts;
            pkt->dts = pkt->pts;
            if(pkt->pts != AV_NOPTS_VALUE)
                st->cur_dts = pkt->pts + pkt->duration;
        }
    }

    if(pkt->pts != AV_NOPTS_VALUE && delay <= MAX_REORDER_DELAY){
        st->pts_buffer[0]= pkt->pts;
        for(i=0; i<delay && st->pts_buffer[i] > st->pts_buffer[i+1]; i++)
            FFSWAP(int64_t, st->pts_buffer[i], st->pts_buffer[i+1]);
        if(pkt->dts == AV_NOPTS_VALUE)
            pkt->dts= st->pts_buffer[0];
        if(st->codec->codec_id == CODEC_ID_H264){ // we skipped it above so we try here
            update_initial_timestamps(s, pkt->stream_index, pkt->dts, pkt->pts); // this should happen on the first packet
        }
        if(pkt->dts > st->cur_dts)
            st->cur_dts = pkt->dts;
    }

//    av_log(NULL, AV_LOG_ERROR, "OUTdelayed:%d/%d pts:%"PRId64", dts:%"PRId64" cur_dts:%"PRId64"\n", presentation_delayed, delay, pkt->pts, pkt->dts, st->cur_dts);

    /* update flags */
    if(is_intra_only(st->codec))
        pkt->flags |= AV_PKT_FLAG_KEY;
    else if (pc) {
        pkt->flags = 0;
        /* keyframe computation */
        if (pc->key_frame == 1)
            pkt->flags |= AV_PKT_FLAG_KEY;
        else if (pc->key_frame == -1 && pc->pict_type == AV_PICTURE_TYPE_I)
            pkt->flags |= AV_PKT_FLAG_KEY;
    }
    if (pc)
        pkt->convergence_duration = pc->convergence_duration;
}


static int read_frame_internal(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st;
    int len, ret, i;

    av_init_packet(pkt);

    for(;;) {
        /* select current input stream component */
        st = s->cur_st;
        if (st) {
            if (!st->need_parsing || !st->parser) {
                /* no parsing needed: we just output the packet as is */
                /* raw data support */
                *pkt = st->cur_pkt; st->cur_pkt.data= NULL;
                compute_pkt_fields(s, st, NULL, pkt);
                s->cur_st = NULL;
                if ((s->iformat->flags & AVFMT_GENERIC_INDEX) &&
                    (pkt->flags & AV_PKT_FLAG_KEY) && pkt->dts != AV_NOPTS_VALUE) {
                    ff_reduce_index(s, st->index);
                    av_add_index_entry(st, pkt->pos, pkt->dts, 0, 0, AVINDEX_KEYFRAME);
                }
                break;
            } else if (st->cur_len > 0 && st->discard < AVDISCARD_ALL) {
                len = av_parser_parse2(st->parser, st->codec, &pkt->data, &pkt->size,
                                       st->cur_ptr, st->cur_len,
                                       st->cur_pkt.pts, st->cur_pkt.dts,
                                       st->cur_pkt.pos);
                st->cur_pkt.pts = AV_NOPTS_VALUE;
                st->cur_pkt.dts = AV_NOPTS_VALUE;
                /* increment read pointer */
                st->cur_ptr += len;
                st->cur_len -= len;

                /* return packet if any */
                if (pkt->size) {
                got_packet:
                    pkt->duration = 0;
                    pkt->stream_index = st->index;
                    pkt->pts = st->parser->pts;
                    pkt->dts = st->parser->dts;
                    pkt->pos = st->parser->pos;
                    if(pkt->data == st->cur_pkt.data && pkt->size == st->cur_pkt.size){
                        s->cur_st = NULL;
                        pkt->destruct= st->cur_pkt.destruct;
                        st->cur_pkt.destruct= NULL;
                        st->cur_pkt.data    = NULL;
                        assert(st->cur_len == 0);
                    }else{
                        pkt->destruct = NULL;
                    }
                    compute_pkt_fields(s, st, st->parser, pkt);

                    if((s->iformat->flags & AVFMT_GENERIC_INDEX) && pkt->flags & AV_PKT_FLAG_KEY){
                        ff_reduce_index(s, st->index);
                        av_add_index_entry(st, st->parser->frame_offset, pkt->dts,
                                           0, 0, AVINDEX_KEYFRAME);
                    }

                    break;
                }
            } else {
                /* free packet */
                av_free_packet(&st->cur_pkt);
                s->cur_st = NULL;
            }
        } else {
            AVPacket cur_pkt;
            /* read next packet */
            ret = av_read_packet(s, &cur_pkt);
            if (ret < 0) {
                if (ret == AVERROR(EAGAIN))
                    return ret;
                /* return the last frames, if any */
                for(i = 0; i < s->nb_streams; i++) {
                    st = s->streams[i];
                    if (st->parser && st->need_parsing) {
                        av_parser_parse2(st->parser, st->codec,
                                        &pkt->data, &pkt->size,
                                        NULL, 0,
                                        AV_NOPTS_VALUE, AV_NOPTS_VALUE,
                                        AV_NOPTS_VALUE);
                        if (pkt->size)
                            goto got_packet;
                    }
                }
                /* no more packets: really terminate parsing */
                return ret;
            }
            st = s->streams[cur_pkt.stream_index];
            st->cur_pkt= cur_pkt;

            if(st->cur_pkt.pts != AV_NOPTS_VALUE &&
               st->cur_pkt.dts != AV_NOPTS_VALUE &&
               st->cur_pkt.pts < st->cur_pkt.dts){
                av_log(s, AV_LOG_WARNING, "Invalid timestamps stream=%d, pts=%"PRId64", dts=%"PRId64", size=%d\n",
                    st->cur_pkt.stream_index,
                    st->cur_pkt.pts,
                    st->cur_pkt.dts,
                    st->cur_pkt.size);
//                av_free_packet(&st->cur_pkt);
//                return -1;
            }

            if(s->debug & FF_FDEBUG_TS)
                av_log(s, AV_LOG_DEBUG, "av_read_packet stream=%d, pts=%"PRId64", dts=%"PRId64", size=%d, duration=%d, flags=%d\n",
                    st->cur_pkt.stream_index,
                    st->cur_pkt.pts,
                    st->cur_pkt.dts,
                    st->cur_pkt.size,
                    st->cur_pkt.duration,
                    st->cur_pkt.flags);

            s->cur_st = st;
            st->cur_ptr = st->cur_pkt.data;
            st->cur_len = st->cur_pkt.size;
            if (st->need_parsing && !st->parser && !(s->flags & AVFMT_FLAG_NOPARSE)) {
                st->parser = av_parser_init(st->codec->codec_id);
                if (!st->parser) {
                    /* no parser available: just output the raw packets */
                    st->need_parsing = AVSTREAM_PARSE_NONE;
                }else if(st->need_parsing == AVSTREAM_PARSE_HEADERS){
                    st->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
                }else if(st->need_parsing == AVSTREAM_PARSE_FULL_ONCE){
                    st->parser->flags |= PARSER_FLAG_ONCE;
                }
            }
        }
    }
    if(s->debug & FF_FDEBUG_TS)
        av_log(s, AV_LOG_DEBUG, "read_frame_internal stream=%d, pts=%"PRId64", dts=%"PRId64", size=%d, duration=%d, flags=%d\n",
            pkt->stream_index,
            pkt->pts,
            pkt->dts,
            pkt->size,
            pkt->duration,
            pkt->flags);

    return 0;
}

static int read_from_packet_buffer(AVFormatContext *s, AVPacket *pkt)
{
    AVPacketList *pktl = s->packet_buffer;
    av_assert0(pktl);
    *pkt = pktl->pkt;
    s->packet_buffer = pktl->next;
    av_freep(&pktl);
    return 0;
}

int av_read_frame(AVFormatContext *s, AVPacket *pkt)
{
    const int genpts = s->flags & AVFMT_FLAG_GENPTS;
    int          eof = 0;

    if (!genpts)
        return s->packet_buffer ? read_from_packet_buffer(s, pkt) :
                                  read_frame_internal(s, pkt);

    for (;;) {
        int ret;
        AVPacketList *pktl = s->packet_buffer;

        if (pktl) {
            AVPacket *next_pkt = &pktl->pkt;

            if (next_pkt->dts != AV_NOPTS_VALUE) {
                int wrap_bits = s->streams[next_pkt->stream_index]->pts_wrap_bits;
                while (pktl && next_pkt->pts == AV_NOPTS_VALUE) {
                    if (pktl->pkt.stream_index == next_pkt->stream_index &&
                        (av_compare_mod(next_pkt->dts, pktl->pkt.dts, 2LL << (wrap_bits - 1)) < 0) &&
                         av_compare_mod(pktl->pkt.pts, pktl->pkt.dts, 2LL << (wrap_bits - 1))) { //not b frame
                        next_pkt->pts = pktl->pkt.dts;
                    }
                    pktl = pktl->next;
                }
                pktl = s->packet_buffer;
            }

            /* read packet from packet buffer, if there is data */
            if (!(next_pkt->pts == AV_NOPTS_VALUE &&
                  next_pkt->dts != AV_NOPTS_VALUE && !eof))
                return read_from_packet_buffer(s, pkt);
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
}

/* XXX: suppress the packet queue */
static void flush_packet_queue(AVFormatContext *s)
{
    AVPacketList *pktl;

    for(;;) {
        pktl = s->packet_buffer;
        if (!pktl)
            break;
        s->packet_buffer = pktl->next;
        av_free_packet(&pktl->pkt);
        av_free(pktl);
    }
    while(s->raw_packet_buffer){
        pktl = s->raw_packet_buffer;
        s->raw_packet_buffer = pktl->next;
        av_free_packet(&pktl->pkt);
        av_free(pktl);
    }
    s->packet_buffer_end=
    s->raw_packet_buffer_end= NULL;
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
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
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

    s->cur_st = NULL;

    /* for each stream, reset read state */
    for(i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];

        if (st->parser) {
            av_parser_close(st->parser);
            st->parser = NULL;
            av_free_packet(&st->cur_pkt);
        }
        st->last_IP_pts = AV_NOPTS_VALUE;
        st->cur_dts = AV_NOPTS_VALUE; /* we set the current DTS to an unspecified origin */
        st->reference_dts = AV_NOPTS_VALUE;
        /* fail safe */
        st->cur_ptr = NULL;
        st->cur_len = 0;

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
        assert(index==0 || ie[-1].timestamp < timestamp);
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

    av_dlog(s, "read_seek: %d %"PRId64"\n", stream_index, target_ts);

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
            av_dlog(s, "using cached pos_min=0x%"PRIx64" dts_min=%"PRId64"\n",
                    pos_min,ts_min);
        }else{
            assert(index==0);
        }

        index= av_index_search_timestamp(st, target_ts, flags & ~AVSEEK_FLAG_BACKWARD);
        assert(index < st->nb_index_entries);
        if(index >= 0){
            e= &st->index_entries[index];
            assert(e->timestamp >= target_ts);
            pos_max= e->pos;
            ts_max= e->timestamp;
            pos_limit= pos_max - e->min_distance;
            av_dlog(s, "using cached pos_max=0x%"PRIx64" pos_limit=0x%"PRIx64" dts_max=%"PRId64"\n",
                    pos_max,pos_limit, ts_max);
        }
    }

    pos= ff_gen_search(s, stream_index, target_ts, pos_min, pos_max, pos_limit, ts_min, ts_max, flags, &ts, avif->read_timestamp);
    if(pos<0)
        return -1;

    /* do the seek */
    if ((ret = avio_seek(s->pb, pos, SEEK_SET)) < 0)
        return ret;

    ff_update_cur_dts(s, st, ts);

    return 0;
}

int64_t ff_gen_search(AVFormatContext *s, int stream_index, int64_t target_ts,
                      int64_t pos_min, int64_t pos_max, int64_t pos_limit,
                      int64_t ts_min, int64_t ts_max, int flags, int64_t *ts_ret,
                      int64_t (*read_timestamp)(struct AVFormatContext *, int , int64_t *, int64_t ))
{
    int64_t pos, ts;
    int64_t start_pos, filesize;
    int no_change;

    av_dlog(s, "gen_seek: %d %"PRId64"\n", stream_index, target_ts);

    if(ts_min == AV_NOPTS_VALUE){
        pos_min = s->data_offset;
        ts_min = read_timestamp(s, stream_index, &pos_min, INT64_MAX);
        if (ts_min == AV_NOPTS_VALUE)
            return -1;
    }

    if(ts_max == AV_NOPTS_VALUE){
        int step= 1024;
        filesize = avio_size(s->pb);
        pos_max = filesize - 1;
        do{
            pos_max -= step;
            ts_max = read_timestamp(s, stream_index, &pos_max, pos_max + step);
            step += step;
        }while(ts_max == AV_NOPTS_VALUE && pos_max >= step);
        if (ts_max == AV_NOPTS_VALUE)
            return -1;

        for(;;){
            int64_t tmp_pos= pos_max + 1;
            int64_t tmp_ts= read_timestamp(s, stream_index, &tmp_pos, INT64_MAX);
            if(tmp_ts == AV_NOPTS_VALUE)
                break;
            ts_max= tmp_ts;
            pos_max= tmp_pos;
            if(tmp_pos >= filesize)
                break;
        }
        pos_limit= pos_max;
    }

    if(ts_min > ts_max){
        return -1;
    }else if(ts_min == ts_max){
        pos_limit= pos_min;
    }

    no_change=0;
    while (pos_min < pos_limit) {
        av_dlog(s, "pos_min=0x%"PRIx64" pos_max=0x%"PRIx64" dts_min=%"PRId64" dts_max=%"PRId64"\n",
                pos_min, pos_max, ts_min, ts_max);
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

        ts = read_timestamp(s, stream_index, &pos, INT64_MAX); //may pass pos_limit instead of -1
        if(pos == pos_max)
            no_change++;
        else
            no_change=0;
        av_dlog(s, "%"PRId64" %"PRId64" %"PRId64" / %"PRId64" %"PRId64" %"PRId64" target:%"PRId64" limit:%"PRId64" start:%"PRId64" noc:%d\n",
                pos_min, pos, pos_max, ts_min, ts, ts_max, target_ts,
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
    pos_min = pos;
    ts_min = read_timestamp(s, stream_index, &pos_min, INT64_MAX);
    pos_min++;
    ts_max = read_timestamp(s, stream_index, &pos_min, INT64_MAX);
    av_dlog(s, "pos=0x%"PRIx64" %"PRId64"<=%"PRId64"<=%"PRId64"\n",
            pos, ts_min, target_ts, ts_max);
    *ts_ret= ts;
    return pos;
}

static int seek_frame_byte(AVFormatContext *s, int stream_index, int64_t pos, int flags){
    int64_t pos_min, pos_max;
#if 0
    AVStream *st;

    if (stream_index < 0)
        return -1;

    st= s->streams[stream_index];
#endif

    pos_min = s->data_offset;
    pos_max = avio_size(s->pb) - 1;

    if     (pos < pos_min) pos= pos_min;
    else if(pos > pos_max) pos= pos_max;

    avio_seek(s->pb, pos, SEEK_SET);

#if 0
    av_update_cur_dts(s, st, ts);
#endif
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

        if(st->nb_index_entries){
            assert(st->index_entries);
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
            if(stream_index == pkt.stream_index){
                if((pkt.flags & AV_PKT_FLAG_KEY) && pkt.dts > timestamp)
                    break;
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

int av_seek_frame(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
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

int avformat_seek_file(AVFormatContext *s, int stream_index, int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    if(min_ts > ts || max_ts < ts)
        return -1;

    if (s->iformat->read_seek2) {
        ff_read_frame_flush(s);
        return s->iformat->read_seek2(s, stream_index, min_ts, ts, max_ts, flags);
    }

    if(s->iformat->read_timestamp){
        //try to seek via read_timestamp()
    }

    //Fallback to old API if new is not implemented but old is
    //Note the old has somewat different sematics
    if(s->iformat->read_seek || 1)
        return av_seek_frame(s, stream_index, ts, flags | (ts - min_ts > (uint64_t)(max_ts - ts) ? AVSEEK_FLAG_BACKWARD : 0));

    // try some generic seek like seek_frame_generic() but with new ts semantics
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
    return 0;
}

/**
 * Estimate the stream timings from the one of each components.
 *
 * Also computes the global bitrate if possible.
 */
static void update_stream_timings(AVFormatContext *ic)
{
    int64_t start_time, start_time1, end_time, end_time1;
    int64_t duration, duration1, filesize;
    int i;
    AVStream *st;

    start_time = INT64_MAX;
    end_time = INT64_MIN;
    duration = INT64_MIN;
    for(i = 0;i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time != AV_NOPTS_VALUE && st->time_base.den) {
            start_time1= av_rescale_q(st->start_time, st->time_base, AV_TIME_BASE_Q);
            start_time = FFMIN(start_time, start_time1);
            if (st->duration != AV_NOPTS_VALUE) {
                end_time1 = start_time1
                          + av_rescale_q(st->duration, st->time_base, AV_TIME_BASE_Q);
                end_time = FFMAX(end_time, end_time1);
            }
        }
        if (st->duration != AV_NOPTS_VALUE) {
            duration1 = av_rescale_q(st->duration, st->time_base, AV_TIME_BASE_Q);
            duration = FFMAX(duration, duration1);
        }
    }
    if (start_time != INT64_MAX) {
        ic->start_time = start_time;
        if (end_time != INT64_MIN)
            duration = FFMAX(duration, end_time - start_time);
    }
    if (duration != INT64_MIN) {
        ic->duration = duration;
        if (ic->pb && (filesize = avio_size(ic->pb)) > 0) {
            /* compute the bitrate */
            ic->bit_rate = (double)filesize * 8.0 * AV_TIME_BASE /
                (double)ic->duration;
        }
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
    int bit_rate, i;
    AVStream *st;

    /* if bit_rate is already set, we believe it */
    if (ic->bit_rate <= 0) {
        bit_rate = 0;
        for(i=0;i<ic->nb_streams;i++) {
            st = ic->streams[i];
            if (st->codec->bit_rate > 0)
            bit_rate += st->codec->bit_rate;
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
                duration= av_rescale(8*filesize, st->time_base.den, ic->bit_rate*(int64_t)st->time_base.num);
                if (st->duration == AV_NOPTS_VALUE)
                    st->duration = duration;
            }
        }
    }
}

#define DURATION_MAX_READ_SIZE 250000
#define DURATION_MAX_RETRY 3

/* only usable for MPEG-PS streams */
static void estimate_timings_from_pts(AVFormatContext *ic, int64_t old_offset)
{
    AVPacket pkt1, *pkt = &pkt1;
    AVStream *st;
    int read_size, i, ret;
    int64_t end_time;
    int64_t filesize, offset, duration;
    int retry=0;

    ic->cur_st = NULL;

    /* flush packet queue */
    flush_packet_queue(ic);

    for (i=0; i<ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time == AV_NOPTS_VALUE && st->first_dts == AV_NOPTS_VALUE)
            av_log(st->codec, AV_LOG_WARNING, "start time is not set in estimate_timings_from_pts\n");

        if (st->parser) {
            av_parser_close(st->parser);
            st->parser= NULL;
            av_free_packet(&st->cur_pkt);
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
                ret = av_read_packet(ic, pkt);
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
                if (duration < 0)
                    duration += 1LL<<st->pts_wrap_bits;
                if (duration > 0) {
                    if (st->duration == AV_NOPTS_VALUE || st->duration < duration)
                        st->duration = duration;
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
    } else if (has_duration(ic)) {
        /* at least one component has timings - we use them for all
           the components */
        fill_all_stream_timings(ic);
    } else {
        av_log(ic, AV_LOG_WARNING, "Estimating duration from bitrate, this may be inaccurate\n");
        /* less precise: use bitrate info */
        estimate_timings_from_bit_rate(ic);
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

static int has_codec_parameters(AVCodecContext *avctx)
{
    int val;
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        val = avctx->sample_rate && avctx->channels && avctx->sample_fmt != AV_SAMPLE_FMT_NONE;
        if (!avctx->frame_size &&
            (avctx->codec_id == CODEC_ID_VORBIS ||
             avctx->codec_id == CODEC_ID_AAC ||
             avctx->codec_id == CODEC_ID_MP1 ||
             avctx->codec_id == CODEC_ID_MP2 ||
             avctx->codec_id == CODEC_ID_MP3 ||
             avctx->codec_id == CODEC_ID_CELT))
            return 0;
        break;
    case AVMEDIA_TYPE_VIDEO:
        val = avctx->width && avctx->pix_fmt != PIX_FMT_NONE;
        break;
    default:
        val = 1;
        break;
    }
    return avctx->codec_id != CODEC_ID_NONE && val != 0;
}

static int has_decode_delay_been_guessed(AVStream *st)
{
    return st->codec->codec_id != CODEC_ID_H264 ||
        st->info->nb_decoded_frames >= 6;
}

/* returns 1 or 0 if or if not decoded data was returned, or a negative error */
static int try_decode_frame(AVStream *st, AVPacket *avpkt, AVDictionary **options)
{
    AVCodec *codec;
    int got_picture = 1, ret = 0;
    AVFrame picture;
    AVPacket pkt = *avpkt;

    if(!st->codec->codec){
        AVDictionary *thread_opt = NULL;

        codec = avcodec_find_decoder(st->codec->codec_id);
        if (!codec)
            return -1;

        /* force thread count to 1 since the h264 decoder will not extract SPS
         *  and PPS to extradata during multi-threaded decoding */
        av_dict_set(options ? options : &thread_opt, "threads", "1", 0);
        ret = avcodec_open2(st->codec, codec, options ? options : &thread_opt);
        if (!options)
            av_dict_free(&thread_opt);
        if (ret < 0)
            return ret;
    }

    while ((pkt.size > 0 || (!pkt.data && got_picture)) &&
           ret >= 0 &&
           (!has_codec_parameters(st->codec)  ||
           !has_decode_delay_been_guessed(st) ||
           (!st->codec_info_nb_frames && st->codec->codec->capabilities & CODEC_CAP_CHANNEL_CONF))) {
        got_picture = 0;
        avcodec_get_frame_defaults(&picture);
        switch(st->codec->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            ret = avcodec_decode_video2(st->codec, &picture,
                                        &got_picture, &pkt);
            break;
        case AVMEDIA_TYPE_AUDIO:
            ret = avcodec_decode_audio4(st->codec, &picture, &got_picture, &pkt);
            break;
        default:
            break;
        }
        if (ret >= 0) {
            if (got_picture)
                st->info->nb_decoded_frames++;
            pkt.data += ret;
            pkt.size -= ret;
            ret       = got_picture;
        }
    }
    return ret;
}

unsigned int ff_codec_get_tag(const AVCodecTag *tags, enum CodecID id)
{
    while (tags->id != CODEC_ID_NONE) {
        if (tags->id == id)
            return tags->tag;
        tags++;
    }
    return 0;
}

enum CodecID ff_codec_get_id(const AVCodecTag *tags, unsigned int tag)
{
    int i;
    for(i=0; tags[i].id != CODEC_ID_NONE;i++) {
        if(tag == tags[i].tag)
            return tags[i].id;
    }
    for(i=0; tags[i].id != CODEC_ID_NONE; i++) {
        if (avpriv_toupper4(tag) == avpriv_toupper4(tags[i].tag))
            return tags[i].id;
    }
    return CODEC_ID_NONE;
}

unsigned int av_codec_get_tag(const AVCodecTag * const *tags, enum CodecID id)
{
    int i;
    for(i=0; tags && tags[i]; i++){
        int tag= ff_codec_get_tag(tags[i], id);
        if(tag) return tag;
    }
    return 0;
}

enum CodecID av_codec_get_id(const AVCodecTag * const *tags, unsigned int tag)
{
    int i;
    for(i=0; tags && tags[i]; i++){
        enum CodecID id= ff_codec_get_id(tags[i], tag);
        if(id!=CODEC_ID_NONE) return id;
    }
    return CODEC_ID_NONE;
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
    if(i<60*12) return i*1001;
    else        return ((const int[]){24,30,60,12,15})[i-60*12]*1000*12;
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
       || c->codec_id == CODEC_ID_MPEG2VIDEO
       || c->codec_id == CODEC_ID_H264
       )
        return 1;
    return 0;
}

int avformat_find_stream_info(AVFormatContext *ic, AVDictionary **options)
{
    int i, count, ret, read_size, j;
    AVStream *st;
    AVPacket pkt1, *pkt;
    int64_t old_offset = avio_tell(ic->pb);
    int orig_nb_streams = ic->nb_streams;        // new streams might appear, no options for those

    for(i=0;i<ic->nb_streams;i++) {
        AVCodec *codec;
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
            if(st->need_parsing == AVSTREAM_PARSE_HEADERS && st->parser){
                st->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
            }
        }
        assert(!st->codec->codec);
        codec = avcodec_find_decoder(st->codec->codec_id);

        /* force thread count to 1 since the h264 decoder will not extract SPS
         *  and PPS to extradata during multi-threaded decoding */
        av_dict_set(options ? &options[i] : &thread_opt, "threads", "1", 0);

        /* Ensure that subtitle_header is properly set. */
        if (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE
            && codec && !st->codec->codec)
            avcodec_open2(st->codec, codec, options ? &options[i]
                              : &thread_opt);

        //try to just open decoders, in case this is enough to get parameters
        if(!has_codec_parameters(st->codec)){
            if (codec && !st->codec->codec)
                avcodec_open2(st->codec, codec, options ? &options[i]
                              : &thread_opt);
        }
        if (!options)
            av_dict_free(&thread_opt);
    }

    for (i=0; i<ic->nb_streams; i++) {
        ic->streams[i]->info->last_dts = AV_NOPTS_VALUE;
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
            if (!has_codec_parameters(st->codec))
                break;
            /* if the timebase is coarse (like the usual millisecond precision
               of mkv), we need to analyze more frames to reliably arrive at
               the correct fps */
            if (av_q2d(st->time_base) > 0.0005)
                fps_analyze_framecount *= 2;
            if (ic->fps_probe_size >= 0)
                fps_analyze_framecount = ic->fps_probe_size;
            /* variable fps and no guess at the real fps */
            if(   tb_unreliable(st->codec) && !(st->r_frame_rate.num && st->avg_frame_rate.num)
               && st->info->duration_count < fps_analyze_framecount
               && st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
                break;
            if(st->parser && st->parser->parser->split && !st->codec->extradata)
                break;
            if(st->first_dts == AV_NOPTS_VALUE)
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
                break;
            }
        }
        /* we did not get all the codec info, but we read too much data */
        if (read_size >= ic->probesize) {
            ret = count;
            av_log(ic, AV_LOG_DEBUG, "Probe buffer size limit %d reached\n", ic->probesize);
            break;
        }

        /* NOTE: a new stream can be added there if no header in file
           (AVFMTCTX_NOHEADER) */
        ret = read_frame_internal(ic, &pkt1);
        if (ret == AVERROR(EAGAIN))
            continue;

        if (ret < 0) {
            /* EOF or error*/
            AVPacket empty_pkt = { 0 };
            int err;
            av_init_packet(&empty_pkt);

            ret = -1; /* we could not have all the codec parameters before EOF */
            for(i=0;i<ic->nb_streams;i++) {
                st = ic->streams[i];

                /* flush the decoders */
                do {
                    err = try_decode_frame(st, &empty_pkt,
                                           (options && i < orig_nb_streams) ?
                                           &options[i] : NULL);
                } while (err > 0 && !has_codec_parameters(st->codec));

                if (err < 0) {
                    av_log(ic, AV_LOG_WARNING,
                           "decoding for stream %d failed\n", st->index);
                } else if (!has_codec_parameters(st->codec)){
                    char buf[256];
                    avcodec_string(buf, sizeof(buf), st->codec, 0);
                    av_log(ic, AV_LOG_WARNING,
                           "Could not find codec parameters (%s)\n", buf);
                } else {
                    ret = 0;
                }
            }
            break;
        }

        pkt= add_to_pktbuf(&ic->packet_buffer, &pkt1, &ic->packet_buffer_end);
        if ((ret = av_dup_packet(pkt)) < 0)
            goto find_stream_info_err;

        read_size += pkt->size;

        st = ic->streams[pkt->stream_index];
        if (st->codec_info_nb_frames>1) {
            if (st->time_base.den > 0 && av_rescale_q(st->info->codec_info_duration, st->time_base, AV_TIME_BASE_Q) >= ic->max_analyze_duration) {
                av_log(ic, AV_LOG_WARNING, "max_analyze_duration reached\n");
                break;
            }
            st->info->codec_info_duration += pkt->duration;
        }
        {
            int64_t last = st->info->last_dts;

            if(pkt->dts != AV_NOPTS_VALUE && last != AV_NOPTS_VALUE && pkt->dts > last){
                int64_t duration= pkt->dts - last;
                double dur= duration * av_q2d(st->time_base);

//                if(st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
//                    av_log(NULL, AV_LOG_ERROR, "%f\n", dur);
                if (st->info->duration_count < 2)
                    memset(st->info->duration_error, 0, sizeof(st->info->duration_error));
                for (i=1; i<FF_ARRAY_ELEMS(st->info->duration_error); i++) {
                    int framerate= get_std_framerate(i);
                    int ticks= lrintf(dur*framerate/(1001*12));
                    double error = dur - (double)ticks*1001*12 / framerate;
                    st->info->duration_error[i] += error*error;
                }
                st->info->duration_count++;
                // ignore the first 4 values, they might have some random jitter
                if (st->info->duration_count > 3)
                    st->info->duration_gcd = av_gcd(st->info->duration_gcd, duration);
            }
            if (last == AV_NOPTS_VALUE || st->info->duration_count <= 1)
                st->info->last_dts = pkt->dts;
        }
        if(st->parser && st->parser->parser->split && !st->codec->extradata){
            int i= st->parser->parser->split(st->codec, pkt->data, pkt->size);
            if (i > 0 && i < FF_MAX_EXTRADATA_SIZE) {
                st->codec->extradata_size= i;
                st->codec->extradata= av_malloc(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
                if (!st->codec->extradata)
                    return AVERROR(ENOMEM);
                memcpy(st->codec->extradata, pkt->data, st->codec->extradata_size);
                memset(st->codec->extradata + i, 0, FF_INPUT_BUFFER_PADDING_SIZE);
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
        try_decode_frame(st, pkt, (options && i < orig_nb_streams ) ? &options[i] : NULL);

        st->codec_info_nb_frames++;
        count++;
    }

    // close codecs which were opened in try_decode_frame()
    for(i=0;i<ic->nb_streams;i++) {
        st = ic->streams[i];
        if(st->codec->codec)
            avcodec_close(st->codec);
    }
    for(i=0;i<ic->nb_streams;i++) {
        st = ic->streams[i];
        if (st->codec_info_nb_frames>2 && !st->avg_frame_rate.num && st->info->codec_info_duration)
            av_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
                     (st->codec_info_nb_frames-2)*(int64_t)st->time_base.den,
                      st->info->codec_info_duration*(int64_t)st->time_base.num, 60000);
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            // the check for tb_unreliable() is not completely correct, since this is not about handling
            // a unreliable/inexact time base, but a time base that is finer than necessary, as e.g.
            // ipmovie.c produces.
            if (tb_unreliable(st->codec) && st->info->duration_count > 15 && st->info->duration_gcd > 1 && !st->r_frame_rate.num)
                av_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den, st->time_base.den, st->time_base.num * st->info->duration_gcd, INT_MAX);
            if (st->info->duration_count && !st->r_frame_rate.num
               && tb_unreliable(st->codec) /*&&
               //FIXME we should not special-case MPEG-2, but this needs testing with non-MPEG-2 ...
               st->time_base.num*duration_sum[i]/st->info->duration_count*101LL > st->time_base.den*/){
                int num = 0;
                double best_error= 2*av_q2d(st->time_base);
                best_error = best_error*best_error*st->info->duration_count*1000*12*30;

                for (j=1; j<FF_ARRAY_ELEMS(st->info->duration_error); j++) {
                    double error = st->info->duration_error[j] * get_std_framerate(j);
//                    if(st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
//                        av_log(NULL, AV_LOG_ERROR, "%f %f\n", get_std_framerate(j) / 12.0/1001, error);
                    if(error < best_error){
                        best_error= error;
                        num = get_std_framerate(j);
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

    estimate_timings(ic, old_offset);

    compute_chapters_end(ic);

#if 0
    /* correct DTS for B-frame streams with no timestamps */
    for(i=0;i<ic->nb_streams;i++) {
        st = ic->streams[i];
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            if(b-frames){
                ppktl = &ic->packet_buffer;
                while(ppkt1){
                    if(ppkt1->stream_index != i)
                        continue;
                    if(ppkt1->pkt->dts < 0)
                        break;
                    if(ppkt1->pkt->pts != AV_NOPTS_VALUE)
                        break;
                    ppkt1->pkt->dts -= delta;
                    ppkt1= ppkt1->next;
                }
                if(ppkt1)
                    continue;
                st->cur_dts -= delta;
            }
        }
    }
#endif

 find_stream_info_err:
    for (i=0; i < ic->nb_streams; i++) {
        if (ic->streams[i]->codec)
            ic->streams[i]->codec->thread_count = 0;
        av_freep(&ic->streams[i]->info);
    }
    return ret;
}

static AVProgram *find_program_from_stream(AVFormatContext *ic, int s)
{
    int i, j;

    for (i = 0; i < ic->nb_programs; i++)
        for (j = 0; j < ic->programs[i]->nb_stream_indexes; j++)
            if (ic->programs[i]->stream_index[j] == s)
                return ic->programs[i];
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
    int ret = AVERROR_STREAM_NOT_FOUND, best_count = -1;
    unsigned *program = NULL;
    AVCodec *decoder = NULL, *best_decoder = NULL;

    if (related_stream >= 0 && wanted_stream_nb < 0) {
        AVProgram *p = find_program_from_stream(ic, related_stream);
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
            decoder = avcodec_find_decoder(st->codec->codec_id);
            if (!decoder) {
                if (ret < 0)
                    ret = AVERROR_DECODER_NOT_FOUND;
                continue;
            }
        }
        if (best_count >= st->codec_info_nb_frames)
            continue;
        best_count = st->codec_info_nb_frames;
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

void avformat_free_context(AVFormatContext *s)
{
    int i;
    AVStream *st;

    av_opt_free(s);
    if (s->iformat && s->iformat->priv_class && s->priv_data)
        av_opt_free(s->priv_data);

    for(i=0;i<s->nb_streams;i++) {
        /* free all data in a stream component */
        st = s->streams[i];
        if (st->parser) {
            av_parser_close(st->parser);
            av_free_packet(&st->cur_pkt);
        }
        av_dict_free(&st->metadata);
        av_free(st->index_entries);
        av_free(st->codec->extradata);
        av_free(st->codec->subtitle_header);
        av_free(st->codec);
        av_free(st->priv_data);
        av_free(st->info);
        av_free(st);
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
        av_free(s->chapters[s->nb_chapters]);
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
    AVFormatContext *s = *ps;
    AVIOContext *pb = (s->iformat->flags & AVFMT_NOFILE) || (s->flags & AVFMT_FLAG_CUSTOM_IO) ?
                       NULL : s->pb;
    flush_packet_queue(s);
    if (s->iformat->read_close)
        s->iformat->read_close(s);
    avformat_free_context(s);
    *ps = NULL;
    if (pb)
        avio_close(pb);
}

AVStream *avformat_new_stream(AVFormatContext *s, AVCodec *c)
{
    AVStream *st;
    int i;
    AVStream **streams;

    if (s->nb_streams >= INT_MAX/sizeof(*streams))
        return NULL;
    streams = av_realloc(s->streams, (s->nb_streams + 1) * sizeof(*streams));
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
    st->cur_dts = 0;
    st->first_dts = AV_NOPTS_VALUE;
    st->probe_packets = MAX_PROBE_PACKETS;

    /* default pts setting is MPEG-like */
    avpriv_set_pts_info(st, 33, 1, 90000);
    st->last_IP_pts = AV_NOPTS_VALUE;
    for(i=0; i<MAX_REORDER_DELAY+1; i++)
        st->pts_buffer[i]= AV_NOPTS_VALUE;
    st->reference_dts = AV_NOPTS_VALUE;

    st->sample_aspect_ratio = (AVRational){0,1};

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

/************************************************************/
/* output media file */

static int validate_codec_tag(AVFormatContext *s, AVStream *st)
{
    const AVCodecTag *avctag;
    int n;
    enum CodecID id = CODEC_ID_NONE;
    unsigned int tag = 0;

    /**
     * Check that tag + id is in the table
     * If neither is in the table -> OK
     * If tag is in the table with another id -> FAIL
     * If id is in the table with another tag -> FAIL unless strict < normal
     */
    for (n = 0; s->oformat->codec_tag[n]; n++) {
        avctag = s->oformat->codec_tag[n];
        while (avctag->id != CODEC_ID_NONE) {
            if (avpriv_toupper4(avctag->tag) == avpriv_toupper4(st->codec->codec_tag)) {
                id = avctag->id;
                if (id == st->codec->codec_id)
                    return 1;
            }
            if (avctag->id == st->codec->codec_id)
                tag = avctag->tag;
            avctag++;
        }
    }
    if (id != CODEC_ID_NONE)
        return 0;
    if (tag && (st->codec->strict_std_compliance >= FF_COMPLIANCE_NORMAL))
        return 0;
    return 1;
}

int avformat_write_header(AVFormatContext *s, AVDictionary **options)
{
    int ret = 0, i;
    AVStream *st;
    AVDictionary *tmp = NULL;

    if (options)
        av_dict_copy(&tmp, *options, 0);
    if ((ret = av_opt_set_dict(s, &tmp)) < 0)
        goto fail;

    // some sanity checks
    if (s->nb_streams == 0 && !(s->oformat->flags & AVFMT_NOSTREAMS)) {
        av_log(s, AV_LOG_ERROR, "no streams\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];

        switch (st->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            if(st->codec->sample_rate<=0){
                av_log(s, AV_LOG_ERROR, "sample rate not set\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
            if(!st->codec->block_align)
                st->codec->block_align = st->codec->channels *
                    av_get_bits_per_sample(st->codec->codec_id) >> 3;
            break;
        case AVMEDIA_TYPE_VIDEO:
            if(st->codec->time_base.num<=0 || st->codec->time_base.den<=0){ //FIXME audio too?
                av_log(s, AV_LOG_ERROR, "time base not set\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
            if((st->codec->width<=0 || st->codec->height<=0) && !(s->oformat->flags & AVFMT_NODIMENSIONS)){
                av_log(s, AV_LOG_ERROR, "dimensions not set\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
            if(av_cmp_q(st->sample_aspect_ratio, st->codec->sample_aspect_ratio)){
                av_log(s, AV_LOG_ERROR, "Aspect ratio mismatch between encoder "
                       "(%d/%d) and muxer layer (%d/%d)\n",
                       st->sample_aspect_ratio.num, st->sample_aspect_ratio.den,
                       st->codec->sample_aspect_ratio.num,
                       st->codec->sample_aspect_ratio.den);
                ret = AVERROR(EINVAL);
                goto fail;
            }
            break;
        }

        if(s->oformat->codec_tag){
            if(st->codec->codec_tag && st->codec->codec_id == CODEC_ID_RAWVIDEO && av_codec_get_tag(s->oformat->codec_tag, st->codec->codec_id) == 0 && !validate_codec_tag(s, st)){
                //the current rawvideo encoding system ends up setting the wrong codec_tag for avi, we override it here
                st->codec->codec_tag= 0;
            }
            if(st->codec->codec_tag){
                if (!validate_codec_tag(s, st)) {
                    char tagbuf[32];
                    av_get_codec_tag_string(tagbuf, sizeof(tagbuf), st->codec->codec_tag);
                    av_log(s, AV_LOG_ERROR,
                           "Tag %s/0x%08x incompatible with output codec id '%d'\n",
                           tagbuf, st->codec->codec_tag, st->codec->codec_id);
                    ret = AVERROR_INVALIDDATA;
                    goto fail;
                }
            }else
                st->codec->codec_tag= av_codec_get_tag(s->oformat->codec_tag, st->codec->codec_id);
        }

        if(s->oformat->flags & AVFMT_GLOBALHEADER &&
            !(st->codec->flags & CODEC_FLAG_GLOBAL_HEADER))
          av_log(s, AV_LOG_WARNING, "Codec for stream %d does not use global headers but container format requires global headers\n", i);
    }

    if (!s->priv_data && s->oformat->priv_data_size > 0) {
        s->priv_data = av_mallocz(s->oformat->priv_data_size);
        if (!s->priv_data) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if (s->oformat->priv_class) {
            *(const AVClass**)s->priv_data= s->oformat->priv_class;
            av_opt_set_defaults(s->priv_data);
            if ((ret = av_opt_set_dict(s->priv_data, &tmp)) < 0)
                goto fail;
        }
    }

    /* set muxer identification string */
    if (s->nb_streams && !(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT)) {
        av_dict_set(&s->metadata, "encoder", LIBAVFORMAT_IDENT, 0);
    }

    if(s->oformat->write_header){
        ret = s->oformat->write_header(s);
        if (ret < 0)
            goto fail;
    }

    /* init PTS generation */
    for(i=0;i<s->nb_streams;i++) {
        int64_t den = AV_NOPTS_VALUE;
        st = s->streams[i];

        switch (st->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            den = (int64_t)st->time_base.num * st->codec->sample_rate;
            break;
        case AVMEDIA_TYPE_VIDEO:
            den = (int64_t)st->time_base.num * st->codec->time_base.den;
            break;
        default:
            break;
        }
        if (den != AV_NOPTS_VALUE) {
            if (den <= 0) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            frac_init(&st->pts, 0, 0, den);
        }
    }

    if (options) {
        av_dict_free(options);
        *options = tmp;
    }
    return 0;
fail:
    av_dict_free(&tmp);
    return ret;
}

//FIXME merge with compute_pkt_fields
static int compute_pkt_fields2(AVFormatContext *s, AVStream *st, AVPacket *pkt){
    int delay = FFMAX(st->codec->has_b_frames, !!st->codec->max_b_frames);
    int num, den, frame_size, i;

    av_dlog(s, "compute_pkt_fields2: pts:%"PRId64" dts:%"PRId64" cur_dts:%"PRId64" b:%d size:%d st:%d\n",
            pkt->pts, pkt->dts, st->cur_dts, delay, pkt->size, pkt->stream_index);

/*    if(pkt->pts == AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE)
        return AVERROR(EINVAL);*/

    /* duration field */
    if (pkt->duration == 0) {
        compute_frame_duration(&num, &den, st, NULL, pkt);
        if (den && num) {
            pkt->duration = av_rescale(1, num * (int64_t)st->time_base.den * st->codec->ticks_per_frame, den * (int64_t)st->time_base.num);
        }
    }

    if(pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE && delay==0)
        pkt->pts= pkt->dts;

    //XXX/FIXME this is a temporary hack until all encoders output pts
    if((pkt->pts == 0 || pkt->pts == AV_NOPTS_VALUE) && pkt->dts == AV_NOPTS_VALUE && !delay){
        pkt->dts=
//        pkt->pts= st->cur_dts;
        pkt->pts= st->pts.val;
    }

    //calculate dts from pts
    if(pkt->pts != AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE && delay <= MAX_REORDER_DELAY){
        st->pts_buffer[0]= pkt->pts;
        for(i=1; i<delay+1 && st->pts_buffer[i] == AV_NOPTS_VALUE; i++)
            st->pts_buffer[i]= pkt->pts + (i-delay-1) * pkt->duration;
        for(i=0; i<delay && st->pts_buffer[i] > st->pts_buffer[i+1]; i++)
            FFSWAP(int64_t, st->pts_buffer[i], st->pts_buffer[i+1]);

        pkt->dts= st->pts_buffer[0];
    }

    if(st->cur_dts && st->cur_dts != AV_NOPTS_VALUE && st->cur_dts >= pkt->dts){
        av_log(s, AV_LOG_ERROR,
               "Application provided invalid, non monotonically increasing dts to muxer in stream %d: %"PRId64" >= %"PRId64"\n",
               st->index, st->cur_dts, pkt->dts);
        return AVERROR(EINVAL);
    }
    if(pkt->dts != AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE && pkt->pts < pkt->dts){
        av_log(s, AV_LOG_ERROR, "pts < dts in stream %d\n", st->index);
        return AVERROR(EINVAL);
    }

//    av_log(s, AV_LOG_DEBUG, "av_write_frame: pts2:%"PRId64" dts2:%"PRId64"\n", pkt->pts, pkt->dts);
    st->cur_dts= pkt->dts;
    st->pts.val= pkt->dts;

    /* update pts */
    switch (st->codec->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        frame_size = get_audio_frame_size(st->codec, pkt->size);

        /* HACK/FIXME, we skip the initial 0 size packets as they are most
           likely equal to the encoder delay, but it would be better if we
           had the real timestamps from the encoder */
        if (frame_size >= 0 && (pkt->size || st->pts.num!=st->pts.den>>1 || st->pts.val)) {
            frac_add(&st->pts, (int64_t)st->time_base.den * frame_size);
        }
        break;
    case AVMEDIA_TYPE_VIDEO:
        frac_add(&st->pts, (int64_t)st->time_base.den * st->codec->time_base.num);
        break;
    default:
        break;
    }
    return 0;
}

int av_write_frame(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    if (!pkt) {
        if (s->oformat->flags & AVFMT_ALLOW_FLUSH)
            return s->oformat->write_packet(s, pkt);
        return 1;
    }

    ret = compute_pkt_fields2(s, s->streams[pkt->stream_index], pkt);

    if(ret<0 && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
        return ret;

    ret= s->oformat->write_packet(s, pkt);

    if (ret >= 0)
        s->streams[pkt->stream_index]->nb_frames++;
    return ret;
}

void ff_interleave_add_packet(AVFormatContext *s, AVPacket *pkt,
                              int (*compare)(AVFormatContext *, AVPacket *, AVPacket *))
{
    AVPacketList **next_point, *this_pktl;

    this_pktl = av_mallocz(sizeof(AVPacketList));
    this_pktl->pkt= *pkt;
    pkt->destruct= NULL;             // do not free original but only the copy
    av_dup_packet(&this_pktl->pkt);  // duplicate the packet if it uses non-alloced memory

    if(s->streams[pkt->stream_index]->last_in_packet_buffer){
        next_point = &(s->streams[pkt->stream_index]->last_in_packet_buffer->next);
    }else
        next_point = &s->packet_buffer;

    if(*next_point){
        if(compare(s, &s->packet_buffer_end->pkt, pkt)){
            while(!compare(s, &(*next_point)->pkt, pkt)){
                next_point= &(*next_point)->next;
            }
            goto next_non_null;
        }else{
            next_point = &(s->packet_buffer_end->next);
        }
    }
    assert(!*next_point);

    s->packet_buffer_end= this_pktl;
next_non_null:

    this_pktl->next= *next_point;

    s->streams[pkt->stream_index]->last_in_packet_buffer=
    *next_point= this_pktl;
}

static int ff_interleave_compare_dts(AVFormatContext *s, AVPacket *next, AVPacket *pkt)
{
    AVStream *st = s->streams[ pkt ->stream_index];
    AVStream *st2= s->streams[ next->stream_index];
    int comp = av_compare_ts(next->dts, st2->time_base, pkt->dts,
                             st->time_base);

    if (comp == 0)
        return pkt->stream_index < next->stream_index;
    return comp > 0;
}

int av_interleave_packet_per_dts(AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush){
    AVPacketList *pktl;
    int stream_count=0;
    int i;

    if(pkt){
        ff_interleave_add_packet(s, pkt, ff_interleave_compare_dts);
    }

    for(i=0; i < s->nb_streams; i++)
        stream_count+= !!s->streams[i]->last_in_packet_buffer;

    if(stream_count && (s->nb_streams == stream_count || flush)){
        pktl= s->packet_buffer;
        *out= pktl->pkt;

        s->packet_buffer= pktl->next;
        if(!s->packet_buffer)
            s->packet_buffer_end= NULL;

        if(s->streams[out->stream_index]->last_in_packet_buffer == pktl)
            s->streams[out->stream_index]->last_in_packet_buffer= NULL;
        av_freep(&pktl);
        return 1;
    }else{
        av_init_packet(out);
        return 0;
    }
}

/**
 * Interleave an AVPacket correctly so it can be muxed.
 * @param out the interleaved packet will be output here
 * @param in the input packet
 * @param flush 1 if no further packets are available as input and all
 *              remaining packets should be output
 * @return 1 if a packet was output, 0 if no packet could be output,
 *         < 0 if an error occurred
 */
static int interleave_packet(AVFormatContext *s, AVPacket *out, AVPacket *in, int flush){
    if (s->oformat->interleave_packet) {
        int ret = s->oformat->interleave_packet(s, out, in, flush);
        if (in)
            av_free_packet(in);
        return ret;
    } else
        return av_interleave_packet_per_dts(s, out, in, flush);
}

int av_interleaved_write_frame(AVFormatContext *s, AVPacket *pkt){
    AVStream *st= s->streams[ pkt->stream_index];
    int ret;

    //FIXME/XXX/HACK drop zero sized packets
    if(st->codec->codec_type == AVMEDIA_TYPE_AUDIO && pkt->size==0)
        return 0;

    av_dlog(s, "av_interleaved_write_frame size:%d dts:%"PRId64" pts:%"PRId64"\n",
            pkt->size, pkt->dts, pkt->pts);
    if((ret = compute_pkt_fields2(s, st, pkt)) < 0 && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
        return ret;

    if(pkt->dts == AV_NOPTS_VALUE && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
        return AVERROR(EINVAL);

    for(;;){
        AVPacket opkt;
        int ret= interleave_packet(s, &opkt, pkt, 0);
        if(ret<=0) //FIXME cleanup needed for ret<0 ?
            return ret;

        ret= s->oformat->write_packet(s, &opkt);
        if (ret >= 0)
            s->streams[opkt.stream_index]->nb_frames++;

        av_free_packet(&opkt);
        pkt= NULL;

        if(ret<0)
            return ret;
    }
}

int av_write_trailer(AVFormatContext *s)
{
    int ret, i;

    for(;;){
        AVPacket pkt;
        ret= interleave_packet(s, &pkt, NULL, 1);
        if(ret<0) //FIXME cleanup needed for ret<0 ?
            goto fail;
        if(!ret)
            break;

        ret= s->oformat->write_packet(s, &pkt);
        if (ret >= 0)
            s->streams[pkt.stream_index]->nb_frames++;

        av_free_packet(&pkt);

        if(ret<0)
            goto fail;
    }

    if(s->oformat->write_trailer)
        ret = s->oformat->write_trailer(s);
fail:
    for(i=0;i<s->nb_streams;i++) {
        av_freep(&s->streams[i]->priv_data);
        av_freep(&s->streams[i]->index_entries);
    }
    if (s->oformat->priv_class)
        av_opt_free(s->priv_data);
    av_freep(&s->priv_data);
    return ret;
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

        tmp = av_realloc(program->stream_index, sizeof(unsigned int)*(program->nb_stream_indexes+1));
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
    if(m && !(m->count == 1 && av_dict_get(m, "language", NULL, 0))){
        AVDictionaryEntry *tag=NULL;

        av_log(ctx, AV_LOG_INFO, "%sMetadata:\n", indent);
        while((tag=av_dict_get(m, "", tag, AV_DICT_IGNORE_SUFFIX))) {
            if(strcmp("language", tag->key))
                av_log(ctx, AV_LOG_INFO, "%s  %-16s: %s\n", indent, tag->key, tag->value);
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
    av_log(NULL, AV_LOG_INFO, "    Stream #%d.%d", index, i);
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
        av_log(NULL, AV_LOG_INFO, ", PAR %d:%d DAR %d:%d",
                 st->sample_aspect_ratio.num, st->sample_aspect_ratio.den,
                 display_aspect_ratio.num, display_aspect_ratio.den);
    }
    if(st->codec->codec_type == AVMEDIA_TYPE_VIDEO){
        if(st->avg_frame_rate.den && st->avg_frame_rate.num)
            print_fps(av_q2d(st->avg_frame_rate), "fps");
        if(st->r_frame_rate.den && st->r_frame_rate.num)
            print_fps(av_q2d(st->r_frame_rate), "tbr");
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
            secs = ic->duration / AV_TIME_BASE;
            us = ic->duration % AV_TIME_BASE;
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

int64_t av_gettime(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
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
                while (isdigit(*p)) {
                    nd = nd * 10 + *p++ - '0';
                }
                c = *p++;
            } while (isdigit(c));

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

static void hex_dump_internal(void *avcl, FILE *f, int level, uint8_t *buf, int size)
{
    int len, i, j, c;
#undef fprintf
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

void av_hex_dump(FILE *f, uint8_t *buf, int size)
{
    hex_dump_internal(NULL, f, 0, buf, size);
}

void av_hex_dump_log(void *avcl, int level, uint8_t *buf, int size)
{
    hex_dump_internal(avcl, NULL, level, buf, size);
}

static void pkt_dump_internal(void *avcl, FILE *f, int level, AVPacket *pkt, int dump_payload, AVRational time_base)
{
#undef fprintf
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
    const char *p, *ls, *at, *col, *brk;

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
    if(!ls)
        ls = strchr(p, '?');
    if(ls)
        av_strlcpy(path, ls, path_size);
    else
        ls = &p[strlen(p)]; // XXX

    /* the rest is hostname, use that to parse auth/port */
    if (ls != p) {
        /* authorization (user[:pass]@hostname) */
        if ((at = strchr(p, '@')) && at < ls) {
            av_strlcpy(authorization, p,
                       FFMIN(authorization_size, at + 1 - p));
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
        c = toupper((unsigned char) *p++);
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
        av_log(NULL, AV_LOG_ERROR, "Ignoring attempt to set invalid timebase for st:%d\n", s->index);
        return;
    }
    s->time_base = new_tb;
    s->pts_wrap_bits = pts_wrap_bits;
}

int ff_url_join(char *str, int size, const char *proto,
                const char *authorization, const char *hostname,
                int port, const char *fmt, ...)
{
#if CONFIG_NETWORK
    struct addrinfo hints, *ai;
#endif

    str[0] = '\0';
    if (proto)
        av_strlcatf(str, size, "%s://", proto);
    if (authorization && authorization[0])
        av_strlcatf(str, size, "%s@", authorization);
#if CONFIG_NETWORK && defined(AF_INET6)
    /* Determine if hostname is a numerical IPv6 address,
     * properly escape it within [] in that case. */
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_NUMERICHOST;
    if (!getaddrinfo(hostname, NULL, &hints, &ai)) {
        if (ai->ai_family == AF_INET6) {
            av_strlcat(str, "[", size);
            av_strlcat(str, hostname, size);
            av_strlcat(str, "]", size);
        } else {
            av_strlcat(str, hostname, size);
        }
        freeaddrinfo(ai);
    } else
#endif
        /* Not an IPv6 address, just output the plain string. */
        av_strlcat(str, hostname, size);

    if (port >= 0)
        av_strlcatf(str, size, ":%d", port);
    if (fmt) {
        va_list vl;
        int len = strlen(str);

        va_start(vl, fmt);
        vsnprintf(str + len, size > len ? size - len : 0, fmt, vl);
        va_end(vl);
    }
    return strlen(str);
}

int ff_write_chained(AVFormatContext *dst, int dst_stream, AVPacket *pkt,
                     AVFormatContext *src)
{
    AVPacket local_pkt;

    local_pkt = *pkt;
    local_pkt.stream_index = dst_stream;
    if (pkt->pts != AV_NOPTS_VALUE)
        local_pkt.pts = av_rescale_q(pkt->pts,
                                     src->streams[pkt->stream_index]->time_base,
                                     dst->streams[dst_stream]->time_base);
    if (pkt->dts != AV_NOPTS_VALUE)
        local_pkt.dts = av_rescale_q(pkt->dts,
                                     src->streams[pkt->stream_index]->time_base,
                                     dst->streams[dst_stream]->time_base);
    return av_write_frame(dst, &local_pkt);
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
        while (*ptr && (isspace(*ptr) || *ptr == ','))
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
            for (; *ptr && !(isspace(*ptr) || *ptr == ','); ptr++)
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

void ff_make_absolute_url(char *buf, int size, const char *base,
                          const char *rel)
{
    char *sep;
    /* Absolute path, relative to the current server */
    if (base && strstr(base, "://") && rel[0] == '/') {
        if (base != buf)
            av_strlcpy(buf, base, size);
        sep = strstr(buf, "://");
        if (sep) {
            sep += 3;
            sep = strchr(sep, '/');
            if (sep)
                *sep = '\0';
        }
        av_strlcat(buf, rel, size);
        return;
    }
    /* If rel actually is an absolute url, just copy it */
    if (!base || strstr(rel, "://") || rel[0] == '/') {
        av_strlcpy(buf, rel, size);
        return;
    }
    if (base != buf)
        av_strlcpy(buf, base, size);
    /* Remove the file name from the base url */
    sep = strrchr(buf, '/');
    if (sep)
        sep[1] = '\0';
    else
        buf[0] = '\0';
    while (av_strstart(rel, "../", NULL) && sep) {
        /* Remove the path delimiter at the end */
        sep[0] = '\0';
        sep = strrchr(buf, '/');
        /* If the next directory name to pop off is "..", break here */
        if (!strcmp(sep ? &sep[1] : buf, "..")) {
            /* Readd the slash we just removed */
            av_strlcat(buf, "/", size);
            break;
        }
        /* Cut off the directory name */
        if (sep)
            sep[1] = '\0';
        else
            buf[0] = '\0';
        rel += 3;
    }
    av_strlcat(buf, rel, size);
}

int64_t ff_iso8601_to_unix_time(const char *datestr)
{
#if HAVE_STRPTIME
    struct tm time1 = {0}, time2 = {0};
    char *ret1, *ret2;
    ret1 = strptime(datestr, "%Y - %m - %d %T", &time1);
    ret2 = strptime(datestr, "%Y - %m - %dT%T", &time2);
    if (ret2 && !ret1)
        return av_timegm(&time2);
    else
        return av_timegm(&time1);
#else
    av_log(NULL, AV_LOG_WARNING, "strptime() unavailable on this system, cannot convert "
                                 "the date string.\n");
    return 0;
#endif
}

int avformat_query_codec(AVOutputFormat *ofmt, enum CodecID codec_id, int std_compliance)
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
