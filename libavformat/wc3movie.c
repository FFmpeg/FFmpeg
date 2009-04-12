/*
 * Wing Commander III Movie (.mve) File Demuxer
 * Copyright (c) 2003 The ffmpeg Project
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
 * @file libavformat/wc3movie.c
 * Wing Commander III Movie file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the WC3 .mve file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define FORM_TAG MKTAG('F', 'O', 'R', 'M')
#define MOVE_TAG MKTAG('M', 'O', 'V', 'E')
#define  PC__TAG MKTAG('_', 'P', 'C', '_')
#define SOND_TAG MKTAG('S', 'O', 'N', 'D')
#define BNAM_TAG MKTAG('B', 'N', 'A', 'M')
#define SIZE_TAG MKTAG('S', 'I', 'Z', 'E')
#define PALT_TAG MKTAG('P', 'A', 'L', 'T')
#define INDX_TAG MKTAG('I', 'N', 'D', 'X')
#define BRCH_TAG MKTAG('B', 'R', 'C', 'H')
#define SHOT_TAG MKTAG('S', 'H', 'O', 'T')
#define VGA__TAG MKTAG('V', 'G', 'A', ' ')
#define TEXT_TAG MKTAG('T', 'E', 'X', 'T')
#define AUDI_TAG MKTAG('A', 'U', 'D', 'I')

/* video resolution unless otherwise specified */
#define WC3_DEFAULT_WIDTH 320
#define WC3_DEFAULT_HEIGHT 165

/* always use the same PCM audio parameters */
#define WC3_SAMPLE_RATE 22050
#define WC3_AUDIO_CHANNELS 1
#define WC3_AUDIO_BITS 16

/* nice, constant framerate */
#define WC3_FRAME_FPS 15

#define PALETTE_SIZE (256 * 3)
#define PALETTE_COUNT 256

typedef struct Wc3DemuxContext {
    int width;
    int height;
    unsigned char *palettes;
    int palette_count;
    int64_t pts;
    int video_stream_index;
    int audio_stream_index;

    AVPaletteControl palette_control;

} Wc3DemuxContext;

/**
 * palette lookup table that does gamma correction
 *
 * can be calculated by this formula:
 * for i between 0 and 251 inclusive:
 * wc3_pal_lookup[i] = round(pow(i / 256.0, 0.8) * 256);
 * values 252, 253, 254 and 255 are all 0xFD
 * calculating this at runtime should not cause any
 * rounding issues, the maximum difference between
 * the table values and the calculated doubles is
 * about 0.497527
 */
static const unsigned char wc3_pal_lookup[] = {
  0x00, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0E,
  0x10, 0x12, 0x13, 0x15, 0x16, 0x18, 0x19, 0x1A,
  0x1C, 0x1D, 0x1F, 0x20, 0x21, 0x23, 0x24, 0x25,
  0x27, 0x28, 0x29, 0x2A, 0x2C, 0x2D, 0x2E, 0x2F,
  0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x38, 0x39,
  0x3A, 0x3B, 0x3C, 0x3D, 0x3F, 0x40, 0x41, 0x42,
  0x43, 0x44, 0x45, 0x46, 0x48, 0x49, 0x4A, 0x4B,
  0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53,
  0x54, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C,
  0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64,
  0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C,
  0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74,
  0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C,
  0x7D, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83,
  0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B,
  0x8C, 0x8D, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92,
  0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x99,
  0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1,
  0xA2, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
  0xA9, 0xAA, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
  0xB0, 0xB1, 0xB2, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
  0xB7, 0xB8, 0xB9, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD,
  0xBE, 0xBF, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4,
  0xC5, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB,
  0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD0, 0xD1,
  0xD2, 0xD3, 0xD4, 0xD5, 0xD5, 0xD6, 0xD7, 0xD8,
  0xD9, 0xDA, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
  0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE4, 0xE5,
  0xE6, 0xE7, 0xE8, 0xE9, 0xE9, 0xEA, 0xEB, 0xEC,
  0xED, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF1, 0xF2,
  0xF3, 0xF4, 0xF5, 0xF6, 0xF6, 0xF7, 0xF8, 0xF9,
  0xFA, 0xFA, 0xFB, 0xFC, 0xFD, 0xFD, 0xFD, 0xFD
};


