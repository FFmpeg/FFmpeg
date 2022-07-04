/*
 * Flash Compatible Streaming Format muxer
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2003 Tinic Uro
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

#include "config_components.h"

#include "libavcodec/put_bits.h"
#include "libavutil/avassert.h"
#include "libavutil/fifo.h"
#include "avformat.h"
#include "flv.h"
#include "swf.h"

#define AUDIO_FIFO_SIZE 65536

typedef struct SWFEncContext {
    int64_t duration_pos;
    int64_t tag_pos;
    int64_t vframes_pos;
    int samples_per_frame;
    int sound_samples;
    int swf_frame_number;
    int video_frame_number;
    int tag;
    AVFifo *audio_fifo;
    AVCodecParameters *audio_par, *video_par;
    AVStream *video_st;
} SWFEncContext;

static void put_swf_tag(AVFormatContext *s, int tag)
{
    SWFEncContext *swf = s->priv_data;
    AVIOContext *pb = s->pb;

    swf->tag_pos = avio_tell(pb);
    swf->tag = tag;
    /* reserve some room for the tag */
    if (tag & TAG_LONG) {
        avio_wl16(pb, 0);
        avio_wl32(pb, 0);
    } else {
        avio_wl16(pb, 0);
    }
}

static void put_swf_end_tag(AVFormatContext *s)
{
    SWFEncContext *swf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pos;
    int tag_len, tag;

    pos = avio_tell(pb);
    tag_len = pos - swf->tag_pos - 2;
    tag = swf->tag;
    avio_seek(pb, swf->tag_pos, SEEK_SET);
    if (tag & TAG_LONG) {
        tag &= ~TAG_LONG;
        avio_wl16(pb, (tag << 6) | 0x3f);
        avio_wl32(pb, tag_len - 4);
    } else {
        av_assert0(tag_len < 0x3f);
        avio_wl16(pb, (tag << 6) | tag_len);
    }
    avio_seek(pb, pos, SEEK_SET);
}

static inline void max_nbits(int *nbits_ptr, int val)
{
    int n;

    if (val == 0)
        return;
    val = FFABS(val);
    n = 1;
    while (val != 0) {
        n++;
        val >>= 1;
    }
    if (n > *nbits_ptr)
        *nbits_ptr = n;
}

static void put_swf_rect(AVIOContext *pb,
                         int xmin, int xmax, int ymin, int ymax)
{
    PutBitContext p;
    uint8_t buf[256];
    int nbits, mask;

    init_put_bits(&p, buf, sizeof(buf));

    nbits = 0;
    max_nbits(&nbits, xmin);
    max_nbits(&nbits, xmax);
    max_nbits(&nbits, ymin);
    max_nbits(&nbits, ymax);
    mask = (1 << nbits) - 1;

    /* rectangle info */
    put_bits(&p, 5, nbits);
    put_bits(&p, nbits, xmin & mask);
    put_bits(&p, nbits, xmax & mask);
    put_bits(&p, nbits, ymin & mask);
    put_bits(&p, nbits, ymax & mask);

    flush_put_bits(&p);
    avio_write(pb, buf, put_bits_ptr(&p) - p.buf);
}

static void put_swf_line_edge(PutBitContext *pb, int dx, int dy)
{
    int nbits, mask;

    put_bits(pb, 1, 1); /* edge */
    put_bits(pb, 1, 1); /* line select */
    nbits = 2;
    max_nbits(&nbits, dx);
    max_nbits(&nbits, dy);

    mask = (1 << nbits) - 1;
    put_bits(pb, 4, nbits - 2); /* 16 bits precision */
    if (dx == 0) {
        put_bits(pb, 1, 0);
        put_bits(pb, 1, 1);
        put_bits(pb, nbits, dy & mask);
    } else if (dy == 0) {
        put_bits(pb, 1, 0);
        put_bits(pb, 1, 0);
        put_bits(pb, nbits, dx & mask);
    } else {
        put_bits(pb, 1, 1);
        put_bits(pb, nbits, dx & mask);
        put_bits(pb, nbits, dy & mask);
    }
}

#define FRAC_BITS 16

