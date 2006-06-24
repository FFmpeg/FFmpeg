/**
 * FLAC audio encoder
 * Copyright (c) 2006  Justin Ruggles <jruggle@earthlink.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avcodec.h"
#include "bitstream.h"
#include "crc.h"
#include "golomb.h"

#define FLAC_MAX_CH  8
#define FLAC_MIN_BLOCKSIZE  16
#define FLAC_MAX_BLOCKSIZE  65535

#define FLAC_SUBFRAME_CONSTANT  0
#define FLAC_SUBFRAME_VERBATIM  1
#define FLAC_SUBFRAME_FIXED     8
#define FLAC_SUBFRAME_LPC      32

#define FLAC_CHMODE_NOT_STEREO      0
#define FLAC_CHMODE_LEFT_RIGHT      1
#define FLAC_CHMODE_LEFT_SIDE       8
#define FLAC_CHMODE_RIGHT_SIDE      9
#define FLAC_CHMODE_MID_SIDE       10

#define FLAC_STREAMINFO_SIZE  34

typedef struct FlacSubframe {
    int type;
    int type_code;
    int obits;
    int order;
    int32_t samples[FLAC_MAX_BLOCKSIZE];
    int32_t residual[FLAC_MAX_BLOCKSIZE];
} FlacSubframe;

typedef struct FlacFrame {
    FlacSubframe subframes[FLAC_MAX_CH];
    int blocksize;
    int bs_code[2];
    uint8_t crc8;
    int ch_mode;
} FlacFrame;

typedef struct FlacEncodeContext {
    PutBitContext pb;
    int channels;
    int ch_code;
    int samplerate;
    int sr_code[2];
    int blocksize;
    int max_framesize;
    uint32_t frame_count;
    FlacFrame frame;
} FlacEncodeContext;

static const int flac_samplerates[16] = {
    0, 0, 0, 0,
    8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000,
    0, 0, 0, 0
};

static const int flac_blocksizes[16] = {
    0,
    192,
    576, 1152, 2304, 4608,
    0, 0,
    256, 512, 1024, 2048, 4096, 8192, 16384, 32768
};

/**
 * Writes streaminfo metadata block to byte array
 */
static void write_streaminfo(FlacEncodeContext *s, uint8_t *header)
{
    PutBitContext pb;

    memset(header, 0, FLAC_STREAMINFO_SIZE);
    init_put_bits(&pb, header, FLAC_STREAMINFO_SIZE);

    /* streaminfo metadata block */
    put_bits(&pb, 16, s->blocksize);
    put_bits(&pb, 16, s->blocksize);
    put_bits(&pb, 24, 0);
    put_bits(&pb, 24, s->max_framesize);
    put_bits(&pb, 20, s->samplerate);
    put_bits(&pb, 3, s->channels-1);
    put_bits(&pb, 5, 15);       /* bits per sample - 1 */
    flush_put_bits(&pb);
    /* total samples = 0 */
    /* MD5 signature = 0 */
}

#define BLOCK_TIME_MS 105

/**
 * Sets blocksize based on samplerate
 * Chooses the closest predefined blocksize >= BLOCK_TIME_MS milliseconds
 */
static int select_blocksize(int samplerate)
{
    int i;
    int target;
    int blocksize;

    assert(samplerate > 0);
    blocksize = flac_blocksizes[1];
    target = (samplerate * BLOCK_TIME_MS) / 1000;
    for(i=0; i<16; i++) {
        if(target >= flac_blocksizes[i] && flac_blocksizes[i] > blocksize) {
            blocksize = flac_blocksizes[i];
        }
    }
    return blocksize;
}