static int wc3_probe(AVProbeData *p)
{
    if (p->buf_size < 12)
        return 0;

    if ((AV_RL32(&p->buf[0]) != FORM_TAG) ||
        (AV_RL32(&p->buf[8]) != MOVE_TAG))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int wc3_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    Wc3DemuxContext *wc3 = s->priv_data;
    ByteIOContext *pb = s->pb;
    unsigned int fourcc_tag;
    unsigned int size;
    AVStream *st;
    char buffer[513];
    int ret = 0;
    int current_palette = 0;
    int bytes_to_read;
    int i;
    unsigned char rotate;

    /* default context members */
    wc3->width = WC3_DEFAULT_WIDTH;
    wc3->height = WC3_DEFAULT_HEIGHT;
    wc3->palettes = NULL;
    wc3->palette_count = 0;
    wc3->pts = 0;
    wc3->video_stream_index = wc3->audio_stream_index = 0;

    /* skip the first 3 32-bit numbers */
    url_fseek(pb, 12, SEEK_CUR);

    /* traverse through the chunks and load the header information before
     * the first BRCH tag */
    fourcc_tag = get_le32(pb);
    size = (get_be32(pb) + 1) & (~1);

    do {
        switch (fourcc_tag) {

        case SOND_TAG:
        case INDX_TAG:
            /* SOND unknown, INDX unnecessary; ignore both */
            url_fseek(pb, size, SEEK_CUR);
            break;

        case PC__TAG:
            /* need the number of palettes */
            url_fseek(pb, 8, SEEK_CUR);
            wc3->palette_count = get_le32(pb);
            if((unsigned)wc3->palette_count >= UINT_MAX / PALETTE_SIZE){
                wc3->palette_count= 0;
                return -1;
            }
            wc3->palettes = av_malloc(wc3->palette_count * PALETTE_SIZE);
            break;

        case BNAM_TAG:
            /* load up the name */
            if ((unsigned)size < 512)
                bytes_to_read = size;
            else
                bytes_to_read = 512;
            if ((ret = get_buffer(pb, buffer, bytes_to_read)) != bytes_to_read)
                return AVERROR(EIO);
            buffer[bytes_to_read] = 0;
            av_metadata_set(&s->metadata, "title", buffer);
            break;

        case SIZE_TAG:
            /* video resolution override */
            wc3->width  = get_le32(pb);
            wc3->height = get_le32(pb);
            break;

        case PALT_TAG:
            /* one of several palettes */
            if ((unsigned)current_palette >= wc3->palette_count)
                return AVERROR_INVALIDDATA;
            if ((ret = get_buffer(pb,
                &wc3->palettes[current_palette * PALETTE_SIZE],
                PALETTE_SIZE)) != PALETTE_SIZE)
                return AVERROR(EIO);

            /* transform the current palette in place */
            for (i = current_palette * PALETTE_SIZE;
                 i < (current_palette + 1) * PALETTE_SIZE; i++) {
                /* rotate each palette component left by 2 and use the result
                 * as an index into the color component table */
                rotate = ((wc3->palettes[i] << 2) & 0xFF) |
                         ((wc3->palettes[i] >> 6) & 0xFF);
                wc3->palettes[i] = wc3_pal_lookup[rotate];
            }
            current_palette++;
            break;

        default:
            av_log(s, AV_LOG_ERROR, "  unrecognized WC3 chunk: %c%c%c%c (0x%02X%02X%02X%02X)\n",
                (uint8_t)fourcc_tag, (uint8_t)(fourcc_tag >> 8), (uint8_t)(fourcc_tag >> 16), (uint8_t)(fourcc_tag >> 24),
                (uint8_t)fourcc_tag, (uint8_t)(fourcc_tag >> 8), (uint8_t)(fourcc_tag >> 16), (uint8_t)(fourcc_tag >> 24));
            return AVERROR_INVALIDDATA;
            break;
        }

        fourcc_tag = get_le32(pb);
        /* chunk sizes are 16-bit aligned */
        size = (get_be32(pb) + 1) & (~1);
        if (url_feof(pb))
            return AVERROR(EIO);

    } while (fourcc_tag != BRCH_TAG);

    /* initialize the decoder streams */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    av_set_pts_info(st, 33, 1, WC3_FRAME_FPS);
    wc3->video_stream_index = st->index;
    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_XAN_WC3;
    st->codec->codec_tag = 0;  /* no fourcc */
    st->codec->width = wc3->width;
    st->codec->height = wc3->height;

    /* palette considerations */
    st->codec->palctrl = &wc3->palette_control;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    av_set_pts_info(st, 33, 1, WC3_FRAME_FPS);
    wc3->audio_stream_index = st->index;
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_PCM_S16LE;
    st->codec->codec_tag = 1;
    st->codec->channels = WC3_AUDIO_CHANNELS;
    st->codec->bits_per_coded_sample = WC3_AUDIO_BITS;
    st->codec->sample_rate = WC3_SAMPLE_RATE;
    st->codec->bit_rate = st->codec->channels * st->codec->sample_rate *
        st->codec->bits_per_coded_sample;
    st->codec->block_align = WC3_AUDIO_BITS * WC3_AUDIO_CHANNELS;

    return 0;
}

