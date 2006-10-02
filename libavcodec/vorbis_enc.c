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
} residue_t;

typedef struct {
    int submaps;
    int * mux;
    int * floor;
    int * residue;
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

static void create_vorbis_context(venc_context_t * venc, AVCodecContext * avccontext) {
    codebook_t * cb;
    floor_t * fc;
    residue_t * rc;
    mapping_t * mc;
    int i, book;

    venc->channels = avccontext->channels;
    venc->sample_rate = avccontext->sample_rate;
    venc->blocksize[0] = venc->blocksize[1] = 8;

    venc->ncodebooks = 10;
    venc->codebooks = av_malloc(sizeof(codebook_t) * venc->ncodebooks);

    // codebook 0 - floor1 book, values 0..255
    cb = &venc->codebooks[0];
    cb->nentries = 512;
    cb->entries = av_malloc(sizeof(cb_entry_t) * cb->nentries);
    for (i = 0; i < cb->nentries; i++) cb->entries[i].len = 9;
    cb->ndimentions = 0;
    cb->min = 0.;
    cb->delta = 0.;
    cb->seq_p = 0;
    cb->lookup = 0;
    cb->quantlist = NULL;
    ready_codebook(cb);

    // codebook 1 - residue classbook, values 0..1, dimentions 4
    cb = &venc->codebooks[1];
    cb->nentries = 2;
    cb->entries = av_malloc(sizeof(cb_entry_t) * cb->nentries);
    for (i = 0; i < cb->nentries; i++) cb->entries[i].len = 1;
    cb->ndimentions = 4;
    cb->min = 0.;
    cb->delta = 0.;
    cb->seq_p = 0;
    cb->lookup = 0;
    cb->quantlist = NULL;
    ready_codebook(cb);

    // codebook 2..9 - vector, for the residue, values -32767..32767, dimentions 1
    for (book = 0; book < 8; book++) {
        cb = &venc->codebooks[2 + book];
        cb->nentries = 5;
        cb->entries = av_malloc(sizeof(cb_entry_t) * cb->nentries);
        for (i = 0; i < cb->nentries; i++) cb->entries[i].len = i == 2 ? 1 : 3;
        cb->ndimentions = 1;
        cb->delta = 1 << ((7 - book) * 2);
        cb->min = -cb->delta*2;
        cb->seq_p = 0;
        cb->lookup = 1;
        cb->quantlist = av_malloc(sizeof(int) * cb_lookup_vals(cb->lookup, cb->ndimentions, cb->nentries));
        for (i = 0; i < cb->nentries; i++) cb->quantlist[i] = i;
        ready_codebook(cb);
    }

    venc->nfloors = 1;
    venc->floors = av_malloc(sizeof(floor_t) * venc->nfloors);

    // just 1 floor
    fc = &venc->floors[0];
    fc->partitions = 1;
    fc->partition_to_class = av_malloc(sizeof(int) * fc->partitions);
    for (i = 0; i < fc->partitions; i++) fc->partition_to_class[i] = 0;
    fc->nclasses = 1;
    fc->classes = av_malloc(sizeof(floor_class_t) * fc->nclasses);
    for (i = 0; i < fc->nclasses; i++) {
        floor_class_t * c = &fc->classes[i];
        int j, books;
        c->dim = 1;
        c->subclass = 0;
        c->masterbook = 0;
        books = (1 << c->subclass);
        c->books = av_malloc(sizeof(int) * books);
        for (j = 0; j < books; j++) c->books[j] = 0;
    }
    fc->multiplier = 1;
    fc->rangebits = venc->blocksize[0] - 1;

    fc->values = 2;
    for (i = 0; i < fc->partitions; i++)
        fc->values += fc->classes[fc->partition_to_class[i]].dim;

    fc->list = av_malloc(sizeof(floor_entry_t) * fc->values);
    fc->list[0].x = 0;
    fc->list[1].x = 1 << fc->rangebits;
    for (i = 2; i < fc->values; i++) {
        int a = i - 1;
        int g = ilog(a);
        assert(g <= fc->rangebits);
        a ^= 1 << (g-1);
        g = 1 << (fc->rangebits - g);
        fc->list[i].x = g + a*2*g;
    }
    ready_floor(fc);

    venc->nresidues = 1;
    venc->residues = av_malloc(sizeof(residue_t) * venc->nresidues);

    // single residue
    rc = &venc->residues[0];
    rc->type = 0;
    rc->begin = 0;
    rc->end = 1 << (venc->blocksize[0] - 1);
    rc->partition_size = 64;
    rc->classifications = 2;
    rc->classbook = 1;
    rc->books = av_malloc(sizeof(int[8]) * rc->classifications);
    for (i = 0; i < rc->classifications; i++) {
        int j;
        for (j = 0; j < 8; j++) rc->books[i][j] = 2 + j;
        rc->books[i][0] = rc->books[i][1] = rc->books[i][2] = rc->books[i][3] = -1;
    }

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

        put_bits(&pb, 1, 0); // channel coupling

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
        average /= 64; // MAGIC!
        for (j = 0; j < range; j++) if (floor1_inverse_db_table[j * fc->multiplier] > average) break;
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
        int highroom = range -  predicted;
        int lowroom = predicted;
        int room = FFMIN(highroom, lowroom);
        if (predicted == posts[i]) {
            coded[i] = 0; // must be used later as flag!
            continue;
        }
        if (posts[i] > predicted) {
            if (posts[i] - predicted > room) coded[i] = posts[i] - predicted + lowroom;
            else coded[i] = (posts[i] - predicted) << 1;
        } else {
            if (predicted - posts[i] > room) coded[i] = predicted - posts[i] + highroom - 1;
            else coded[i] = ((predicted - posts[i]) << 1) + 1;
        }
    }

    counter = 2;
    for (i = 0; i < fc->partitions; i++) {
        floor_class_t * c = &fc->classes[fc->partition_to_class[i]];
        codebook_t * book = &venc->codebooks[c->books[0]];
        int k;
        assert(!c->subclass);
        for (k = 0; k < c->dim; k++) {
            int entry = coded[counter++];
            if (entry >= book->nentries || entry < 0) av_log(NULL, AV_LOG_ERROR, "%d %d %d %d \n", entry, book->nentries, counter, fc->values);
            assert(entry < book->nentries);
            assert(entry >= 0);
            put_bits(pb, book->entries[entry].len, book->entries[entry].codeword);
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

static void residue_encode(venc_context_t * venc, residue_t * rc, PutBitContext * pb, float * coeffs, int samples, int channels) {
    int pass, i, j, p, k;
    int psize = rc->partition_size;
    int partitions = (rc->end - rc->begin) / psize;
    int classes[channels][partitions];
    int classwords = venc->codebooks[rc->classbook].ndimentions;

    for (pass = 0; pass < 8; pass++) {
        p = 0;
        while (p < partitions) {
            if (pass == 0) for (j = 0; j < channels; j++) {
                codebook_t * book = &venc->codebooks[rc->classbook];
                int entry = 0;
                put_bits(pb, book->entries[entry].len, book->entries[entry].codeword);
                for (i = classwords; i--; ) {
                    classes[j][p + i] = entry % rc->classifications;
                    entry /= rc->classifications;
                }
            }
            for (i = 0; i < classwords && p < partitions; i++, p++) {
                for (j = 0; j < channels; j++) {
                    int nbook = rc->books[classes[j][p]][pass];
                    codebook_t * book = &venc->codebooks[nbook];
                    float * buf = coeffs + samples*j + rc->begin + p*psize;
                    if (nbook == -1) continue;

                    assert(rc->type == 0);
                    assert(!(psize % book->ndimentions));

                    for (k = 0; k < psize; k += book->ndimentions) {
                        float * a = put_vector(book, pb, &buf[k]);
                        int l;
                        for (l = 0; l < book->ndimentions; l++) buf[k + l] -= a[l];
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
                offset[i] = audio[j] / 32768. * win[window_len - i] / n;
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
                offset[i] = audio[j] / 32768. * win[i] / n;
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
