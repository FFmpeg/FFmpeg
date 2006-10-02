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

#define BITSTREAM_H // don't include this
typedef int VLC;
typedef int GetBitContext;
#include "vorbis.h"

#undef NDEBUG
#include <assert.h>

//#define ALT_BITSTREAM_WRITER
//#include "bitstream.h"

typedef struct {
    int len;
    uint32_t codeword;
} cb_entry_t;

typedef struct {
    int nentries;
    cb_entry_t * entries;
    int ndimentions;
    float min;
    float delta;
    int seq_p;
    int lookup;
    int * quantlist;
    float * dimentions;
} codebook_t;

typedef struct {
    int dim;
    int subclass;
    int masterbook;
    int * books;
} floor_class_t;

typedef struct {
    int x;
    int low;
    int high;
    int sort;
} floor_entry_t;

typedef struct {
    int partitions;
    int * partition_to_class;
    int nclasses;
    floor_class_t * classes;
    int multiplier;
    int rangebits;
    int values;
    floor_entry_t * list;
} floor_t;

typedef struct {
    int type;
    int begin;
    int end;
    int partition_size;
    int classifications;
    int classbook;
    int (*books)[8];
    float (*maxes)[2];
} residue_t;

typedef struct {
    int submaps;
    int * mux;
    int * floor;
    int * residue;
    int coupling_steps;
    int * magnitude;
    int * angle;
} mapping_t;

typedef struct {
    int blockflag;
    int mapping;
} vorbis_mode_t;

typedef struct {
    int channels;
    int sample_rate;
    int blocksize[2]; // in (1<<n) format
    MDCTContext mdct[2];
    const float * win[2];
    int have_saved;
    float * saved;
    float * samples;
    float * floor; // also used for tmp values for mdct
    float * coeffs; // also used for residue after floor

    int ncodebooks;
    codebook_t * codebooks;

    int nfloors;
    floor_t * floors;

    int nresidues;
    residue_t * residues;

    int nmappings;
    mapping_t * mappings;

    int nmodes;
    vorbis_mode_t * modes;
} venc_context_t;

typedef struct {
    int total;
    int total_pos;
    int pos;
    uint8_t * buf_ptr;
} PutBitContext;

#define ilog(i) av_log2(2*(i))

static inline void init_put_bits(PutBitContext * pb, uint8_t * buf, int buffer_len) {
    pb->total = buffer_len * 8;
    pb->total_pos = 0;
    pb->pos = 0;
    pb->buf_ptr = buf;
}

static void put_bits(PutBitContext * pb, int bits, uint64_t val) {
    if ((pb->total_pos += bits) >= pb->total) return;
    if (!bits) return;
    if (pb->pos) {
        if (pb->pos > bits) {
            *pb->buf_ptr |= val << (8 - pb->pos);
            pb->pos -= bits;
            bits = 0;
        } else {
            *pb->buf_ptr++ |= (val << (8 - pb->pos)) & 0xFF;
            val >>= pb->pos;
            bits -= pb->pos;
            pb->pos = 0;
        }
    }
    for (; bits >= 8; bits -= 8) {
        *pb->buf_ptr++ = val & 0xFF;
        val >>= 8;
    }
    if (bits) {
        *pb->buf_ptr = val;
        pb->pos = 8 - bits;
    }
}

static inline void flush_put_bits(PutBitContext * pb) {
}

static inline int put_bits_count(PutBitContext * pb) {
    return pb->total_pos;
}

static int cb_lookup_vals(int lookup, int dimentions, int entries) {
    if (lookup == 1) {
        int tmp, i;
        for (tmp = 0; ; tmp++) {
                int n = 1;
                for (i = 0; i < dimentions; i++) n *= tmp;
                if (n > entries) break;
        }
        return tmp - 1;
    } else if (lookup == 2) return dimentions * entries;
    return 0;
}

