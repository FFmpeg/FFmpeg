/*
 * ScreenPressor version 3 decoder
 *
 * Copyright (c) 2017 Paul B Mahol
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/qsort.h"

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "scpr.h"

static void renew_table3(uint32_t nsym, uint32_t *cntsum,
                         uint16_t *freqs, uint16_t *freqs1,
                         uint16_t *cnts, uint8_t *dectab)
{
    uint32_t a = 0, b = 4096 / nsym, c = b - (b >> 1);

    *cntsum = c * nsym;

    for (int d = 0; d < nsym; d++) {
        freqs[d] = b;
        freqs1[d] = a;
        cnts[d] = c;
        for (int q = a + 128 - 1 >> 7, f = (a + b - 1 >> 7) + 1; q < f; q++)
            dectab[q] = d;

        a += b;
    }
}

static void reinit_tables3(SCPRContext * s)
{
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4096; j++) {
            PixelModel3 *m = &s->pixel_model3[i][j];
            m->type = 0;
        }
    }

    for (int i = 0; i < 6; i++) {
        renew_table3(256, &s->run_model3[i].cntsum,
                     s->run_model3[i].freqs[0], s->run_model3[i].freqs[1],
                     s->run_model3[i].cnts, s->run_model3[i].dectab);
    }

    renew_table3(256, &s->range_model3.cntsum,
                 s->range_model3.freqs[0], s->range_model3.freqs[1],
                 s->range_model3.cnts, s->range_model3.dectab);

    renew_table3(5, &s->fill_model3.cntsum,
                 s->fill_model3.freqs[0], s->fill_model3.freqs[1],
                 s->fill_model3.cnts, s->fill_model3.dectab);

    renew_table3(256, &s->count_model3.cntsum,
                 s->count_model3.freqs[0], s->count_model3.freqs[1],
                 s->count_model3.cnts, s->count_model3.dectab);

    for (int i = 0; i < 4; i++) {
        renew_table3(16, &s->sxy_model3[i].cntsum,
                     s->sxy_model3[i].freqs[0], s->sxy_model3[i].freqs[1],
                     s->sxy_model3[i].cnts, s->sxy_model3[i].dectab);
    }

    for (int i = 0; i < 2; i++) {
        renew_table3(512, &s->mv_model3[i].cntsum,
                     s->mv_model3[i].freqs[0], s->mv_model3[i].freqs[1],
                     s->mv_model3[i].cnts, s->mv_model3[i].dectab);
    }

    for (int i = 0; i < 6; i++) {
        renew_table3(6, &s->op_model3[i].cntsum,
                     s->op_model3[i].freqs[0], s->op_model3[i].freqs[1],
                     s->op_model3[i].cnts, s->op_model3[i].dectab);
    }
}

static int decode3(GetByteContext *gb, RangeCoder *rc, uint32_t a, uint32_t b)
{
    uint32_t code = a * (rc->code >> 12) + (rc->code & 0xFFF) - b;

    while (code < 0x800000 && bytestream2_get_bytes_left(gb) > 0)
        code = bytestream2_get_byteu(gb) | (code << 8);
    rc->code = code;

    return 0;
}

static void rescale(PixelModel3 *m, int *totfr)
{
    uint32_t a;

    a = 256 - m->size;
    for (int b = 0; b < m->size; b++) {
        m->freqs[b] -= m->freqs[b] >> 1;
        a += m->freqs[b];
    }

    *totfr = a;
}

static int add_symbol(PixelModel3 *m, int index, uint32_t symbol, int *totfr, int max)
{
    if (m->size == max)
        return 0;

    for (int c = m->size - 1; c >= index; c--) {
        m->symbols[c + 1] = m->symbols[c];
        m->freqs[c + 1] = m->freqs[c];
    }

    m->symbols[index] = symbol;
    m->freqs[index] = 50;
    m->size++;

    if (m->maxpos >= index)
        m->maxpos++;

    *totfr += 50;
    if (*totfr + 50 > 4096)
        rescale(m, totfr);

    return 1;
}

static int decode_adaptive45(PixelModel3 *m, int rccode, uint32_t *value,
                             uint16_t *a, uint16_t *b, uint32_t *c, int max)
{
    uint32_t q, g, maxpos, d, e = *c, totfr = *c;
    int ret;

    for (d = 0; e <= 2048; d++)
        e <<= 1;
    maxpos = m->maxpos;
    rccode >>= d;
    *c = m->freqs[maxpos];
    m->freqs[maxpos] += 4096 - e >> d;

    for (q = 0, g = 0, e = 0; q < m->size; q++) {
        uint32_t f = m->symbols[q];
        uint32_t p = e + f - g;
        uint32_t k = m->freqs[q];

        if (rccode < p) {
            *value = rccode - e + g;
            *b = rccode << d;
            *a = 1 << d;
            m->freqs[maxpos] = *c;
            ret = add_symbol(m, q, *value, &totfr, max);
            *c = totfr;
            return ret;
        }

        if (p + k > rccode) {
            *value = f;
            e += *value - g;
            *b = e << d;
            *a = k << d;
            m->freqs[maxpos] = *c;
            m->freqs[q] += 50;
            totfr += 50;
            if ((q != maxpos) && (m->freqs[q] > m->freqs[maxpos]))
                m->maxpos = q;
            if (totfr + 50 > 4096)
                rescale(m, &totfr);
            *c = totfr;
            return 1;
        }

        e += f - g + k;
        g = f + 1;
    }

    m->freqs[maxpos] = *c;
    *value = g + rccode - e;
    *b = rccode << d;
    *a = 1 << d;
    ret = add_symbol(m, q, *value, &totfr, max);
    *c = totfr;
    return ret;
}

static int update_model6_to_7(PixelModel3 *m)
{
    PixelModel3 n = {0};
    int c, d, e, f, k, p, length, i, j, index;
    uint16_t *freqs, *freqs1, *cnts;

    n.type = 7;

    length = m->length;
    freqs = n.freqs;
    freqs1 = n.freqs1;
    cnts = n.cnts;
    n.cntsum = m->cnts[length];
    for (i = 0; i < length; i++) {
        if (!m->cnts[i])
            continue;
        index = m->symbols[i];
        freqs[index] = m->freqs[2 * i];
        freqs1[index] = m->freqs[2 * i + 1];
        cnts[index] = m->cnts[i];
    }
    c = 1 << m->fshift;
    d = c - (c >> 1);
    for (j = 0, e = 0; j < 256; j++) {
        f = freqs[j];
        if (!f) {
            f = c;
            freqs[j] = c;
            freqs1[j] = e;
            cnts[j] = d;
        }
        p = (e + 127) >> 7;
        k = ((f + e - 1) >> 7) + 1;
        if (k > FF_ARRAY_ELEMS(n.dectab))
            return AVERROR_INVALIDDATA;
        for (i = 0; i < k - p; i++)
            n.dectab[p + i] = j;
        e += f;
    }

    memcpy(m, &n, sizeof(n));

    return 0;
}

static void calc_sum(PixelModel3 *m)
{
    uint32_t a;
    int len;

    len = m->length;
    a = 256 - m->size << (m->fshift > 0 ? m->fshift - 1 : 0);
    for (int c = 0; c < len; c++)
        a += m->cnts[c];
    m->cnts[len] = a;
}

static void rescale_dec(PixelModel3 *m)
{
    uint16_t cnts[256] = {0};
    uint16_t freqs[512] = {0};
    int b, c, e, g;
    uint32_t a;

    for (a = 1 << (0 < m->fshift ? m->fshift - 1 : 0), b = 0; b < 256; b++)
        cnts[b] = a;

    for (a = 0, b = m->size; a < b; a++)
        cnts[m->symbols[a]] = m->cnts[a];

    for (b = a = 0; b < 256; b++) {
        freqs[2 * b] = cnts[b];
        freqs[2 * b + 1] = a;
        a += cnts[b];
    }

    if (m->fshift > 0)
        m->fshift--;

    a = 256 - m->size << (0 < m->fshift ? m->fshift - 1 : 0);
    for (b = 0, c = m->size; b < c; b++) {
        m->cnts[b] -= m->cnts[b] >> 1;
        a = a + m->cnts[b];
        e = m->symbols[b];
        g = freqs[2 * e + 1];
        m->freqs[2 * b] = freqs[2 * e];
        m->freqs[2 * b + 1] = g;
    }
    m->cnts[m->length] = a;
}

static int update_model5_to_6(PixelModel3 *m, uint8_t value)
{
    PixelModel3 n = {0};
    int c, d, e, f, g, k, q, p;

    n.type = 6;
    n.length = 32;

    for (c = m->size, d = 256 - c, e = 0; e < c; e++)
        d = d + m->freqs[e];

    for (e = 0; d <= 2048; e++)
        d <<= 1;

    for (q = d = 0, g = q = 0; g < c; g++) {
        p = m->symbols[g];
        d = d + (p - q);
        q = m->freqs[g];
        k = q << e;
        n.freqs[2 * g] = k;
        n.freqs[2 * g + 1] = d << e;
        n.cnts[g] = k - (k >> 1);
        n.symbols[g] = p;
        d += q;
        q = p + 1;
    }

    n.fshift = e;
    e = 1 << n.fshift;
    d = 0;
    if (value > 0) {
        d = -1;
        for (p = f = g = 0; p < c; p++) {
            k = n.symbols[p];
            if (k > d && k < value) {
                d = k;
                g = n.freqs[2 * p];
                f = n.freqs[2 * p + 1];
            }
        }
        d = 0 < g ? f + g + (value - d - 1 << n.fshift) : value << n.fshift;
    }
    n.freqs[2 * c] = e;
    n.freqs[2 * c + 1] = d;
    n.cnts[c] = e - (e >> 1);
    n.symbols[c] = value;
    n.size = c + 1;
    e = 25 << n.fshift;
    n.cnts[c] += e;
    n.cnts[32] += e;
    if (n.cnts[32] + e > 4096)
        rescale_dec(&n);

    calc_sum(&n);
    for (c = 0, e = n.size - 1; c < e; c++) {
        for (g = c + 1, f = n.size; g < f; g++) {
            if (q = n.freqs[2 * g], k = n.freqs[2 * c], q > k) {
                int l = n.freqs[2 * c + 1];
                int h = n.freqs[2 * g + 1];
                n.freqs[2 * c] = q;
                n.freqs[2 * c + 1] = h;
                n.freqs[2 * g] = k;
                n.freqs[2 * g + 1] = l;
                FFSWAP(uint16_t, n.cnts[c], n.cnts[g]);
                FFSWAP(uint8_t, n.symbols[c], n.symbols[g]);
            }
        }
    }

    memcpy(m, &n, sizeof(n));

    return 0;
}

static void grow_dec(PixelModel3 *m)
{
    int a;

    a = 2 * m->length;
    m->cnts[2 * m->length] = m->cnts[m->length];
    m->length = a;
}

static int add_dec(PixelModel3 *m, int sym, int f1, int f2)
{
    int size;

    if (m->size >= 40 || m->size >= m->length)
        return -1;

    size = m->size;
    m->symbols[size] = sym;
    m->freqs[2 * size] = f1;
    m->freqs[2 * size + 1] = f2;
    m->cnts[size] = f1 - (f1 >> 1);
    m->size++;

    return size;
}

static void incr_cntdec(PixelModel3 *m, int a)
{
    int b, len, d, e, g;

    b = 25 << m->fshift;
    len = m->length;
    m->cnts[a] += b;
    m->cnts[len] += b;
    if (a > 0 && m->cnts[a] > m->cnts[a - 1]) {
        FFSWAP(uint16_t, m->cnts[a], m->cnts[a - 1]);
        d = m->freqs[2 * a];
        e = m->freqs[2 * a + 1];
        g = m->freqs[2 * (a - 1) + 1];
        m->freqs[2 * a] = m->freqs[2 * (a - 1)];
        m->freqs[2 * a + 1] = g;
        g = a - 1;
        m->freqs[2 * g] = d;
        m->freqs[2 * g + 1] = e;
        FFSWAP(uint8_t, m->symbols[a], m->symbols[a - 1]);
    }

    if (m->cnts[len] + b > 4096)
        rescale_dec(m);
}

static int decode_adaptive6(PixelModel3 *m, uint32_t code, uint32_t *value,
                            uint16_t *a, uint16_t *b)
{
    int c, d, e, f, g, q;

    for (c = 0, d = 0, e = 0, f = 0, g = 0, q = m->size; g < q; g++) {
        uint32_t p = m->freqs[2 * g + 1];

        if (p <= code) {
            uint32_t k = m->freqs[2 * g];

            if (p + k > code) {
                *value = m->symbols[g];
                *a = k;
                *b = p;
                incr_cntdec(m, g);
                return 1;
            }

            if (p >= d) {
                c = k;
                d = p;
                e = m->symbols[g];
            }
        }
    }

    g = 1 << m->fshift;
    q = f = 0;

    if (c > 0) {
        f = code - (d + c) >> m->fshift;
        q = f + e + 1;
        f = d + c + (f << m->fshift);
    } else {
        q = code >> m->fshift;
        f = q << m->fshift;
    }

    *a = g;
    *b = f;
    *value = q;

    c = add_dec(m, q, g, f);
    if (c < 0) {
        if (m->length == 64)
            return 0;
        grow_dec(m);
        c = add_dec(m, q, g, f);
        if (c < 0)
            return AVERROR_INVALIDDATA;
    }

    incr_cntdec(m, c);
    return 1;
}

static int cmpbytes(const void *p1, const void *p2)
{
    int left  = *(const uint8_t *)p1;
    int right = *(const uint8_t *)p2;
    return FFDIFFSIGN(left, right);
}

static int update_model1_to_2(PixelModel3 *m, uint32_t val)
{
    PixelModel3 n = {0};
    int i, b;

    n.type = 2;
    n.size = m->size + 1;
    b = m->size;
    for (i = 0; i < b; i++)
        n.symbols[i] = m->symbols[i];
    n.symbols[b] = val;

    memcpy(m, &n, sizeof(n));

    return 0;
}

static int update_model1_to_4(PixelModel3 *m, uint32_t val)
{
    PixelModel3 n = {0};
    int size, i;

    size = m->size;
    n.type = 4;
    n.size = size;
    for (i = 0; i < n.size; i++) {
        n.symbols[i] = m->symbols[i];
    }
    AV_QSORT(n.symbols, size, uint8_t, cmpbytes);
    for (i = 0; i < n.size; i++) {
        if (val == n.symbols[i]) {
            n.freqs[i] = 100;
            n.maxpos = i;
        } else {
            n.freqs[i] = 50;
        }
    }

    memcpy(m, &n, sizeof(n));

    return 0;
}

static int update_model1_to_5(PixelModel3 *m, uint32_t val)
{
    int i, size, freqs;
    uint32_t a;

    update_model1_to_4(m, val);
    size = m->size;
    a = 256 - size;
    for (i = 0; i < size; i++, a += freqs)
        freqs = m->freqs[i];
    m->type = 5;
    m->cntsum = a;

    return 0;
}

static int decode_static1(PixelModel3 *m, uint32_t val)
{
    uint32_t size;

    size = m->size;
    for (int i = 0; i < size; i++) {
        if (val == m->symbols[i]) {
            if (size <= 4)
                return update_model1_to_4(m, val);
            else
                return update_model1_to_5(m, val);
        }
    }

    if (size >= 14)
        return update_model1_to_2(m, val);

    m->symbols[size] = val;
    m->size++;
    return 0;
}

static int update_model2_to_6(PixelModel3 *m, uint8_t value, int a4)
{
    PixelModel3 n = {0};
    int c, d, e, f, g, q;

    n.type = 6;
    n.length = a4;

    memset(n.symbols, 1u, a4);

    c = m->size;
    d = 256 - c + (64 * c + 64);
    for (e = 0; d <= 2048; e++) {
        d <<= 1;
    }

    g = q = 0;
    AV_QSORT(m->symbols, c, uint8_t, cmpbytes);
    for (f = d = 0; f < c; f++) {
        int p = f;
        int k = m->symbols[p];
        int l;
        g = g + (k - q);

        if (k == value) {
            d = p;
            q = 128;
        } else {
            q = 64;
        }
        l = q << e;
        n.freqs[2 * p] = l;
        n.freqs[2 * p + 1] = g << e;
        n.symbols[p] = k;
        n.cnts[p] = l - (l >> 1);
        g += q;
        q = k + 1;
    }
    n.size = c;
    n.fshift = e;
    calc_sum(&n);

    if (d > 0) {
        c = n.freqs[0];
        e = n.freqs[1];
        g = n.freqs[2 * d + 1];
        n.freqs[0] = n.freqs[2 * d];
        n.freqs[1] = g;
        n.freqs[2 * d] = c;
        n.freqs[2 * d + 1] = e;
        FFSWAP(uint16_t, n.cnts[0], n.cnts[d]);
        FFSWAP(uint8_t, n.symbols[0], n.symbols[d]);
    }

    memcpy(m, &n, sizeof(n));

    return 0;
}

static int update_model2_to_3(PixelModel3 *m, uint32_t val)
{
    PixelModel3 n = {0};
    uint32_t size;

    n.type = 3;
    n.size = m->size + 1;

    size = m->size;
    for (int i = 0; i < size; i++)
        n.symbols[i] = m->symbols[i];
    n.symbols[size] = val;

    memcpy(m, &n, sizeof(n));

    return 0;
}

static int decode_static2(PixelModel3 *m, uint32_t val)
{
    uint32_t size;

    size = m->size;
    for (int i = 0; i < size; i++) {
        if (val == m->symbols[i]) {
            int a;

            if (m->size <= 32)
                a = 32;
            else
                a = 64;
            return update_model2_to_6(m, val, a);
        }
    }

    if (size >= 64)
        return update_model2_to_3(m, val);

    m->symbols[size] = val;
    m->size++;

    return 0;
}

static int update_model3_to_7(PixelModel3 *m, uint8_t value)
{
    PixelModel3 n = {0};
    int c, d, e, f, g, q;

    n.type = 7;

    for (c = 0; c < 256; c++) {
        d = c;
        n.freqs[d] = 1;
        n.cnts[d] = 1;
    }

    for (c = m->size, d = (4096 - (256 - c)) / (c + 1) | 0, e = d - (d >> 1), g = 0; g < c;) {
        q = g++;
        q = m->symbols[q];
        n.freqs[q] = d;
        n.cnts[q] = e;
    }
    n.freqs[value] += d;
    n.cnts[value] += 16;
    for (d = c = n.cntsum = 0; 256 > d; d++) {
        e = d;
        n.cntsum += n.cnts[e];
        n.freqs1[e] = c;
        g = n.freqs[e];
        f = (c + g - 1 >> 7) + 1;
        if (f > FF_ARRAY_ELEMS(n.dectab))
            return AVERROR_INVALIDDATA;
        for (q = c + 128 - 1 >> 7; q < f; q++) {
            n.dectab[q] = e;
        }
        c += g;
    }

    memcpy(m, &n, sizeof(n));

    return 0;
}

static int decode_static3(PixelModel3 *m, uint32_t val)
{
    uint32_t size = m->size;

    for (int i = 0; i < size; i++) {
        if (val == m->symbols[i])
            return update_model3_to_7(m, val);
    }

    if (size >= 256)
        return 0;

    m->symbols[size] = val;
    m->size++;
    return 0;
}

static void sync_code3(GetByteContext *gb, RangeCoder *rc)
{
    rc->code1++;
    if (rc->code1 == 0x20000) {
        rc->code = bytestream2_get_le32(gb);
        rc->code1 = 0;
    }
}

static int decode_value3(SCPRContext *s, uint32_t max, uint32_t *cntsum,
                         uint16_t *freqs1, uint16_t *freqs2,
                         uint16_t *cnts, uint8_t *dectable,
                         uint32_t *value)
{
    GetByteContext *gb = &s->gb;
    RangeCoder *rc = &s->rc;
    uint32_t r, y, a, b, e, g, q;

    r = dectable[(rc->code & 0xFFFu) >> 7];
    if (r < max) {
        while (freqs2[r + 1] <= (rc->code & 0xFFF)) {
            if (++r >= max)
                break;
        }
    }

    if (r > max)
        return AVERROR_INVALIDDATA;

    cnts[r] += 16;
    a = freqs1[r];
    b = freqs2[r];
    *cntsum += 16;
    if (*cntsum + 16 > 4096) {
        *cntsum = 0;
        for (int c = 0, i = 0; i < max + 1; i++) {
            e = cnts[i];
            freqs2[i] = c;
            freqs1[i] = e;
            g = (c + 127) >> 7;
            c += e;
            q = ((c - 1) >> 7) + 1;
            if (q > g) {
                for (int j = 0; j < q - g; j++)
                    dectable[j + g] = i;
            }
            y = e - (e >> 1);
            cnts[i] = y;
            *cntsum += y;
        }
    }

    decode3(gb, rc, a, b);
    sync_code3(gb, rc);

    *value = r;

    return 0;
}

static void calc_sum5(PixelModel3 *m)
{
    uint32_t a;

    a = 256 - m->size;
    for (int b = 0; b < m->size; b++)
        a += m->freqs[b];
    m->cntsum = a;
}

static int update_model4_to_5(PixelModel3 *m, uint32_t value)
{
    PixelModel3 n = {0};
    int c, e, g, totfr;

    n.type = 5;

    for (c = 0, e = 0; c < m->size && m->symbols[c] < value; c++) {
        n.symbols[c] = m->symbols[c];
        e += n.freqs[c] = m->freqs[c];
    }

    g = c;
    n.symbols[g] = value;
    e += n.freqs[g++] = 50;
    for (; c < m->size; g++, c++) {
        n.symbols[g] = m->symbols[c];
        e += n.freqs[g] = m->freqs[c];
    }
    n.size = m->size + 1;
    if (e > 4096)
        rescale(&n, &totfr);

    calc_sum5(&n);

    memcpy(m, &n, sizeof(n));

    return 0;
}

static int decode_unit3(SCPRContext *s, PixelModel3 *m, uint32_t code, uint32_t *value)
{
    GetByteContext *gb = &s->gb;
    RangeCoder *rc = &s->rc;
    uint16_t a = 0, b = 0;
    uint32_t param;
    int type;
    int ret;

    type = m->type;
    switch (type) {
    case 0:
        *value = bytestream2_get_byte(&s->gb);
        m->type = 1;
        m->size = 1;
        m->symbols[0] = *value;
        sync_code3(gb, rc);
        break;
    case 1:
        *value = bytestream2_get_byte(&s->gb);
        decode_static1(m, *value);
        sync_code3(gb, rc);
        break;
    case 2:
        *value = bytestream2_get_byte(&s->gb);
        decode_static2(m, *value);
        sync_code3(gb, rc);
        break;
    case 3:
        *value = bytestream2_get_byte(&s->gb);
        ret = decode_static3(m, *value);
        if (ret < 0)
            return AVERROR_INVALIDDATA;
        sync_code3(gb, rc);
        break;
    case 4:
        param = m->freqs[0] + m->freqs[1] + m->freqs[2] + m->freqs[3] + 256 - m->size;
        if (!decode_adaptive45(m, code, value, &a, &b, &param, 4))
            update_model4_to_5(m, *value);
        decode3(gb, rc, a, b);
        sync_code3(gb, rc);
        break;
    case 5:
        if (!decode_adaptive45(m, code, value, &a, &b, &m->cntsum, 16))
            update_model5_to_6(m, *value);
        decode3(gb, rc, a, b);
        sync_code3(gb, rc);
        break;
    case 6:
        ret = decode_adaptive6(m, code, value, &a, &b);
        if (!ret)
            ret = update_model6_to_7(m);
        if (ret < 0)
            return ret;
        decode3(gb, rc, a, b);
        sync_code3(gb, rc);
        break;
    case 7:
        return decode_value3(s, 255, &m->cntsum,
                             m->freqs, m->freqs1,
                             m->cnts, m->dectab, value);
    }

    if (*value > 255)
        return AVERROR_INVALIDDATA;

    return 0;
}

static int decode_units3(SCPRContext * s, uint32_t *red,
                         uint32_t *green, uint32_t *blue,
                         int *cx, int *cx1)
{
    RangeCoder *rc = &s->rc;
    int ret;

    ret = decode_unit3(s, &s->pixel_model3[0][*cx + *cx1], rc->code & 0xFFF, red);
    if (ret < 0)
        return ret;

    *cx1 = (*cx << 6) & 0xFC0;
    *cx = *red >> 2;

    ret = decode_unit3(s, &s->pixel_model3[1][*cx + *cx1], rc->code & 0xFFF, green);
    if (ret < 0)
        return ret;

    *cx1 = (*cx << 6) & 0xFC0;
    *cx = *green >> 2;

    ret = decode_unit3(s, &s->pixel_model3[2][*cx + *cx1], rc->code & 0xFFF, blue);
    if (ret < 0)
        return ret;

    *cx1 = (*cx << 6) & 0xFC0;
    *cx = *blue >> 2;

    return 0;
}

static void init_rangecoder3(RangeCoder *rc, GetByteContext *gb)
{
    rc->code  = bytestream2_get_le32(gb);
    rc->code1 = 0;
}

static int decompress_i3(AVCodecContext *avctx, uint32_t *dst, int linesize)
{
    SCPRContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    RangeCoder *rc = &s->rc;
    int cx = 0, cx1 = 0, k = 0;
    int run, off, y = 0, x = 0, ret;
    uint32_t backstep = linesize - avctx->width;
    uint32_t clr = 0, lx, ly, ptype, r, g, b;

    bytestream2_skip(gb, 1);
    init_rangecoder3(rc, gb);
    reinit_tables3(s);

    while (k < avctx->width + 1) {
        ret = decode_units3(s, &r, &g, &b, &cx, &cx1);
        if (ret < 0)
            return ret;
        ret = decode_value3(s, 255, &s->run_model3[0].cntsum,
                            s->run_model3[0].freqs[0],
                            s->run_model3[0].freqs[1],
                            s->run_model3[0].cnts,
                            s->run_model3[0].dectab, &run);
        if (ret < 0)
            return ret;
        if (run <= 0)
            return AVERROR_INVALIDDATA;

        clr = (b << 16) + (g << 8) + r;
        k += run;
        while (run-- > 0) {
            if (y >= avctx->height)
                return AVERROR_INVALIDDATA;

            dst[y * linesize + x] = clr;
            lx = x;
            ly = y;
            x++;
            if (x >= avctx->width) {
                x = 0;
                y++;
            }
        }
    }
    off = -linesize - 1;
    ptype = 0;

    while (x < avctx->width && y < avctx->height) {
        ret = decode_value3(s, 5, &s->op_model3[ptype].cntsum,
                            s->op_model3[ptype].freqs[0],
                            s->op_model3[ptype].freqs[1],
                            s->op_model3[ptype].cnts,
                            s->op_model3[ptype].dectab, &ptype);
        if (ret < 0)
            return ret;
        if (ptype == 0) {
            ret = decode_units3(s, &r, &g, &b, &cx, &cx1);
            if (ret < 0)
                return ret;
            clr = (b << 16) + (g << 8) + r;
        }
        if (ptype > 5)
            return AVERROR_INVALIDDATA;
        ret = decode_value3(s, 255, &s->run_model3[ptype].cntsum,
                            s->run_model3[ptype].freqs[0],
                            s->run_model3[ptype].freqs[1],
                            s->run_model3[ptype].cnts,
                            s->run_model3[ptype].dectab, &run);
        if (ret < 0)
            return ret;
        if (run <= 0)
            return AVERROR_INVALIDDATA;

        ret = decode_run_i(avctx, ptype, run, &x, &y, clr,
                           dst, linesize, &lx, &ly,
                           backstep, off, &cx, &cx1);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int decompress_p3(AVCodecContext *avctx,
                         uint32_t *dst, int linesize,
                         uint32_t *prev, int plinesize)
{
    SCPRContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    int ret, temp, min, max, x, y, cx = 0, cx1 = 0;
    int backstep = linesize - avctx->width;
    int mvx = 0, mvy = 0;

    if (bytestream2_get_byte(gb) == 0)
        return 1;
    init_rangecoder3(&s->rc, gb);

    ret  = decode_value3(s, 255, &s->range_model3.cntsum,
                         s->range_model3.freqs[0],
                         s->range_model3.freqs[1],
                         s->range_model3.cnts,
                         s->range_model3.dectab, &min);
    ret |= decode_value3(s, 255, &s->range_model3.cntsum,
                         s->range_model3.freqs[0],
                         s->range_model3.freqs[1],
                         s->range_model3.cnts,
                         s->range_model3.dectab, &temp);
    if (ret < 0)
        return ret;

    min += temp << 8;
    ret |= decode_value3(s, 255, &s->range_model3.cntsum,
                         s->range_model3.freqs[0],
                         s->range_model3.freqs[1],
                         s->range_model3.cnts,
                         s->range_model3.dectab, &max);
    ret |= decode_value3(s, 255, &s->range_model3.cntsum,
                         s->range_model3.freqs[0],
                         s->range_model3.freqs[1],
                         s->range_model3.cnts,
                         s->range_model3.dectab, &temp);
    if (ret < 0)
        return ret;

    max += temp << 8;
    if (min > max || min >= s->nbcount)
        return AVERROR_INVALIDDATA;

    memset(s->blocks, 0, sizeof(*s->blocks) * s->nbcount);

    while (min <= max) {
        int fill, count;

        ret  = decode_value3(s, 4, &s->fill_model3.cntsum,
                             s->fill_model3.freqs[0],
                             s->fill_model3.freqs[1],
                             s->fill_model3.cnts,
                             s->fill_model3.dectab, &fill);
        ret |= decode_value3(s, 255, &s->count_model3.cntsum,
                             s->count_model3.freqs[0],
                             s->count_model3.freqs[1],
                             s->count_model3.cnts,
                             s->count_model3.dectab, &count);
        if (ret < 0)
            return ret;
        if (count <= 0)
            return AVERROR_INVALIDDATA;

        while (min < s->nbcount && count-- > 0) {
            s->blocks[min++] = fill;
        }
    }

    ret = av_frame_copy(s->current_frame, s->last_frame);
    if (ret < 0)
        return ret;

    for (y = 0; y < s->nby; y++) {
        for (x = 0; x < s->nbx; x++) {
            int sy1 = 0, sy2 = 16, sx1 = 0, sx2 = 16;

            if (s->blocks[y * s->nbx + x] == 0)
                continue;

            if (((s->blocks[y * s->nbx + x] + 1) & 1) > 0) {
                ret  = decode_value3(s, 15, &s->sxy_model3[0].cntsum,
                                     s->sxy_model3[0].freqs[0],
                                     s->sxy_model3[0].freqs[1],
                                     s->sxy_model3[0].cnts,
                                     s->sxy_model3[0].dectab, &sx1);
                ret |= decode_value3(s, 15, &s->sxy_model3[1].cntsum,
                                     s->sxy_model3[1].freqs[0],
                                     s->sxy_model3[1].freqs[1],
                                     s->sxy_model3[1].cnts,
                                     s->sxy_model3[1].dectab, &sy1);
                ret |= decode_value3(s, 15, &s->sxy_model3[2].cntsum,
                                     s->sxy_model3[2].freqs[0],
                                     s->sxy_model3[2].freqs[1],
                                     s->sxy_model3[2].cnts,
                                     s->sxy_model3[2].dectab, &sx2);
                ret |= decode_value3(s, 15, &s->sxy_model3[3].cntsum,
                                     s->sxy_model3[3].freqs[0],
                                     s->sxy_model3[3].freqs[1],
                                     s->sxy_model3[3].cnts,
                                     s->sxy_model3[3].dectab, &sy2);
                if (ret < 0)
                    return ret;

                sx2++;
                sy2++;
            }
            if (((s->blocks[y * s->nbx + x] + 3) & 2) > 0) {
                int i, a, b, c, j, by = y * 16, bx = x * 16;
                uint32_t code;

                a = s->rc.code & 0xFFF;
                c = 1;

                if (a < 0x800)
                    c = 0;
                b = 2048;
                if (!c)
                    b = 0;

                code = a + ((s->rc.code >> 1) & 0xFFFFF800) - b;
                while (code < 0x800000 && bytestream2_get_bytes_left(gb) > 0)
                    code = bytestream2_get_byteu(gb) | (code << 8);
                s->rc.code = code;

                sync_code3(gb, &s->rc);

                if (!c) {
                    ret  = decode_value3(s, 511, &s->mv_model3[0].cntsum,
                                         s->mv_model3[0].freqs[0],
                                         s->mv_model3[0].freqs[1],
                                         s->mv_model3[0].cnts,
                                         s->mv_model3[0].dectab, &mvx);
                    ret |= decode_value3(s, 511, &s->mv_model3[1].cntsum,
                                         s->mv_model3[1].freqs[0],
                                         s->mv_model3[1].freqs[1],
                                         s->mv_model3[1].cnts,
                                         s->mv_model3[1].dectab, &mvy);
                    if (ret < 0)
                        return ret;

                    mvx -= 256;
                    mvy -= 256;
                }

                if (by + mvy + sy1 < 0 || bx + mvx + sx1 < 0 ||
                    by + mvy + sy1 >= avctx->height || bx + mvx + sx1 >= avctx->width)
                    return AVERROR_INVALIDDATA;

                for (i = 0; i < sy2 - sy1 && (by + sy1 + i) < avctx->height && (by + mvy + sy1 + i) < avctx->height; i++) {
                    for (j = 0; j < sx2 - sx1 && (bx + sx1 + j) < avctx->width && (bx + mvx + sx1 + j) < avctx->width; j++) {
                        dst[(by + i + sy1) * linesize + bx + sx1 + j] = prev[(by + mvy + sy1 + i) * plinesize + bx + sx1 + mvx + j];
                    }
                }
            } else {
                int run, bx = x * 16 + sx1, by = y * 16 + sy1;
                uint32_t clr, ptype = 0, r, g, b;

                if (bx >= avctx->width)
                    return AVERROR_INVALIDDATA;

                for (; by < y * 16 + sy2 && by < avctx->height;) {
                    ret = decode_value3(s, 5, &s->op_model3[ptype].cntsum,
                                        s->op_model3[ptype].freqs[0],
                                        s->op_model3[ptype].freqs[1],
                                        s->op_model3[ptype].cnts,
                                        s->op_model3[ptype].dectab, &ptype);
                    if (ret < 0)
                        return ret;
                    if (ptype == 0) {
                        ret = decode_units3(s, &r, &g, &b, &cx, &cx1);
                        if (ret < 0)
                            return ret;

                        clr = (b << 16) + (g << 8) + r;
                    }
                    if (ptype > 5)
                        return AVERROR_INVALIDDATA;
                    ret = decode_value3(s, 255, &s->run_model3[ptype].cntsum,
                                        s->run_model3[ptype].freqs[0],
                                        s->run_model3[ptype].freqs[1],
                                        s->run_model3[ptype].cnts,
                                        s->run_model3[ptype].dectab, &run);
                    if (ret < 0)
                        return ret;
                    if (run <= 0)
                        return AVERROR_INVALIDDATA;

                    ret = decode_run_p(avctx, ptype, run, x, y, clr,
                                       dst, prev, linesize, plinesize, &bx, &by,
                                       backstep, sx1, sx2, &cx, &cx1);
                    if (ret < 0)
                        return ret;
                }
            }
        }
    }

    return 0;
}
