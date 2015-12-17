/*
 * MPEG1/2 muxer
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/fifo.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"

#include "libavcodec/put_bits.h"

#include "avformat.h"
#include "internal.h"
#include "mpeg.h"

#define MAX_PAYLOAD_SIZE 4096

typedef struct PacketDesc {
    int64_t pts;
    int64_t dts;
    int size;
    int unwritten_size;
    struct PacketDesc *next;
} PacketDesc;

typedef struct StreamInfo {
    AVFifoBuffer *fifo;
    uint8_t id;
    int max_buffer_size; /* in bytes */
    int buffer_index;
    PacketDesc *predecode_packet;
    PacketDesc *premux_packet;
    PacketDesc **next_packet;
    int packet_number;
    uint8_t lpcm_header[3];
    int lpcm_align;
    int bytes_to_iframe;
    int align_iframe;
    int64_t vobu_start_pts;
} StreamInfo;

typedef struct MpegMuxContext {
    const AVClass *class;
    int packet_size; /* required packet size */
    int packet_number;
    int pack_header_freq;     /* frequency (in packets^-1) at which we send pack headers */
    int system_header_freq;
    int system_header_size;
    int user_mux_rate; /* bitrate in units of bits/s */
    int mux_rate; /* bitrate in units of 50 bytes/s */
    /* stream info */
    int audio_bound;
    int video_bound;
    int is_mpeg2;
    int is_vcd;
    int is_svcd;
    int is_dvd;
    int64_t last_scr; /* current system clock */

    int64_t vcd_padding_bitrate_num;
    int64_t vcd_padding_bytes_written;

    int preload;
} MpegMuxContext;

extern AVOutputFormat ff_mpeg1vcd_muxer;
extern AVOutputFormat ff_mpeg2dvd_muxer;
extern AVOutputFormat ff_mpeg2svcd_muxer;
extern AVOutputFormat ff_mpeg2vob_muxer;

static int put_pack_header(AVFormatContext *ctx, uint8_t *buf,
                           int64_t timestamp)
{
    MpegMuxContext *s = ctx->priv_data;
    PutBitContext pb;

    init_put_bits(&pb, buf, 128);

    put_bits32(&pb, PACK_START_CODE);
    if (s->is_mpeg2)
        put_bits(&pb, 2, 0x1);
    else
        put_bits(&pb, 4, 0x2);
    put_bits(&pb,  3, (uint32_t)((timestamp >> 30) & 0x07));
    put_bits(&pb,  1, 1);
    put_bits(&pb, 15, (uint32_t)((timestamp >> 15) & 0x7fff));
    put_bits(&pb,  1, 1);
    put_bits(&pb, 15, (uint32_t)((timestamp)       & 0x7fff));
    put_bits(&pb,  1, 1);
    if (s->is_mpeg2)
        /* clock extension */
        put_bits(&pb, 9, 0);
    put_bits(&pb, 1, 1);
    put_bits(&pb, 22, s->mux_rate);
    put_bits(&pb, 1, 1);
    if (s->is_mpeg2) {
        put_bits(&pb, 1, 1);
        put_bits(&pb, 5, 0x1f); /* reserved */
        put_bits(&pb, 3, 0); /* stuffing length */
    }
    flush_put_bits(&pb);
    return put_bits_ptr(&pb) - pb.buf;
}

