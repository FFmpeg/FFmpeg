/*
 * FFM (ffserver live feed) demuxer
 * Copyright (c) 2001 Fabrice Bellard
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

#include <stdint.h>

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/intfloat.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/internal.h"
#include "avformat.h"
#include "internal.h"
#include "ffm.h"
#include "avio_internal.h"

static int ffm_is_avail_data(AVFormatContext *s, int size)
{
    FFMContext *ffm = s->priv_data;
    int64_t pos, avail_size;
    ptrdiff_t len;

    len = ffm->packet_end - ffm->packet_ptr;
    if (size <= len)
        return 1;
    pos = avio_tell(s->pb);
    if (!ffm->write_index) {
        if (pos == ffm->file_size)
            return AVERROR_EOF;
        avail_size = ffm->file_size - pos;
    } else {
    if (pos == ffm->write_index) {
        /* exactly at the end of stream */
        if (ffm->server_attached)
            return AVERROR(EAGAIN);
        else
            return AVERROR_INVALIDDATA;
    } else if (pos < ffm->write_index) {
        avail_size = ffm->write_index - pos;
    } else {
        avail_size = (ffm->file_size - pos) + (ffm->write_index - FFM_PACKET_SIZE);
    }
    }
    avail_size = (avail_size / ffm->packet_size) * (ffm->packet_size - FFM_HEADER_SIZE) + len;
    if (size <= avail_size)
        return 1;
    else if (ffm->server_attached)
        return AVERROR(EAGAIN);
    else
        return AVERROR_INVALIDDATA;
}

static int ffm_resync(AVFormatContext *s, uint32_t state)
{
    av_log(s, AV_LOG_ERROR, "resyncing\n");
    while (state != PACKET_ID) {
        if (avio_feof(s->pb)) {
            av_log(s, AV_LOG_ERROR, "cannot find FFM syncword\n");
            return -1;
        }
        state = (state << 8) | avio_r8(s->pb);
    }
    return 0;
}

/* first is true if we read the frame header */
static int ffm_read_data(AVFormatContext *s,
                         uint8_t *buf, int size, int header)
{
    FFMContext *ffm = s->priv_data;
    AVIOContext *pb = s->pb;
    int fill_size, size1, frame_offset;
    uint32_t id;
    ptrdiff_t len;
    int64_t last_pos = -1;

    size1 = size;
    while (size > 0) {
    redo:
        len = ffm->packet_end - ffm->packet_ptr;
        if (len < 0)
            return -1;
        if (len > size)
            len = size;
        if (len == 0) {
            if (avio_tell(pb) == ffm->file_size) {
                if (ffm->server_attached) {
                    avio_seek(pb, ffm->packet_size, SEEK_SET);
                } else
                    return AVERROR_EOF;
            }
    retry_read:
            if (pb->buffer_size != ffm->packet_size) {
                int64_t tell = avio_tell(pb);
                int ret = ffio_set_buf_size(pb, ffm->packet_size);
                if (ret < 0)
                    return ret;
                avio_seek(pb, tell, SEEK_SET);
            }
            id = avio_rb16(pb); /* PACKET_ID */
            if (id != PACKET_ID) {
                if (ffm_resync(s, id) < 0)
                    return -1;
                last_pos = avio_tell(pb);
            }
            fill_size = avio_rb16(pb);
            ffm->dts = avio_rb64(pb);
            frame_offset = avio_rb16(pb);
            avio_read(pb, ffm->packet, ffm->packet_size - FFM_HEADER_SIZE);
            if (ffm->packet_size < FFM_HEADER_SIZE + fill_size || frame_offset < 0) {
                return -1;
            }
            ffm->packet_end = ffm->packet + (ffm->packet_size - FFM_HEADER_SIZE - fill_size);
            /* if first packet or resynchronization packet, we must
               handle it specifically */
            if (ffm->first_packet || (frame_offset & 0x8000)) {
                if (!frame_offset) {
                    /* This packet has no frame headers in it */
                    if (avio_tell(pb) >= ffm->packet_size * 3LL) {
                        int64_t seekback = FFMIN(ffm->packet_size * 2LL, avio_tell(pb) - last_pos);
                        seekback = FFMAX(seekback, 0);
                        avio_seek(pb, -seekback, SEEK_CUR);
                        goto retry_read;
                    }
                    /* This is bad, we cannot find a valid frame header */
                    return 0;
                }
                ffm->first_packet = 0;
                if ((frame_offset & 0x7fff) < FFM_HEADER_SIZE) {
                    ffm->packet_end = ffm->packet_ptr;
                    return -1;
                }
                ffm->packet_ptr = ffm->packet + (frame_offset & 0x7fff) - FFM_HEADER_SIZE;
                if (!header)
                    break;
            } else {
                ffm->packet_ptr = ffm->packet;
            }
            goto redo;
        }
        memcpy(buf, ffm->packet_ptr, len);
        buf += len;
        ffm->packet_ptr += len;
        size -= len;
        header = 0;
    }
    return size1 - size;
}