static void ready_codebook(codebook_t * cb) {
    int h[33] = { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
    int i;

    for (i = 0; i < cb->nentries; i++) {
        cb_entry_t * e = &cb->entries[i];
        int j = 0;
        if (!e->len) continue;
        if (h[0]) h[0] = 0;
        else {
            for (j = e->len; j; j--)
                if (h[j]) break;
            assert(j);
        }
        e->codeword = h[j];
        h[j] = 0;
        for (j++; j <= e->len; j++) h[j] = e->codeword | (1 << (j - 1));
    }
    for (i = 0; i < 33; i++) assert(!h[i]);

    if (!cb->lookup) cb->dimentions = NULL;
    else {
        int vals = cb_lookup_vals(cb->lookup, cb->ndimentions, cb->nentries);
        cb->dimentions = av_malloc(sizeof(float) * cb->nentries * cb->ndimentions);
        for (i = 0; i < cb->nentries; i++) {
            float last = 0;
            int j;
            int div = 1;
            for (j = 0; j < cb->ndimentions; j++) {
                int off;
                if (cb->lookup == 1) off = (i / div) % vals; // lookup type 1
                else off = i * cb->ndimentions + j; // lookup type 2

                cb->dimentions[i * cb->ndimentions + j] = last + cb->min + cb->quantlist[off] * cb->delta;
                if (cb->seq_p) last = cb->dimentions[i * cb->ndimentions + j];
                div *= vals;
            }
        }
    }

}

static void ready_floor(floor_t * fc) {
    int i;
    fc->list[0].sort = 0;
    fc->list[1].sort = 1;
    for (i = 2; i < fc->values; i++) {
        int j;
        fc->list[i].low = 0;
        fc->list[i].high = 1;
        fc->list[i].sort = i;
        for (j = 2; j < i; j++) {
            int tmp = fc->list[j].x;
            if (tmp < fc->list[i].x) {
                if (tmp > fc->list[fc->list[i].low].x) fc->list[i].low = j;
            } else {
                if (tmp < fc->list[fc->list[i].high].x) fc->list[i].high = j;
            }
        }
    }
    for (i = 0; i < fc->values - 1; i++) {
        int j;
        for (j = i + 1; j < fc->values; j++) {
            if (fc->list[fc->list[i].sort].x > fc->list[fc->list[j].sort].x) {
                int tmp = fc->list[i].sort;
                fc->list[i].sort = fc->list[j].sort;
                fc->list[j].sort = tmp;
            }
        }
    }
}

static void ready_residue(residue_t * rc, venc_context_t * venc) {
    int i;
    assert(rc->type == 2);
    rc->maxes = av_mallocz(sizeof(float[2]) * rc->classifications);
    for (i = 0; i < rc->classifications; i++) {
        int j;
        codebook_t * cb;
        for (j = 0; j < 8; j++) if (rc->books[i][j] != -1) break;
        if (j == 8) continue; // zero
        cb = &venc->codebooks[rc->books[i][j]];
        assert(cb->ndimentions >= 2);
        assert(cb->lookup);

        for (j = 0; j < cb->nentries; j++) {
            float a;
            if (!cb->entries[j].len) continue;
            a = fabs(cb->dimentions[j * cb->ndimentions]);
            if (a > rc->maxes[i][0]) rc->maxes[i][0] = a;
            a = fabs(cb->dimentions[j * cb->ndimentions + 1]);
            if (a > rc->maxes[i][1]) rc->maxes[i][1] = a;
        }
    }
    // small bias
    for (i = 0; i < rc->classifications; i++) {
        rc->maxes[i][0] += 0.8;
        rc->maxes[i][1] += 0.8;
    }
}

static void create_vorbis_context(venc_context_t * venc, AVCodecContext * avccontext) {
    codebook_t * cb;
    floor_t * fc;
    residue_t * rc;
    mapping_t * mc;
    int i, book;

    venc->channels = avccontext->channels;
    venc->sample_rate = avccontext->sample_rate;
    venc->blocksize[0] = venc->blocksize[1] = 11;

    venc->ncodebooks = 29;
    venc->codebooks = av_malloc(sizeof(codebook_t) * venc->ncodebooks);

    int codebook0[] = { 2, 10, 8, 14, 7, 12, 11, 14, 1, 5, 3, 7, 4, 9, 7, 13, };
    int codebook1[] = { 1, 4, 2, 6, 3, 7, 5, 7, };
    int codebook2[] = { 1, 5, 7, 21, 5, 8, 9, 21, 10, 9, 12, 20, 20, 16, 20, 20, 4, 8, 9, 20, 6, 8, 9, 20, 11, 11, 13, 20, 20, 15, 17, 20, 9, 11, 14, 20, 8, 10, 15, 20, 11, 13, 15, 20, 20, 20, 20, 20, 20, 20, 20, 20, 13, 20, 20, 20, 18, 18, 20, 20, 20, 20, 20, 20, 3, 6, 8, 20, 6, 7, 9, 20, 10, 9, 12, 20, 20, 20, 20, 20, 5, 7, 9, 20, 6, 6, 9, 20, 10, 9, 12, 20, 20, 20, 20, 20, 8, 10, 13, 20, 8, 9, 12, 20, 11, 10, 12, 20, 20, 20, 20, 20, 18, 20, 20, 20, 15, 17, 18, 20, 18, 17, 18, 20, 20, 20, 20, 20, 7, 10, 12, 20, 8, 9, 11, 20, 14, 13, 14, 20, 20, 20, 20, 20, 6, 9, 12, 20, 7, 8, 11, 20, 12, 11, 13, 20, 20, 20, 20, 20, 9, 11, 15, 20, 8, 10, 14, 20, 12, 11, 14, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 11, 16, 18, 20, 15, 15, 17, 20, 20, 17, 20, 20, 20, 20, 20, 20, 9, 14, 16, 20, 12, 12, 15, 20, 17, 15, 18, 20, 20, 20, 20, 20, 16, 19, 18, 20, 15, 16, 20, 20, 17, 17, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, };
    int codebook3[] = { 2, 3, 7, 13, 4, 4, 7, 15, 8, 6, 9, 17, 21, 16, 15, 21, 2, 5, 7, 11, 5, 5, 7, 14, 9, 7, 10, 16, 17, 15, 16, 21, 4, 7, 10, 17, 7, 7, 9, 15, 11, 9, 11, 16, 21, 18, 15, 21, 18, 21, 21, 21, 15, 17, 17, 19, 21, 19, 18, 20, 21, 21, 21, 20, };
    int codebook4[] = { 5, 5, 5, 5, 6, 5, 6, 5, 6, 5, 6, 5, 6, 5, 6, 5, 6, 5, 6, 5, 6, 5, 6, 5, 7, 5, 7, 5, 7, 5, 7, 5, 8, 6, 8, 6, 8, 6, 9, 6, 9, 6, 10, 6, 10, 6, 11, 6, 11, 7, 11, 7, 12, 7, 12, 7, 12, 7, 12, 7, 12, 7, 12, 7, 12, 7, 12, 8, 13, 8, 12, 8, 12, 8, 13, 8, 13, 9, 13, 9, 13, 9, 13, 9, 12, 10, 12, 10, 13, 10, 14, 11, 14, 12, 14, 13, 14, 13, 14, 14, 15, 16, 15, 15, 15, 14, 15, 17, 21, 22, 22, 21, 22, 22, 22, 22, 22, 22, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, };
    int codebook5[] = { 2, 5, 5, 4, 5, 4, 5, 4, 5, 4, 6, 5, 6, 5, 6, 5, 6, 5, 7, 5, 7, 6, 8, 6, 8, 6, 8, 6, 9, 6, 9, 6, };
    int codebook6[] = { 8, 5, 8, 4, 9, 4, 9, 4, 9, 4, 9, 4, 9, 4, 9, 4, 9, 4, 9, 4, 9, 4, 8, 4, 8, 4, 9, 5, 9, 5, 9, 5, 9, 5, 9, 6, 10, 6, 10, 7, 10, 8, 11, 9, 11, 11, 12, 13, 12, 14, 13, 15, 13, 15, 14, 16, 14, 17, 15, 17, 15, 15, 16, 16, 15, 16, 16, 16, 15, 18, 16, 15, 17, 17, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, };
    int codebook7[] = { 1, 5, 5, 5, 5, 5, 5, 5, 6, 5, 6, 5, 6, 5, 6, 5, 6, 6, 7, 7, 7, 7, 8, 7, 8, 8, 9, 8, 10, 9, 10, 9, };
    int codebook8[] = { 4, 3, 4, 3, 4, 4, 5, 4, 5, 4, 5, 5, 6, 5, 6, 5, 7, 5, 7, 6, 7, 6, 8, 7, 8, 7, 8, 7, 9, 8, 9, 9, 9, 9, 10, 10, 10, 11, 9, 12, 9, 12, 9, 15, 10, 14, 9, 13, 10, 13, 10, 12, 10, 12, 10, 13, 10, 12, 11, 13, 11, 14, 12, 13, 13, 14, 14, 13, 14, 15, 14, 16, 13, 13, 14, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 15, 15, };
    int codebook9[] = { 4, 5, 4, 5, 3, 5, 3, 5, 3, 5, 4, 4, 4, 4, 5, 5, 5, };
    int codebook10[] = { 3, 3, 4, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 5, 7, 5, 8, 6, 8, 6, 9, 7, 10, 7, 10, 8, 10, 8, 11, 9, 11, };
    int codebook11[] = { 3, 7, 3, 8, 3, 10, 3, 8, 3, 9, 3, 8, 4, 9, 4, 9, 5, 9, 6, 10, 6, 9, 7, 11, 7, 12, 9, 13, 10, 13, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, };
    int codebook12[] = { 4, 5, 4, 5, 4, 5, 4, 5, 3, 5, 3, 5, 3, 5, 4, 5, 4, };
    int codebook13[] = { 4, 2, 4, 2, 5, 3, 5, 4, 6, 6, 6, 7, 7, 8, 7, 8, 7, 8, 7, 9, 8, 9, 8, 9, 8, 10, 8, 11, 9, 12, 9, 12, };
    int codebook14[] = { 2, 5, 2, 6, 3, 6, 4, 7, 4, 7, 5, 9, 5, 11, 6, 11, 6, 11, 7, 11, 6, 11, 6, 11, 9, 11, 8, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, };
    int codebook15[] = { 5, 6, 11, 11, 11, 11, 10, 10, 12, 11, 5, 2, 11, 5, 6, 6, 7, 9, 11, 13, 13, 10, 7, 11, 6, 7, 8, 9, 10, 12, 11, 5, 11, 6, 8, 7, 9, 11, 14, 15, 11, 6, 6, 8, 4, 5, 7, 8, 10, 13, 10, 5, 7, 7, 5, 5, 6, 8, 10, 11, 10, 7, 7, 8, 6, 5, 5, 7, 9, 9, 11, 8, 8, 11, 8, 7, 6, 6, 7, 9, 12, 11, 10, 13, 9, 9, 7, 7, 7, 9, 11, 13, 12, 15, 12, 11, 9, 8, 8, 8, };
    int codebook16[] = { 2, 4, 4, 0, 0, 0, 0, 0, 0, 5, 6, 6, 0, 0, 0, 0, 0, 0, 5, 6, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 7, 7, 0, 0, 0, 0, 0, 0, 7, 8, 8, 0, 0, 0, 0, 0, 0, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 7, 7, 0, 0, 0, 0, 0, 0, 6, 8, 7, 0, 0, 0, 0, 0, 0, 7, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 7, 7, 0, 0, 0, 0, 0, 0, 7, 8, 8, 0, 0, 0, 0, 0, 0, 7, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 8, 8, 0, 0, 0, 0, 0, 0, 8, 8, 9, 0, 0, 0, 0, 0, 0, 8, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 8, 8, 0, 0, 0, 0, 0, 0, 7, 9, 8, 0, 0, 0, 0, 0, 0, 8, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 7, 7, 0, 0, 0, 0, 0, 0, 7, 8, 8, 0, 0, 0, 0, 0, 0, 7, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 8, 8, 0, 0, 0, 0, 0, 0, 8, 9, 9, 0, 0, 0, 0, 0, 0, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 8, 8, 0, 0, 0, 0, 0, 0, 8, 9, 9, 0, 0, 0, 0, 0, 0, 8, 9, 8, };
    int codebook17[] = { 2, 5, 5, 0, 0, 0, 5, 5, 0, 0, 0, 5, 5, 0, 0, 0, 7, 8, 0, 0, 0, 0, 0, 0, 0, 5, 6, 6, 0, 0, 0, 7, 7, 0, 0, 0, 7, 7, 0, 0, 0, 10, 10, 0, 0, 0, 0, 0, 0, 0, 5, 6, 6, 0, 0, 0, 7, 7, 0, 0, 0, 7, 7, 0, 0, 0, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 7, 7, 0, 0, 0, 7, 7, 0, 0, 0, 7, 7, 0, 0, 0, 9, 9, 0, 0, 0, 0, 0, 0, 0, 5, 7, 7, 0, 0, 0, 7, 7, 0, 0, 0, 7, 7, 0, 0, 0, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 7, 7, 0, 0, 0, 7, 7, 0, 0, 0, 7, 7, 0, 0, 0, 9, 9, 0, 0, 0, 0, 0, 0, 0, 5, 7, 7, 0, 0, 0, 7, 7, 0, 0, 0, 7, 7, 0, 0, 0, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 10, 10, 0, 0, 0, 9, 9, 0, 0, 0, 9, 9, 0, 0, 0, 10, 10, 0, 0, 0, 0, 0, 0, 0, 8, 10, 10, 0, 0, 0, 9, 9, 0, 0, 0, 9, 9, 0, 0, 0, 10, 10, };
    int codebook18[] = { 2, 4, 3, 6, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 6, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 6, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 6, 6, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 6, 7, 9, 9, };
    int codebook19[] = { 2, 3, 3, 6, 6, 0, 0, 0, 0, 0, 4, 4, 6, 6, 0, 0, 0, 0, 0, 4, 4, 6, 6, 0, 0, 0, 0, 0, 5, 5, 6, 6, 0, 0, 0, 0, 0, 0, 0, 6, 6, 0, 0, 0, 0, 0, 0, 0, 7, 8, 0, 0, 0, 0, 0, 0, 0, 7, 7, 0, 0, 0, 0, 0, 0, 0, 9, 9, };
    int codebook20[] = { 1, 3, 4, 6, 6, 7, 7, 9, 9, 0, 5, 5, 7, 7, 7, 8, 9, 9, 0, 5, 5, 7, 7, 8, 8, 9, 9, 0, 7, 7, 8, 8, 8, 8, 10, 10, 0, 0, 0, 8, 8, 8, 8, 10, 10, 0, 0, 0, 9, 9, 9, 9, 10, 10, 0, 0, 0, 9, 9, 9, 9, 10, 10, 0, 0, 0, 10, 10, 10, 10, 11, 11, 0, 0, 0, 0, 0, 10, 10, 11, 11, };
    int codebook21[] = { 2, 3, 3, 6, 6, 7, 7, 8, 8, 8, 8, 9, 9, 10, 10, 11, 10, 0, 5, 5, 7, 7, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 0, 5, 5, 7, 7, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 0, 6, 6, 7, 7, 8, 8, 9, 9, 9, 9, 10, 10, 11, 11, 11, 11, 0, 0, 0, 7, 7, 8, 8, 9, 9, 9, 9, 10, 10, 11, 11, 11, 12, 0, 0, 0, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 11, 11, 12, 12, 0, 0, 0, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 11, 11, 12, 12, 0, 0, 0, 9, 9, 9, 9, 10, 10, 10, 10, 11, 10, 11, 11, 12, 12, 0, 0, 0, 0, 0, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 0, 0, 0, 0, 0, 9, 8, 9, 9, 10, 10, 11, 11, 12, 12, 12, 12, 0, 0, 0, 0, 0, 8, 8, 9, 9, 10, 10, 11, 11, 12, 11, 12, 12, 0, 0, 0, 0, 0, 9, 10, 10, 10, 11, 11, 11, 11, 12, 12, 13, 13, 0, 0, 0, 0, 0, 0, 0, 10, 10, 10, 10, 11, 11, 12, 12, 13, 13, 0, 0, 0, 0, 0, 0, 0, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 0, 0, 0, 0, 0, 0, 0, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 0, 0, 0, 0, 0, 0, 0, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 12, 12, 12, 13, 13, 13, 13, };
    int codebook22[] = { 1, 4, 4, 7, 6, 6, 7, 6, 6, 4, 7, 7, 10, 9, 9, 11, 9, 9, 4, 7, 7, 10, 9, 9, 11, 9, 9, 7, 10, 10, 11, 11, 10, 12, 11, 11, 6, 9, 9, 11, 10, 10, 11, 10, 10, 6, 9, 9, 11, 10, 10, 11, 10, 10, 7, 11, 11, 11, 11, 11, 12, 11, 11, 6, 9, 9, 11, 10, 10, 11, 10, 10, 6, 9, 9, 11, 10, 10, 11, 10, 10, };
    int codebook23[] = { 2, 4, 4, 6, 6, 7, 7, 7, 7, 8, 8, 10, 5, 5, 6, 6, 7, 7, 8, 8, 8, 8, 10, 5, 5, 6, 6, 7, 7, 8, 8, 8, 8, 10, 6, 6, 7, 7, 8, 8, 8, 8, 8, 8, 10, 10, 10, 7, 7, 8, 7, 8, 8, 8, 8, 10, 10, 10, 8, 8, 8, 8, 8, 8, 8, 8, 10, 10, 10, 7, 8, 8, 8, 8, 8, 8, 8, 10, 10, 10, 8, 8, 8, 8, 8, 8, 8, 8, 10, 10, 10, 10, 10, 8, 8, 8, 8, 8, 8, 10, 10, 10, 10, 10, 9, 9, 8, 8, 9, 8, 10, 10, 10, 10, 10, 8, 8, 8, 8, 8, 8, };
    int codebook24[] = { 1, 4, 4, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 6, 5, 5, 7, 7, 8, 8, 8, 8, 9, 9, 10, 10, 7, 5, 5, 7, 7, 8, 8, 8, 8, 9, 9, 11, 10, 0, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 11, 11, 0, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 11, 11, 0, 12, 12, 9, 9, 10, 10, 10, 10, 11, 11, 11, 12, 0, 13, 13, 9, 9, 10, 10, 10, 10, 11, 11, 12, 12, 0, 0, 0, 10, 10, 10, 10, 11, 11, 12, 12, 12, 12, 0, 0, 0, 10, 10, 10, 10, 11, 11, 12, 12, 12, 12, 0, 0, 0, 14, 14, 11, 11, 11, 11, 12, 12, 13, 13, 0, 0, 0, 14, 14, 11, 11, 11, 11, 12, 12, 13, 13, 0, 0, 0, 0, 0, 12, 12, 12, 12, 13, 13, 14, 13, 0, 0, 0, 0, 0, 13, 13, 12, 12, 13, 12, 14, 13, };
    int codebook25[] = { 2, 4, 4, 5, 5, 6, 5, 5, 5, 5, 6, 4, 5, 5, 5, 6, 5, 5, 5, 5, 6, 6, 6, 5, 5, };
    int codebook26[] = { 1, 4, 4, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 4, 9, 8, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 2, 9, 7, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, };
    int codebook27[] = { 1, 4, 4, 6, 6, 7, 7, 8, 7, 9, 9, 10, 10, 10, 10, 6, 5, 5, 7, 7, 8, 8, 10, 8, 11, 10, 12, 12, 13, 13, 6, 5, 5, 7, 7, 8, 8, 10, 9, 11, 11, 12, 12, 13, 12, 18, 8, 8, 8, 8, 9, 9, 10, 9, 11, 10, 12, 12, 13, 13, 18, 8, 8, 8, 8, 9, 9, 10, 10, 11, 11, 13, 12, 14, 13, 18, 11, 11, 9, 9, 10, 10, 11, 11, 11, 12, 13, 12, 13, 14, 18, 11, 11, 9, 8, 11, 10, 11, 11, 11, 11, 12, 12, 14, 13, 18, 18, 18, 10, 11, 10, 11, 12, 12, 12, 12, 13, 12, 14, 13, 18, 18, 18, 10, 11, 11, 9, 12, 11, 12, 12, 12, 13, 13, 13, 18, 18, 17, 14, 14, 11, 11, 12, 12, 13, 12, 14, 12, 14, 13, 18, 18, 18, 14, 14, 11, 10, 12, 9, 12, 13, 13, 13, 13, 13, 18, 18, 17, 16, 18, 13, 13, 12, 12, 13, 11, 14, 12, 14, 14, 17, 18, 18, 17, 18, 13, 12, 13, 10, 12, 11, 14, 14, 14, 14, 17, 18, 18, 18, 18, 15, 16, 12, 12, 13, 10, 14, 12, 14, 15, 18, 18, 18, 16, 17, 16, 14, 12, 11, 13, 10, 13, 13, 14, 15, };
    int codebook28[] = { 2, 5, 5, 6, 6, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 10, 6, 6, 7, 7, 8, 7, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 10, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 10, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 11, 11, 11, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 9, 10, 10, 10, 11, 11, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 11, 10, 11, 11, 11, 9, 9, 9, 9, 9, 9, 10, 10, 9, 9, 10, 9, 11, 10, 11, 11, 11, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 9, 11, 11, 11, 11, 11, 9, 9, 9, 9, 10, 10, 9, 9, 9, 9, 10, 9, 11, 11, 11, 11, 11, 11, 11, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 10, 9, 10, 10, 9, 10, 9, 9, 10, 9, 11, 10, 10, 11, 11, 11, 11, 9, 10, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 10, 10, 10, 9, 9, 10, 9, 10, 9, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 9, 9, 9, 9, 9, 10, 10, 10, };

    int codebook_sizes[] = { 16, 8, 256, 64, 128, 32, 96, 32, 96, 17, 32, 78, 17, 32, 78, 100, 1641, 443, 105, 68, 81, 289, 81, 121, 169, 25, 169, 225, 289, };
    int * codebook_lens[] = { codebook0,  codebook1,  codebook2,  codebook3,  codebook4,  codebook5,  codebook6,  codebook7,
                              codebook8,  codebook9,  codebook10, codebook11, codebook12, codebook13, codebook14, codebook15,
                              codebook16, codebook17, codebook18, codebook19, codebook20, codebook21, codebook22, codebook23,
                              codebook24, codebook25, codebook26, codebook27, codebook28, };

    struct {
        int lookup;
        int dim;
        float min;
        float delta;
        int real_len;
        int * quant;
    } cvectors[] = {
        { 1, 8,    -1.0,   1.0, 6561,(int[]){ 1, 0, 2, } },
        { 1, 4,    -2.0,   1.0, 625, (int[]){ 2, 1, 3, 0, 4, } },
        { 1, 4,    -2.0,   1.0, 625, (int[]){ 2, 1, 3, 0, 4, } },
        { 1, 2,    -4.0,   1.0, 81,  (int[]){ 4, 3, 5, 2, 6, 1, 7, 0, 8, } },
        { 1, 2,    -4.0,   1.0, 81,  (int[]){ 4, 3, 5, 2, 6, 1, 7, 0, 8, } },
        { 1, 2,    -8.0,   1.0, 289, (int[]){ 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15, 0, 16, } },
        { 1, 4,   -11.0,  11.0, 81,  (int[]){ 1, 0, 2, } },
        { 1, 2,    -5.0,   1.0, 121, (int[]){ 5, 4, 6, 3, 7, 2, 8, 1, 9, 0, 10, } },
        { 1, 2,   -30.0,   5.0, 169, (int[]){ 6, 5, 7, 4, 8, 3, 9, 2, 10, 1, 11, 0, 12, } },
        { 1, 2,    -2.0,   1.0, 25,  (int[]){ 2, 1, 3, 0, 4, } },
        { 1, 2, -1530.0, 255.0, 169, (int[]){ 6, 5, 7, 4, 8, 3, 9, 2, 10, 1, 11, 0, 12, } },
        { 1, 2,  -119.0,  17.0, 225, (int[]){ 7, 6, 8, 5, 9, 4, 10, 3, 11, 2, 12, 1, 13, 0, 14, } },
        { 1, 2,    -8.0,   1.0, 289, (int[]){ 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15, 0, 16, } },
    };

    // codebook 0..14 - floor1 book, values 0..255
    // codebook 15 residue masterbook
    // codebook 16..29 residue
    for (book = 0; book < venc->ncodebooks; book++) {
        cb = &venc->codebooks[book];
        cb->nentries = codebook_sizes[book];
        if (book < 16) {
            cb->ndimentions = 2;
            cb->min = 0.;
            cb->delta = 0.;
            cb->seq_p = 0;
            cb->lookup = 0;
            cb->quantlist = NULL;
        } else {
            int vals;
            cb->seq_p = 0;
            cb->nentries = cvectors[book - 16].real_len;
            cb->ndimentions = cvectors[book - 16].dim;
            cb->min = cvectors[book - 16].min;
            cb->delta = cvectors[book - 16].delta;
            cb->lookup = cvectors[book - 16].lookup;
            vals = cb_lookup_vals(cb->lookup, cb->ndimentions, cb->nentries);
            cb->quantlist = av_malloc(sizeof(int) * vals);
            for (i = 0; i < vals; i++) cb->quantlist[i] = cvectors[book - 16].quant[i];
        }
        cb->entries = av_malloc(sizeof(cb_entry_t) * cb->nentries);
        for (i = 0; i < cb->nentries; i++) {
            if (i < codebook_sizes[book]) cb->entries[i].len = codebook_lens[book][i];
            else cb->entries[i].len = 0;
        }
        ready_codebook(cb);
    }

    venc->nfloors = 1;
    venc->floors = av_malloc(sizeof(floor_t) * venc->nfloors);

    // just 1 floor
    fc = &venc->floors[0];
    fc->partitions = 8;
    fc->partition_to_class = av_malloc(sizeof(int) * fc->partitions);
    fc->nclasses = 0;
    for (i = 0; i < fc->partitions; i++) {
        int a[] = {0,1,2,2,3,3,4,4};
        fc->partition_to_class[i] = a[i];
        fc->nclasses = FFMAX(fc->nclasses, fc->partition_to_class[i]);
    }
    fc->nclasses++;
    fc->classes = av_malloc(sizeof(floor_class_t) * fc->nclasses);
    for (i = 0; i < fc->nclasses; i++) {
        floor_class_t * c = &fc->classes[i];
        int j, books;
        int dim[] = {3,4,3,4,3};
        int subclass[] = {0,1,1,2,2};
        int masterbook[] = {0/*none*/,0,1,2,3};
        int * nbooks[] = {
            (int[]){ 4 },
            (int[]){ 5, 6 },
            (int[]){ 7, 8 },
            (int[]){ -1, 9, 10, 11 },
            (int[]){ -1, 12, 13, 14 },
        };
        c->dim = dim[i];
        c->subclass = subclass[i];
        c->masterbook = masterbook[i];
        books = (1 << c->subclass);
        c->books = av_malloc(sizeof(int) * books);
        for (j = 0; j < books; j++) c->books[j] = nbooks[i][j];
    }
    fc->multiplier = 2;
    fc->rangebits = venc->blocksize[0] - 1;

    fc->values = 2;
    for (i = 0; i < fc->partitions; i++)
        fc->values += fc->classes[fc->partition_to_class[i]].dim;

    fc->list = av_malloc(sizeof(floor_entry_t) * fc->values);
    fc->list[0].x = 0;
    fc->list[1].x = 1 << fc->rangebits;
    for (i = 2; i < fc->values; i++) {
        /*int a = i - 1;
        int g = ilog(a);
        assert(g <= fc->rangebits);
        a ^= 1 << (g-1);
        g = 1 << (fc->rangebits - g);
        fc->list[i].x = g + a*2*g;*/
        //int a[] = {14, 4, 58, 2, 8, 28, 90};
        int a[] = {93,23,372,6,46,186,750,14,33,65,130,260,556,3,10,18,28,39,55,79,111,158,220,312,464,650,850};
        fc->list[i].x = a[i - 2];
    }
    ready_floor(fc);

    venc->nresidues = 1;
    venc->residues = av_malloc(sizeof(residue_t) * venc->nresidues);

    // single residue
    rc = &venc->residues[0];
    rc->type = 2;
    rc->begin = 0;
    rc->end = 1600;
    rc->partition_size = 32;
    rc->classifications = 10;
    rc->classbook = 15;
    rc->books = av_malloc(sizeof(int[8]) * rc->classifications);
    for (i = 0; i < rc->classifications; i++) {
        int a[][10] = {
            { -1, -1, -1, -1, -1, -1, -1, -1, },
            { -1, -1, 16, -1, -1, -1, -1, -1, },
            { -1, -1, 17, -1, -1, -1, -1, -1, },
            { -1, -1, 18, -1, -1, -1, -1, -1, },
            { -1, -1, 19, -1, -1, -1, -1, -1, },
            { -1, -1, 20, -1, -1, -1, -1, -1, },
            { -1, -1, 21, -1, -1, -1, -1, -1, },
            { 22, 23, -1, -1, -1, -1, -1, -1, },
            { 24, 25, -1, -1, -1, -1, -1, -1, },
            { 26, 27, 28, -1, -1, -1, -1, -1, },
        };
        int j;
        for (j = 0; j < 8; j++) rc->books[i][j] = a[i][j];
    }
    ready_residue(rc, venc);

    venc->nmappings = 1;
    venc->mappings = av_malloc(sizeof(mapping_t) * venc->nmappings);

    // single mapping
    mc = &venc->mappings[0];
    mc->submaps = 1;
    mc->mux = av_malloc(sizeof(int) * venc->channels);
    for (i = 0; i < venc->channels; i++) mc->mux[i] = 0;
    mc->floor = av_malloc(sizeof(int) * mc->submaps);
    mc->residue = av_malloc(sizeof(int) * mc->submaps);
    for (i = 0; i < mc->submaps; i++) {
        mc->floor[i] = 0;
        mc->residue[i] = 0;
    }
    mc->coupling_steps = venc->channels == 2 ? 1 : 0;
    mc->magnitude = av_malloc(sizeof(int) * mc->coupling_steps);
    mc->angle = av_malloc(sizeof(int) * mc->coupling_steps);
    if (mc->coupling_steps) {
        mc->magnitude[0] = 0;
        mc->angle[0] = 1;
    }

    venc->nmodes = 1;
    venc->modes = av_malloc(sizeof(vorbis_mode_t) * venc->nmodes);

    // single mode
    venc->modes[0].blockflag = 0;
    venc->modes[0].mapping = 0;

    venc->have_saved = 0;
    venc->saved = av_malloc(sizeof(float) * venc->channels * (1 << venc->blocksize[1]) / 2);
    venc->samples = av_malloc(sizeof(float) * venc->channels * (1 << venc->blocksize[1]));
    venc->floor = av_malloc(sizeof(float) * venc->channels * (1 << venc->blocksize[1]) / 2);
    venc->coeffs = av_malloc(sizeof(float) * venc->channels * (1 << venc->blocksize[1]) / 2);

    {
        const float *vwin[8]={ vwin64, vwin128, vwin256, vwin512, vwin1024, vwin2048, vwin4096, vwin8192 };
        venc->win[0] = vwin[venc->blocksize[0] - 6];
        venc->win[1] = vwin[venc->blocksize[1] - 6];
    }

    ff_mdct_init(&venc->mdct[0], venc->blocksize[0], 0);
    ff_mdct_init(&venc->mdct[1], venc->blocksize[1], 0);
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
        put_bits(pb, 5, len - 1);
        i = 0;
        while (i < cb->nentries) {
            int j;
            for (j = 0; j+i < cb->nentries; j++) if (cb->entries[j+i].len != len) break;
            put_bits(pb, ilog(cb->nentries - i), j);
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
            if (cb->entries[i].len) put_bits(pb, 5, cb->entries[i].len - 1);
        }
    }

    put_bits(pb, 4, cb->lookup);
    if (cb->lookup) {
        int tmp = cb_lookup_vals(cb->lookup, cb->ndimentions, cb->nentries);
        int bits = ilog(cb->quantlist[0]);

        for (i = 1; i < tmp; i++) bits = FFMAX(bits, ilog(cb->quantlist[i]));

        put_float(pb, cb->min);
        put_float(pb, cb->delta);

        put_bits(pb, 4, bits - 1);
        put_bits(pb, 1, cb->seq_p);

        for (i = 0; i < tmp; i++) put_bits(pb, bits, cb->quantlist[i]);
    }
}