static int put_system_header(AVFormatContext *ctx, uint8_t *buf,
                             int only_for_stream_id)
{
    MpegMuxContext *s = ctx->priv_data;
    int size, i, private_stream_coded, id;
    PutBitContext pb;

    init_put_bits(&pb, buf, 128);

    put_bits32(&pb, SYSTEM_HEADER_START_CODE);
    put_bits(&pb, 16, 0);
    put_bits(&pb, 1, 1);

    /* maximum bit rate of the multiplexed stream */
    put_bits(&pb, 22, s->mux_rate);
    put_bits(&pb, 1, 1); /* marker */
    if (s->is_vcd && only_for_stream_id == VIDEO_ID) {
        /* This header applies only to the video stream
         * (see VCD standard p. IV-7) */
        put_bits(&pb, 6, 0);
    } else
        put_bits(&pb, 6, s->audio_bound);

    if (s->is_vcd) {
        /* see VCD standard, p. IV-7 */
        put_bits(&pb, 1, 0);
        put_bits(&pb, 1, 1);
    } else {
        put_bits(&pb, 1, 0); /* variable bitrate */
        put_bits(&pb, 1, 0); /* non constrainted bit stream */
    }

    if (s->is_vcd || s->is_dvd) {
        /* see VCD standard p IV-7 */
        put_bits(&pb, 1, 1); /* audio locked */
        put_bits(&pb, 1, 1); /* video locked */
    } else {
        put_bits(&pb, 1, 0); /* audio locked */
        put_bits(&pb, 1, 0); /* video locked */
    }

    put_bits(&pb, 1, 1); /* marker */

    if (s->is_vcd && (only_for_stream_id & 0xe0) == AUDIO_ID) {
        /* This header applies only to the audio stream
         * (see VCD standard p. IV-7) */
        put_bits(&pb, 5, 0);
    } else
        put_bits(&pb, 5, s->video_bound);

    if (s->is_dvd) {
        put_bits(&pb, 1, 0);    /* packet_rate_restriction_flag */
        put_bits(&pb, 7, 0x7f); /* reserved byte */
    } else
        put_bits(&pb, 8, 0xff); /* reserved byte */

    /* DVD-Video Stream_bound entries
     * id (0xB9) video, maximum P-STD for stream 0xE0. (P-STD_buffer_bound_scale = 1)
     * id (0xB8) audio, maximum P-STD for any MPEG audio (0xC0 to 0xC7) streams. If there are none set to 4096 (32x128). (P-STD_buffer_bound_scale = 0)
     * id (0xBD) private stream 1 (audio other than MPEG and subpictures). (P-STD_buffer_bound_scale = 1)
     * id (0xBF) private stream 2, NAV packs, set to 2x1024. */
    if (s->is_dvd) {

        int P_STD_max_video = 0;
        int P_STD_max_mpeg_audio = 0;
        int P_STD_max_mpeg_PS1 = 0;

        for (i = 0; i < ctx->nb_streams; i++) {
            StreamInfo *stream = ctx->streams[i]->priv_data;

            id = stream->id;
            if (id == 0xbd && stream->max_buffer_size > P_STD_max_mpeg_PS1) {
                P_STD_max_mpeg_PS1 = stream->max_buffer_size;
            } else if (id >= 0xc0 && id <= 0xc7 &&
                       stream->max_buffer_size > P_STD_max_mpeg_audio) {
                P_STD_max_mpeg_audio = stream->max_buffer_size;
            } else if (id == 0xe0 &&
                       stream->max_buffer_size > P_STD_max_video) {
                P_STD_max_video = stream->max_buffer_size;
            }
        }

        /* video */
        put_bits(&pb, 8, 0xb9); /* stream ID */
        put_bits(&pb, 2, 3);
        put_bits(&pb, 1, 1);
        put_bits(&pb, 13, P_STD_max_video / 1024);

        /* audio */
        if (P_STD_max_mpeg_audio == 0)
            P_STD_max_mpeg_audio = 4096;
        put_bits(&pb, 8, 0xb8); /* stream ID */
        put_bits(&pb, 2, 3);
        put_bits(&pb, 1, 0);
        put_bits(&pb, 13, P_STD_max_mpeg_audio / 128);

        /* private stream 1 */
        put_bits(&pb, 8, 0xbd); /* stream ID */
        put_bits(&pb, 2, 3);
        put_bits(&pb, 1, 0);
        put_bits(&pb, 13, P_STD_max_mpeg_PS1 / 128);

        /* private stream 2 */
        put_bits(&pb, 8, 0xbf); /* stream ID */
        put_bits(&pb, 2, 3);
        put_bits(&pb, 1, 1);
        put_bits(&pb, 13, 2);
    } else {
        /* audio stream info */
        private_stream_coded = 0;
        for (i = 0; i < ctx->nb_streams; i++) {
            StreamInfo *stream = ctx->streams[i]->priv_data;

            /* For VCDs, only include the stream info for the stream
             * that the pack which contains this system belongs to.
             * (see VCD standard p. IV-7) */
            if (!s->is_vcd || stream->id == only_for_stream_id ||
                only_for_stream_id == 0) {
                id = stream->id;
                if (id < 0xc0) {
                    /* special case for private streams (AC-3 uses that) */
                    if (private_stream_coded)
                        continue;
                    private_stream_coded = 1;
                    id = 0xbd;
                }
                put_bits(&pb, 8, id);         /* stream ID */
                put_bits(&pb, 2, 3);
                if (id < 0xe0) {
                    /* audio */
                    put_bits(&pb, 1, 0);
                    put_bits(&pb, 13, stream->max_buffer_size / 128);
                } else {
                    /* video */
                    put_bits(&pb, 1, 1);
                    put_bits(&pb, 13, stream->max_buffer_size / 1024);
                }
            }
        }
    }

    flush_put_bits(&pb);
    size = put_bits_ptr(&pb) - pb.buf;
    /* patch packet size */
    AV_WB16(buf + 4, size - 6);

    return size;
}

static int get_system_header_size(AVFormatContext *ctx)
{
    int buf_index, i, private_stream_coded;
    StreamInfo *stream;
    MpegMuxContext *s = ctx->priv_data;

    if (s->is_dvd)
        return 18; // DVD-Video system headers are 18 bytes fixed length.

    buf_index = 12;
    private_stream_coded = 0;
    for (i = 0; i < ctx->nb_streams; i++) {
        stream = ctx->streams[i]->priv_data;
        if (stream->id < 0xc0) {
            if (private_stream_coded)
                continue;
            private_stream_coded = 1;
        }
        buf_index += 3;
    }
    return buf_index;
}

