/*
 * id Quake II CIN File Demuxer
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
 * @file
 * id Quake II CIN file demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information about the id CIN format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 *
 * CIN is a somewhat quirky and ill-defined format. Here are some notes
 * for anyone trying to understand the technical details of this format:
 *
 * The format has no definite file signature. This is problematic for a
 * general-purpose media player that wants to automatically detect file
 * types. However, a CIN file does start with 5 32-bit numbers that
 * specify audio and video parameters. This demuxer gets around the lack
 * of file signature by performing sanity checks on those parameters.
 * Probabalistically, this is a reasonable solution since the number of
 * valid combinations of the 5 parameters is a very small subset of the
 * total 160-bit number space.
 *
 * Refer to the function idcin_probe() for the precise A/V parameters
 * that this demuxer allows.
 *
 * Next, each audio and video frame has a duration of 1/14 sec. If the
 * audio sample rate is a multiple of the common frequency 22050 Hz it will
 * divide evenly by 14. However, if the sample rate is 11025 Hz:
 *   11025 (samples/sec) / 14 (frames/sec) = 787.5 (samples/frame)
 * The way the CIN stores audio in this case is by storing 787 sample
 * frames in the first audio frame and 788 sample frames in the second
 * audio frame. Therefore, the total number of bytes in an audio frame
 * is given as:
 *   audio frame #0: 787 * (bytes/sample) * (# channels) bytes in frame
 *   audio frame #1: 788 * (bytes/sample) * (# channels) bytes in frame
 *   audio frame #2: 787 * (bytes/sample) * (# channels) bytes in frame
 *   audio frame #3: 788 * (bytes/sample) * (# channels) bytes in frame
 *
 * Finally, not all id CIN creation tools agree on the resolution of the
 * color palette, apparently. Some creation tools specify red, green, and
 * blue palette components in terms of 6-bit VGA color DAC values which
 * range from 0..63. Other tools specify the RGB components as full 8-bit
 * values that range from 0..255. Since there are no markers in the file to
 * differentiate between the two variants, this demuxer uses the following
 * heuristic:
 *   - load the 768 palette bytes from disk
 *   - assume that they will need to be shifted left by 2 bits to
 *     transform them from 6-bit values to 8-bit values
 *   - scan through all 768 palette bytes
 *     - if any bytes exceed 63, do not shift the bytes at all before
 *       transmitting them to the video decoder
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define HUFFMAN_TABLE_SIZE (64 * 1024)
#define IDCIN_FPS 14

typedef struct IdcinDemuxContext {
    int video_stream_index;
    int audio_stream_index;
    int audio_chunk_size1;
    int audio_chunk_size2;

    /* demux state variables */
    int current_audio_chunk;
    int next_chunk_is_video;
    int audio_present;

    int64_t pts;
} IdcinDemuxContext;

static int idcin_probe(AVProbeData *p)
{
    unsigned int number;

    /*
     * This is what you could call a "probabilistic" file check: id CIN
     * files don't have a definite file signature. In lieu of such a marker,
     * perform sanity checks on the 5 32-bit header fields:
     *  width, height: greater than 0, less than or equal to 1024
     * audio sample rate: greater than or equal to 8000, less than or
     *  equal to 48000, or 0 for no audio
     * audio sample width (bytes/sample): 0 for no audio, or 1 or 2
     * audio channels: 0 for no audio, or 1 or 2
     */

    /* check we have enough data to do all checks, otherwise the
       0-padding may cause a wrong recognition */
    if (p->buf_size < 20)
        return 0;

    /* check the video width */
    number = AV_RL32(&p->buf[0]);
    if ((number == 0) || (number > 1024))
       return 0;

    /* check the video height */
    number = AV_RL32(&p->buf[4]);
    if ((number == 0) || (number > 1024))
       return 0;

    /* check the audio sample rate */
    number = AV_RL32(&p->buf[8]);
    if ((number != 0) && ((number < 8000) | (number > 48000)))
        return 0;

    /* check the audio bytes/sample */
    number = AV_RL32(&p->buf[12]);
    if (number > 2)
        return 0;

    /* check the audio channels */
    number = AV_RL32(&p->buf[16]);
    if (number > 2)
        return 0;

    /* return half certainly since this check is a bit sketchy */
    return AVPROBE_SCORE_MAX / 2;
}

