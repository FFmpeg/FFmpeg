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
 * @file libavformat/bethsoftvid.c
 * @brief Bethesda Softworks VID (.vid) file demuxer
 * @author Nicholas Tung [ntung (at. ntung com] (2007-03)
 * @sa http://wiki.multimedia.cx/index.php?title=Bethsoft_VID
 * @sa http://www.svatopluk.com/andux/docs/dfvid.html
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "libavcodec/bethsoftvideo.h"

typedef struct BVID_DemuxContext
{
    int nframes;
    /** delay value between frames, added to individual frame delay.
     * custom units, which will be added to other custom units (~=16ms according
     * to free, unofficial documentation) */
    int bethsoft_global_delay;

    /** video presentation time stamp.
     * delay = 16 milliseconds * (global_delay + per_frame_delay) */
    int video_pts;

    int is_finished;

} BVID_DemuxContext;

static int vid_probe(AVProbeData *p)
{
    // little endian VID tag, file starts with "VID\0"
    if (AV_RL32(p->buf) != MKTAG('V', 'I', 'D', 0))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int vid_read_header(AVFormatContext *s,
                            AVFormatParameters *ap)
{
    BVID_DemuxContext *vid = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *stream;

    /* load main header. Contents:
    *    bytes: 'V' 'I' 'D'
    *    int16s: always_512, nframes, width, height, delay, always_14
    */
    url_fseek(pb, 5, SEEK_CUR);
    vid->nframes = get_le16(pb);

    stream = av_new_stream(s, 0);
    if (!stream)
        return AVERROR(ENOMEM);
    av_set_pts_info(stream, 32, 1, 60);     // 16 ms increments, i.e. 60 fps
    stream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codec->codec_id = CODEC_ID_BETHSOFTVID;
    stream->codec->width = get_le16(pb);
    stream->codec->height = get_le16(pb);
    stream->codec->pix_fmt = PIX_FMT_PAL8;
    vid->bethsoft_global_delay = get_le16(pb);
    get_le16(pb);

    // done with video codec, set up audio codec
    stream = av_new_stream(s, 0);
    if (!stream)
        return AVERROR(ENOMEM);
    stream->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    stream->codec->codec_id = CODEC_ID_PCM_U8;
    stream->codec->channels = 1;
    stream->codec->sample_rate = 11025;
    stream->codec->bits_per_coded_sample = 8;
    stream->codec->bit_rate = stream->codec->channels * stream->codec->sample_rate * stream->codec->bits_per_coded_sample;

    return 0;
}

#define BUFFER_PADDING_SIZE 1000
static int read_frame(BVID_DemuxContext *vid, ByteIOContext *pb, AVPacket *pkt,
                      uint8_t block_type, AVFormatContext *s, int npixels)
{
    uint8_t * vidbuf_start = NULL;
    int vidbuf_nbytes = 0;
    int code;
    int bytes_copied = 0;
    int position;
    unsigned int vidbuf_capacity;

    vidbuf_start = av_malloc(vidbuf_capacity = BUFFER_PADDING_SIZE);
    if(!vidbuf_start)
        return AVERROR(ENOMEM);

    // save the file position for the packet, include block type
    position = url_ftell(pb) - 1;

    vidbuf_start[vidbuf_nbytes++] = block_type;

    // get the video delay (next int16), and set the presentation time
    vid->video_pts += vid->bethsoft_global_delay + get_le16(pb);

    // set the y offset if it exists (decoder header data should be in data section)
    if(block_type == VIDEO_YOFF_P_FRAME){
        if(get_buffer(pb, &vidbuf_start[vidbuf_nbytes], 2) != 2)
            goto fail;
        vidbuf_nbytes += 2;
    }

    do{
        vidbuf_start = av_fast_realloc(vidbuf_start, &vidbuf_capacity, vidbuf_nbytes + BUFFER_PADDING_SIZE);
        if(!vidbuf_start)
            return AVERROR(ENOMEM);

        code = get_byte(pb);
        vidbuf_start[vidbuf_nbytes++] = code;

        if(code >= 0x80){ // rle sequence
            if(block_type == VIDEO_I_FRAME)
                vidbuf_start[vidbuf_nbytes++] = get_byte(pb);
        } else if(code){ // plain sequence
            if(get_buffer(pb, &vidbuf_start[vidbuf_nbytes], code) != code)
                goto fail;
            vidbuf_nbytes += code;
        }
        bytes_copied += code & 0x7F;
        if(bytes_copied == npixels){ // sometimes no stop character is given, need to keep track of bytes copied
            // may contain a 0 byte even if read all pixels
            if(get_byte(pb))
                url_fseek(pb, -1, SEEK_CUR);
            break;
        }
        if(bytes_copied > npixels)
            goto fail;
    } while(code);

    // copy data into packet
    if(av_new_packet(pkt, vidbuf_nbytes) < 0)
        goto fail;
    memcpy(pkt->data, vidbuf_start, vidbuf_nbytes);
    av_free(vidbuf_start);

    pkt->pos = position;
    pkt->stream_index = 0;  // use the video decoder, which was initialized as the first stream
    pkt->pts = vid->video_pts;

    vid->nframes--;  // used to check if all the frames were read
    return vidbuf_nbytes;
fail:
    av_free(vidbuf_start);
    return -1;
}

static int vid_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    BVID_DemuxContext *vid = s->priv_data;
    ByteIOContext *pb = s->pb;
    unsigned char block_type;
    int audio_length;
    int ret_value;

    if(vid->is_finished || url_feof(pb))
        return AVERROR(EIO);

    block_type = get_byte(pb);
    switch(block_type){
        case PALETTE_BLOCK:
            url_fseek(pb, -1, SEEK_CUR);     // include block type
            ret_value = av_get_packet(pb, pkt, 3 * 256 + 1);
            if(ret_value != 3 * 256 + 1){
                av_free_packet(pkt);
                return AVERROR(EIO);
            }
            pkt->stream_index = 0;
            return ret_value;

        case FIRST_AUDIO_BLOCK:
            get_le16(pb);
            // soundblaster DAC used for sample rate, as on specification page (link above)
            s->streams[1]->codec->sample_rate = 1000000 / (256 - get_byte(pb));
            s->streams[1]->codec->bit_rate = s->streams[1]->codec->channels * s->streams[1]->codec->sample_rate * s->streams[1]->codec->bits_per_coded_sample;
        case AUDIO_BLOCK:
            audio_length = get_le16(pb);
            ret_value = av_get_packet(pb, pkt, audio_length);
            pkt->stream_index = 1;
            return ret_value != audio_length ? AVERROR(EIO) : ret_value;

        case VIDEO_P_FRAME:
        case VIDEO_YOFF_P_FRAME:
        case VIDEO_I_FRAME:
            return read_frame(vid, pb, pkt, block_type, s,
                              s->streams[0]->codec->width * s->streams[0]->codec->height);

        case EOF_BLOCK:
            if(vid->nframes != 0)
                av_log(s, AV_LOG_VERBOSE, "reached terminating character but not all frames read.\n");
            vid->is_finished = 1;
            return AVERROR(EIO);
        default:
            av_log(s, AV_LOG_ERROR, "unknown block (character = %c, decimal = %d, hex = %x)!!!\n",
                   block_type, block_type, block_type); return -1;
    }

    return 0;
}

AVInputFormat bethsoftvid_demuxer = {
    "bethsoftvid",
    NULL_IF_CONFIG_SMALL("Bethesda Softworks VID format"),
    sizeof(BVID_DemuxContext),
    vid_probe,
    vid_read_header,
    vid_read_packet,
};
