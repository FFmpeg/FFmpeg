/*
 * Apple HTTP Live Streaming Sample Encryption/Decryption
 *
 * Copyright (c) 2021 Nachiket Tarate
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
 * Apple HTTP Live Streaming Sample Encryption
 * https://developer.apple.com/library/ios/documentation/AudioVideo/Conceptual/HLS_Sample_Encryption
 */

#include "libavutil/channel_layout.h"

#include "hls_sample_encryption.h"

#include "libavcodec/adts_header.h"
#include "libavcodec/adts_parser.h"
#include "libavcodec/ac3_parser_internal.h"


typedef struct NALUnit {
    uint8_t     *data;
    int         type;
    int         length;
    int         start_code_length;
} NALUnit;

typedef struct AudioFrame {
    uint8_t     *data;
    int         length;
    int         header_length;
} AudioFrame;

typedef struct CodecParserContext {
    const uint8_t   *buf_ptr;
    const uint8_t   *buf_end;
} CodecParserContext;

static const int eac3_sample_rate_tab[] = { 48000, 44100, 32000, 0 };

void ff_hls_senc_read_audio_setup_info(HLSAudioSetupInfo *info, const uint8_t *buf, size_t size)
{
    if (size < 8)
        return;

    info->codec_tag = AV_RL32(buf);

    if (info->codec_tag == MKTAG('z','a','a','c'))
        info->codec_id = AV_CODEC_ID_AAC;
    else if (info->codec_tag == MKTAG('z','a','c','3'))
        info->codec_id = AV_CODEC_ID_AC3;
    else if (info->codec_tag == MKTAG('z','e','c','3'))
        info->codec_id = AV_CODEC_ID_EAC3;
    else
        info->codec_id = AV_CODEC_ID_NONE;

    buf += 4;
    info->priming               = AV_RL16(buf);
    buf += 2;
    info->version               = *buf++;
    info->setup_data_length     = *buf++;

    if (info->setup_data_length > size - 8)
        info->setup_data_length = size - 8;

    if (info->setup_data_length > HLS_MAX_AUDIO_SETUP_DATA_LEN)
        return;

    memcpy(info->setup_data, buf, info->setup_data_length);
}

int ff_hls_senc_parse_audio_setup_info(AVStream *st, HLSAudioSetupInfo *info)
{
    int ret = 0;

    st->codecpar->codec_tag = info->codec_tag;

    if (st->codecpar->codec_id == AV_CODEC_ID_AAC)
        return 0;

    if (st->codecpar->codec_id != AV_CODEC_ID_AC3 && st->codecpar->codec_id != AV_CODEC_ID_EAC3)
        return AVERROR_INVALIDDATA;

    if (st->codecpar->codec_id == AV_CODEC_ID_AC3) {
        AC3HeaderInfo *ac3hdr = NULL;

        ret = avpriv_ac3_parse_header(&ac3hdr, info->setup_data, info->setup_data_length);
        if (ret < 0) {
            if (ret != AVERROR(ENOMEM))
                av_free(ac3hdr);
            return ret;
        }

        st->codecpar->sample_rate       = ac3hdr->sample_rate;
        st->codecpar->channels          = ac3hdr->channels;
        st->codecpar->channel_layout    = ac3hdr->channel_layout;
        st->codecpar->bit_rate          = ac3hdr->bit_rate;

        av_free(ac3hdr);
    } else {  /*  Parse 'dec3' EC3SpecificBox */
        GetBitContext gb;
        int data_rate, fscod, acmod, lfeon;

        ret = init_get_bits8(&gb, info->setup_data, info->setup_data_length);
        if (ret < 0)
            return AVERROR_INVALIDDATA;

        data_rate = get_bits(&gb, 13);
        skip_bits(&gb, 3);
        fscod = get_bits(&gb, 2);
        skip_bits(&gb, 10);
        acmod = get_bits(&gb, 3);
        lfeon = get_bits(&gb, 1);

        st->codecpar->sample_rate = eac3_sample_rate_tab[fscod];

        st->codecpar->channel_layout = ff_ac3_channel_layout_tab[acmod];
        if (lfeon)
            st->codecpar->channel_layout |= AV_CH_LOW_FREQUENCY;

        st->codecpar->channels = av_get_channel_layout_nb_channels(st->codecpar->channel_layout);

        st->codecpar->bit_rate = data_rate*1000;
    }

    return 0;
}