static int flac_encode_init(AVCodecContext *avctx)
{
    int freq = avctx->sample_rate;
    int channels = avctx->channels;
    FlacEncodeContext *s = avctx->priv_data;
    int i;
    uint8_t *streaminfo;

    if(avctx->sample_fmt != SAMPLE_FMT_S16) {
        return -1;
    }

    if(channels < 1 || channels > FLAC_MAX_CH) {
        return -1;
    }
    s->channels = channels;
    s->ch_code = s->channels-1;

    /* find samplerate in table */
    if(freq < 1)
        return -1;
    for(i=4; i<12; i++) {
        if(freq == flac_samplerates[i]) {
            s->samplerate = flac_samplerates[i];
            s->sr_code[0] = i;
            s->sr_code[1] = 0;
            break;
        }
    }
    /* if not in table, samplerate is non-standard */
    if(i == 12) {
        if(freq % 1000 == 0 && freq < 255000) {
            s->sr_code[0] = 12;
            s->sr_code[1] = freq / 1000;
        } else if(freq % 10 == 0 && freq < 655350) {
            s->sr_code[0] = 14;
            s->sr_code[1] = freq / 10;
        } else if(freq < 65535) {
            s->sr_code[0] = 13;
            s->sr_code[1] = freq;
        } else {
            return -1;
        }
        s->samplerate = freq;
    }

    s->blocksize = select_blocksize(s->samplerate);
    avctx->frame_size = s->blocksize;

    s->max_framesize = 14 + (s->blocksize * s->channels * 2);

    streaminfo = av_malloc(FLAC_STREAMINFO_SIZE);
    write_streaminfo(s, streaminfo);
    avctx->extradata = streaminfo;
    avctx->extradata_size = FLAC_STREAMINFO_SIZE;

    s->frame_count = 0;

    avctx->coded_frame = avcodec_alloc_frame();
    avctx->coded_frame->key_frame = 1;

    return 0;
}

static int init_frame(FlacEncodeContext *s)
{
    int i, ch;
    FlacFrame *frame;

    frame = &s->frame;

    for(i=0; i<16; i++) {
        if(s->blocksize == flac_blocksizes[i]) {
            frame->blocksize = flac_blocksizes[i];
            frame->bs_code[0] = i;
            frame->bs_code[1] = 0;
            break;
        }
    }
    if(i == 16) {
        frame->blocksize = s->blocksize;
        if(frame->blocksize <= 256) {
            frame->bs_code[0] = 6;
            frame->bs_code[1] = frame->blocksize-1;
        } else {
            frame->bs_code[0] = 7;
            frame->bs_code[1] = frame->blocksize-1;
        }
    }

    for(ch=0; ch<s->channels; ch++) {
        frame->subframes[ch].obits = 16;
    }
    if(s->channels == 2) {
        frame->ch_mode = FLAC_CHMODE_LEFT_RIGHT;
    } else {
        frame->ch_mode = FLAC_CHMODE_NOT_STEREO;
    }

    return 0;
}

/**
 * Copy channel-interleaved input samples into separate subframes
 */
static void copy_samples(FlacEncodeContext *s, int16_t *samples)
{
    int i, j, ch;
    FlacFrame *frame;

    frame = &s->frame;
    for(i=0,j=0; i<frame->blocksize; i++) {
        for(ch=0; ch<s->channels; ch++,j++) {
            frame->subframes[ch].samples[i] = samples[j];
        }
    }
}

static void encode_residual_verbatim(FlacEncodeContext *s, int ch)
{
    FlacFrame *frame;
    FlacSubframe *sub;
    int32_t *res;
    int32_t *smp;
    int n;

    frame = &s->frame;
    sub = &frame->subframes[ch];
    res = sub->residual;
    smp = sub->samples;
    n = frame->blocksize;

    sub->order = 0;
    sub->type = FLAC_SUBFRAME_VERBATIM;
    sub->type_code = sub->type;

    memcpy(res, smp, n * sizeof(int32_t));
}

