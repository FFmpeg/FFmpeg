/*
 * Bethsoft VID format Demuxer
 * Copyright (c) 2007 Nicholas Tung
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
 * @brief Bethesda Softworks VID (.vid) file demuxer
 * @author Nicholas Tung [ntung (at. ntung com] (2007-03)
 * @see http://wiki.multimedia.cx/index.php?title=Bethsoft_VID
 * @see http://www.svatopluk.com/andux/docs/dfvid.html
 */

#include "libavutil/channel_layout.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "libavcodec/bethsoftvideo.h"

#define BVID_PALETTE_SIZE 3 * 256

#define DEFAULT_SAMPLE_RATE 11111

typedef struct BVID_DemuxContext
{
    int nframes;
    int sample_rate;        /**< audio sample rate */
    int width;              /**< video width       */
    int height;             /**< video height      */
    /** delay value between frames, added to individual frame delay.
     * custom units, which will be added to other custom units (~=16ms according
     * to free, unofficial documentation) */
    int bethsoft_global_delay;
    int video_index;        /**< video stream index */
    int audio_index;        /**< audio stream index */
    int has_palette;
    uint8_t palette[BVID_PALETTE_SIZE];

    int is_finished;

} BVID_DemuxContext;

static int vid_probe(const AVProbeData *p)
{
    // little-endian VID tag, file starts with "VID\0"
    if (AV_RL32(p->buf) != MKTAG('V', 'I', 'D', 0))
        return 0;

    if (p->buf[4] != 2)
        return AVPROBE_SCORE_MAX / 4;

    return AVPROBE_SCORE_MAX;
}

static int vid_read_header(AVFormatContext *s)
{
    BVID_DemuxContext *vid = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret;

    /* load main header. Contents:
    *    bytes: 'V' 'I' 'D'
    *    int16s: always_512, nframes, width, height, delay, always_14
    */
    avio_skip(pb, 5);
    vid->nframes = avio_rl16(pb);
    vid->width   = avio_rl16(pb);
    vid->height  = avio_rl16(pb);
    vid->bethsoft_global_delay = avio_rl16(pb);
    avio_rl16(pb);

    ret = av_image_check_size(vid->width, vid->height, 0, s);
    if (ret < 0)
        return ret;

    // wait until the first packet to create each stream
    vid->video_index = -1;
    vid->audio_index = -1;
    vid->sample_rate = DEFAULT_SAMPLE_RATE;
    s->ctx_flags |= AVFMTCTX_NOHEADER;

    return 0;
}

#define BUFFER_PADDING_SIZE 1000
static int read_frame(BVID_DemuxContext *vid, AVIOContext *pb, AVPacket *pkt,
                      uint8_t block_type, AVFormatContext *s)
{
    uint8_t * vidbuf_start = NULL;
    int vidbuf_nbytes = 0;
    int code;
    int bytes_copied = 0;
    int position, duration, npixels;
    unsigned int vidbuf_capacity;
    int ret = 0;
    AVStream *st;

    if (vid->video_index < 0) {
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        vid->video_index = st->index;
        if (vid->audio_index < 0) {
            avpriv_request_sample(s, "Using default video time base since "
                                  "having no audio packet before the first "
                                  "video packet");
        }
        avpriv_set_pts_info(st, 64, 185, vid->sample_rate);
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id   = AV_CODEC_ID_BETHSOFTVID;
        st->codecpar->width      = vid->width;
        st->codecpar->height     = vid->height;
    }
    st      = s->streams[vid->video_index];
    npixels = st->codecpar->width * st->codecpar->height;

    vidbuf_start = av_malloc(vidbuf_capacity = BUFFER_PADDING_SIZE);
    if(!vidbuf_start)
        return AVERROR(ENOMEM);

    // save the file position for the packet, include block type
    position = avio_tell(pb) - 1;

    vidbuf_start[vidbuf_nbytes++] = block_type;

    // get the current packet duration
    duration = vid->bethsoft_global_delay + avio_rl16(pb);

    // set the y offset if it exists (decoder header data should be in data section)
    if(block_type == VIDEO_YOFF_P_FRAME){
        if (avio_read(pb, &vidbuf_start[vidbuf_nbytes], 2) != 2) {
            ret = AVERROR(EIO);
            goto fail;
        }
        vidbuf_nbytes += 2;
    }

    do{
        uint8_t *tmp = av_fast_realloc(vidbuf_start, &vidbuf_capacity,
                                       vidbuf_nbytes + BUFFER_PADDING_SIZE);
        if (!tmp) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        vidbuf_start = tmp;

        code = avio_r8(pb);
        vidbuf_start[vidbuf_nbytes++] = code;

        if(code >= 0x80){ // rle sequence
            if(block_type == VIDEO_I_FRAME)
                vidbuf_start[vidbuf_nbytes++] = avio_r8(pb);
        } else if(code){ // plain sequence
            if (avio_read(pb, &vidbuf_start[vidbuf_nbytes], code) != code) {
                ret = AVERROR(EIO);
                goto fail;
            }
            vidbuf_nbytes += code;
        }
        bytes_copied += code & 0x7F;
        if(bytes_copied == npixels){ // sometimes no stop character is given, need to keep track of bytes copied
            // may contain a 0 byte even if read all pixels
            if(avio_r8(pb))
                avio_seek(pb, -1, SEEK_CUR);
            break;
        }
        if (bytes_copied > npixels) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
    } while(code);

    // copy data into packet
    if ((ret = av_new_packet(pkt, vidbuf_nbytes)) < 0)
        goto fail;
    memcpy(pkt->data, vidbuf_start, vidbuf_nbytes);

    pkt->pos = position;
    pkt->stream_index = vid->video_index;
    pkt->duration = duration;
    if (block_type == VIDEO_I_FRAME)
        pkt->flags |= AV_PKT_FLAG_KEY;

    /* if there is a new palette available, add it to packet side data */
    if (vid->has_palette) {
        uint8_t *pdata = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE,
                                                 BVID_PALETTE_SIZE);
        if (!pdata) {
            ret = AVERROR(ENOMEM);
            av_log(s, AV_LOG_ERROR, "Failed to allocate palette side data\n");
            goto fail;
        }
        memcpy(pdata, vid->palette, BVID_PALETTE_SIZE);
        vid->has_palette = 0;
    }

    vid->nframes--;  // used to check if all the frames were read