static av_cold int mpeg_mux_init(AVFormatContext *ctx)
{
    MpegMuxContext *s = ctx->priv_data;
    int bitrate, i, mpa_id, mpv_id, h264_id, mps_id, ac3_id, dts_id, lpcm_id, j;
    AVStream *st;
    StreamInfo *stream;
    int audio_bitrate;
    int video_bitrate;

    s->packet_number = 0;
    s->is_vcd   =  (CONFIG_MPEG1VCD_MUXER  && ctx->oformat == &ff_mpeg1vcd_muxer);
    s->is_svcd  =  (CONFIG_MPEG2SVCD_MUXER && ctx->oformat == &ff_mpeg2svcd_muxer);
    s->is_mpeg2 = ((CONFIG_MPEG2VOB_MUXER  && ctx->oformat == &ff_mpeg2vob_muxer) ||
                   (CONFIG_MPEG2DVD_MUXER  && ctx->oformat == &ff_mpeg2dvd_muxer) ||
                   (CONFIG_MPEG2SVCD_MUXER && ctx->oformat == &ff_mpeg2svcd_muxer));
    s->is_dvd   =  (CONFIG_MPEG2DVD_MUXER  && ctx->oformat == &ff_mpeg2dvd_muxer);

    if (ctx->packet_size) {
        if (ctx->packet_size < 20 || ctx->packet_size > (1 << 23) + 10) {
            av_log(ctx, AV_LOG_ERROR, "Invalid packet size %d\n",
                   ctx->packet_size);
            goto fail;
        }
        s->packet_size = ctx->packet_size;
    } else
        s->packet_size = 2048;
    if (ctx->max_delay < 0)     /* Not set by the caller */
        ctx->max_delay = AV_TIME_BASE*7/10;

    s->vcd_padding_bytes_written = 0;
    s->vcd_padding_bitrate_num   = 0;

    s->audio_bound = 0;
    s->video_bound = 0;

    mpa_id  = AUDIO_ID;
    ac3_id  = AC3_ID;
    dts_id  = DTS_ID;
    mpv_id  = VIDEO_ID;
    h264_id = H264_ID;
    mps_id  = SUB_ID;
    lpcm_id = LPCM_ID;

    for (i = 0; i < ctx->nb_streams; i++) {
        AVCPBProperties *props;

        st     = ctx->streams[i];
        stream = av_mallocz(sizeof(StreamInfo));
        if (!stream)
            goto fail;
        st->priv_data = stream;

        avpriv_set_pts_info(st, 64, 1, 90000);

        switch (st->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            if (!s->is_mpeg2 &&
                (st->codec->codec_id == AV_CODEC_ID_AC3 ||
                 st->codec->codec_id == AV_CODEC_ID_DTS ||
                 st->codec->codec_id == AV_CODEC_ID_PCM_S16BE))
                 av_log(ctx, AV_LOG_WARNING,
                        "%s in MPEG-1 system streams is not widely supported, "
                        "consider using the vob or the dvd muxer "
                        "to force a MPEG-2 program stream.\n",
                        avcodec_get_name(st->codec->codec_id));
            if (st->codec->codec_id == AV_CODEC_ID_AC3) {
                stream->id = ac3_id++;
            } else if (st->codec->codec_id == AV_CODEC_ID_DTS) {
                stream->id = dts_id++;
            } else if (st->codec->codec_id == AV_CODEC_ID_PCM_S16BE) {
                stream->id = lpcm_id++;
                for (j = 0; j < 4; j++) {
                    if (lpcm_freq_tab[j] == st->codec->sample_rate)
                        break;
                }
                if (j == 4)
                    goto fail;
                if (st->codec->channels > 8)
                    return -1;
                stream->lpcm_header[0] = 0x0c;
                stream->lpcm_header[1] = (st->codec->channels - 1) | (j << 4);
                stream->lpcm_header[2] = 0x80;
                stream->lpcm_align     = st->codec->channels * 2;
            } else {
                stream->id = mpa_id++;
            }

            /* This value HAS to be used for VCD (see VCD standard, p. IV-7).
             * Right now it is also used for everything else. */
            stream->max_buffer_size = 4 * 1024;
            s->audio_bound++;
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (st->codec->codec_id == AV_CODEC_ID_H264)
                stream->id = h264_id++;
            else
                stream->id = mpv_id++;

            props = (AVCPBProperties*)av_stream_get_side_data(st, AV_PKT_DATA_CPB_PROPERTIES, NULL);
            if (props && props->buffer_size)
                stream->max_buffer_size = 6 * 1024 + props->buffer_size / 8;
            else {
                av_log(ctx, AV_LOG_WARNING,
                       "VBV buffer size not set, using default size of 130KB\n"
                       "If you want the mpeg file to be compliant to some specification\n"
                       "Like DVD, VCD or others, make sure you set the correct buffer size\n");
                // FIXME: this is probably too small as default
                stream->max_buffer_size = 230 * 1024;
            }
            if (stream->max_buffer_size > 1024 * 8191) {
                av_log(ctx, AV_LOG_WARNING, "buffer size %d, too large\n", stream->max_buffer_size);
                stream->max_buffer_size = 1024 * 8191;
            }
            s->video_bound++;
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            stream->id              = mps_id++;
            stream->max_buffer_size = 16 * 1024;
            break;
        default:
            av_log(ctx, AV_LOG_ERROR, "Invalid media type %s for output stream #%d\n",
                   av_get_media_type_string(st->codec->codec_type), i);
            return AVERROR(EINVAL);
        }
        stream->fifo = av_fifo_alloc(16);
        if (!stream->fifo)
            goto fail;
    }
    bitrate       = 0;
    audio_bitrate = 0;
    video_bitrate = 0;
    for (i = 0; i < ctx->nb_streams; i++) {
        AVCPBProperties *props;
        int codec_rate;
        st     = ctx->streams[i];
        stream = (StreamInfo *)st->priv_data;

        props = (AVCPBProperties*)av_stream_get_side_data(st, AV_PKT_DATA_CPB_PROPERTIES, NULL);
        if (props)
            codec_rate = props->max_bitrate;
        else
            codec_rate = st->codec->bit_rate;

        if (!codec_rate)
            codec_rate = (1 << 21) * 8 * 50 / ctx->nb_streams;

        bitrate += codec_rate;

        if ((stream->id & 0xe0) == AUDIO_ID)
            audio_bitrate += codec_rate;
        else if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            video_bitrate += codec_rate;
    }

    if (s->user_mux_rate) {
        s->mux_rate = (s->user_mux_rate + (8 * 50) - 1) / (8 * 50);
    } else {
        /* we increase slightly the bitrate to take into account the
         * headers. XXX: compute it exactly */
        bitrate    += bitrate / 20;
        bitrate    += 10000;
        s->mux_rate = (bitrate + (8 * 50) - 1) / (8 * 50);
        if (s->mux_rate >= (1<<22)) {
            av_log(ctx, AV_LOG_WARNING, "mux rate %d is too large\n", s->mux_rate);
            s->mux_rate = (1<<22) - 1;
        }
    }

    if (s->is_vcd) {
        int64_t overhead_rate;

        /* The VCD standard mandates that the mux_rate field is 3528
         * (see standard p. IV-6).
         * The value is actually "wrong", i.e. if you calculate
         * it using the normal formula and the 75 sectors per second transfer
         * rate you get a different value because the real pack size is 2324,
         * not 2352. But the standard explicitly specifies that the mux_rate
         * field in the header must have this value. */
        // s->mux_rate = 2352 * 75 / 50;    /* = 3528 */

        /* The VCD standard states that the muxed stream must be
         * exactly 75 packs / second (the data rate of a single speed cdrom).
         * Since the video bitrate (probably 1150000 bits/sec) will be below
         * the theoretical maximum we have to add some padding packets
         * to make up for the lower data rate.
         * (cf. VCD standard p. IV-6 ) */

        /* Add the header overhead to the data rate.
         * 2279 data bytes per audio pack, 2294 data bytes per video pack */
        overhead_rate  = audio_bitrate * 2294LL * (2324 - 2279);
        overhead_rate += video_bitrate * 2279LL * (2324 - 2294);

        /* Add padding so that the full bitrate is 2324*75 bytes/sec */
        s->vcd_padding_bitrate_num = (2324LL * 75 * 8 - bitrate) * 2279 * 2294 - overhead_rate;
#define VCD_PADDING_BITRATE_DEN (2279 * 2294)
    }

    if (s->is_vcd || s->is_mpeg2)
        /* every packet */
        s->pack_header_freq = 1;
    else
        /* every 2 seconds */
        s->pack_header_freq = 2 * bitrate / s->packet_size / 8;

    /* the above seems to make pack_header_freq zero sometimes */
    if (s->pack_header_freq == 0)
        s->pack_header_freq = 1;

    if (s->is_mpeg2)
        /* every 200 packets. Need to look at the spec.  */
        s->system_header_freq = s->pack_header_freq * 40;
    else if (s->is_vcd)
        /* the standard mandates that there are only two system headers
         * in the whole file: one in the first packet of each stream.
         * (see standard p. IV-7 and IV-8) */
        s->system_header_freq = 0x7fffffff;
    else
        s->system_header_freq = s->pack_header_freq * 5;

    for (i = 0; i < ctx->nb_streams; i++) {
        stream                = ctx->streams[i]->priv_data;
        stream->packet_number = 0;
    }
    s->system_header_size = get_system_header_size(ctx);
    s->last_scr           = AV_NOPTS_VALUE;
    return 0;

fail:
    for (i = 0; i < ctx->nb_streams; i++)
        av_freep(&ctx->streams[i]->priv_data);
    return AVERROR(ENOMEM);
}