static void encode_residual_fixed(int32_t *res, int32_t *smp, int n, int order)
{
    int i;

    for(i=0; i<order; i++) {
        res[i] = smp[i];
    }

    if(order==0){
        for(i=order; i<n; i++)
            res[i]= smp[i];
    }else if(order==1){
        for(i=order; i<n; i++)
            res[i]= smp[i] - smp[i-1];
    }else if(order==2){
        for(i=order; i<n; i++)
            res[i]= smp[i] - 2*smp[i-1] + smp[i-2];
    }else if(order==3){
        for(i=order; i<n; i++)
            res[i]= smp[i] - 3*smp[i-1] + 3*smp[i-2] - smp[i-3];
    }else{
        for(i=order; i<n; i++)
            res[i]= smp[i] - 4*smp[i-1] + 6*smp[i-2] - 4*smp[i-3] + smp[i-4];
    }
}

static void encode_residual(FlacEncodeContext *s, int ch)
{
    FlacFrame *frame;
    FlacSubframe *sub;
    int32_t *res;
    int32_t *smp;
    int n;

    frame = &s->frame;
    sub = &frame->subframes[ch];
    res = sub->residual;
    smp = sub->samples;
    n = frame->blocksize;

    sub->order = 2;
    sub->type = FLAC_SUBFRAME_FIXED;
    sub->type_code = sub->type | sub->order;
    encode_residual_fixed(res, smp, n, sub->order);
}

static void
put_sbits(PutBitContext *pb, int bits, int32_t val)
{
    assert(bits >= 0 && bits <= 31);

    put_bits(pb, bits, val & ((1<<bits)-1));
}

static void
write_utf8(PutBitContext *pb, uint32_t val)
{
    int bytes, shift;

    if(val < 0x80){
        put_bits(pb, 8, val);
        return;
    }

    bytes= (av_log2(val)-1) / 5;
    shift = (bytes - 1) * 6;
    put_bits(pb, 8, (256 - (256>>bytes)) | (val >> shift));
    while(shift >= 6){
        shift -= 6;
        put_bits(pb, 8, 0x80 | ((val >> shift) & 0x3F));
    }
}

static void
output_frame_header(FlacEncodeContext *s)
{
    FlacFrame *frame;
    int crc;

    frame = &s->frame;

    put_bits(&s->pb, 16, 0xFFF8);
    put_bits(&s->pb, 4, frame->bs_code[0]);
    put_bits(&s->pb, 4, s->sr_code[0]);
    if(frame->ch_mode == FLAC_CHMODE_NOT_STEREO) {
        put_bits(&s->pb, 4, s->ch_code);
    } else {
        put_bits(&s->pb, 4, frame->ch_mode);
    }
    put_bits(&s->pb, 3, 4); /* bits-per-sample code */
    put_bits(&s->pb, 1, 0);
    write_utf8(&s->pb, s->frame_count);
    if(frame->bs_code[1] > 0) {
        if(frame->bs_code[1] < 256) {
            put_bits(&s->pb, 8, frame->bs_code[1]);
        } else {
            put_bits(&s->pb, 16, frame->bs_code[1]);
        }
    }
    if(s->sr_code[1] > 0) {
        if(s->sr_code[1] < 256) {
            put_bits(&s->pb, 8, s->sr_code[1]);
        } else {
            put_bits(&s->pb, 16, s->sr_code[1]);
        }
    }
    flush_put_bits(&s->pb);
    crc = av_crc(av_crc07, 0, s->pb.buf, put_bits_count(&s->pb)>>3);
    put_bits(&s->pb, 8, crc);
}

static void output_subframe_verbatim(FlacEncodeContext *s, int ch)
{
    int i;
    FlacFrame *frame;
    FlacSubframe *sub;
    int32_t res;

    frame = &s->frame;
    sub = &frame->subframes[ch];

    for(i=0; i<frame->blocksize; i++) {
        res = sub->residual[i];
        put_sbits(&s->pb, sub->obits, res);
    }
}

