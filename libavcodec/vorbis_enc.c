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
    int ncodebooks;
    codebook_t * codebooks;
} venc_context_t;

static inline int ilog(int a) {
    int i;
    for (i = 0; (a >> i) > 0; i++);
    return i;
}

static void put_codebook_header(PutBitContext * pb, codebook_t * cb) {
    int i;
    int ordered = 0;

    put_bits(pb, 24, 0x564342); //magic
    put_bits(pb, 16, cb->ndimentions);
    put_bits(pb, 24, cb->nentries);

    for (i = 1; i < cb->nentries; i++) if (cb->entries[i].len < cb->entries[i-1].len) break;
    if (i == cb->entires) ordered = 1;

    put_bits(pb, 1, ordered);
    if (ordered) {
        int len = cb->entries[0].len;
        put_bits(pb, 5, len);
        i = 0;
        while (i < cb->nentries) {
            for (j = 0; j+i < cb->nentries; j++) if (cb->entries[j+i].len != len) break;
            put_bits(pb, 5, j);
            i += j;
            len++;
        }
    } else {
        int sparse = 0;
        for (i = 0; i < cb->nentries; i++) if (!cb->entries[i].len) break;
        if (i != cb->entires) sparse = 1;
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

        put_float(bc, cb->min);
        put_float(bc, cb->delta);

        put_bits(bc, 4, bits - 1);
        put_bits(bc, 1, seq_p);

        for (i = 0; i < tmp; i++) put_bits(bc, bits, cb->quantlist[i])
    }
}

static void put_main_header(PutBitContext * pb, venc_context_t * venc) {
    int i;
    put_bits(pb, 8, 5); //magic
    for (i = 0; "vorbis"[i]; i++) put_bits(pb, 8, "vorbis"[i]);

    put_bits(pb, 8, venc->ncodebooks - 1);
    for (i = 0; i < venc->ncodebooks; i++) put_codebook_header(pb, venc->codebooks[0]);
}

static int vorbis_encode_init(AVCodecContext * avccontext)
{
    venc_context_t * venc = avccontext->priv_data;
    uint8_t *p;
    unsigned int offset, len;

    avccontext->channels;
    avccontext->sample_rate;
    //if (avccontext->flags & CODEC_FLAG_QSCALE) avccontext->global_quality / (float)FF_QP2LAMBDA); else avccontext->bit_rate;
    //if(avccontext->cutoff > 0) cfreq = avccontext->cutoff / 1000.0;

    len = header.bytes + header_comm.bytes + header_code.bytes;

    p = avccontext->extradata = av_mallocz(64 + len + len/255);
    p[0] = 2;
    offset = 1;
    offset += av_xiphlacing(&p[offset], header.bytes);
    offset += av_xiphlacing(&p[offset], header_comm.bytes);
    memcpy(&p[offset], header.packet, header.bytes);
    offset += header.bytes;
    memcpy(&p[offset], header_comm.packet, header_comm.bytes);
    offset += header_comm.bytes;
    memcpy(&p[offset], header_code.packet, header_code.bytes);
    offset += header_code.bytes;
    avccontext->extradata_size = offset;

    avccontext->frame_size = VORBIS_FRAME_SIZE;

    avccontext->coded_frame = avcodec_alloc_frame();
    avccontext->coded_frame->key_frame = 1;

    return 0;
}


static int vorbis_encode_frame(AVCodecContext * avccontext, unsigned char * packets, int buf_size, void *data)
{
    venc_context_t * venc = avccontext->priv_data;
    signed short * audio = data;
    int l, samples = data ? VORBIS_FRAME_SIZE : 0;



    if (!l) return 0;

    avccontext->coded_frame->pts = av_rescale_q(op2->granulepos, (AVRational){1, avccontext->sample_rate}, avccontext->time_base);
    memcpy(packets, compressed_frame, l);
    return l;
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