/* ensure that actual seeking happens between FFM_PACKET_SIZE
   and file_size - FFM_PACKET_SIZE */
static int64_t ffm_seek1(AVFormatContext *s, int64_t pos1)
{
    FFMContext *ffm = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pos;

    pos = FFMIN(pos1, ffm->file_size - FFM_PACKET_SIZE);
    pos = FFMAX(pos, FFM_PACKET_SIZE);
    ff_dlog(s, "seek to %"PRIx64" -> %"PRIx64"\n", pos1, pos);
    return avio_seek(pb, pos, SEEK_SET);
}

static int64_t get_dts(AVFormatContext *s, int64_t pos)
{
    AVIOContext *pb = s->pb;
    int64_t dts;

    ffm_seek1(s, pos);
    avio_skip(pb, 4);
    dts = avio_rb64(pb);
    ff_dlog(s, "dts=%0.6f\n", dts / 1000000.0);
    return dts;
}

static void adjust_write_index(AVFormatContext *s)
{
    FFMContext *ffm = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pts;
    //int64_t orig_write_index = ffm->write_index;
    int64_t pos_min, pos_max;
    int64_t pts_start;
    int64_t ptr = avio_tell(pb);


    pos_min = 0;
    pos_max = ffm->file_size - 2 * FFM_PACKET_SIZE;

    pts_start = get_dts(s, pos_min);

    pts = get_dts(s, pos_max);

    if (pts - 100000 > pts_start)
        goto end;

    ffm->write_index = FFM_PACKET_SIZE;

    pts_start = get_dts(s, pos_min);

    pts = get_dts(s, pos_max);

    if (pts - 100000 <= pts_start) {
        while (1) {
            int64_t newpos;
            int64_t newpts;

            newpos = ((pos_max + pos_min) / (2 * FFM_PACKET_SIZE)) * FFM_PACKET_SIZE;

            if (newpos == pos_min)
                break;

            newpts = get_dts(s, newpos);

            if (newpts - 100000 <= pts) {
                pos_max = newpos;
                pts = newpts;
            } else {
                pos_min = newpos;
            }
        }
        ffm->write_index += pos_max;
    }

 end:
    avio_seek(pb, ptr, SEEK_SET);
}


static int ffm_append_recommended_configuration(AVStream *st, char **conf)
{
    int ret;
    size_t newsize;
    av_assert0(conf && st);
    if (!*conf)
        return 0;
    if (!st->recommended_encoder_configuration) {
        st->recommended_encoder_configuration = *conf;
        *conf = 0;
        return 0;
    }
    newsize = strlen(*conf) + strlen(st->recommended_encoder_configuration) + 2;
    if ((ret = av_reallocp(&st->recommended_encoder_configuration, newsize)) < 0)
        return ret;
    av_strlcat(st->recommended_encoder_configuration, ",", newsize);
    av_strlcat(st->recommended_encoder_configuration, *conf, newsize);
    av_freep(conf);
    return 0;
}

#define VALIDATE_PARAMETER(parameter, name, check) {                              \
    if (check) {                                                                  \
        av_log(s, AV_LOG_ERROR, "Invalid " name " %d\n", codecpar->parameter);   \
        ret = AVERROR_INVALIDDATA;                                                \
        goto fail;                                                                \
    }                                                                             \
}

