/*
 * Copyright (c) 2015 Ludmila Glinskih
 * Copyright (c) 2001 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * FLAC codec test.
 *
 * @example flac_test.c
 * Encodes raw data to FLAC format and decodes it back to raw. Compares raw-data
 * after that.
 */

#include <libavcodec/avcodec.h>
#include <libavutil/common.h>
#include <libavutil/samplefmt.h>

#define AUDIO_INBUF_SIZE 20480
#define NUMBER_OF_FRAMES 200


/* check that a given sample format is supported by the encoder */
static int check_sample_fmt(AVCodec *codec, enum AVSampleFormat sample_fmt)
{
    const enum AVSampleFormat *p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}

/* just pick the highest supported samplerate */
static int select_sample_rate(AVCodec *codec)
{
    const int *p;
    int best_samplerate = 0;

    if (!codec->supported_samplerates)
        return 44100;

    p = codec->supported_samplerates;
    while (*p) {
        best_samplerate = FFMAX(*p, best_samplerate);
        p++;
    }
    return best_samplerate;
}

/* select layout with the highest channel count */
static int select_channel_layout(AVCodec *codec)
{
    const uint64_t *p;
    uint64_t best_ch_layout = 0;
    int best_nb_channels   = 0;

    if (!codec->channel_layouts)
        return AV_CH_LAYOUT_STEREO;

    p = codec->channel_layouts;
    while (*p) {
        int nb_channels = av_get_channel_layout_nb_channels(*p);

        if (nb_channels > best_nb_channels) {
            best_ch_layout    = *p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return best_ch_layout;
}

/* generate i-th frame of test audio */
static int generate_raw_frame(uint16_t *frame_data, int i, int sample_rate, int channels, int frame_size)
{
    float t, tincr, tincr2;
    int j, k;

    t = 0.0;
    tincr = 2 * M_PI * 440.0 / sample_rate;
    tincr2 = tincr / sample_rate;
    for (j = 0; j < frame_size; j++)
    {
        frame_data[channels * j] = (int)(sin(t) * 10000);
        for (k = 1; k < channels; k++)
            frame_data[channels * j + k] = frame_data[channels * j] * 2;
        t = i * tincr + (i * (i + 1) / 2.0 * tincr2);
    }
    return 0;
}

static int init_encoder(AVCodec **encoder, AVCodecContext **encoder_ctx, AVFrame **frame)
{
    AVCodec *enc;
    AVCodecContext *ctx;
    AVFrame *fr;
    int result;

    enc = avcodec_find_encoder(AV_CODEC_ID_FLAC);
    if (!enc)
    {
        av_log(NULL, AV_LOG_ERROR, "Couldn't find encoder\n");
        return AVERROR_ENCODER_NOT_FOUND;
    }

    ctx = avcodec_alloc_context3(enc);
    if (!ctx)
    {
        av_log(NULL, AV_LOG_ERROR, "Couldn't allocate encoder context\n");
        return AVERROR(ENOMEM);
    }

    ctx->bit_rate = 64000;
    ctx->sample_fmt = AV_SAMPLE_FMT_S16;
    if (!check_sample_fmt(enc, ctx->sample_fmt))
    {
        av_log(NULL, AV_LOG_ERROR, "Sample format doesn't supported by encoder\n");
        return AVERROR_UNKNOWN;
    }
    ctx->sample_rate = select_sample_rate(enc);
    ctx->channel_layout = select_channel_layout(enc);
    ctx->channels = av_get_channel_layout_nb_channels(ctx->channel_layout);

    result = avcodec_open2(ctx, enc, NULL);
    if (result < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't open enc\n");
        return AVERROR_UNKNOWN;
    }

    fr = av_frame_alloc();
    if (!fr)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't alloc frame\n");
        return AVERROR(ENOMEM);
    }

    fr->nb_samples     = ctx->frame_size;
    fr->format         = ctx->sample_fmt;
    fr->channel_layout = ctx->channel_layout;

    *encoder = enc;
    *encoder_ctx = ctx;
    *frame = fr;
    return 0;
}

static int init_decoder(AVCodec **decoder, AVCodecContext **decoder_ctx, AVFrame **frame)
{
    AVCodec *dec;
    AVCodecContext *ctx;
    AVFrame *fr;
    int result;

    dec = avcodec_find_decoder(AV_CODEC_ID_FLAC);
    if (!dec)
    {
        av_log(NULL, AV_LOG_ERROR, "Couldn't find dec\n");
        return AVERROR_DECODER_NOT_FOUND;
    }

    ctx = avcodec_alloc_context3(dec);
    if (!ctx)
    {
        av_log(NULL, AV_LOG_ERROR , "Couldn't allocate dec context\n");
        return AVERROR(ENOMEM);
    }

    ctx->request_sample_fmt = AV_SAMPLE_FMT_S16;

    result = avcodec_open2(ctx, dec, NULL);
    if (result < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't open dec\n");
        return AVERROR_UNKNOWN;
    }

    fr = av_frame_alloc();
    if (!fr)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't alloc output frame\n");
        return AVERROR(ENOMEM);
    }

    /* We don't need to change output frame parameters -- decoder does this */

    *decoder = dec;
    *decoder_ctx = ctx;
    *frame = fr;
    return 0;
}