static int wc3_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    Wc3DemuxContext *wc3 = s->priv_data;
    ByteIOContext *pb = s->pb;
    unsigned int fourcc_tag;
    unsigned int size;
    int packet_read = 0;
    int ret = 0;
    unsigned char text[1024];
    unsigned int palette_number;
    int i;
    unsigned char r, g, b;
    int base_palette_index;

    while (!packet_read) {

        fourcc_tag = get_le32(pb);
        /* chunk sizes are 16-bit aligned */
        size = (get_be32(pb) + 1) & (~1);
        if (url_feof(pb))
            return AVERROR(EIO);

        switch (fourcc_tag) {

        case BRCH_TAG:
            /* no-op */
            break;

        case SHOT_TAG:
            /* load up new palette */
            palette_number = get_le32(pb);
            if (palette_number >= wc3->palette_count)
                return AVERROR_INVALIDDATA;
            base_palette_index = palette_number * PALETTE_COUNT * 3;
            for (i = 0; i < PALETTE_COUNT; i++) {
                r = wc3->palettes[base_palette_index + i * 3 + 0];
                g = wc3->palettes[base_palette_index + i * 3 + 1];
                b = wc3->palettes[base_palette_index + i * 3 + 2];
                wc3->palette_control.palette[i] = (r << 16) | (g << 8) | (b);
            }
            wc3->palette_control.palette_changed = 1;
            break;

        case VGA__TAG:
            /* send out video chunk */
            ret= av_get_packet(pb, pkt, size);
            pkt->stream_index = wc3->video_stream_index;
            pkt->pts = wc3->pts;
            packet_read = 1;
            break;

        case TEXT_TAG:
            /* subtitle chunk */
#if 0
            url_fseek(pb, size, SEEK_CUR);
#else
            if ((unsigned)size > sizeof(text) || (ret = get_buffer(pb, text, size)) != size)
                ret = AVERROR(EIO);
            else {
                int i = 0;
                av_log (s, AV_LOG_DEBUG, "Subtitle time!\n");
                av_log (s, AV_LOG_DEBUG, "  inglish: %s\n", &text[i + 1]);
                i += text[i] + 1;
                av_log (s, AV_LOG_DEBUG, "  doytsch: %s\n", &text[i + 1]);
                i += text[i] + 1;
                av_log (s, AV_LOG_DEBUG, "  fronsay: %s\n", &text[i + 1]);
            }
#endif
            break;

        case AUDI_TAG:
            /* send out audio chunk */
            ret= av_get_packet(pb, pkt, size);
            pkt->stream_index = wc3->audio_stream_index;
            pkt->pts = wc3->pts;

            /* time to advance pts */
            wc3->pts++;

            packet_read = 1;
            break;

        default:
            av_log (s, AV_LOG_ERROR, "  unrecognized WC3 chunk: %c%c%c%c (0x%02X%02X%02X%02X)\n",
                (uint8_t)fourcc_tag, (uint8_t)(fourcc_tag >> 8), (uint8_t)(fourcc_tag >> 16), (uint8_t)(fourcc_tag >> 24),
                (uint8_t)fourcc_tag, (uint8_t)(fourcc_tag >> 8), (uint8_t)(fourcc_tag >> 16), (uint8_t)(fourcc_tag >> 24));
            ret = AVERROR_INVALIDDATA;
            packet_read = 1;
            break;
        }
    }

    return ret;
}

static int wc3_read_close(AVFormatContext *s)
{
    Wc3DemuxContext *wc3 = s->priv_data;

    av_free(wc3->palettes);

    return 0;
}

AVInputFormat wc3_demuxer = {
    "wc3movie",
    NULL_IF_CONFIG_SMALL("Wing Commander III movie format"),
    sizeof(Wc3DemuxContext),
    wc3_probe,
    wc3_read_header,
    wc3_read_packet,
    wc3_read_close,
};