static inline void put_timestamp(AVIOContext *pb, int id, int64_t timestamp)
{
    avio_w8(pb, (id << 4) |  (((timestamp >> 30) & 0x07)   << 1) | 1);
    avio_wb16(pb, (uint16_t)((((timestamp >> 15) & 0x7fff) << 1) | 1));
    avio_wb16(pb, (uint16_t)((((timestamp)       & 0x7fff) << 1) | 1));
}

/* return the number of padding bytes that should be inserted into
 * the multiplexed stream. */
static int get_vcd_padding_size(AVFormatContext *ctx, int64_t pts)
{
    MpegMuxContext *s = ctx->priv_data;
    int pad_bytes = 0;

    if (s->vcd_padding_bitrate_num > 0 && pts != AV_NOPTS_VALUE) {
        int64_t full_pad_bytes;

        // FIXME: this is wrong
        full_pad_bytes =
            av_rescale(s->vcd_padding_bitrate_num, pts, 90000LL * 8 * VCD_PADDING_BITRATE_DEN);
        pad_bytes = (int)(full_pad_bytes - s->vcd_padding_bytes_written);

        if (pad_bytes < 0)
            /* might happen if we have already padded to a later timestamp. This
             * can occur if another stream has already advanced further. */
            pad_bytes = 0;
    }

    return pad_bytes;
}

/* Write an MPEG padding packet header. */
static void put_padding_packet(AVFormatContext *ctx, AVIOContext *pb,
                               int packet_bytes)
{
    MpegMuxContext *s = ctx->priv_data;
    int i;

    avio_wb32(pb, PADDING_STREAM);
    avio_wb16(pb, packet_bytes - 6);
    if (!s->is_mpeg2) {
        avio_w8(pb, 0x0f);
        packet_bytes -= 7;
    } else
        packet_bytes -= 6;

    for (i = 0; i < packet_bytes; i++)
        avio_w8(pb, 0xff);
}

static int get_nb_frames(AVFormatContext *ctx, StreamInfo *stream, int len)
{
    int nb_frames        = 0;
    PacketDesc *pkt_desc = stream->premux_packet;

    while (len > 0) {
        if (pkt_desc->size == pkt_desc->unwritten_size)
            nb_frames++;
        len     -= pkt_desc->unwritten_size;
        pkt_desc = pkt_desc->next;
    }

    return nb_frames;
}

