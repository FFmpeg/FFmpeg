/*
 * RAW AC-3 and E-AC-3 demuxer
 * Copyright (c) 2007 Justin Ruggles <justin.ruggles@gmail.com>
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

#include "libavutil/crc.h"
#include "libavcodec/ac3_parser.h"
#include "avformat.h"
#include "rawdec.h"

static int ac3_eac3_probe(AVProbeData *p, enum AVCodecID expected_codec_id)
{
    int max_frames, first_frames = 0, frames;
    uint8_t *buf, *buf2, *end;
    AC3HeaderInfo hdr;
    GetBitContext gbc;
    enum AVCodecID codec_id = AV_CODEC_ID_AC3;

    max_frames = 0;
    buf = p->buf;
    end = buf + p->buf_size;

    for(; buf < end; buf++) {
        if(buf > p->buf && !(buf[0] == 0x0B && buf[1] == 0x77)
                        && !(buf[0] == 0x77 && buf[1] == 0x0B) )
            continue;
        buf2 = buf;

        for(frames = 0; buf2 < end; frames++) {
            uint8_t buf3[4096];
            int i;
            if(!memcmp(buf2, "\x1\x10\0\0\0\0\0\0", 8))
                buf2+=16;
            if (buf[0] == 0x77 && buf[1] == 0x0B) {
                for(i=0; i<8; i+=2) {
                    buf3[i  ] = buf[i+1];
                    buf3[i+1] = buf[i  ];
                }
                init_get_bits(&gbc, buf3, 54);
            }else
                init_get_bits(&gbc, buf2, 54);
            if(avpriv_ac3_parse_header(&gbc, &hdr) < 0)
                break;
            if(buf2 + hdr.frame_size > end)
                break;
            if (buf[0] == 0x77 && buf[1] == 0x0B) {
                av_assert0(hdr.frame_size <= sizeof(buf3));
                for(i=8; i<hdr.frame_size; i+=2) {
                    buf3[i  ] = buf[i+1];
                    buf3[i+1] = buf[i  ];
                }
            }
            if(av_crc(av_crc_get_table(AV_CRC_16_ANSI), 0, gbc.buffer + 2, hdr.frame_size - 2))
                break;
            if (hdr.bitstream_id > 10)
                codec_id = AV_CODEC_ID_EAC3;
            buf2 += hdr.frame_size;
        }
        max_frames = FFMAX(max_frames, frames);
        if(buf == p->buf)
            first_frames = frames;
    }
    if(codec_id != expected_codec_id) return 0;
    // keep this in sync with mp3 probe, both need to avoid
    // issues with MPEG-files!
    if   (first_frames>=4) return AVPROBE_SCORE_MAX/2+1;
    else if(max_frames>200)return AVPROBE_SCORE_MAX/2;
    else if(max_frames>=4) return AVPROBE_SCORE_MAX/4;
    else if(max_frames>=1) return 1;
    else                   return 0;
}

#if CONFIG_AC3_DEMUXER
static int ac3_probe(AVProbeData *p)
{
    return ac3_eac3_probe(p, AV_CODEC_ID_AC3);
}

AVInputFormat ff_ac3_demuxer = {
    .name           = "ac3",
    .long_name      = NULL_IF_CONFIG_SMALL("raw AC-3"),
    .read_probe     = ac3_probe,
    .read_header    = ff_raw_audio_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "ac3",
    .raw_codec_id   = AV_CODEC_ID_AC3,
};
#endif

#if CONFIG_EAC3_DEMUXER
static int eac3_probe(AVProbeData *p)
{
    return ac3_eac3_probe(p, AV_CODEC_ID_EAC3);
}

AVInputFormat ff_eac3_demuxer = {
    .name           = "eac3",
    .long_name      = NULL_IF_CONFIG_SMALL("raw E-AC-3"),
    .read_probe     = eac3_probe,
    .read_header    = ff_raw_audio_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "eac3",
    .raw_codec_id   = AV_CODEC_ID_EAC3,
};
#endif
