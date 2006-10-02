/*
 * copyright (c) 2006 Oded Shimon <ods15@ods15.dyndns.org>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file vorbis_enc.c
 * Native Vorbis encoder.
 * @author Oded Shimon <ods15@ods15.dyndns.org>
 */

#include "avcodec.h"

#undef NDEBUG
#include <assert.h>

#define ALT_BITSTREAM_READER_LE
#include "bitstream.h"

#define VORBIS_FRAME_SIZE 64

#define BUFFER_SIZE (1024*64)

typedef struct {
    int len;
    uint32_t codeword;
} entry_t;

typedef struct {
    int nentries;
    entry_t * entries;
    int ndimentions;
    float min;
    float delta;
    int seq_p;
    int lookup;
    //float * dimentions;
    int * quantlist;
} codebook_t;

typedef struct {
} floor_t;

typedef struct {
} residue_t;

typedef struct {
} mapping_t;

typedef struct {
    int channels;
    int sample_rate;
    int blocksize[2];

    int ncodebooks;
    codebook_t * codebooks;

    int nfloors;
    floor_t * floors;

    int nresidues;
    residue_t * residues;

    int nmappings;
    mapping_t * mappings;
} venc_context_t;

static inline int ilog(unsigned int a) {
    int i;
    for (i = 0; a >> i; i++);
    return i;
}

static void put_float(PutBitContext * pb, float f) {
    int exp, mant;
    uint32_t res = 0;
    mant = (int)ldexp(frexp(f, &exp), 20);
    exp += 788 - 20;
    if (mant < 0) { res |= (1 << 31); mant = -mant; }
    res |= mant | (exp << 21);
    put_bits(pb, 32, res);
}

static void put_codebook_header(PutBitContext * pb, codebook_t * cb) {
    int i;
    int ordered = 0;

    put_bits(pb, 24, 0x564342); //magic
    put_bits(pb, 16, cb->ndimentions);
    put_bits(pb, 24, cb->nentries);

    for (i = 1; i < cb->nentries; i++) if (cb->entries[i].len < cb->entries[i-1].len) break;
    if (i == cb->nentries) ordered = 1;

    put_bits(pb, 1, ordered);
    if (ordered) {
        int len = cb->entries[0].len;
        put_bits(pb, 5, len);
        i = 0;
        while (i < cb->nentries) {
            int j;
            for (j = 0; j+i < cb->nentries; j++) if (cb->entries[j+i].len != len) break;
            put_bits(pb, 5, j);
            i += j;
            len++;
        }
    } else {
        int sparse = 0;
        for (i = 0; i < cb->nentries; i++) if (!cb->entries[i].len) break;
        if (i != cb->nentries) sparse = 1;
        put_bits(pb, 1, sparse);

        for (i = 0; i < cb->nentries; i++) {
            if (sparse) put_bits(pb, 1, !!cb->entries[i].len);
            if (cb->entries[i].len) put_bits(pb, 5, cb->entries[i].len);
        }
    }

    put_bits(pb, 4, cb->lookup);
    if (cb->lookup) {
        int tmp, bits = ilog(cb->quantlist[0]);

        if (cb->lookup == 1) {
            for (tmp = 0; ; tmp++) {
                int n = 1;
                for (i = 0; i < cb->ndimentions; i++) n *= tmp;
                if (n > cb->nentries) break;
            }
            tmp--;
        } else tmp = cb->ndimentions * cb->nentries;

        for (i = 1; i < tmp; i++) bits = FFMIN(bits, ilog(cb->quantlist[i]));

        put_float(pb, cb->min);
        put_float(pb, cb->delta);

        put_bits(pb, 4, bits - 1);
        put_bits(pb, 1, cb->seq_p);

        for (i = 0; i < tmp; i++) put_bits(pb, bits, cb->quantlist[i]);
    }
}

static void put_floor_header(PutBitContext * pb, floor_t * fl) {
}

static void put_residue_header(PutBitContext * pb, residue_t * r) {
}