/*
 * Remove start code emulation prevention 0x03 bytes
 */
static void remove_scep_3_bytes(NALUnit *nalu)
{
    int i = 0;
    int j = 0;

    uint8_t *data = nalu->data;

    while (i < nalu->length) {
        if (nalu->length - i > 3 && AV_RB24(&data[i]) == 0x000003) {
            data[j++] = data[i++];
            data[j++] = data[i++];
            i++;
        } else {
            data[j++] = data[i++];
        }
    }

    nalu->length = j;
}

static int get_next_nal_unit(CodecParserContext *ctx, NALUnit *nalu)
{
    const uint8_t *nalu_start = ctx->buf_ptr;

    if (ctx->buf_end - ctx->buf_ptr >= 4 && AV_RB32(ctx->buf_ptr) == 0x00000001)
        nalu->start_code_length = 4;
    else if (ctx->buf_end - ctx->buf_ptr >= 3 && AV_RB24(ctx->buf_ptr) == 0x000001)
        nalu->start_code_length = 3;
    else /* No start code at the beginning of the NAL unit */
        return -1;

    ctx->buf_ptr += nalu->start_code_length;

    while (ctx->buf_ptr < ctx->buf_end) {
        if (ctx->buf_end - ctx->buf_ptr >= 4 && AV_RB32(ctx->buf_ptr) == 0x00000001)
            break;
        else if (ctx->buf_end - ctx->buf_ptr >= 3 && AV_RB24(ctx->buf_ptr) == 0x000001)
            break;
        ctx->buf_ptr++;
    }

    nalu->data   = (uint8_t *)nalu_start + nalu->start_code_length;
    nalu->length = ctx->buf_ptr - nalu->data;
    nalu->type   = *nalu->data & 0x1F;

    return 0;
}

static int decrypt_nal_unit(HLSCryptoContext *crypto_ctx, NALUnit *nalu)
{
    int ret = 0;
    int rem_bytes;
    uint8_t *data;
    uint8_t iv[16];

    ret = av_aes_init(crypto_ctx->aes_ctx, crypto_ctx->key, 16 * 8, 1);
    if (ret < 0)
        return ret;

    /* Remove start code emulation prevention 0x03 bytes */
    remove_scep_3_bytes(nalu);

    data = nalu->data + 32;
    rem_bytes = nalu->length - 32;

    memcpy(iv, crypto_ctx->iv, 16);

    while (rem_bytes > 0) {
        if (rem_bytes > 16) {
            av_aes_crypt(crypto_ctx->aes_ctx, data, data, 1, iv, 1);
            data += 16;
            rem_bytes -= 16;
        }
        data += FFMIN(144, rem_bytes);
        rem_bytes -= FFMIN(144, rem_bytes);
    }

    return 0;
}

static int decrypt_video_frame(HLSCryptoContext *crypto_ctx, AVPacket *pkt)
{
    int ret = 0;
    CodecParserContext  ctx;
    NALUnit nalu;
    uint8_t *data_ptr;
    int move_nalu = 0;

    memset(&ctx, 0, sizeof(ctx));
    ctx.buf_ptr  = pkt->data;
    ctx.buf_end = pkt->data + pkt->size;

    data_ptr = pkt->data;

    while (ctx.buf_ptr < ctx.buf_end) {
        memset(&nalu, 0, sizeof(nalu));
        ret = get_next_nal_unit(&ctx, &nalu);
        if (ret < 0)
            return ret;
        if ((nalu.type == 0x01 || nalu.type == 0x05) && nalu.length > 48) {
            int encrypted_nalu_length = nalu.length;
            ret = decrypt_nal_unit(crypto_ctx, &nalu);
            if (ret < 0)
                return ret;
            move_nalu = nalu.length != encrypted_nalu_length;
        }
        if (move_nalu)
            memmove(data_ptr, nalu.data - nalu.start_code_length, nalu.start_code_length + nalu.length);
        data_ptr += nalu.start_code_length + nalu.length;
    }

    av_shrink_packet(pkt, data_ptr - pkt->data);

    return 0;
}