static void put_swf_matrix(AVIOContext *pb,
                           int a, int b, int c, int d, int tx, int ty)
{
    PutBitContext p;
    uint8_t buf[256];
    int nbits;

    init_put_bits(&p, buf, sizeof(buf));

    put_bits(&p, 1, 1); /* a, d present */
    nbits = 1;
    max_nbits(&nbits, a);
    max_nbits(&nbits, d);
    put_bits(&p, 5, nbits); /* nb bits */
    put_bits(&p, nbits, a);
    put_bits(&p, nbits, d);

    put_bits(&p, 1, 1); /* b, c present */
    nbits = 1;
    max_nbits(&nbits, c);
    max_nbits(&nbits, b);
    put_bits(&p, 5, nbits); /* nb bits */
    put_bits(&p, nbits, c);
    put_bits(&p, nbits, b);

    nbits = 1;
    max_nbits(&nbits, tx);
    max_nbits(&nbits, ty);
    put_bits(&p, 5, nbits); /* nb bits */
    put_bits(&p, nbits, tx);
    put_bits(&p, nbits, ty);

    flush_put_bits(&p);
    avio_write(pb, buf, put_bits_ptr(&p) - p.buf);
}

static int swf_write_header(AVFormatContext *s)
{
    SWFEncContext *swf = s->priv_data;
    AVIOContext *pb = s->pb;
    PutBitContext p;
    uint8_t buf1[256];
    int i, width, height, rate, rate_base;
    int version;

    swf->sound_samples = 0;
    swf->swf_frame_number = 0;
    swf->video_frame_number = 0;

    for(i=0;i<s->nb_streams;i++) {
        AVCodecParameters *par = s->streams[i]->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (swf->audio_par) {
                av_log(s, AV_LOG_ERROR, "SWF muxer only supports 1 audio stream\n");
                return AVERROR_INVALIDDATA;
            }
            if (par->codec_id == AV_CODEC_ID_MP3) {
                swf->audio_par = par;
                swf->audio_fifo = av_fifo_alloc2(AUDIO_FIFO_SIZE, 1, 0);
                if (!swf->audio_fifo)
                    return AVERROR(ENOMEM);
            } else {
                av_log(s, AV_LOG_ERROR, "SWF muxer only supports MP3\n");
                return -1;
            }
        } else {
            if (swf->video_par) {
                av_log(s, AV_LOG_ERROR, "SWF muxer only supports 1 video stream\n");
                return AVERROR_INVALIDDATA;
            }
            if (ff_codec_get_tag(ff_swf_codec_tags, par->codec_id) ||
                par->codec_id == AV_CODEC_ID_PNG ||
                par->codec_id == AV_CODEC_ID_MJPEG) {
                swf->video_st  = s->streams[i];
                swf->video_par = par;
            } else {
                av_log(s, AV_LOG_ERROR, "SWF muxer only supports VP6, FLV, Flash Screen Video, PNG and MJPEG\n");
                return -1;
            }
        }
    }

    if (!swf->video_par) {
        /* currently, cannot work correctly if audio only */
        width = 320;
        height = 200;
        rate = 10;
        rate_base= 1;
    } else {
        width = swf->video_par->width;
        height = swf->video_par->height;
        // TODO: should be avg_frame_rate
        rate = swf->video_st->time_base.den;
        rate_base = swf->video_st->time_base.num;
    }

    if (!swf->audio_par)
        swf->samples_per_frame = (44100LL * rate_base) / rate;
    else
        swf->samples_per_frame = (swf->audio_par->sample_rate * rate_base) / rate;

    avio_write(pb, "FWS", 3);

    if (!strcmp("avm2", s->oformat->name))
        version = 9;
    else if (swf->video_par && (swf->video_par->codec_id == AV_CODEC_ID_VP6A ||
                                swf->video_par->codec_id == AV_CODEC_ID_VP6F ||
                                swf->video_par->codec_id == AV_CODEC_ID_PNG))
        version = 8; /* version 8 and above support VP6 and PNG codec */
    else if (swf->video_par && swf->video_par->codec_id == AV_CODEC_ID_FLASHSV)
        version = 7; /* version 7 and above support Flash Screen Video codec */
    else if (swf->video_par && swf->video_par->codec_id == AV_CODEC_ID_FLV1)
        version = 6; /* version 6 and above support FLV1 codec */
    else
        version = 4; /* version 4 for mpeg audio support */
    avio_w8(pb, version);

    avio_wl32(pb, DUMMY_FILE_SIZE); /* dummy size
                                      (will be patched if not streamed) */

    put_swf_rect(pb, 0, width * 20, 0, height * 20);
    if ((rate * 256LL) / rate_base >= (1<<16)) {
        av_log(s, AV_LOG_ERROR, "Invalid (too large) frame rate %d/%d\n", rate, rate_base);
        return AVERROR(EINVAL);
    }
    avio_wl16(pb, (rate * 256LL) / rate_base); /* frame rate */
    swf->duration_pos = avio_tell(pb);
    avio_wl16(pb, (uint16_t)(DUMMY_DURATION * (int64_t)rate / rate_base)); /* frame count */

    /* swf v8 and later files require a file attribute tag */
    if (version >= 8) {
        put_swf_tag(s, TAG_FILEATTRIBUTES);
        avio_wl32(pb, (version >= 9) << 3); /* set ActionScript v3/AVM2 flag */
        put_swf_end_tag(s);
    }

    /* define a shape with the jpeg inside */
    if (swf->video_par && (swf->video_par->codec_id == AV_CODEC_ID_MJPEG || swf->video_par->codec_id == AV_CODEC_ID_PNG)) {
        put_swf_tag(s, TAG_DEFINESHAPE);

        avio_wl16(pb, SHAPE_ID); /* ID of shape */
        /* bounding rectangle */
        put_swf_rect(pb, 0, width, 0, height);
        /* style info */
        avio_w8(pb, 1); /* one fill style */
        avio_w8(pb, 0x41); /* clipped bitmap fill */
        avio_wl16(pb, BITMAP_ID); /* bitmap ID */
        /* position of the bitmap */
        put_swf_matrix(pb, 1 << FRAC_BITS, 0,
                       0,  1 << FRAC_BITS, 0, 0);
        avio_w8(pb, 0); /* no line style */

        /* shape drawing */
        init_put_bits(&p, buf1, sizeof(buf1));
        put_bits(&p, 4, 1); /* one fill bit */
        put_bits(&p, 4, 0); /* zero line bit */

        put_bits(&p, 1, 0); /* not an edge */
        put_bits(&p, 5, FLAG_MOVETO | FLAG_SETFILL0);
        put_bits(&p, 5, 1); /* nbits */
        put_bits(&p, 1, 0); /* X */
        put_bits(&p, 1, 0); /* Y */
        put_bits(&p, 1, 1); /* set fill style 1 */

        /* draw the rectangle ! */
        put_swf_line_edge(&p, width, 0);
        put_swf_line_edge(&p, 0, height);
        put_swf_line_edge(&p, -width, 0);
        put_swf_line_edge(&p, 0, -height);

        /* end of shape */
        put_bits(&p, 1, 0); /* not an edge */
        put_bits(&p, 5, 0);

        flush_put_bits(&p);
        avio_write(pb, buf1, put_bits_ptr(&p) - p.buf);

        put_swf_end_tag(s);
    }

    if (swf->audio_par && swf->audio_par->codec_id == AV_CODEC_ID_MP3) {
        int v = 0;

        /* start sound */
        put_swf_tag(s, TAG_STREAMHEAD2);
        switch(swf->audio_par->sample_rate) {
        case 11025: v |= 1 << 2; break;
        case 22050: v |= 2 << 2; break;
        case 44100: v |= 3 << 2; break;
        default:
            /* not supported */
            av_log(s, AV_LOG_ERROR, "swf does not support that sample rate, choose from (44100, 22050, 11025).\n");
            return -1;
        }
        v |= 0x02; /* 16 bit playback */
        if (swf->audio_par->ch_layout.nb_channels == 2)
            v |= 0x01; /* stereo playback */
        avio_w8(s->pb, v);
        v |= 0x20; /* mp3 compressed */
        avio_w8(s->pb, v);
        avio_wl16(s->pb, swf->samples_per_frame);  /* avg samples per frame */
        avio_wl16(s->pb, 0);

        put_swf_end_tag(s);
    }

    return 0;
}