/* flush the packet on stream stream_index */
static int flush_packet(AVFormatContext *ctx, int stream_index,
                        int64_t pts, int64_t dts, int64_t scr, int trailer_size)
{
    MpegMuxContext *s  = ctx->priv_data;
    StreamInfo *stream = ctx->streams[stream_index]->priv_data;
    uint8_t *buf_ptr;
    int size, payload_size, startcode, id, stuffing_size, i, header_len;
    int packet_size;
    uint8_t buffer[128];
    int zero_trail_bytes = 0;
    int pad_packet_bytes = 0;
    int pes_flags;
    /* "general" pack without data specific to one stream? */
    int general_pack = 0;
    int nb_frames;

    id = stream->id;

    av_log(ctx, AV_LOG_TRACE, "packet ID=%2x PTS=%0.3f\n", id, pts / 90000.0);

    buf_ptr = buffer;

    if ((s->packet_number % s->pack_header_freq) == 0 || s->last_scr != scr) {
        /* output pack and systems header if needed */
        size        = put_pack_header(ctx, buf_ptr, scr);
        buf_ptr    += size;
        s->last_scr = scr;

        if (s->is_vcd) {
            /* there is exactly one system header for each stream in a VCD MPEG,
             * One in the very first video packet and one in the very first
             * audio packet (see VCD standard p. IV-7 and IV-8). */

            if (stream->packet_number == 0) {
                size     = put_system_header(ctx, buf_ptr, id);
                buf_ptr += size;
            }
        } else if (s->is_dvd) {
            if (stream->align_iframe || s->packet_number == 0) {
                int PES_bytes_to_fill = s->packet_size - size - 10;

                if (pts != AV_NOPTS_VALUE) {
                    if (dts != pts)
                        PES_bytes_to_fill -= 5 + 5;
                    else
                        PES_bytes_to_fill -= 5;
                }

                if (stream->bytes_to_iframe == 0 || s->packet_number == 0) {
                    size     = put_system_header(ctx, buf_ptr, 0);
                    buf_ptr += size;
                    size     = buf_ptr - buffer;
                    avio_write(ctx->pb, buffer, size);

                    avio_wb32(ctx->pb, PRIVATE_STREAM_2);
                    avio_wb16(ctx->pb, 0x03d4);     // length
                    avio_w8(ctx->pb, 0x00);         // substream ID, 00=PCI
                    for (i = 0; i < 979; i++)
                        avio_w8(ctx->pb, 0x00);

                    avio_wb32(ctx->pb, PRIVATE_STREAM_2);
                    avio_wb16(ctx->pb, 0x03fa);     // length
                    avio_w8(ctx->pb, 0x01);         // substream ID, 01=DSI
                    for (i = 0; i < 1017; i++)
                        avio_w8(ctx->pb, 0x00);

                    memset(buffer, 0, 128);
                    buf_ptr = buffer;
                    s->packet_number++;
                    stream->align_iframe = 0;
                    // FIXME: rounding and first few bytes of each packet
                    scr        += s->packet_size * 90000LL /
                                  (s->mux_rate * 50LL);
                    size        = put_pack_header(ctx, buf_ptr, scr);
                    s->last_scr = scr;
                    buf_ptr    += size;
                    /* GOP Start */
                } else if (stream->bytes_to_iframe < PES_bytes_to_fill) {
                    pad_packet_bytes = PES_bytes_to_fill -
                                       stream->bytes_to_iframe;
                }
            }
        } else {
            if ((s->packet_number % s->system_header_freq) == 0) {
                size     = put_system_header(ctx, buf_ptr, 0);
                buf_ptr += size;
            }
        }
    }
    size = buf_ptr - buffer;
    avio_write(ctx->pb, buffer, size);

    packet_size = s->packet_size - size;

    if (s->is_vcd && (id & 0xe0) == AUDIO_ID)
        /* The VCD standard demands that 20 zero bytes follow
         * each audio pack (see standard p. IV-8). */
        zero_trail_bytes += 20;

    if ((s->is_vcd && stream->packet_number == 0) ||
        (s->is_svcd && s->packet_number == 0)) {
        /* for VCD the first pack of each stream contains only the pack header,
         * the system header and lots of padding (see VCD standard p. IV-6).
         * In the case of an audio pack, 20 zero bytes are also added at
         * the end. */
        /* For SVCD we fill the very first pack to increase compatibility with
         * some DVD players. Not mandated by the standard. */
        if (s->is_svcd)
            /* the system header refers to both streams and no stream data */
            general_pack = 1;
        pad_packet_bytes = packet_size - zero_trail_bytes;
    }

    packet_size -= pad_packet_bytes + zero_trail_bytes;

    if (packet_size > 0) {
        /* packet header size */
        packet_size -= 6;

        /* packet header */
        if (s->is_mpeg2) {
            header_len = 3;
            if (stream->packet_number == 0)
                header_len += 3; /* PES extension */
            header_len += 1; /* obligatory stuffing byte */
        } else {
            header_len = 0;
        }
        if (pts != AV_NOPTS_VALUE) {
            if (dts != pts)
                header_len += 5 + 5;
            else
                header_len += 5;
        } else {
            if (!s->is_mpeg2)
                header_len++;
        }

        payload_size = packet_size - header_len;
        if (id < 0xc0) {
            startcode     = PRIVATE_STREAM_1;
            payload_size -= 1;
            if (id >= 0x40) {
                payload_size -= 3;
                if (id >= 0xa0)
                    payload_size -= 3;
            }
        } else {
            startcode = 0x100 + id;
        }

        stuffing_size = payload_size - av_fifo_size(stream->fifo);

        // first byte does not fit -> reset pts/dts + stuffing
        if (payload_size <= trailer_size && pts != AV_NOPTS_VALUE) {
            int timestamp_len = 0;
            if (dts != pts)
                timestamp_len += 5;
            if (pts != AV_NOPTS_VALUE)
                timestamp_len += s->is_mpeg2 ? 5 : 4;
            pts         =
            dts         = AV_NOPTS_VALUE;
            header_len -= timestamp_len;
            if (s->is_dvd && stream->align_iframe) {
                pad_packet_bytes += timestamp_len;
                packet_size      -= timestamp_len;
            } else {
                payload_size += timestamp_len;
            }
            stuffing_size += timestamp_len;
            if (payload_size > trailer_size)
                stuffing_size += payload_size - trailer_size;
        }

        // can't use padding, so use stuffing
        if (pad_packet_bytes > 0 && pad_packet_bytes <= 7) {
            packet_size  += pad_packet_bytes;
            payload_size += pad_packet_bytes; // undo the previous adjustment
            if (stuffing_size < 0)
                stuffing_size = pad_packet_bytes;
            else
                stuffing_size += pad_packet_bytes;
            pad_packet_bytes = 0;
        }

        if (stuffing_size < 0)
            stuffing_size = 0;

        if (startcode == PRIVATE_STREAM_1 && id >= 0xa0) {
            if (payload_size < av_fifo_size(stream->fifo))
                stuffing_size += payload_size % stream->lpcm_align;
        }

        if (stuffing_size > 16) {   /* <=16 for MPEG-1, <=32 for MPEG-2 */
            pad_packet_bytes += stuffing_size;
            packet_size      -= stuffing_size;
            payload_size     -= stuffing_size;
            stuffing_size     = 0;
        }

        nb_frames = get_nb_frames(ctx, stream, payload_size - stuffing_size);

        avio_wb32(ctx->pb, startcode);

        avio_wb16(ctx->pb, packet_size);

        if (!s->is_mpeg2)
            for (i = 0; i < stuffing_size; i++)
                avio_w8(ctx->pb, 0xff);

        if (s->is_mpeg2) {
            avio_w8(ctx->pb, 0x80); /* mpeg2 id */

            pes_flags = 0;

            if (pts != AV_NOPTS_VALUE) {
                pes_flags |= 0x80;
                if (dts != pts)
                    pes_flags |= 0x40;
            }

            /* Both the MPEG-2 and the SVCD standards demand that the
             * P-STD_buffer_size field be included in the first packet of
             * every stream. (see SVCD standard p. 26 V.2.3.1 and V.2.3.2
             * and MPEG-2 standard 2.7.7) */
            if (stream->packet_number == 0)
                pes_flags |= 0x01;

            avio_w8(ctx->pb, pes_flags); /* flags */
            avio_w8(ctx->pb, header_len - 3 + stuffing_size);

            if (pes_flags & 0x80)  /* write pts */
                put_timestamp(ctx->pb, (pes_flags & 0x40) ? 0x03 : 0x02, pts);
            if (pes_flags & 0x40)  /* write dts */
                put_timestamp(ctx->pb, 0x01, dts);

            if (pes_flags & 0x01) {  /* write pes extension */
                avio_w8(ctx->pb, 0x10); /* flags */

                /* P-STD buffer info */
                if ((id & 0xe0) == AUDIO_ID)
                    avio_wb16(ctx->pb, 0x4000 | stream->max_buffer_size / 128);
                else
                    avio_wb16(ctx->pb, 0x6000 | stream->max_buffer_size / 1024);
            }
        } else {
            if (pts != AV_NOPTS_VALUE) {
                if (dts != pts) {
                    put_timestamp(ctx->pb, 0x03, pts);
                    put_timestamp(ctx->pb, 0x01, dts);
                } else {
                    put_timestamp(ctx->pb, 0x02, pts);
                }
            } else {
                avio_w8(ctx->pb, 0x0f);
            }
        }

        if (s->is_mpeg2) {
            /* special stuffing byte that is always written
             * to prevent accidental generation of start codes. */
            avio_w8(ctx->pb, 0xff);

            for (i = 0; i < stuffing_size; i++)
                avio_w8(ctx->pb, 0xff);
        }

        if (startcode == PRIVATE_STREAM_1) {
            avio_w8(ctx->pb, id);
            if (id >= 0xa0) {
                /* LPCM (XXX: check nb_frames) */
                avio_w8(ctx->pb, 7);
                avio_wb16(ctx->pb, 4); /* skip 3 header bytes */
                avio_w8(ctx->pb, stream->lpcm_header[0]);
                avio_w8(ctx->pb, stream->lpcm_header[1]);
                avio_w8(ctx->pb, stream->lpcm_header[2]);
            } else if (id >= 0x40) {
                /* AC-3 */
                avio_w8(ctx->pb, nb_frames);
                avio_wb16(ctx->pb, trailer_size + 1);
            }
        }

        /* output data */
        av_assert0(payload_size - stuffing_size <= av_fifo_size(stream->fifo));
        av_fifo_generic_read(stream->fifo, ctx->pb,
                             payload_size - stuffing_size,
                             (void (*)(void*, void*, int))avio_write);
        stream->bytes_to_iframe -= payload_size - stuffing_size;
    } else {
        payload_size  =
        stuffing_size = 0;
    }

    if (pad_packet_bytes > 0)
        put_padding_packet(ctx, ctx->pb, pad_packet_bytes);

    for (i = 0; i < zero_trail_bytes; i++)
        avio_w8(ctx->pb, 0x00);

    avio_flush(ctx->pb);

    s->packet_number++;

    /* only increase the stream packet number if this pack actually contains
     * something that is specific to this stream! I.e. a dedicated header
     * or some data. */
    if (!general_pack)
        stream->packet_number++;

    return payload_size - stuffing_size;
}