static int get_next_adts_frame(CodecParserContext *ctx, AudioFrame *frame)
{
    int ret = 0;

    AACADTSHeaderInfo *adts_hdr = NULL;

    /* Find next sync word 0xFFF */
    while (ctx->buf_ptr < ctx->buf_end - 1) {
        if (*ctx->buf_ptr == 0xFF && (*(ctx->buf_ptr + 1) & 0xF0) == 0xF0)
            break;
        ctx->buf_ptr++;
    }

    if (ctx->buf_ptr >= ctx->buf_end - 1)
        return -1;

    frame->data = (uint8_t*)ctx->buf_ptr;

    ret = avpriv_adts_header_parse (&adts_hdr, frame->data, ctx->buf_end - frame->data);
    if (ret < 0)
        return ret;

    frame->header_length = adts_hdr->crc_absent ? AV_AAC_ADTS_HEADER_SIZE : AV_AAC_ADTS_HEADER_SIZE + 2;
    frame->length = adts_hdr->frame_length;

    av_free(adts_hdr);

    return 0;
}

static int get_next_ac3_eac3_sync_frame(CodecParserContext *ctx, AudioFrame *frame)
{
    int ret = 0;

    AC3HeaderInfo *hdr = NULL;

    /* Find next sync word 0x0B77 */
    while (ctx->buf_ptr < ctx->buf_end - 1) {
        if (*ctx->buf_ptr == 0x0B && *(ctx->buf_ptr + 1) == 0x77)
            break;
        ctx->buf_ptr++;
    }

    if (ctx->buf_ptr >= ctx->buf_end - 1)
        return -1;

    frame->data = (uint8_t*)ctx->buf_ptr;
    frame->header_length = 0;

    ret = avpriv_ac3_parse_header(&hdr, frame->data, ctx->buf_end - frame->data);
    if (ret < 0) {
        if (ret != AVERROR(ENOMEM))
            av_free(hdr);
        return ret;
    }

    frame->length = hdr->frame_size;

    av_free(hdr);

    return 0;
}

static int get_next_sync_frame(enum AVCodecID codec_id, CodecParserContext *ctx, AudioFrame *frame)
{
    if (codec_id == AV_CODEC_ID_AAC)
        return get_next_adts_frame(ctx, frame);
    else if (codec_id == AV_CODEC_ID_AC3 || codec_id == AV_CODEC_ID_EAC3)
        return get_next_ac3_eac3_sync_frame(ctx, frame);
    else
        return AVERROR_INVALIDDATA;
}

static int decrypt_sync_frame(enum AVCodecID codec_id, HLSCryptoContext *crypto_ctx, AudioFrame *frame)
{
    int ret = 0;
    uint8_t *data;
    int num_of_encrypted_blocks;

    ret = av_aes_init(crypto_ctx->aes_ctx, crypto_ctx->key, 16 * 8, 1);
    if (ret < 0)
        return ret;

    data = frame->data + frame->header_length + 16;

    num_of_encrypted_blocks = (frame->length - frame->header_length - 16)/16;

    av_aes_crypt(crypto_ctx->aes_ctx, data, data, num_of_encrypted_blocks, crypto_ctx->iv, 1);

    return 0;
}

static int decrypt_audio_frame(enum AVCodecID codec_id, HLSCryptoContext *crypto_ctx, AVPacket *pkt)
{
    int ret = 0;
    CodecParserContext  ctx;
    AudioFrame frame;

    memset(&ctx, 0, sizeof(ctx));
    ctx.buf_ptr = pkt->data;
    ctx.buf_end = pkt->data + pkt->size;

    while (ctx.buf_ptr < ctx.buf_end) {
        memset(&frame, 0, sizeof(frame));
        ret = get_next_sync_frame(codec_id, &ctx, &frame);
        if (ret < 0)
            return ret;
        if (frame.length - frame.header_length > 31) {
            ret = decrypt_sync_frame(codec_id, crypto_ctx, &frame);
            if (ret < 0)
                return ret;
        }
        ctx.buf_ptr += frame.length;
    }

    return 0;
}

int ff_hls_senc_decrypt_frame(enum AVCodecID codec_id, HLSCryptoContext *crypto_ctx, AVPacket *pkt)
{
    if (codec_id == AV_CODEC_ID_H264)
        return decrypt_video_frame(crypto_ctx, pkt);
    else if (codec_id == AV_CODEC_ID_AAC || codec_id == AV_CODEC_ID_AC3 || codec_id == AV_CODEC_ID_EAC3)
        return decrypt_audio_frame(codec_id, crypto_ctx, pkt);

    return AVERROR_INVALIDDATA;
}
