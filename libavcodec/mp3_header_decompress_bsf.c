/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "bsf.h"
#include "bsf_internal.h"
#include "mpegaudiodecheader.h"
#include "mpegaudiodata.h"


static int mp3_header_decompress(AVBSFContext *ctx, AVPacket *out)
{
    AVPacket *in;
    uint32_t header;
    int sample_rate= ctx->par_in->sample_rate;
    int sample_rate_index=0;
    int lsf, mpeg25, bitrate_index, frame_size, ret;
    uint8_t *buf;
    int buf_size;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    buf      = in->data;
    buf_size = in->size;

    header = AV_RB32(buf);
    if(ff_mpa_check_header(header) >= 0){
        av_packet_move_ref(out, in);
        av_packet_free(&in);

        return 0;
    }

    if(ctx->par_in->extradata_size != 15 || strcmp(ctx->par_in->extradata, "FFCMP3 0.0")){
        av_log(ctx, AV_LOG_ERROR, "Extradata invalid %d\n", ctx->par_in->extradata_size);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    header= AV_RB32(ctx->par_in->extradata+11) & MP3_MASK;

    lsf     = sample_rate < (24000+32000)/2;
    mpeg25  = sample_rate < (12000+16000)/2;
    sample_rate_index= (header>>10)&3;
    if (sample_rate_index == 3) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    sample_rate= avpriv_mpa_freq_tab[sample_rate_index] >> (lsf + mpeg25); //in case sample rate is a little off

    for(bitrate_index=2; bitrate_index<30; bitrate_index++){
        frame_size = avpriv_mpa_bitrate_tab[lsf][2][bitrate_index>>1];
        frame_size = (frame_size * 144000) / (sample_rate << lsf) + (bitrate_index&1);
        if(frame_size == buf_size + 4)
            break;
        if(frame_size == buf_size + 6)
            break;
    }
    if(bitrate_index == 30){
        av_log(ctx, AV_LOG_ERROR, "Could not find bitrate_index.\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    header |= (bitrate_index&1)<<9;
    header |= (bitrate_index>>1)<<12;
    header |= (frame_size == buf_size + 4)<<16; //FIXME actually set a correct crc instead of 0

    ret = av_new_packet(out, frame_size);
    if (ret < 0)
        goto fail;
    ret = av_packet_copy_props(out, in);
    if (ret < 0) {
        av_packet_unref(out);
        goto fail;
    }
    memcpy(out->data + frame_size - buf_size, buf, buf_size + AV_INPUT_BUFFER_PADDING_SIZE);

    if(ctx->par_in->channels==2){
        uint8_t *p= out->data + frame_size - buf_size;
        if(lsf){
            FFSWAP(int, p[1], p[2]);
            header |= (p[1] & 0xC0)>>2;
            p[1] &= 0x3F;
        }else{
            header |= p[1] & 0x30;
            p[1] &= 0xCF;
        }
    }

    AV_WB32(out->data, header);

    ret = 0;

fail:
    av_packet_free(&in);
    return ret;
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_MP3, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_mp3_header_decompress_bsf = {
    .name      = "mp3decomp",
    .filter    = mp3_header_decompress,
    .codec_ids = codec_ids,
};