fail:
    av_free(vidbuf_start);
    return ret;
}

static int vid_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    BVID_DemuxContext *vid = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned char block_type;
    int audio_length;
    int ret_value;

    if(vid->is_finished || avio_feof(pb))
        return AVERROR_EOF;

    block_type = avio_r8(pb);
    switch(block_type){
        case PALETTE_BLOCK:
            if (vid->has_palette) {
                av_log(s, AV_LOG_WARNING, "discarding unused palette\n");
                vid->has_palette = 0;
            }
            if (avio_read(pb, vid->palette, BVID_PALETTE_SIZE) != BVID_PALETTE_SIZE) {
                return AVERROR(EIO);
            }
            vid->has_palette = 1;
            return vid_read_packet(s, pkt);

        case FIRST_AUDIO_BLOCK:
            avio_rl16(pb);
            // soundblaster DAC used for sample rate, as on specification page (link above)
            vid->sample_rate = 1000000 / (256 - avio_r8(pb));
        case AUDIO_BLOCK:
            if (vid->audio_index < 0) {
                AVStream *st = avformat_new_stream(s, NULL);
                if (!st)
                    return AVERROR(ENOMEM);
                vid->audio_index                 = st->index;
                st->codecpar->codec_type            = AVMEDIA_TYPE_AUDIO;
                st->codecpar->codec_id              = AV_CODEC_ID_PCM_U8;
                st->codecpar->ch_layout             = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
                st->codecpar->bits_per_coded_sample = 8;
                st->codecpar->sample_rate           = vid->sample_rate;
                st->codecpar->bit_rate              = 8 * st->codecpar->sample_rate;
                st->start_time                   = 0;
                avpriv_set_pts_info(st, 64, 1, vid->sample_rate);
            }
            audio_length = avio_rl16(pb);
            if ((ret_value = av_get_packet(pb, pkt, audio_length)) != audio_length) {
                if (ret_value < 0)
                    return ret_value;
                av_log(s, AV_LOG_ERROR, "incomplete audio block\n");
                return AVERROR(EIO);
            }
            pkt->stream_index = vid->audio_index;
            pkt->duration     = audio_length;
            pkt->flags |= AV_PKT_FLAG_KEY;
            return 0;

        case VIDEO_P_FRAME:
        case VIDEO_YOFF_P_FRAME:
        case VIDEO_I_FRAME:
            return read_frame(vid, pb, pkt, block_type, s);

        case EOF_BLOCK:
            if(vid->nframes != 0)
                av_log(s, AV_LOG_VERBOSE, "reached terminating character but not all frames read.\n");
            vid->is_finished = 1;
            return AVERROR(EIO);
        default:
            av_log(s, AV_LOG_ERROR, "unknown block (character = %c, decimal = %d, hex = %x)!!!\n",
                   block_type, block_type, block_type);
            return AVERROR_INVALIDDATA;
    }
}

const AVInputFormat ff_bethsoftvid_demuxer = {
    .name           = "bethsoftvid",
    .long_name      = NULL_IF_CONFIG_SMALL("Bethesda Softworks VID"),
    .priv_data_size = sizeof(BVID_DemuxContext),
    .read_probe     = vid_probe,
    .read_header    = vid_read_header,
    .read_packet    = vid_read_packet,
};