static int ffm2_read_header(AVFormatContext *s)
{
    FFMContext *ffm = s->priv_data;
    AVStream *st = NULL;
    AVIOContext *pb = s->pb;
    AVCodecContext *dummy_codec = NULL;
    AVCodecParameters *codecpar = NULL;
    const AVCodecDescriptor *codec_desc;
    int ret;
    int f_main = 0, f_cprv = -1, f_stvi = -1, f_stau = -1;
    AVCodec *enc;
    char *buffer;

    ffm->packet_size = avio_rb32(pb);
    if (ffm->packet_size != FFM_PACKET_SIZE) {
        av_log(s, AV_LOG_ERROR, "Invalid packet size %d, expected size was %d\n",
               ffm->packet_size, FFM_PACKET_SIZE);
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    ffm->write_index = avio_rb64(pb);
    /* get also filesize */
    if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
        ffm->file_size = avio_size(pb);
        if (ffm->write_index && 0)
            adjust_write_index(s);
    } else {
        ffm->file_size = (UINT64_C(1) << 63) - 1;
    }
    dummy_codec = avcodec_alloc_context3(NULL);

    while(!avio_feof(pb)) {
        unsigned id = avio_rb32(pb);
        unsigned size = avio_rb32(pb);
        int64_t next = avio_tell(pb) + size;
        char rc_eq_buf[128];
        int flags;

        if(!id)
            break;

        switch(id) {
        case MKBETAG('M', 'A', 'I', 'N'):
            if (f_main++) {
                ret = AVERROR(EINVAL);
                goto fail;
            }
            avio_rb32(pb); /* nb_streams */
            avio_rb32(pb); /* total bitrate */
            break;
        case MKBETAG('C', 'O', 'M', 'M'):
            f_cprv = f_stvi = f_stau = 0;
            st = avformat_new_stream(s, NULL);
            if (!st) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            avpriv_set_pts_info(st, 64, 1, 1000000);

            codecpar = st->codecpar;
            /* generic info */
            codecpar->codec_id = avio_rb32(pb);
            codec_desc = avcodec_descriptor_get(codecpar->codec_id);
            if (!codec_desc) {
                av_log(s, AV_LOG_ERROR, "Invalid codec id: %d\n", codecpar->codec_id);
                codecpar->codec_id = AV_CODEC_ID_NONE;
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            codecpar->codec_type = avio_r8(pb);
            if (codecpar->codec_type != codec_desc->type) {
                av_log(s, AV_LOG_ERROR, "Codec type mismatch: expected %d, found %d\n",
                       codec_desc->type, codecpar->codec_type);
                codecpar->codec_id = AV_CODEC_ID_NONE;
                codecpar->codec_type = AVMEDIA_TYPE_UNKNOWN;
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            codecpar->bit_rate = avio_rb32(pb);
            if (codecpar->bit_rate < 0) {
                av_log(s, AV_LOG_ERROR, "Invalid bit rate %"PRId64"\n", codecpar->bit_rate);
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            flags = avio_rb32(pb);
#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
            st->codec->flags = flags;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
            avio_rb32(pb); // flags2
            avio_rb32(pb); // debug
            if (flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
                int size = avio_rb32(pb);
                if (size < 0 || size >= FF_MAX_EXTRADATA_SIZE) {
                    av_log(s, AV_LOG_ERROR, "Invalid extradata size %d\n", size);
                    ret = AVERROR_INVALIDDATA;
                    goto fail;
                }
                codecpar->extradata = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
                if (!codecpar->extradata) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                codecpar->extradata_size = size;
                avio_read(pb, codecpar->extradata, size);
            }
            break;
        case MKBETAG('S', 'T', 'V', 'I'):
            if (f_stvi++ || codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
                ret = AVERROR(EINVAL);
                goto fail;
            }
            avio_rb32(pb); // time_base.num
            avio_rb32(pb); // time_base.den
            codecpar->width = avio_rb16(pb);
            codecpar->height = avio_rb16(pb);
            ret = av_image_check_size(codecpar->width, codecpar->height, 0, s);
            if (ret < 0)
                goto fail;
            avio_rb16(pb); // gop_size
            codecpar->format = avio_rb32(pb);
            if (!av_pix_fmt_desc_get(codecpar->format)) {
                av_log(s, AV_LOG_ERROR, "Invalid pix fmt id: %d\n", codecpar->format);
                codecpar->format = AV_PIX_FMT_NONE;
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            avio_r8(pb);   // qmin
            avio_r8(pb);   // qmax
            avio_r8(pb);   // max_qdiff
            avio_rb16(pb); // qcompress / 10000.0
            avio_rb16(pb); // qblur / 10000.0
            avio_rb32(pb); // bit_rate_tolerance
            avio_get_str(pb, INT_MAX, rc_eq_buf, sizeof(rc_eq_buf));

            avio_rb32(pb); // rc_max_rate
            avio_rb32(pb); // rc_min_rate
            avio_rb32(pb); // rc_buffer_size
            avio_rb64(pb); // i_quant_factor
            avio_rb64(pb); // b_quant_factor
            avio_rb64(pb); // i_quant_offset
            avio_rb64(pb); // b_quant_offset
            avio_rb32(pb); // dct_algo
            avio_rb32(pb); // strict_std_compliance
            avio_rb32(pb); // max_b_frames
            avio_rb32(pb); // mpeg_quant
            avio_rb32(pb); // intra_dc_precision
            avio_rb32(pb); // me_method
            avio_rb32(pb); // mb_decision
            avio_rb32(pb); // nsse_weight
            avio_rb32(pb); // frame_skip_cmp
            avio_rb64(pb); // rc_buffer_aggressivity
            codecpar->codec_tag = avio_rb32(pb);
            avio_r8(pb);   // thread_count
            avio_rb32(pb); // coder_type
            avio_rb32(pb); // me_cmp
            avio_rb32(pb); // me_subpel_quality
            avio_rb32(pb); // me_range
            avio_rb32(pb); // keyint_min
            avio_rb32(pb); // scenechange_threshold
            avio_rb32(pb); // b_frame_strategy
            avio_rb64(pb); // qcompress
            avio_rb64(pb); // qblur
            avio_rb32(pb); // max_qdiff
            avio_rb32(pb); // refs
            break;
        case MKBETAG('S', 'T', 'A', 'U'):
            if (f_stau++ || codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
                ret = AVERROR(EINVAL);
                goto fail;
            }
            codecpar->sample_rate = avio_rb32(pb);
            VALIDATE_PARAMETER(sample_rate, "sample rate",        codecpar->sample_rate < 0)
            codecpar->channels = avio_rl16(pb);
            VALIDATE_PARAMETER(channels,    "number of channels", codecpar->channels < 0)
            codecpar->frame_size = avio_rl16(pb);
            VALIDATE_PARAMETER(frame_size,  "frame size",         codecpar->frame_size < 0)
            break;
        case MKBETAG('C', 'P', 'R', 'V'):
            if (f_cprv++) {
                ret = AVERROR(EINVAL);
                goto fail;
            }
            enc = avcodec_find_encoder(codecpar->codec_id);
            if (enc && enc->priv_data_size && enc->priv_class) {
                buffer = av_malloc(size + 1);
                if (!buffer) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                avio_get_str(pb, size, buffer, size + 1);
                if ((ret = ffm_append_recommended_configuration(st, &buffer)) < 0)
                    goto fail;
            }
            break;
        case MKBETAG('S', '2', 'V', 'I'):
            if (f_stvi++ || !size || codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
                ret = AVERROR(EINVAL);
                goto fail;
            }
            buffer = av_malloc(size);
            if (!buffer) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            avio_get_str(pb, INT_MAX, buffer, size);
            // The lack of AVOptions support in AVCodecParameters makes this back and forth copying needed
            avcodec_parameters_to_context(dummy_codec, codecpar);
            av_set_options_string(dummy_codec, buffer, "=", ",");
            avcodec_parameters_from_context(codecpar, dummy_codec);
            if ((ret = ffm_append_recommended_configuration(st, &buffer)) < 0)
                goto fail;
            break;
        case MKBETAG('S', '2', 'A', 'U'):
            if (f_stau++ || !size || codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
                ret = AVERROR(EINVAL);
                goto fail;
            }
            buffer = av_malloc(size);
            if (!buffer) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            avio_get_str(pb, INT_MAX, buffer, size);
            // The lack of AVOptions support in AVCodecParameters makes this back and forth copying needed
            avcodec_parameters_to_context(dummy_codec, codecpar);
            av_set_options_string(dummy_codec, buffer, "=", ",");
            avcodec_parameters_from_context(codecpar, dummy_codec);
            if ((ret = ffm_append_recommended_configuration(st, &buffer)) < 0)
                goto fail;
            break;
        }
        avio_seek(pb, next, SEEK_SET);
    }

    /* get until end of block reached */
    while ((avio_tell(pb) % ffm->packet_size) != 0 && !pb->eof_reached)
        avio_r8(pb);

    /* init packet demux */
    ffm->packet_ptr = ffm->packet;
    ffm->packet_end = ffm->packet;
    ffm->frame_offset = 0;
    ffm->dts = 0;
    ffm->read_state = READ_HEADER;
    ffm->first_packet = 1;
    avcodec_free_context(&dummy_codec);
    return 0;
 fail:
    avcodec_free_context(&dummy_codec);
    return ret;
}

static int ffm_read_header(AVFormatContext *s)
{
    FFMContext *ffm = s->priv_data;
    AVStream *st;
    AVIOContext *pb = s->pb;
    AVCodecContext *dummy_codec = NULL;
    AVCodecParameters *codecpar;
    const AVCodecDescriptor *codec_desc;
    int i, nb_streams, ret;
    uint32_t tag;

    /* header */
    tag = avio_rl32(pb);
    if (tag == MKTAG('F', 'F', 'M', '2'))
        return ffm2_read_header(s);
    if (tag != MKTAG('F', 'F', 'M', '1')) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }
    ffm->packet_size = avio_rb32(pb);
    if (ffm->packet_size != FFM_PACKET_SIZE) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }
    ffm->write_index = avio_rb64(pb);
    /* get also filesize */
    if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
        ffm->file_size = avio_size(pb);
        if (ffm->write_index && 0)
            adjust_write_index(s);
    } else {
        ffm->file_size = (UINT64_C(1) << 63) - 1;
    }
    dummy_codec = avcodec_alloc_context3(NULL);

    nb_streams = avio_rb32(pb);
    avio_rb32(pb); /* total bitrate */
    /* read each stream */
    for(i=0;i<nb_streams;i++) {
        char rc_eq_buf[128];
        int flags;

        st = avformat_new_stream(s, NULL);
        if (!st) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        avpriv_set_pts_info(st, 64, 1, 1000000);

        codecpar = st->codecpar;
        /* generic info */
        codecpar->codec_id = avio_rb32(pb);
        codec_desc = avcodec_descriptor_get(codecpar->codec_id);
        if (!codec_desc) {
            av_log(s, AV_LOG_ERROR, "Invalid codec id: %d\n", codecpar->codec_id);
            codecpar->codec_id = AV_CODEC_ID_NONE;
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        codecpar->codec_type = avio_r8(pb); /* codec_type */
        if (codecpar->codec_type != codec_desc->type) {
            av_log(s, AV_LOG_ERROR, "Codec type mismatch: expected %d, found %d\n",
                   codec_desc->type, codecpar->codec_type);
            codecpar->codec_id = AV_CODEC_ID_NONE;
            codecpar->codec_type = AVMEDIA_TYPE_UNKNOWN;
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        codecpar->bit_rate = avio_rb32(pb);
        if (codecpar->bit_rate < 0) {
            av_log(s, AV_LOG_WARNING, "Invalid bit rate %"PRId64"\n", codecpar->bit_rate);
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        flags = avio_rb32(pb);
#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
            st->codec->flags = flags;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        avio_rb32(pb); // flags2
        avio_rb32(pb); // debug
        /* specific info */
        switch(codecpar->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            avio_rb32(pb); // time_base.num
            avio_rb32(pb); // time_base.den
            codecpar->width = avio_rb16(pb);
            codecpar->height = avio_rb16(pb);
            if ((ret = av_image_check_size(codecpar->width, codecpar->height, 0, s)) < 0)
                goto fail;
            avio_rb16(pb); // gop_size
            codecpar->format = avio_rb32(pb);
            if (!av_pix_fmt_desc_get(codecpar->format)) {
                av_log(s, AV_LOG_ERROR, "Invalid pix fmt id: %d\n", codecpar->format);
                codecpar->format = AV_PIX_FMT_NONE;
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            avio_r8(pb);   // qmin
            avio_r8(pb);   // qmax
            avio_r8(pb);   // max_qdiff
            avio_rb16(pb); // qcompress / 10000.0
            avio_rb16(pb); // qblur / 10000.0
            avio_rb32(pb); // bit_rate_tolerance
            avio_get_str(pb, INT_MAX, rc_eq_buf, sizeof(rc_eq_buf));

            avio_rb32(pb); // rc_max_rate
            avio_rb32(pb); // rc_min_rate
            avio_rb32(pb); // rc_buffer_size
            avio_rb64(pb); // i_quant_factor
            avio_rb64(pb); // b_quant_factor
            avio_rb64(pb); // i_quant_offset
            avio_rb64(pb); // b_quant_offset
            avio_rb32(pb); // dct_algo
            avio_rb32(pb); // strict_std_compliance
            avio_rb32(pb); // max_b_frames
            avio_rb32(pb); // mpeg_quant
            avio_rb32(pb); // intra_dc_precision
            avio_rb32(pb); // me_method
            avio_rb32(pb); // mb_decision
            avio_rb32(pb); // nsse_weight
            avio_rb32(pb); // frame_skip_cmp
            avio_rb64(pb); // rc_buffer_aggressivity
            codecpar->codec_tag = avio_rb32(pb);
            avio_r8(pb);   // thread_count
            avio_rb32(pb); // coder_type
            avio_rb32(pb); // me_cmp
            avio_rb32(pb); // me_subpel_quality
            avio_rb32(pb); // me_range
            avio_rb32(pb); // keyint_min
            avio_rb32(pb); // scenechange_threshold
            avio_rb32(pb); // b_frame_strategy
            avio_rb64(pb); // qcompress
            avio_rb64(pb); // qblur
            avio_rb32(pb); // max_qdiff
            avio_rb32(pb); // refs
            break;
        case AVMEDIA_TYPE_AUDIO:
            codecpar->sample_rate = avio_rb32(pb);
            VALIDATE_PARAMETER(sample_rate, "sample rate",        codecpar->sample_rate < 0)
            codecpar->channels = avio_rl16(pb);
            VALIDATE_PARAMETER(channels,    "number of channels", codecpar->channels < 0)
            codecpar->frame_size = avio_rl16(pb);
            VALIDATE_PARAMETER(frame_size,  "frame size",         codecpar->frame_size < 0)
            break;
        default:
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        if (flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
            int size = avio_rb32(pb);
            if (size < 0 || size >= FF_MAX_EXTRADATA_SIZE) {
                av_log(s, AV_LOG_ERROR, "Invalid extradata size %d\n", size);
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            codecpar->extradata = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!codecpar->extradata) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            codecpar->extradata_size = size;
            avio_read(pb, codecpar->extradata, size);
        }
    }

    /* get until end of block reached */
    while ((avio_tell(pb) % ffm->packet_size) != 0 && !pb->eof_reached)
        avio_r8(pb);

    /* init packet demux */
    ffm->packet_ptr = ffm->packet;
    ffm->packet_end = ffm->packet;
    ffm->frame_offset = 0;
    ffm->dts = 0;
    ffm->read_state = READ_HEADER;
    ffm->first_packet = 1;
    avcodec_free_context(&dummy_codec);
    return 0;
 fail:
    avcodec_free_context(&dummy_codec);
    return ret;
}

/* return < 0 if eof */
static int ffm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int size;
    FFMContext *ffm = s->priv_data;
    int duration, ret;

    switch(ffm->read_state) {
    case READ_HEADER:
        if ((ret = ffm_is_avail_data(s, FRAME_HEADER_SIZE+4)) < 0)
            return ret;

        ff_dlog(s, "pos=%08"PRIx64" spos=%"PRIx64", write_index=%"PRIx64" size=%"PRIx64"\n",
               avio_tell(s->pb), s->pb->pos, ffm->write_index, ffm->file_size);
        if (ffm_read_data(s, ffm->header, FRAME_HEADER_SIZE, 1) !=
            FRAME_HEADER_SIZE)
            return -1;
        if (ffm->header[1] & FLAG_DTS)
            if (ffm_read_data(s, ffm->header+16, 4, 1) != 4)
                return -1;
        ffm->read_state = READ_DATA;
        /* fall through */
    case READ_DATA:
        size = AV_RB24(ffm->header + 2);
        if ((ret = ffm_is_avail_data(s, size)) < 0)
            return ret;

        duration = AV_RB24(ffm->header + 5);

        if (av_new_packet(pkt, size) < 0) {
            return AVERROR(ENOMEM);
        }
        pkt->stream_index = ffm->header[0];
        if ((unsigned)pkt->stream_index >= s->nb_streams) {
            av_log(s, AV_LOG_ERROR, "invalid stream index %d\n", pkt->stream_index);
            av_packet_unref(pkt);
            ffm->read_state = READ_HEADER;
            return -1;
        }
        pkt->pos = avio_tell(s->pb);
        if (ffm->header[1] & FLAG_KEY_FRAME)
            pkt->flags |= AV_PKT_FLAG_KEY;

        ffm->read_state = READ_HEADER;
        if (ffm_read_data(s, pkt->data, size, 0) != size) {
            /* bad case: desynchronized packet. we cancel all the packet loading */
            av_packet_unref(pkt);
            return -1;
        }
        pkt->pts = AV_RB64(ffm->header+8);
        if (ffm->header[1] & FLAG_DTS)
            pkt->dts = pkt->pts - AV_RB32(ffm->header+16);
        else
            pkt->dts = pkt->pts;
        pkt->duration = duration;
        break;
    }
    return 0;
}

/* seek to a given time in the file. The file read pointer is
   positioned at or before pts. XXX: the following code is quite
   approximative */
static int ffm_seek(AVFormatContext *s, int stream_index, int64_t wanted_pts, int flags)
{
    FFMContext *ffm = s->priv_data;
    int64_t pos_min, pos_max, pos;
    int64_t pts_min, pts_max, pts;
    double pos1;

    ff_dlog(s, "wanted_pts=%0.6f\n", wanted_pts / 1000000.0);
    /* find the position using linear interpolation (better than
       dichotomy in typical cases) */
    if (ffm->write_index && ffm->write_index < ffm->file_size) {
        if (get_dts(s, FFM_PACKET_SIZE) < wanted_pts) {
            pos_min = FFM_PACKET_SIZE;
            pos_max = ffm->write_index - FFM_PACKET_SIZE;
        } else {
            pos_min = ffm->write_index;
            pos_max = ffm->file_size - FFM_PACKET_SIZE;
        }
    } else {
        pos_min = FFM_PACKET_SIZE;
        pos_max = ffm->file_size - FFM_PACKET_SIZE;
    }
    while (pos_min <= pos_max) {
        pts_min = get_dts(s, pos_min);
        pts_max = get_dts(s, pos_max);
        if (pts_min > wanted_pts || pts_max <= wanted_pts) {
            pos = pts_min > wanted_pts ? pos_min : pos_max;
            goto found;
        }
        /* linear interpolation */
        pos1 = (double)(pos_max - pos_min) * (double)(wanted_pts - pts_min) /
            (double)(pts_max - pts_min);
        pos = (((int64_t)pos1) / FFM_PACKET_SIZE) * FFM_PACKET_SIZE;
        if (pos <= pos_min)
            pos = pos_min;
        else if (pos >= pos_max)
            pos = pos_max;
        pts = get_dts(s, pos);
        /* check if we are lucky */
        if (pts == wanted_pts) {
            goto found;
        } else if (pts > wanted_pts) {
            pos_max = pos - FFM_PACKET_SIZE;
        } else {
            pos_min = pos + FFM_PACKET_SIZE;
        }
    }
    pos = (flags & AVSEEK_FLAG_BACKWARD) ? pos_min : pos_max;

 found:
    if (ffm_seek1(s, pos) < 0)
        return -1;

    /* reset read state */
    ffm->read_state = READ_HEADER;
    ffm->packet_ptr = ffm->packet;
    ffm->packet_end = ffm->packet;
    ffm->first_packet = 1;

    return 0;
}

static int ffm_probe(AVProbeData *p)
{
    if (
        p->buf[0] == 'F' && p->buf[1] == 'F' && p->buf[2] == 'M' &&
        (p->buf[3] == '1' || p->buf[3] == '2'))
        return AVPROBE_SCORE_MAX + 1;
    return 0;
}

static const AVOption options[] = {
    {"server_attached", NULL, offsetof(FFMContext, server_attached), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, AV_OPT_FLAG_EXPORT },
    {"ffm_write_index", NULL, offsetof(FFMContext, write_index), AV_OPT_TYPE_INT64, {.i64 = 0}, 0, INT64_MAX, AV_OPT_FLAG_EXPORT },
    {"ffm_file_size", NULL, offsetof(FFMContext, file_size), AV_OPT_TYPE_INT64, {.i64 = 0}, 0, INT64_MAX, AV_OPT_FLAG_EXPORT },
    { NULL },
};

static const AVClass ffm_class = {
    .class_name = "ffm demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};
AVInputFormat ff_ffm_demuxer = {
    .name           = "ffm",
    .long_name      = NULL_IF_CONFIG_SMALL("FFM (FFserver live feed)"),
    .priv_data_size = sizeof(FFMContext),
    .read_probe     = ffm_probe,
    .read_header    = ffm_read_header,
    .read_packet    = ffm_read_packet,
    .read_seek      = ffm_seek,
    .priv_class     = &ffm_class,
};