static int idcin_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    AVIOContext *pb = s->pb;
    IdcinDemuxContext *idcin = s->priv_data;
    AVStream *st;
    unsigned int width, height;
    unsigned int sample_rate, bytes_per_sample, channels;

    /* get the 5 header parameters */
    width = avio_rl32(pb);
    height = avio_rl32(pb);
    sample_rate = avio_rl32(pb);
    bytes_per_sample = avio_rl32(pb);
    channels = avio_rl32(pb);

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    av_set_pts_info(st, 33, 1, IDCIN_FPS);
    idcin->video_stream_index = st->index;
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_IDCIN;
    st->codec->codec_tag = 0;  /* no fourcc */
    st->codec->width = width;
    st->codec->height = height;

    /* load up the Huffman tables into extradata */
    st->codec->extradata_size = HUFFMAN_TABLE_SIZE;
    st->codec->extradata = av_malloc(HUFFMAN_TABLE_SIZE);
    if (avio_read(pb, st->codec->extradata, HUFFMAN_TABLE_SIZE) !=
        HUFFMAN_TABLE_SIZE)
        return AVERROR(EIO);

    /* if sample rate is 0, assume no audio */
    if (sample_rate) {
        idcin->audio_present = 1;
        st = av_new_stream(s, 0);
        if (!st)
            return AVERROR(ENOMEM);
        av_set_pts_info(st, 33, 1, IDCIN_FPS);
        idcin->audio_stream_index = st->index;
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_tag = 1;
        st->codec->channels = channels;
        st->codec->sample_rate = sample_rate;
        st->codec->bits_per_coded_sample = bytes_per_sample * 8;
        st->codec->bit_rate = sample_rate * bytes_per_sample * 8 * channels;
        st->codec->block_align = bytes_per_sample * channels;
        if (bytes_per_sample == 1)
            st->codec->codec_id = CODEC_ID_PCM_U8;
        else
            st->codec->codec_id = CODEC_ID_PCM_S16LE;

        if (sample_rate % 14 != 0) {
            idcin->audio_chunk_size1 = (sample_rate / 14) *
            bytes_per_sample * channels;
            idcin->audio_chunk_size2 = (sample_rate / 14 + 1) *
                bytes_per_sample * channels;
        } else {
            idcin->audio_chunk_size1 = idcin->audio_chunk_size2 =
                (sample_rate / 14) * bytes_per_sample * channels;
        }
        idcin->current_audio_chunk = 0;
    } else
        idcin->audio_present = 1;

    idcin->next_chunk_is_video = 1;
    idcin->pts = 0;

    return 0;
}

static int idcin_read_packet(AVFormatContext *s,
                             AVPacket *pkt)
{
    int ret;
    unsigned int command;
    unsigned int chunk_size;
    IdcinDemuxContext *idcin = s->priv_data;
    AVIOContext *pb = s->pb;
    int i;
    int palette_scale;
    unsigned char r, g, b;
    unsigned char palette_buffer[768];
    uint32_t palette[256];

    if (url_feof(s->pb))
        return AVERROR(EIO);

    if (idcin->next_chunk_is_video) {
        command = avio_rl32(pb);
        if (command == 2) {
            return AVERROR(EIO);
        } else if (command == 1) {
            /* trigger a palette change */
            if (avio_read(pb, palette_buffer, 768) != 768)
                return AVERROR(EIO);
            /* scale the palette as necessary */
            palette_scale = 2;
            for (i = 0; i < 768; i++)
                if (palette_buffer[i] > 63) {
                    palette_scale = 0;
                    break;
                }

            for (i = 0; i < 256; i++) {
                r = palette_buffer[i * 3    ] << palette_scale;
                g = palette_buffer[i * 3 + 1] << palette_scale;
                b = palette_buffer[i * 3 + 2] << palette_scale;
                palette[i] = (r << 16) | (g << 8) | (b);
            }
        }

        chunk_size = avio_rl32(pb);
        /* skip the number of decoded bytes (always equal to width * height) */
        avio_skip(pb, 4);
        chunk_size -= 4;
        ret= av_get_packet(pb, pkt, chunk_size);
        if (ret < 0)
            return ret;
        if (command == 1) {
            uint8_t *pal;

            pal = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE,
                                          AVPALETTE_SIZE);
            if (ret < 0)
                return ret;
            memcpy(pal, palette, AVPALETTE_SIZE);
        }
        pkt->stream_index = idcin->video_stream_index;
        pkt->pts = idcin->pts;
    } else {
        /* send out the audio chunk */
        if (idcin->current_audio_chunk)
            chunk_size = idcin->audio_chunk_size2;
        else
            chunk_size = idcin->audio_chunk_size1;
        ret= av_get_packet(pb, pkt, chunk_size);
        if (ret < 0)
            return ret;
        pkt->stream_index = idcin->audio_stream_index;
        pkt->pts = idcin->pts;

        idcin->current_audio_chunk ^= 1;
        idcin->pts++;
    }

    if (idcin->audio_present)
        idcin->next_chunk_is_video ^= 1;

    return ret;
}

AVInputFormat ff_idcin_demuxer = {
    "idcin",
    NULL_IF_CONFIG_SMALL("id Cinematic format"),
    sizeof(IdcinDemuxContext),
    idcin_probe,
    idcin_read_header,
    idcin_read_packet,
};