static void put_vcd_padding_sector(AVFormatContext *ctx)
{
    /* There are two ways to do this padding: writing a sector/pack
     * of 0 values, or writing an MPEG padding pack. Both seem to
     * work with most decoders, BUT the VCD standard only allows a 0-sector
     * (see standard p. IV-4, IV-5).
     * So a 0-sector it is... */

    MpegMuxContext *s = ctx->priv_data;
    int i;

    for (i = 0; i < s->packet_size; i++)
        avio_w8(ctx->pb, 0);

    s->vcd_padding_bytes_written += s->packet_size;

    avio_flush(ctx->pb);

    /* increasing the packet number is correct. The SCR of the following packs
     * is calculated from the packet_number and it has to include the padding
     * sector (it represents the sector index, not the MPEG pack index)
     * (see VCD standard p. IV-6) */
    s->packet_number++;
}

static int remove_decoded_packets(AVFormatContext *ctx, int64_t scr)
{
    int i;

    for (i = 0; i < ctx->nb_streams; i++) {
        AVStream *st = ctx->streams[i];
        StreamInfo *stream = st->priv_data;
        PacketDesc *pkt_desc;

        while ((pkt_desc = stream->predecode_packet) &&
               scr > pkt_desc->dts) { // FIXME: > vs >=
            if (stream->buffer_index < pkt_desc->size ||
                stream->predecode_packet == stream->premux_packet) {
                av_log(ctx, AV_LOG_ERROR,
                       "buffer underflow st=%d bufi=%d size=%d\n",
                       i, stream->buffer_index, pkt_desc->size);
                break;
            }
            stream->buffer_index    -= pkt_desc->size;
            stream->predecode_packet = pkt_desc->next;
            av_freep(&pkt_desc);
        }
    }

    return 0;
}