static int fifo_avio_wrapper(void *opaque, void *buf, size_t *nb_elems)
{
    avio_write(opaque, buf, *nb_elems);
    return 0;
}

static int swf_write_video(AVFormatContext *s,
                           AVCodecParameters *par, const uint8_t *buf, int size, unsigned pkt_flags)
{
    SWFEncContext *swf = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned codec_tag = ff_codec_get_tag(ff_swf_codec_tags, par->codec_id);

    /* Flash Player limit */
    if (swf->swf_frame_number == 16000)
        av_log(s, AV_LOG_INFO, "warning: Flash Player limit of 16000 frames reached\n");

    if (codec_tag) {
        if (swf->video_frame_number == 0) {
            /* create a new video object */
            put_swf_tag(s, TAG_VIDEOSTREAM);
            avio_wl16(pb, VIDEO_ID);
            swf->vframes_pos = avio_tell(pb);
            avio_wl16(pb, 15000); /* hard flash player limit */
            avio_wl16(pb, par->width);
            avio_wl16(pb, par->height);
            avio_w8(pb, 0);
            avio_w8(pb, codec_tag);
            put_swf_end_tag(s);

            /* place the video object for the first time */
            put_swf_tag(s, TAG_PLACEOBJECT2);
            avio_w8(pb, 0x36);
            avio_wl16(pb, 1);
            avio_wl16(pb, VIDEO_ID);
            put_swf_matrix(pb, 1 << FRAC_BITS, 0, 0, 1 << FRAC_BITS, 0, 0);
            avio_wl16(pb, swf->video_frame_number);
            avio_write(pb, "video", 5);
            avio_w8(pb, 0x00);
            put_swf_end_tag(s);
        } else {
            /* mark the character for update */
            put_swf_tag(s, TAG_PLACEOBJECT2);
            avio_w8(pb, 0x11);
            avio_wl16(pb, 1);
            avio_wl16(pb, swf->video_frame_number);
            put_swf_end_tag(s);
        }

        /* set video frame data */
        put_swf_tag(s, TAG_VIDEOFRAME | TAG_LONG);
        avio_wl16(pb, VIDEO_ID);
        avio_wl16(pb, swf->video_frame_number++);
        if (par->codec_id == AV_CODEC_ID_FLASHSV) {
            /* FrameType and CodecId is needed here even if it is not documented correctly in the SWF specs */
            int flags = codec_tag | (pkt_flags & AV_PKT_FLAG_KEY ? FLV_FRAME_KEY : FLV_FRAME_INTER);
            avio_w8(pb, flags);
        }
        avio_write(pb, buf, size);
        put_swf_end_tag(s);
    } else if (par->codec_id == AV_CODEC_ID_MJPEG || par->codec_id == AV_CODEC_ID_PNG) {
        if (swf->swf_frame_number > 0) {
            /* remove the shape */
            put_swf_tag(s, TAG_REMOVEOBJECT);
            avio_wl16(pb, SHAPE_ID); /* shape ID */
            avio_wl16(pb, 1); /* depth */
            put_swf_end_tag(s);

            /* free the bitmap */
            put_swf_tag(s, TAG_FREECHARACTER);
            avio_wl16(pb, BITMAP_ID);
            put_swf_end_tag(s);
        }

        put_swf_tag(s, TAG_JPEG2 | TAG_LONG);

        avio_wl16(pb, BITMAP_ID); /* ID of the image */

        /* a dummy jpeg header seems to be required */
        if (par->codec_id == AV_CODEC_ID_MJPEG)
            avio_wb32(pb, 0xffd8ffd9);
        /* write the jpeg/png image */
        avio_write(pb, buf, size);

        put_swf_end_tag(s);

        /* draw the shape */

        put_swf_tag(s, TAG_PLACEOBJECT);
        avio_wl16(pb, SHAPE_ID); /* shape ID */
        avio_wl16(pb, 1); /* depth */
        put_swf_matrix(pb, 20 << FRAC_BITS, 0, 0, 20 << FRAC_BITS, 0, 0);
        put_swf_end_tag(s);
    }

    swf->swf_frame_number++;

    /* streaming sound always should be placed just before showframe tags */
    if (swf->audio_par && av_fifo_can_read(swf->audio_fifo)) {
        size_t frame_size = av_fifo_can_read(swf->audio_fifo);
        put_swf_tag(s, TAG_STREAMBLOCK | TAG_LONG);
        avio_wl16(pb, swf->sound_samples);
        avio_wl16(pb, 0); // seek samples
        av_fifo_read_to_cb(swf->audio_fifo, fifo_avio_wrapper, pb, &frame_size);
        put_swf_end_tag(s);

        /* update FIFO */
        swf->sound_samples = 0;
    }

    /* output the frame */
    put_swf_tag(s, TAG_SHOWFRAME);
    put_swf_end_tag(s);

    return 0;
}