static void put_floor_header(PutBitContext * pb, floor_t * fc) {
    int i;

    put_bits(pb, 16, 1); // type, only floor1 is supported

    put_bits(pb, 5, fc->partitions);

    for (i = 0; i < fc->partitions; i++) put_bits(pb, 4, fc->partition_to_class[i]);

    for (i = 0; i < fc->nclasses; i++) {
        int j, books;

        put_bits(pb, 3, fc->classes[i].dim - 1);
        put_bits(pb, 2, fc->classes[i].subclass);

        if (fc->classes[i].subclass) put_bits(pb, 8, fc->classes[i].masterbook);

        books = (1 << fc->classes[i].subclass);

        for (j = 0; j < books; j++) put_bits(pb, 8, fc->classes[i].books[j] + 1);
    }

    put_bits(pb, 2, fc->multiplier - 1);
    put_bits(pb, 4, fc->rangebits);

    for (i = 2; i < fc->values; i++) put_bits(pb, fc->rangebits, fc->list[i].x);
}

static void put_residue_header(PutBitContext * pb, residue_t * rc) {
    int i;

    put_bits(pb, 16, rc->type);

    put_bits(pb, 24, rc->begin);
    put_bits(pb, 24, rc->end);
    put_bits(pb, 24, rc->partition_size - 1);
    put_bits(pb, 6, rc->classifications - 1);
    put_bits(pb, 8, rc->classbook);

    for (i = 0; i < rc->classifications; i++) {
        int j, tmp = 0;
        for (j = 0; j < 8; j++) tmp |= (rc->books[i][j] != -1) << j;

        put_bits(pb, 3, tmp & 7);
        put_bits(pb, 1, tmp > 7);

        if (tmp > 7) put_bits(pb, 5, tmp >> 3);
    }

    for (i = 0; i < rc->classifications; i++) {
        int j;
        for (j = 0; j < 8; j++)
            if (rc->books[i][j] != -1)
                put_bits(pb, 8, rc->books[i][j]);
    }
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
    for (i = 0; i < venc->ncodebooks; i++) put_codebook_header(&pb, &venc->codebooks[i]);

    // time domain, reserved, zero
    put_bits(&pb, 6, 0);
    put_bits(&pb, 16, 0);

    // floors
    put_bits(&pb, 6, venc->nfloors - 1);
    for (i = 0; i < venc->nfloors; i++) put_floor_header(&pb, &venc->floors[i]);

    // residues
    put_bits(&pb, 6, venc->nresidues - 1);
    for (i = 0; i < venc->nresidues; i++) put_residue_header(&pb, &venc->residues[i]);

    // mappings
    put_bits(&pb, 6, venc->nmappings - 1);
    for (i = 0; i < venc->nmappings; i++) {
        mapping_t * mc = &venc->mappings[i];
        int j;
        put_bits(&pb, 16, 0); // mapping type

        put_bits(&pb, 1, mc->submaps > 1);
        if (mc->submaps > 1) put_bits(&pb, 4, mc->submaps - 1);

        put_bits(&pb, 1, !!mc->coupling_steps);
        if (mc->coupling_steps) {
            put_bits(&pb, 8, mc->coupling_steps - 1);
            for (j = 0; j < mc->coupling_steps; j++) {
                put_bits(&pb, ilog(venc->channels - 1), mc->magnitude[j]);
                put_bits(&pb, ilog(venc->channels - 1), mc->angle[j]);
            }
        }

        put_bits(&pb, 2, 0); // reserved

        if (mc->submaps > 1) for (j = 0; j < venc->channels; j++) put_bits(&pb, 4, mc->mux[j]);

        for (j = 0; j < mc->submaps; j++) {
            put_bits(&pb, 8, 0); // reserved time configuration
            put_bits(&pb, 8, mc->floor[j]);
            put_bits(&pb, 8, mc->residue[j]);
        }
    }

    // modes
    put_bits(&pb, 6, venc->nmodes - 1);
    for (i = 0; i < venc->nmodes; i++) {
        put_bits(&pb, 1, venc->modes[i].blockflag);
        put_bits(&pb, 16, 0); // reserved window type
        put_bits(&pb, 16, 0); // reserved transform type
        put_bits(&pb, 8, venc->modes[i].mapping);
    }

    put_bits(&pb, 1, 1); // framing

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

static void floor_fit(venc_context_t * venc, floor_t * fc, float * coeffs, int * posts, int samples) {
    int range = 255 / fc->multiplier + 1;
    int i;
    for (i = 0; i < fc->values; i++) {
        int position = fc->list[fc->list[i].sort].x;
        int begin = fc->list[fc->list[FFMAX(i-1, 0)].sort].x;
        int end   = fc->list[fc->list[FFMIN(i+1, fc->values - 1)].sort].x;
        int j;
        float average = 0;
        begin = (position + begin) / 2;
        end   = (position + end  ) / 2;

        assert(end <= samples);
        for (j = begin; j < end; j++) average += fabs(coeffs[j]);
        average /= end - begin;
        average /= 32; // MAGIC!
        for (j = 0; j < range - 1; j++) if (floor1_inverse_db_table[j * fc->multiplier] > average) break;
        posts[fc->list[i].sort] = j;
    }
}

static int render_point(int x0, int y0, int x1, int y1, int x) {
    return y0 +  (x - x0) * (y1 - y0) / (x1 - x0);
}

static void render_line(int x0, int y0, int x1, int y1, float * buf, int n) {
    int dy = y1 - y0;
    int adx = x1 - x0;
    int ady = FFMAX(dy, -dy);
    int base = dy / adx;
    int x = x0;
    int y = y0;
    int err = 0;
    int sy;
    if (dy < 0) sy = base - 1;
    else sy = base + 1;
    ady = ady - FFMAX(base, -base) * adx;
    if (x >= n) return;
    buf[x] = floor1_inverse_db_table[y];
    for (x = x0 + 1; x < x1; x++) {
        if (x >= n) return;
        err += ady;
        if (err >= adx) {
            err -= adx;
            y += sy;
        } else {
            y += base;
        }
        buf[x] = floor1_inverse_db_table[y];
    }
}

static void floor_encode(venc_context_t * venc, floor_t * fc, PutBitContext * pb, int * posts, float * floor, int samples) {
    int range = 255 / fc->multiplier + 1;
    int coded[fc->values]; // first 2 values are unused
    int i, counter;
    int lx, ly;

    put_bits(pb, 1, 1); // non zero
    put_bits(pb, ilog(range - 1), posts[0]);
    put_bits(pb, ilog(range - 1), posts[1]);

    for (i = 2; i < fc->values; i++) {
        int predicted = render_point(fc->list[fc->list[i].low].x,
                                     posts[fc->list[i].low],
                                     fc->list[fc->list[i].high].x,
                                     posts[fc->list[i].high],
                                     fc->list[i].x);
        int highroom = range - predicted;
        int lowroom = predicted;
        int room = FFMIN(highroom, lowroom);
        if (predicted == posts[i]) {
            coded[i] = 0; // must be used later as flag!
            continue;
        } else {
            if (!coded[fc->list[i].low]) coded[fc->list[i].low] = -1;
            if (!coded[fc->list[i].high]) coded[fc->list[i].high] = -1;
        }
        if (posts[i] > predicted) {
            if (posts[i] - predicted > room) coded[i] = posts[i] - predicted + lowroom;
            else coded[i] = (posts[i] - predicted) << 1;
        } else {
            if (predicted - posts[i] > room) coded[i] = predicted - posts[i] + highroom - 1;
            else coded[i] = ((predicted - posts[i]) << 1) - 1;
        }
    }

    counter = 2;
    for (i = 0; i < fc->partitions; i++) {
        floor_class_t * c = &fc->classes[fc->partition_to_class[i]];
        int k, cval = 0, csub = 1<<c->subclass;
        if (c->subclass) {
            codebook_t * book = &venc->codebooks[c->masterbook];
            int cshift = 0;
            for (k = 0; k < c->dim; k++) {
                int l;
                for (l = 0; l < csub; l++) {
                    int maxval = 1;
                    if (c->books[l] != -1) maxval = venc->codebooks[c->books[l]].nentries;
                    // coded could be -1, but this still works, cause thats 0
                    if (coded[counter + k] < maxval) break;
                }
                assert(l != csub);
                cval |= l << cshift;
                cshift += c->subclass;
            }
            assert(cval < book->nentries);
            put_bits(pb, book->entries[cval].len, book->entries[cval].codeword);
        }
        for (k = 0; k < c->dim; k++) {
            int book = c->books[cval & (csub-1)];
            int entry = coded[counter++];
            cval >>= c->subclass;
            if (book == -1) continue;
            if (entry == -1) entry = 0;
            assert(entry < venc->codebooks[book].nentries);
            assert(entry >= 0);
            put_bits(pb, venc->codebooks[book].entries[entry].len, venc->codebooks[book].entries[entry].codeword);
        }
    }

    lx = 0;
    ly = posts[0] * fc->multiplier; // sorted 0 is still 0
    coded[0] = coded[1] = 1;
    for (i = 1; i < fc->values; i++) {
        int pos = fc->list[i].sort;
        if (coded[pos]) {
            render_line(lx, ly, fc->list[pos].x, posts[pos] * fc->multiplier, floor, samples);
            lx = fc->list[pos].x;
            ly = posts[pos] * fc->multiplier;
        }
        if (lx >= samples) break;
    }
    if (lx < samples) render_line(lx, ly, samples, ly, floor, samples);
}

static float * put_vector(codebook_t * book, PutBitContext * pb, float * num) {
    int i;
    int entry = -1;
    float distance = 0;
    assert(book->dimentions);
    for (i = 0; i < book->nentries; i++) {
        float d = 0.;
        int j;
        for (j = 0; j < book->ndimentions; j++) {
            float a = (book->dimentions[i * book->ndimentions + j] - num[j]);
            d += a*a;
        }
        if (entry == -1 || distance > d) {
            entry = i;
            distance = d;
        }
    }
    put_bits(pb, book->entries[entry].len, book->entries[entry].codeword);
    return &book->dimentions[entry * book->ndimentions];
}

static void residue_encode(venc_context_t * venc, residue_t * rc, PutBitContext * pb, float * coeffs, int samples, int real_ch) {
    int pass, i, j, p, k;
    int psize = rc->partition_size;
    int partitions = (rc->end - rc->begin) / psize;
    int channels = (rc->type == 2) ? 1 : real_ch;
    int classes[channels][partitions];
    int classwords = venc->codebooks[rc->classbook].ndimentions;

    if (rc->type == 2) channels = 1;

    assert(rc->type == 2);
    assert(real_ch == 2);
    for (p = 0; p < partitions; p++) {
        float max1 = 0., max2 = 0.;
        int s = rc->begin + p * psize;
        for (k = s; k < s + psize; k += 2) {
            if (fabs(coeffs[k / real_ch]) > max1) max1 = fabs(coeffs[k / real_ch]);
            if (fabs(coeffs[samples + k / real_ch]) > max2) max2 = fabs(coeffs[samples + k / real_ch]);
        }

        for (i = 0; i < rc->classifications - 1; i++) {
            if (max1 < rc->maxes[i][0] && max2 < rc->maxes[i][1]) break;
        }
        classes[0][p] = i;
    }

    for (pass = 0; pass < 8; pass++) {
        p = 0;
        while (p < partitions) {
            if (pass == 0) for (j = 0; j < channels; j++) {
                codebook_t * book = &venc->codebooks[rc->classbook];
                int entry = 0;
                for (i = 0; i < classwords; i++) {
                    entry *= rc->classifications;
                    entry += classes[j][p + i];
                }
                assert(entry < book->nentries);
                assert(entry >= 0);
                put_bits(pb, book->entries[entry].len, book->entries[entry].codeword);
            }
            for (i = 0; i < classwords && p < partitions; i++, p++) {
                for (j = 0; j < channels; j++) {
                    int nbook = rc->books[classes[j][p]][pass];
                    codebook_t * book = &venc->codebooks[nbook];
                    float * buf = coeffs + samples*j + rc->begin + p*psize;
                    if (nbook == -1) continue;

                    assert(rc->type == 0 || rc->type == 2);
                    assert(!(psize % book->ndimentions));

                    if (rc->type == 0) {
                        for (k = 0; k < psize; k += book->ndimentions) {
                            float * a = put_vector(book, pb, &buf[k]);
                            int l;
                            for (l = 0; l < book->ndimentions; l++) buf[k + l] -= a[l];
                        }
                    } else {
                        for (k = 0; k < psize; k += book->ndimentions) {
                            int dim = book->ndimentions, s = rc->begin + p * psize, l;
                            float vec[dim], * a = vec;
                            for (l = s + k; l < s + k + dim; l++)
                                *a++ = coeffs[(l % real_ch) * samples + l / real_ch];
                            a = put_vector(book, pb, vec);
                            for (l = s + k; l < s + k + dim; l++)
                                coeffs[(l % real_ch) * samples + l / real_ch] -= *a++;
                        }
                    }
                }
            }
        }
    }
}

static int window(venc_context_t * venc, signed short * audio, int samples) {
    int i, j, channel;
    const float * win = venc->win[0];
    int window_len = 1 << (venc->blocksize[0] - 1);
    float n = (float)(1 << venc->blocksize[0]) / 4.;
    // FIXME use dsp

    if (!venc->have_saved && !samples) return 0;

    if (venc->have_saved) {
        for (channel = 0; channel < venc->channels; channel++) {
            memcpy(venc->samples + channel*window_len*2, venc->saved + channel*window_len, sizeof(float)*window_len);
        }
    } else {
        for (channel = 0; channel < venc->channels; channel++) {
            memset(venc->samples + channel*window_len*2, 0, sizeof(float)*window_len);
        }
    }

    if (samples) {
        for (channel = 0; channel < venc->channels; channel++) {
            float * offset = venc->samples + channel*window_len*2 + window_len;
            j = channel;
            for (i = 0; i < samples; i++, j += venc->channels)
                offset[i] = audio[j] / 32768. / n * win[window_len - i - 1];
        }
    } else {
        for (channel = 0; channel < venc->channels; channel++) {
            memset(venc->samples + channel*window_len*2 + window_len, 0, sizeof(float)*window_len);
        }
    }

    for (channel = 0; channel < venc->channels; channel++) {
        ff_mdct_calc(&venc->mdct[0], venc->coeffs + channel*window_len, venc->samples + channel*window_len*2, venc->floor/*tmp*/);
    }

    if (samples) {
        for (channel = 0; channel < venc->channels; channel++) {
            float * offset = venc->saved + channel*window_len;
            j = channel;
            for (i = 0; i < samples; i++, j += venc->channels)
                offset[i] = audio[j] / 32768. / n * win[i];
        }
        venc->have_saved = 1;
    } else {
        venc->have_saved = 0;
    }
    return 1;
}

static int vorbis_encode_init(AVCodecContext * avccontext)
{
    venc_context_t * venc = avccontext->priv_data;

    create_vorbis_context(venc, avccontext);

    //if (avccontext->flags & CODEC_FLAG_QSCALE) avccontext->global_quality / (float)FF_QP2LAMBDA); else avccontext->bit_rate;
    //if(avccontext->cutoff > 0) cfreq = avccontext->cutoff / 1000.0;

    avccontext->extradata_size = put_main_header(venc, (uint8_t**)&avccontext->extradata);

    avccontext->frame_size = 1 << (venc->blocksize[0] - 1);

    avccontext->coded_frame = avcodec_alloc_frame();
    avccontext->coded_frame->key_frame = 1;

    return 0;
}

static int vorbis_encode_frame(AVCodecContext * avccontext, unsigned char * packets, int buf_size, void *data)
{
    venc_context_t * venc = avccontext->priv_data;
    signed short * audio = data;
    int samples = data ? avccontext->frame_size : 0;
    vorbis_mode_t * mode;
    mapping_t * mapping;
    PutBitContext pb;
    int i;

    if (!window(venc, audio, samples)) return 0;
    samples = 1 << (venc->blocksize[0] - 1);

    init_put_bits(&pb, packets, buf_size);

    put_bits(&pb, 1, 0); // magic bit

    put_bits(&pb, ilog(venc->nmodes - 1), 0); // 0 bits, the mode

    mode = &venc->modes[0];
    mapping = &venc->mappings[mode->mapping];
    if (mode->blockflag) {
        put_bits(&pb, 1, 0);
        put_bits(&pb, 1, 0);
    }

    for (i = 0; i < venc->channels; i++) {
        floor_t * fc = &venc->floors[mapping->floor[mapping->mux[i]]];
        int posts[fc->values];
        floor_fit(venc, fc, &venc->coeffs[i * samples], posts, samples);
        floor_encode(venc, fc, &pb, posts, &venc->floor[i * samples], samples);
    }

    for (i = 0; i < venc->channels; i++) {
        int j;
        for (j = 0; j < samples; j++) {
            venc->coeffs[i * samples + j] /= venc->floor[i * samples + j];
        }
    }

    for (i = 0; i < mapping->coupling_steps; i++) {
        float * mag = venc->coeffs + mapping->magnitude[i] * samples;
        float * ang = venc->coeffs + mapping->angle[i] * samples;
        int j;
        for (j = 0; j < samples; j++) {
            float m = mag[j];
            float a = ang[j];
            if (m > 0) {
                ang[j] = m - a;
                if (a > m) mag[j] = a;
                else mag[j] = m;
            } else {
                ang[j] = a - m;
                if (a > m) mag[j] = m;
                else mag[j] = a;
            }
        }
    }

    residue_encode(venc, &venc->residues[mapping->residue[mapping->mux[0]]], &pb, venc->coeffs, samples, venc->channels);

    return (put_bits_count(&pb) + 7) / 8;
}


static int vorbis_encode_close(AVCodecContext * avccontext)
{
    venc_context_t * venc = avccontext->priv_data;
    int i;

    if (venc->codebooks) for (i = 0; i < venc->ncodebooks; i++) {
        av_freep(&venc->codebooks[i].entries);
        av_freep(&venc->codebooks[i].quantlist);
        av_freep(&venc->codebooks[i].dimentions);
    }
    av_freep(&venc->codebooks);

    if (venc->floors) for (i = 0; i < venc->nfloors; i++) {
        int j;
        av_freep(&venc->floors[i].classes);
        if (venc->floors[i].classes)
            for (j = 0; j < venc->floors[i].nclasses; j++)
                av_freep(&venc->floors[i].classes[j].books);
        av_freep(&venc->floors[i].partition_to_class);
        av_freep(&venc->floors[i].list);
    }
    av_freep(&venc->floors);

    if (venc->residues) for (i = 0; i < venc->nresidues; i++) {
        av_freep(&venc->residues[i].books);
        av_freep(&venc->residues[i].maxes);
    }
    av_freep(&venc->residues);

    if (venc->mappings) for (i = 0; i < venc->nmappings; i++) {
        av_freep(&venc->mappings[i].mux);
        av_freep(&venc->mappings[i].floor);
        av_freep(&venc->mappings[i].residue);
    }
    av_freep(&venc->mappings);

    av_freep(&venc->modes);

    av_freep(&venc->saved);
    av_freep(&venc->samples);
    av_freep(&venc->floor);
    av_freep(&venc->coeffs);

    ff_mdct_end(&venc->mdct[0]);
    ff_mdct_end(&venc->mdct[1]);

    av_freep(&avccontext->coded_frame);
    av_freep(&avccontext->extradata);

    return 0 ;
}

AVCodec vorbis_encoder = {
    "vorbis",
    CODEC_TYPE_AUDIO,
    CODEC_ID_VORBIS,
    sizeof(venc_context_t),
    vorbis_encode_init,
    vorbis_encode_frame,
    vorbis_encode_close,
    .capabilities= CODEC_CAP_DELAY,
};