int main(int argc, char** argv)
{
    AVCodec *encoder = NULL, *decoder = NULL;
    AVCodecContext *encoder_ctx = NULL, *decoder_ctx = NULL;
    AVFrame *in_frame = NULL, *out_frame = NULL;
    AVPacket enc_pkt, dec_pkt;
    uint8_t *frame_data = NULL;
    uint8_t *raw_in = NULL, *raw_out = NULL;
    int in_offset = 0, out_offset = 0;
    uint8_t buffer[AUDIO_INBUF_SIZE + FF_INPUT_BUFFER_PADDING_SIZE];
    int frame_data_size = 0;
    int result = 0;
    int got_output = 0;
    int out_frame_data_size = 0;
    int i = 0;

    avcodec_register_all();

    memset(buffer, 0, AUDIO_INBUF_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);

    if (result = init_encoder(&encoder, &encoder_ctx, &in_frame))
        return result;
    if (result = init_decoder(&decoder, &decoder_ctx, &out_frame))
        return result;

    frame_data_size = av_samples_get_buffer_size(NULL, encoder_ctx->channels,
                                                 encoder_ctx->frame_size,
                                                 encoder_ctx->sample_fmt, 0);

    frame_data = av_malloc(frame_data_size);
    if (!frame_data)
    {
        av_log(NULL, AV_LOG_ERROR, "Couldn't allocate memory for frame_data\n");
        return AVERROR(ENOMEM);
    }

    raw_in = av_malloc(frame_data_size * NUMBER_OF_FRAMES);
    if (!raw_in)
    {
        av_log(NULL, AV_LOG_ERROR, "Couldn't allocate memory for raw_in\n");
        return AVERROR(ENOMEM);
    }

    raw_out = av_malloc(frame_data_size * NUMBER_OF_FRAMES);
    if (!raw_out)
    {
        av_log(NULL, AV_LOG_ERROR, "Couldn't allocate memory for raw_out\n");
        return AVERROR(ENOMEM);
    }

    /* setup the data pointers in the AVFrame */
    result = avcodec_fill_audio_frame(in_frame, encoder_ctx->channels, encoder_ctx->sample_fmt,
                                      (const uint8_t*)frame_data, frame_data_size, 0);
    if (result < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Couldn't setup audio frame\n");
        return AVERROR_UNKNOWN;
    }

    for (i = 0; i < NUMBER_OF_FRAMES; i++)
    {
        av_init_packet(&enc_pkt);

        /* data will be allocated by encoder */
        enc_pkt.data = NULL;
        enc_pkt.size = 0;

        generate_raw_frame((uint16_t*)frame_data, i, encoder_ctx->sample_rate,
                           encoder_ctx->channels, encoder_ctx->frame_size);
        memcpy(raw_in + in_offset, frame_data, frame_data_size);
        in_offset += frame_data_size;

        result = avcodec_encode_audio2(encoder_ctx, &enc_pkt, in_frame, &got_output);
        if (result < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Error encoding audio frame\n");
            return AVERROR_UNKNOWN;
        }

        /* if we get an encoded packet, feed it to the decoder */
        if (got_output)
        {
            /* using extra array here, as some decoders read too big blocks */
            av_init_packet(&dec_pkt);
            memcpy(buffer, enc_pkt.data, enc_pkt.size);
            dec_pkt.data = buffer;
            dec_pkt.size = enc_pkt.size;

            result = avcodec_decode_audio4(decoder_ctx, out_frame, &got_output, &dec_pkt);
            if (result < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Error decoding audio packet\n");
                return AVERROR_UNKNOWN;
            }

            if (got_output)
            {
                out_frame_data_size = av_samples_get_buffer_size(NULL, decoder_ctx->channels,
                                                                 out_frame->nb_samples,
                                                                 decoder_ctx->sample_fmt, 1);
                memcpy(raw_out + out_offset, out_frame->data[0], out_frame_data_size);
                out_offset += out_frame_data_size;
                av_free_packet(&dec_pkt);
            }
        }
        av_free_packet(&enc_pkt);
        av_log(NULL, AV_LOG_INFO, "%i frame(s) encoded-decoded\n", i + 1);
    }

    if (memcmp(raw_in, raw_out, frame_data_size * NUMBER_OF_FRAMES) != 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Frames are not the same\n");
        return 1;
    }
    av_log(NULL, AV_LOG_INFO, "OK\n");

    avcodec_close(encoder_ctx);
    avcodec_close(decoder_ctx);
    av_free(encoder_ctx);
    av_free(decoder_ctx);
    av_frame_free(&in_frame);
    av_frame_free(&out_frame);
    av_free(frame_data);
    av_free(raw_in);
    av_free(raw_out);

    return 0;
}