static int swf_write_audio(AVFormatContext *s, AVCodecParameters *par,
                           const uint8_t *buf, int size)
{
    SWFEncContext *swf = s->priv_data;

    /* Flash Player limit */
    if (swf->swf_frame_number == 16000)
        av_log(s, AV_LOG_INFO, "warning: Flash Player limit of 16000 frames reached\n");

    if (av_fifo_can_write(swf->audio_fifo) < size) {
        av_log(s, AV_LOG_ERROR, "audio fifo too small to mux audio essence\n");
        return -1;
    }

    av_fifo_write(swf->audio_fifo, buf, size);
    swf->sound_samples += av_get_audio_frame_duration2(par, size);

    /* if audio only stream make sure we add swf frames */
    if (!swf->video_par)
        swf_write_video(s, par, 0, 0, 0);

    return 0;
}

static int swf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecParameters *par = s->streams[pkt->stream_index]->codecpar;
    if (par->codec_type == AVMEDIA_TYPE_AUDIO)
        return swf_write_audio(s, par, pkt->data, pkt->size);
    else
        return swf_write_video(s, par, pkt->data, pkt->size, pkt->flags);
}

static int swf_write_trailer(AVFormatContext *s)
{
    SWFEncContext *swf = s->priv_data;
    AVIOContext *pb = s->pb;
    int file_size;

    put_swf_tag(s, TAG_END);
    put_swf_end_tag(s);

    /* patch file size and number of frames if not streamed */
    if ((s->pb->seekable & AVIO_SEEKABLE_NORMAL) && swf->video_par) {
        file_size = avio_tell(pb);
        avio_seek(pb, 4, SEEK_SET);
        avio_wl32(pb, file_size);
        avio_seek(pb, swf->duration_pos, SEEK_SET);
        avio_wl16(pb, swf->video_frame_number);
        if (swf->vframes_pos) {
        avio_seek(pb, swf->vframes_pos, SEEK_SET);
        avio_wl16(pb, swf->video_frame_number);
        }
        avio_seek(pb, file_size, SEEK_SET);
    }
    return 0;
}

