/*
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

static int FUNC(frame_header)(CodedBitstreamContext *ctx, RWContext *rw,
                              JPEGRawFrameHeader *current)
{
    int err, i;

    HEADER("Frame Header");

    u(16, Lf, 8, 8 + 3 * JPEG_MAX_COMPONENTS);

    u(8,  P,  2, 16);
    u(16, Y,  0, JPEG_MAX_HEIGHT);
    u(16, X,  1, JPEG_MAX_WIDTH);
    u(8,  Nf, 1, JPEG_MAX_COMPONENTS);

    for (i = 0; i < current->Nf; i++) {
        us(8, C[i],  i, 0, JPEG_MAX_COMPONENTS);
        us(4, H[i],  i, 1, 4);
        us(4, V[i],  i, 1, 4);
        us(8, Tq[i], i, 0, 3);
    }

    return 0;
}

static int FUNC(quantisation_table)(CodedBitstreamContext *ctx, RWContext *rw,
                                     JPEGRawQuantisationTable *current)
{
    int err, i;

    u(4, Pq, 0, 1);
    u(4, Tq, 0, 3);

    if (current->Pq) {
        for (i = 0; i < 64; i++)
            us(16, Q[i], i, 1, 255);
    } else {
        for (i = 0; i < 64; i++)
            us(8,  Q[i], i, 1, 255);
    }

    return 0;
}

static int FUNC(dqt)(CodedBitstreamContext *ctx, RWContext *rw,
                     JPEGRawQuantisationTableSpecification *current)
{
    int err, i, n;

    HEADER("Quantisation Tables");

    u(16, Lq, 2, 2 + 4 * 65);
    n = current->Lq / 65;

    for (i = 0; i < n; i++)
        CHECK(FUNC(quantisation_table)(ctx, rw, &current->table[i]));

    return 0;
}

static int FUNC(huffman_table)(CodedBitstreamContext *ctx, RWContext *rw,
                               JPEGRawHuffmanTable *current)
{
    int err, i, j, ij;

    u(4, Tc, 0, 1);
    u(4, Th, 0, 3);

    for (i = 0; i < 16; i++)
        us(8, L[i], i, 0, 224);

    ij = 0;
    for (i = 0; i < 16; i++) {
        for (j = 0; j < current->L[i]; j++) {
            us(8, V[ij], ij, 0, 255);
            ++ij;
        }
    }

    return 0;
}

static int FUNC(dht)(CodedBitstreamContext *ctx, RWContext *rw,
                     JPEGRawHuffmanTableSpecification *current)
{
    int err, i, j, n;

    HEADER("Huffman Tables");

    u(16, Lh, 2, 2 + 8 * (1 + 16 + 256));

    n = 2;
    for (i = 0; n < current->Lh; i++) {
        CHECK(FUNC(huffman_table)(ctx, rw, &current->table[i]));

        ++n;
        for (j = 0; j < 16; j++)
            n += 1 + current->table[i].L[j];
    }

    return 0;
}

static int FUNC(scan_header)(CodedBitstreamContext *ctx, RWContext *rw,
                             JPEGRawScanHeader *current)
{
    int err, j;

    HEADER("Scan");

    u(16, Ls, 6, 6 + 2 * JPEG_MAX_COMPONENTS);

    u(8, Ns, 1, 4);
    for (j = 0; j < current->Ns; j++) {
        us(8, Cs[j], j, 0, JPEG_MAX_COMPONENTS);
        us(4, Td[j], j, 0, 3);
        us(4, Ta[j], j, 0, 3);
    }

    u(8, Ss, 0, 63);
    u(8, Se, 0, 63);
    u(4, Ah, 0, 13);
    u(4, Al, 0, 15);

    return 0;
}

static int FUNC(application_data)(CodedBitstreamContext *ctx, RWContext *rw,
                                  JPEGRawApplicationData *current)
{
    int err, i;

    HEADER("Application Data");

    u(16, Lp, 2, 65535);

    if (current->Lp > 2) {
#ifdef READ
        current->Ap_ref = av_buffer_alloc(current->Lp - 2);
        if (!current->Ap_ref)
            return AVERROR(ENOMEM);
        current->Ap = current->Ap_ref->data;
#endif

        for (i = 0; i < current->Lp - 2; i++)
            us(8, Ap[i], i, 0, 255);
    }

    return 0;
}

static int FUNC(comment)(CodedBitstreamContext *ctx, RWContext *rw,
                         JPEGRawComment *current)
{
    int err, i;

    HEADER("Comment");

    u(16, Lc, 2, 65535);

    if (current->Lc > 2) {
#ifdef READ
        current->Cm_ref = av_buffer_alloc(current->Lc - 2);
        if (!current->Cm_ref)
            return AVERROR(ENOMEM);
        current->Cm = current->Cm_ref->data;
#endif

        for (i = 0; i < current->Lc - 2; i++)
            us(8, Cm[i], i, 0, 255);
    }

    return 0;
}