static void
output_residual(FlacEncodeContext *ctx, int ch)
{
    int i, j, p;
    int k, porder, psize, res_cnt;
    FlacFrame *frame;
    FlacSubframe *sub;

    frame = &ctx->frame;
    sub = &frame->subframes[ch];

    /* rice-encoded block */
    put_bits(&ctx->pb, 2, 0);

    /* partition order */
    porder = 0;
    psize = frame->blocksize;
    //porder = sub->rc.porder;
    //psize = frame->blocksize >> porder;
    put_bits(&ctx->pb, 4, porder);
    res_cnt = psize - sub->order;

    /* residual */
    j = sub->order;
    for(p=0; p<(1 << porder); p++) {
        //k = sub->rc.params[p];
        k = 9;
        put_bits(&ctx->pb, 4, k);
        if(p == 1) res_cnt = psize;
        for(i=0; i<res_cnt && j<frame->blocksize; i++, j++) {
            set_sr_golomb_flac(&ctx->pb, sub->residual[j], k, INT32_MAX, 0);
        }
    }
}

static void
output_subframe_fixed(FlacEncodeContext *ctx, int ch)
{
    int i;
    FlacFrame *frame;
    FlacSubframe *sub;

    frame = &ctx->frame;
    sub = &frame->subframes[ch];

    /* warm-up samples */
    for(i=0; i<sub->order; i++) {
        put_sbits(&ctx->pb, sub->obits, sub->residual[i]);
    }

    /* residual */
    output_residual(ctx, ch);
}

static void output_subframes(FlacEncodeContext *s)
{
    FlacFrame *frame;
    FlacSubframe *sub;
    int ch;

    frame = &s->frame;

    for(ch=0; ch<s->channels; ch++) {
        sub = &frame->subframes[ch];

        /* subframe header */
        put_bits(&s->pb, 1, 0);
        put_bits(&s->pb, 6, sub->type_code);
        put_bits(&s->pb, 1, 0); /* no wasted bits */

        /* subframe */
        if(sub->type == FLAC_SUBFRAME_VERBATIM) {
            output_subframe_verbatim(s, ch);
        } else {
            output_subframe_fixed(s, ch);
        }
    }
}

static void output_frame_footer(FlacEncodeContext *s)
{
    int crc;
    flush_put_bits(&s->pb);
    crc = bswap_16(av_crc(av_crc8005, 0, s->pb.buf, put_bits_count(&s->pb)>>3));
    put_bits(&s->pb, 16, crc);
    flush_put_bits(&s->pb);
}

static int flac_encode_frame(AVCodecContext *avctx, uint8_t *frame,
                             int buf_size, void *data)
{
    int ch;
    FlacEncodeContext *s;
    int16_t *samples = data;
    int out_bytes;

    s = avctx->priv_data;

    s->blocksize = avctx->frame_size;
    if(init_frame(s)) {
        return 0;
    }

    copy_samples(s, samples);

    for(ch=0; ch<s->channels; ch++) {
        encode_residual(s, ch);
    }
    init_put_bits(&s->pb, frame, buf_size);
    output_frame_header(s);
    output_subframes(s);
    output_frame_footer(s);
    out_bytes = put_bits_count(&s->pb) >> 3;

    if(out_bytes > s->max_framesize || out_bytes >= buf_size) {
        /* frame too large. use verbatim mode */
        for(ch=0; ch<s->channels; ch++) {
            encode_residual_verbatim(s, ch);
        }
        init_put_bits(&s->pb, frame, buf_size);
        output_frame_header(s);
        output_subframes(s);
        output_frame_footer(s);
        out_bytes = put_bits_count(&s->pb) >> 3;

        if(out_bytes > s->max_framesize || out_bytes >= buf_size) {
            /* still too large. must be an error. */
            av_log(avctx, AV_LOG_ERROR, "error encoding frame\n");
            return -1;
        }
    }

    s->frame_count++;
    return out_bytes;
}

static int flac_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);
    return 0;
}

AVCodec flac_encoder = {
    "flac",
    CODEC_TYPE_AUDIO,
    CODEC_ID_FLAC,
    sizeof(FlacEncodeContext),
    flac_encode_init,
    flac_encode_frame,
    flac_encode_close,
    NULL,
    .capabilities = CODEC_CAP_SMALL_LAST_FRAME,
};