static int output_packet(AVFormatContext *ctx, int flush)
{
    MpegMuxContext *s = ctx->priv_data;
    AVStream *st;
    StreamInfo *stream;
    int i, avail_space = 0, es_size, trailer_size;
    int best_i = -1;
    int best_score = INT_MIN;
    int ignore_constraints = 0;
    int ignore_delay = 0;
    int64_t scr = s->last_scr;
    PacketDesc *timestamp_packet;
    const int64_t max_delay = av_rescale(ctx->max_delay, 90000, AV_TIME_BASE);

retry:
    for (i = 0; i < ctx->nb_streams; i++) {
        AVStream *st = ctx->streams[i];
        StreamInfo *stream = st->priv_data;
        const int avail_data = av_fifo_size(stream->fifo);
        const int space = stream->max_buffer_size - stream->buffer_index;
        int rel_space = 1024LL * space / stream->max_buffer_size;
        PacketDesc *next_pkt = stream->premux_packet;

        /* for subtitle, a single PES packet must be generated,
         * so we flush after every single subtitle packet */
        if (s->packet_size > avail_data && !flush
            && st->codec->codec_type != AVMEDIA_TYPE_SUBTITLE)
            return 0;
        if (avail_data == 0)
            continue;
        av_assert0(avail_data > 0);

        if (space < s->packet_size && !ignore_constraints)
            continue;

        if (next_pkt && next_pkt->dts - scr > max_delay && !ignore_delay)
            continue;
        if (   stream->predecode_packet
            && stream->predecode_packet->size > stream->buffer_index)
            rel_space += 1<<28;
        if (rel_space > best_score) {
            best_score  = rel_space;
            best_i      = i;
            avail_space = space;
        }
    }

    if (best_i < 0) {
        int64_t best_dts = INT64_MAX;
        int has_premux = 0;

        for (i = 0; i < ctx->nb_streams; i++) {
            AVStream *st = ctx->streams[i];
            StreamInfo *stream = st->priv_data;
            PacketDesc *pkt_desc = stream->predecode_packet;
            if (pkt_desc && pkt_desc->dts < best_dts)
                best_dts = pkt_desc->dts;
            has_premux |= !!stream->premux_packet;
        }

        if (best_dts < INT64_MAX) {
            av_log(ctx, AV_LOG_TRACE, "bumping scr, scr:%f, dts:%f\n",
                    scr / 90000.0, best_dts / 90000.0);

            if (scr >= best_dts + 1 && !ignore_constraints) {
                av_log(ctx, AV_LOG_ERROR,
                    "packet too large, ignoring buffer limits to mux it\n");
                ignore_constraints = 1;
            }
            scr = FFMAX(best_dts + 1, scr);
            if (remove_decoded_packets(ctx, scr) < 0)
                return -1;
        } else if (has_premux && flush) {
            av_log(ctx, AV_LOG_ERROR,
                  "delay too large, ignoring ...\n");
            ignore_delay = 1;
            ignore_constraints = 1;
        } else
            return 0;

        goto retry;
    }

    av_assert0(best_i >= 0);

    st     = ctx->streams[best_i];
    stream = st->priv_data;

    av_assert0(av_fifo_size(stream->fifo) > 0);

    av_assert0(avail_space >= s->packet_size || ignore_constraints);

    timestamp_packet = stream->premux_packet;
    if (timestamp_packet->unwritten_size == timestamp_packet->size) {
        trailer_size = 0;
    } else {
        trailer_size     = timestamp_packet->unwritten_size;
        timestamp_packet = timestamp_packet->next;
    }

    if (timestamp_packet) {
        av_log(ctx, AV_LOG_TRACE, "dts:%f pts:%f scr:%f stream:%d\n",
                timestamp_packet->dts / 90000.0,
                timestamp_packet->pts / 90000.0,
                scr / 90000.0, best_i);
        es_size = flush_packet(ctx, best_i, timestamp_packet->pts,
                               timestamp_packet->dts, scr, trailer_size);
    } else {
        av_assert0(av_fifo_size(stream->fifo) == trailer_size);
        es_size = flush_packet(ctx, best_i, AV_NOPTS_VALUE, AV_NOPTS_VALUE, scr,
                               trailer_size);
    }

    if (s->is_vcd) {
        /* Write one or more padding sectors, if necessary, to reach
         * the constant overall bitrate. */
        int vcd_pad_bytes;

        // FIXME: pts cannot be correct here
        while ((vcd_pad_bytes = get_vcd_padding_size(ctx, stream->premux_packet->pts)) >= s->packet_size) {
            put_vcd_padding_sector(ctx);
            // FIXME: rounding and first few bytes of each packet
            s->last_scr += s->packet_size * 90000LL / (s->mux_rate * 50LL);
        }
    }

    stream->buffer_index += es_size;
    // FIXME: rounding and first few bytes of each packet
    s->last_scr          += s->packet_size * 90000LL / (s->mux_rate * 50LL);

    while (stream->premux_packet &&
           stream->premux_packet->unwritten_size <= es_size) {
        es_size              -= stream->premux_packet->unwritten_size;
        stream->premux_packet = stream->premux_packet->next;
    }
    if (es_size) {
        av_assert0(stream->premux_packet);
        stream->premux_packet->unwritten_size -= es_size;
    }

    if (remove_decoded_packets(ctx, s->last_scr) < 0)
        return -1;

    return 1;
}

static int mpeg_mux_write_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    int stream_index = pkt->stream_index;
    int size         = pkt->size;
    uint8_t *buf     = pkt->data;
    MpegMuxContext *s = ctx->priv_data;
    AVStream *st      = ctx->streams[stream_index];
    StreamInfo *stream = st->priv_data;
    int64_t pts, dts;
    PacketDesc *pkt_desc;
    int preload;
    const int is_iframe = st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
                          (pkt->flags & AV_PKT_FLAG_KEY);

    preload = av_rescale(s->preload, 90000, AV_TIME_BASE);

    pts = pkt->pts;
    dts = pkt->dts;

    if (s->last_scr == AV_NOPTS_VALUE) {
        if (dts == AV_NOPTS_VALUE || (dts < preload && ctx->avoid_negative_ts) || s->is_dvd) {
            if (dts != AV_NOPTS_VALUE)
                s->preload += av_rescale(-dts, AV_TIME_BASE, 90000);
            s->last_scr = 0;
        } else {
            s->last_scr = dts - preload;
            s->preload = 0;
        }
        preload = av_rescale(s->preload, 90000, AV_TIME_BASE);
        av_log(ctx, AV_LOG_DEBUG, "First SCR: %"PRId64" First DTS: %"PRId64"\n", s->last_scr, dts + preload);
    }

    if (dts != AV_NOPTS_VALUE) dts += preload;
    if (pts != AV_NOPTS_VALUE) pts += preload;

    av_log(ctx, AV_LOG_TRACE, "dts:%f pts:%f flags:%d stream:%d nopts:%d\n",
            dts / 90000.0, pts / 90000.0, pkt->flags,
            pkt->stream_index, pts != AV_NOPTS_VALUE);
    if (!stream->premux_packet)
        stream->next_packet = &stream->premux_packet;
    *stream->next_packet     =
    pkt_desc                 = av_mallocz(sizeof(PacketDesc));
    pkt_desc->pts            = pts;
    pkt_desc->dts            = dts;
    pkt_desc->unwritten_size =
    pkt_desc->size           = size;
    if (!stream->predecode_packet)
        stream->predecode_packet = pkt_desc;
    stream->next_packet = &pkt_desc->next;

    if (av_fifo_realloc2(stream->fifo, av_fifo_size(stream->fifo) + size) < 0)
        return -1;

    if (s->is_dvd) {
        // min VOBU length 0.4 seconds (mpucoder)
        if (is_iframe &&
            (s->packet_number == 0 ||
             (pts - stream->vobu_start_pts >= 36000))) {
            stream->bytes_to_iframe = av_fifo_size(stream->fifo);
            stream->align_iframe    = 1;
            stream->vobu_start_pts  = pts;
        }
    }

    av_fifo_generic_write(stream->fifo, buf, size, NULL);

    for (;;) {
        int ret = output_packet(ctx, 0);
        if (ret <= 0)
            return ret;
    }
}

