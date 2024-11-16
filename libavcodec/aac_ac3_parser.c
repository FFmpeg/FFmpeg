/*
 * Common AAC and AC-3 parser
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2003 Michael Niedermayer
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

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "parser.h"
#include "aac_ac3_parser.h"
#include "ac3_parser_internal.h"
#include "adts_header.h"

int ff_aac_ac3_parse(AVCodecParserContext *s1,
                     AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    AACAC3ParseContext *s = s1->priv_data;
    ParseContext *pc = &s->pc;
    int len, i;
    int new_frame_start;
    int got_frame = 0;

    s1->key_frame = -1;

    if (s1->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        i = buf_size;
        got_frame = 1;
    } else {
get_next:
        i=END_NOT_FOUND;
        if(s->remaining_size <= buf_size){
            if(s->remaining_size && !s->need_next_header){
                i= s->remaining_size;
                s->remaining_size = 0;
            }else{ //we need a header first
                len=0;
                for(i=s->remaining_size; i<buf_size; i++){
                    s->state = (s->state<<8) + buf[i];
                    if((len=s->sync(s->state, &s->need_next_header, &new_frame_start)))
                        break;
                }
                if(len<=0){
                    i=END_NOT_FOUND;
                }else{
                    got_frame = 1;
                    s->state=0;
                    i-= s->header_size -1;
                    s->remaining_size = len;
                    if(!new_frame_start || pc->index+i<=0){
                        s->remaining_size += i;
                        goto get_next;
                    }
                    else if (i < 0) {
                        s->remaining_size += i;
                    }
                }
            }
        }

        if(ff_combine_frame(pc, i, &buf, &buf_size)<0){
            s->remaining_size -= FFMIN(s->remaining_size, buf_size);
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    *poutbuf = buf;
    *poutbuf_size = buf_size;

    if (got_frame) {
        int bit_rate;

        /* Due to backwards compatible HE-AAC the sample rate, channel count,
           and total number of samples found in an AAC ADTS header are not
           reliable. Bit rate is still accurate because the total frame
           duration in seconds is still correct (as is the number of bits in
           the frame). */
        if (avctx->codec_id != AV_CODEC_ID_AAC) {
#if CONFIG_AC3_PARSER
            AC3HeaderInfo hdr, *phrd = &hdr;
            int offset = ff_ac3_find_syncword(buf, buf_size);

            if (offset < 0)
                return i;

            buf += offset;
            buf_size -= offset;
            while (buf_size > 0) {
                int ret = avpriv_ac3_parse_header(&phrd, buf, buf_size);

                if (ret < 0 || hdr.frame_size > buf_size)
                    return i;

                if (buf_size > hdr.frame_size) {
                    buf += hdr.frame_size;
                    buf_size -= hdr.frame_size;
                    continue;
                }
                /* Check for false positives since the syncword is not enough.
                   See section 6.1.2 of A/52. */
                if (av_crc(s->crc_ctx, 0, buf + 2, hdr.frame_size - 2))
                    return i;
                break;
            }

            avctx->sample_rate = hdr.sample_rate;

            if (hdr.bitstream_id > 10)
                avctx->codec_id = AV_CODEC_ID_EAC3;

            if (!CONFIG_EAC3_DECODER || avctx->codec_id != AV_CODEC_ID_EAC3) {
                av_channel_layout_uninit(&avctx->ch_layout);
                if (hdr.channel_layout) {
                    av_channel_layout_from_mask(&avctx->ch_layout, hdr.channel_layout);
                } else {
                    avctx->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
                    avctx->ch_layout.nb_channels = hdr.channels;
                }
            }
            s1->duration = hdr.num_blocks * 256;
            avctx->audio_service_type = hdr.bitstream_mode;
            if (hdr.bitstream_mode == 0x7 && hdr.channels > 1)
                avctx->audio_service_type = AV_AUDIO_SERVICE_TYPE_KARAOKE;
            bit_rate = hdr.bit_rate;
#endif
        } else {
#if CONFIG_AAC_PARSER
            AACADTSHeaderInfo hdr;
            GetBitContext gb;
            int profile;
            init_get_bits8(&gb, buf, buf_size);
            if (buf_size < AV_AAC_ADTS_HEADER_SIZE ||
                ff_adts_header_parse(&gb, &hdr) < 0)
                return i;

            avctx->profile = hdr.object_type - 1;
            s1->key_frame = (avctx->profile == AV_PROFILE_AAC_USAC) ? get_bits1(&gb) : 1;
            bit_rate = hdr.bit_rate;
#endif
        }

        /* Calculate the average bit rate */
        s->frame_number++;
        if (!CONFIG_EAC3_DECODER || avctx->codec_id != AV_CODEC_ID_EAC3) {
            avctx->bit_rate +=
                (bit_rate - avctx->bit_rate) / s->frame_number;
        }
    }

    return i;
}