static int put_main_header(venc_context_t * venc, uint8_t ** out) {
    int i;
    PutBitContext pb;
    uint8_t buffer[50000] = {0}, * p = buffer;
    int buffer_len = sizeof buffer;
    int len, hlens[3];

    // identification header
    init_put_bits(&pb, p, buffer_len);
    put_bits(&pb, 8, 1); //magic
    for (i = 0; "vorbis"[i]; i++) put_bits(&pb, 8, "vorbis"[i]);
    put_bits(&pb, 32, 0); // version
    put_bits(&pb, 8, venc->channels);
    put_bits(&pb, 32, venc->sample_rate);
    put_bits(&pb, 32, 0); // bitrate
    put_bits(&pb, 32, 0); // bitrate
    put_bits(&pb, 32, 0); // bitrate
    put_bits(&pb, 4, venc->blocksize[0]);
    put_bits(&pb, 4, venc->blocksize[1]);
    put_bits(&pb, 1, 1); // framing

    flush_put_bits(&pb);
    hlens[0] = (put_bits_count(&pb) + 7) / 8;
    buffer_len -= hlens[0];
    p += hlens[0];

    // comment header
    init_put_bits(&pb, p, buffer_len);
    put_bits(&pb, 8, 3); //magic
    for (i = 0; "vorbis"[i]; i++) put_bits(&pb, 8, "vorbis"[i]);
    put_bits(&pb, 32, 0); // vendor length TODO
    put_bits(&pb, 32, 0); // amount of comments
    put_bits(&pb, 1, 1); // framing

    flush_put_bits(&pb);
    hlens[1] = (put_bits_count(&pb) + 7) / 8;
    buffer_len -= hlens[1];
    p += hlens[1];

    // setup header
    init_put_bits(&pb, p, buffer_len);
    put_bits(&pb, 8, 5); //magic
    for (i = 0; "vorbis"[i]; i++) put_bits(&pb, 8, "vorbis"[i]);

    // codebooks
    put_bits(&pb, 8, venc->ncodebooks - 1);
    for (i = 0; i < venc->ncodebooks; i++) put_codebook_header(&pb, &venc->codebooks[0]);

    // time domain, reserved, zero
    put_bits(&pb, 6, 0);
    put_bits(&pb, 16, 0);

    // floors
    put_bits(&pb, 6, venc->nfloors - 1);
    for (i = 0; i < venc->nfloors; i++) put_floor_header(&pb, &venc->floors[0]);

    // residues
    put_bits(&pb, 6, venc->nresidues - 1);
    for (i = 0; i < venc->nresidues; i++) put_residue_header(&pb, &venc->residues[0]);

    // mappings
    put_bits(&pb, 6, venc->nmappings - 1);
    for (i = 0; i < venc->nmappings; i++) {
    }

    flush_put_bits(&pb);
    hlens[2] = (put_bits_count(&pb) + 7) / 8;

    len = hlens[0] + hlens[1] + hlens[2];
    p = *out = av_mallocz(64 + len + len/255);

    *p++ = 2;
    p += av_xiphlacing(p, hlens[0]);
    p += av_xiphlacing(p, hlens[1]);
    buffer_len = 0;
    for (i = 0; i < 3; i++) {
        memcpy(p, buffer + buffer_len, hlens[i]);
        p += hlens[i];
        buffer_len += hlens[i];
    }

    return p - *out;
}

static int vorbis_encode_init(AVCodecContext * avccontext)
{
    venc_context_t * venc = avccontext->priv_data;

    venc->channels = avccontext->channels;
    venc->sample_rate = avccontext->sample_rate;

    //if (avccontext->flags & CODEC_FLAG_QSCALE) avccontext->global_quality / (float)FF_QP2LAMBDA); else avccontext->bit_rate;
    //if(avccontext->cutoff > 0) cfreq = avccontext->cutoff / 1000.0;

    avccontext->extradata_size = put_main_header(venc, (uint8_t**)&avccontext->extradata);

    avccontext->frame_size = VORBIS_FRAME_SIZE;

    avccontext->coded_frame = avcodec_alloc_frame();
    avccontext->coded_frame->key_frame = 1;

    return 0;
}


static int vorbis_encode_frame(AVCodecContext * avccontext, unsigned char * packets, int buf_size, void *data)
{
#if 0
    venc_context_t * venc = avccontext->priv_data;
    signed short * audio = data;
    int samples = data ? VORBIS_FRAME_SIZE : 0;

    avccontext->coded_frame->pts = av_rescale_q(op2->granulepos, (AVRational){1, avccontext->sample_rate}, avccontext->time_base);
    memcpy(packets, compressed_frame, l);
#endif
    return 0;
}


static int vorbis_encode_close(AVCodecContext * avccontext)
{
    venc_context_t * venc = avccontext->priv_data;

    av_freep(&avccontext->coded_frame);
    av_freep(&avccontext->extradata);

    return 0 ;
}

AVCodec oggvorbis_encoder = {
    "vorbis",
    CODEC_TYPE_AUDIO,
    CODEC_ID_VORBIS,
    sizeof(venc_context_t),
    vorbis_encode_init,
    vorbis_encode_frame,
    vorbis_encode_close,
    .capabilities= CODEC_CAP_DELAY,
};