static int mpeg_mux_end(AVFormatContext *ctx)
{
    StreamInfo *stream;
    int i;

    for (;;) {
        int ret = output_packet(ctx, 1);
        if (ret < 0)
            return ret;
        else if (ret == 0)
            break;
    }

    /* End header according to MPEG1 systems standard. We do not write
     * it as it is usually not needed by decoders and because it
     * complicates MPEG stream concatenation. */
    // avio_wb32(ctx->pb, ISO_11172_END_CODE);
    // avio_flush(ctx->pb);

    for (i = 0; i < ctx->nb_streams; i++) {
        stream = ctx->streams[i]->priv_data;

        av_assert0(av_fifo_size(stream->fifo) == 0);
        av_fifo_freep(&stream->fifo);
    }
    return 0;
}

#define OFFSET(x) offsetof(MpegMuxContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "muxrate", NULL,                                          OFFSET(user_mux_rate), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, ((1<<22) - 1) * (8 * 50), E },
    { "preload", "Initial demux-decode delay in microseconds.", OFFSET(preload),  AV_OPT_TYPE_INT, { .i64 = 500000 }, 0, INT_MAX, E },
    { NULL },
};

#define MPEGENC_CLASS(flavor)                   \
static const AVClass flavor ## _class = {       \
    .class_name = #flavor " muxer",             \
    .item_name  = av_default_item_name,         \
    .version    = LIBAVUTIL_VERSION_INT,        \
    .option     = options,                      \
};

#if CONFIG_MPEG1SYSTEM_MUXER
MPEGENC_CLASS(mpeg)
AVOutputFormat ff_mpeg1system_muxer = {
    .name              = "mpeg",
    .long_name         = NULL_IF_CONFIG_SMALL("MPEG-1 Systems / MPEG program stream"),
    .mime_type         = "video/mpeg",
    .extensions        = "mpg,mpeg",
    .priv_data_size    = sizeof(MpegMuxContext),
    .audio_codec       = AV_CODEC_ID_MP2,
    .video_codec       = AV_CODEC_ID_MPEG1VIDEO,
    .write_header      = mpeg_mux_init,
    .write_packet      = mpeg_mux_write_packet,
    .write_trailer     = mpeg_mux_end,
    .priv_class        = &mpeg_class,
};
#endif

#if CONFIG_MPEG1VCD_MUXER
MPEGENC_CLASS(vcd)
AVOutputFormat ff_mpeg1vcd_muxer = {
    .name              = "vcd",
    .long_name         = NULL_IF_CONFIG_SMALL("MPEG-1 Systems / MPEG program stream (VCD)"),
    .mime_type         = "video/mpeg",
    .priv_data_size    = sizeof(MpegMuxContext),
    .audio_codec       = AV_CODEC_ID_MP2,
    .video_codec       = AV_CODEC_ID_MPEG1VIDEO,
    .write_header      = mpeg_mux_init,
    .write_packet      = mpeg_mux_write_packet,
    .write_trailer     = mpeg_mux_end,
    .priv_class        = &vcd_class,
};
#endif

#if CONFIG_MPEG2VOB_MUXER
MPEGENC_CLASS(vob)
AVOutputFormat ff_mpeg2vob_muxer = {
    .name              = "vob",
    .long_name         = NULL_IF_CONFIG_SMALL("MPEG-2 PS (VOB)"),
    .mime_type         = "video/mpeg",
    .extensions        = "vob",
    .priv_data_size    = sizeof(MpegMuxContext),
    .audio_codec       = AV_CODEC_ID_MP2,
    .video_codec       = AV_CODEC_ID_MPEG2VIDEO,
    .write_header      = mpeg_mux_init,
    .write_packet      = mpeg_mux_write_packet,
    .write_trailer     = mpeg_mux_end,
    .priv_class        = &vob_class,
};
#endif

/* Same as mpeg2vob_mux except that the pack size is 2324 */
#if CONFIG_MPEG2SVCD_MUXER
MPEGENC_CLASS(svcd)
AVOutputFormat ff_mpeg2svcd_muxer = {
    .name              = "svcd",
    .long_name         = NULL_IF_CONFIG_SMALL("MPEG-2 PS (SVCD)"),
    .mime_type         = "video/mpeg",
    .extensions        = "vob",
    .priv_data_size    = sizeof(MpegMuxContext),
    .audio_codec       = AV_CODEC_ID_MP2,
    .video_codec       = AV_CODEC_ID_MPEG2VIDEO,
    .write_header      = mpeg_mux_init,
    .write_packet      = mpeg_mux_write_packet,
    .write_trailer     = mpeg_mux_end,
    .priv_class        = &svcd_class,
};
#endif

/*  Same as mpeg2vob_mux except the 'is_dvd' flag is set to produce NAV pkts */
#if CONFIG_MPEG2DVD_MUXER
MPEGENC_CLASS(dvd)
AVOutputFormat ff_mpeg2dvd_muxer = {
    .name              = "dvd",
    .long_name         = NULL_IF_CONFIG_SMALL("MPEG-2 PS (DVD VOB)"),
    .mime_type         = "video/mpeg",
    .extensions        = "dvd",
    .priv_data_size    = sizeof(MpegMuxContext),
    .audio_codec       = AV_CODEC_ID_MP2,
    .video_codec       = AV_CODEC_ID_MPEG2VIDEO,
    .write_header      = mpeg_mux_init,
    .write_packet      = mpeg_mux_write_packet,
    .write_trailer     = mpeg_mux_end,
    .priv_class        = &dvd_class,
};
#endif