static void swf_deinit(AVFormatContext *s)
{
    SWFEncContext *swf = s->priv_data;

    av_fifo_freep2(&swf->audio_fifo);
}

#if CONFIG_SWF_MUXER
const AVOutputFormat ff_swf_muxer = {
    .name              = "swf",
    .long_name         = NULL_IF_CONFIG_SMALL("SWF (ShockWave Flash)"),
    .mime_type         = "application/x-shockwave-flash",
    .extensions        = "swf",
    .priv_data_size    = sizeof(SWFEncContext),
    .audio_codec       = AV_CODEC_ID_MP3,
    .video_codec       = AV_CODEC_ID_FLV1,
    .write_header      = swf_write_header,
    .write_packet      = swf_write_packet,
    .write_trailer     = swf_write_trailer,
    .deinit            = swf_deinit,
    .flags             = AVFMT_TS_NONSTRICT,
};
#endif
#if CONFIG_AVM2_MUXER
const AVOutputFormat ff_avm2_muxer = {
    .name              = "avm2",
    .long_name         = NULL_IF_CONFIG_SMALL("SWF (ShockWave Flash) (AVM2)"),
    .mime_type         = "application/x-shockwave-flash",
    .priv_data_size    = sizeof(SWFEncContext),
    .audio_codec       = AV_CODEC_ID_MP3,
    .video_codec       = AV_CODEC_ID_FLV1,
    .write_header      = swf_write_header,
    .write_packet      = swf_write_packet,
    .write_trailer     = swf_write_trailer,
    .deinit            = swf_deinit,
    .flags             = AVFMT_TS_NONSTRICT,
};
#endif
