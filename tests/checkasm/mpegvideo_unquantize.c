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

#include <assert.h>
#include <stddef.h>

#include "config.h"

#include "checkasm.h"

#include "libavcodec/idctdsp.h"
#include "libavcodec/mathops.h"
#include "libavcodec/mpegvideo.h"
#include "libavcodec/mpegvideodata.h"
#include "libavcodec/mpegvideo_unquantize.h"

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define randomize_struct(TYPE, s) do {                    \
    static_assert(!(_Alignof(TYPE) % 4),                  \
                  "can't use aligned stores");            \
    unsigned char *ptr = (unsigned char*)s;               \
    for (size_t i = 0; i < sizeof(*s) & ~3; i += 4)       \
        AV_WN32A(ptr + i, rnd());                         \
    for (size_t i = sizeof(*s) & ~3; i < sizeof(*s); ++i) \
        ptr[i] = rnd();                                   \
   } while (0)

enum TestType {
    H263,
    MPEG1,
    MPEG2,
};

static void init_idct_scantable(MPVContext *const s, int intra_scantable)
{
    static const enum idct_permutation_type permutation_types[] = {
        FF_IDCT_PERM_NONE,
        FF_IDCT_PERM_LIBMPEG2,
#if ARCH_X86_32 && HAVE_X86ASM
        FF_IDCT_PERM_SIMPLE,
#endif
#if ARCH_PPC || ARCH_X86
        FF_IDCT_PERM_TRANSPOSE,
#endif
#if ARCH_ARM || ARCH_AARCH64
        FF_IDCT_PERM_PARTTRANS,
#endif
#if ARCH_X86 && HAVE_X86ASM
        FF_IDCT_PERM_SSE2,
#endif
    };
    // Copied here to avoid #ifs.
    static const uint8_t ff_wmv1_scantable[][64] = {
    { 0x00, 0x08, 0x01, 0x02, 0x09, 0x10, 0x18, 0x11,
      0x0A, 0x03, 0x04, 0x0B, 0x12, 0x19, 0x20, 0x28,
      0x30, 0x38, 0x29, 0x21, 0x1A, 0x13, 0x0C, 0x05,
      0x06, 0x0D, 0x14, 0x1B, 0x22, 0x31, 0x39, 0x3A,
      0x32, 0x2A, 0x23, 0x1C, 0x15, 0x0E, 0x07, 0x0F,
      0x16, 0x1D, 0x24, 0x2B, 0x33, 0x3B, 0x3C, 0x34,
      0x2C, 0x25, 0x1E, 0x17, 0x1F, 0x26, 0x2D, 0x35,
      0x3D, 0x3E, 0x36, 0x2E, 0x27, 0x2F, 0x37, 0x3F, },
    { 0x00, 0x08, 0x01, 0x02, 0x09, 0x10, 0x18, 0x11,
      0x0A, 0x03, 0x04, 0x0B, 0x12, 0x19, 0x20, 0x28,
      0x21, 0x30, 0x1A, 0x13, 0x0C, 0x05, 0x06, 0x0D,
      0x14, 0x1B, 0x22, 0x29, 0x38, 0x31, 0x39, 0x2A,
      0x23, 0x1C, 0x15, 0x0E, 0x07, 0x0F, 0x16, 0x1D,
      0x24, 0x2B, 0x32, 0x3A, 0x33, 0x3B, 0x2C, 0x25,
      0x1E, 0x17, 0x1F, 0x26, 0x2D, 0x34, 0x3C, 0x35,
      0x3D, 0x2E, 0x27, 0x2F, 0x36, 0x3E, 0x37, 0x3F, },
    { 0x00, 0x01, 0x08, 0x02, 0x03, 0x09, 0x10, 0x18,
      0x11, 0x0A, 0x04, 0x05, 0x0B, 0x12, 0x19, 0x20,
      0x28, 0x30, 0x21, 0x1A, 0x13, 0x0C, 0x06, 0x07,
      0x0D, 0x14, 0x1B, 0x22, 0x29, 0x38, 0x31, 0x39,
      0x2A, 0x23, 0x1C, 0x15, 0x0E, 0x0F, 0x16, 0x1D,
      0x24, 0x2B, 0x32, 0x3A, 0x33, 0x2C, 0x25, 0x1E,
      0x17, 0x1F, 0x26, 0x2D, 0x34, 0x3B, 0x3C, 0x35,
      0x2E, 0x27, 0x2F, 0x36, 0x3D, 0x3E, 0x37, 0x3F, },
    { 0x00, 0x08, 0x10, 0x01, 0x18, 0x20, 0x28, 0x09,
      0x02, 0x03, 0x0A, 0x11, 0x19, 0x30, 0x38, 0x29,
      0x21, 0x1A, 0x12, 0x0B, 0x04, 0x05, 0x0C, 0x13,
      0x1B, 0x22, 0x31, 0x39, 0x32, 0x2A, 0x23, 0x1C,
      0x14, 0x0D, 0x06, 0x07, 0x0E, 0x15, 0x1D, 0x24,
      0x2B, 0x33, 0x3A, 0x3B, 0x34, 0x2C, 0x25, 0x1E,
      0x16, 0x0F, 0x17, 0x1F, 0x26, 0x2D, 0x3C, 0x35,
      0x2E, 0x27, 0x2F, 0x36, 0x3D, 0x3E, 0x37, 0x3F, }
    };

    static const uint8_t *const scantables[] = {
        ff_alternate_vertical_scan,
        ff_alternate_horizontal_scan,
        ff_zigzag_direct,
        ff_wmv1_scantable[0],
        ff_wmv1_scantable[1],
        ff_wmv1_scantable[2],
        ff_wmv1_scantable[3],
    };
    static const uint8_t *scantable = NULL;
    static enum idct_permutation_type idct_permutation;

    if (!scantable) {
        scantable        = scantables[rnd() % FF_ARRAY_ELEMS(scantables)];
        idct_permutation = permutation_types[rnd() % FF_ARRAY_ELEMS(permutation_types)];
    }
    ff_init_scantable_permutation(s->idsp.idct_permutation, idct_permutation);
    ff_init_scantable(s->idsp.idct_permutation,
                      intra_scantable ? &s->intra_scantable : &s->inter_scantable,
                      scantable);
}

static void init_h263_test(MPVContext *const s, int16_t block[64],
                           int last_nonzero_coeff, int qscale, int intra)
{
    const uint8_t *permutation = s->inter_scantable.permutated;
    if (intra) {
        permutation = s->intra_scantable.permutated;
        block[0]    = rnd() & 511;
        static int h263_aic = -1, ac_pred;
        if (h263_aic < 0) {
            h263_aic = rnd() & 1;
            ac_pred  = rnd() & 1;
        }
        s->h263_aic = h263_aic;
        s->ac_pred  = ac_pred;
        if (s->ac_pred)
            last_nonzero_coeff = 63;
    }
    for (int i = intra; i <= last_nonzero_coeff; ++i) {
        int random = rnd();
        if (random & 1)
            continue;
        random >>= 1;
        // Select level so that the multiplication fits into 16 bits.
        // FIXME: The FLV and MPEG-4 decoders can have escape values exceeding this.
        block[permutation[i]] = sign_extend(random, 10);
    }
}

static void init_mpeg12_test(MPVContext *const s, int16_t block[64],
                             int last_nonzero_coeff, int qscale, int intra,
                             enum TestType type)
{
    uint16_t *matrix = intra ? s->intra_matrix : s->inter_matrix;

    if (type == MPEG2)
        qscale = s->q_scale_type ? ff_mpeg2_non_linear_qscale[qscale] : qscale << 1;

    for (int i = 0; i < 64; ++i)
        matrix[i] = 1 + rnd() % 254;

    const uint8_t *permutation = s->intra_scantable.permutated;
    if (intra) {
        block[0] = (int8_t)rnd();
        for (int i = 1; i <= last_nonzero_coeff; ++i) {
            int j = permutation[i];
            unsigned random = rnd();
            if (random & 1)
                continue;
            random >>= 1;
            // Select level so that the multiplication does not overflow
            // an int16_t and so that it is within the possible range
            // (-2048..2047). FIXME: It seems that this need not be fulfilled
            // in practice for the MPEG-4 decoder at least.
            int limit = FFMIN(INT16_MAX / (qscale * matrix[j]), 2047);
            block[j] = random % (2 * limit + 1) - limit;
        }
    } else {
        for (int i = 0; i <= last_nonzero_coeff; ++i) {
            int j = permutation[i];
            unsigned random = rnd();
            if (random & 1)
                continue;
            random >>= 1;
            int limit = FFMIN((INT16_MAX / (qscale * matrix[j]) - 1) / 2, 2047);
            block[j] = random % (2 * limit + 1) - limit;
        }
    }
}

void checkasm_check_mpegvideo_unquantize(void)
{
    static const struct {
        const char *name;
        size_t offset;
        int intra, intra_scantable;
        enum TestType type;
    } tests[] = {
#define TEST(NAME, INTRA, INTRA_SCANTABLE, TYPE)                         \
    { .name = #NAME, .offset = offsetof(MPVUnquantDSPContext, NAME),     \
      .intra = INTRA, .intra_scantable = INTRA_SCANTABLE, .type = TYPE }
        TEST(dct_unquantize_mpeg1_intra, 1, 1, MPEG1),
        TEST(dct_unquantize_mpeg1_inter, 0, 1, MPEG1),
        TEST(dct_unquantize_mpeg2_intra, 1, 1, MPEG2),
        TEST(dct_unquantize_mpeg2_inter, 0, 1, MPEG2),
        TEST(dct_unquantize_h263_intra,  1, 1, H263),
        TEST(dct_unquantize_h263_inter,  0, 0, H263),
    };
    MPVUnquantDSPContext unquant_dsp_ctx;
    int q_scale_type = rnd() & 1;

    ff_mpv_unquantize_init(&unquant_dsp_ctx, 1 /* bitexact */, q_scale_type);
    declare_func(void, const MPVContext *s, int16_t *block, int n, int qscale);

    for (size_t i = 0; i < FF_ARRAY_ELEMS(tests); ++i) {
        void (*func)(const MPVContext *s, int16_t *block, int n, int qscale) =
            *(void (**)(const MPVContext *, int16_t *, int, int))((char*)&unquant_dsp_ctx + tests[i].offset);
        if (check_func(func, "%s", tests[i].name)) {
            MPVContext new, ref;
            DECLARE_ALIGNED(16, int16_t, block_new)[64];
            DECLARE_ALIGNED(16, int16_t, block_ref)[64];
            static int block_last_index = -1;

            randomize_struct(MPVContext, &ref);

            ref.q_scale_type = q_scale_type;

            init_idct_scantable(&ref, tests[i].intra_scantable);

            if (block_last_index < 0)
                block_last_index = rnd() % 64;

            memset(block_ref, 0, sizeof(block_ref));

            if (tests[i].intra) {
                // Less restricted than real dc_scale values
                ref.y_dc_scale = 1 + rnd() % 64;
                ref.c_dc_scale = 1 + rnd() % 64;
            }

            static int qscale = 0;

            if (qscale == 0)
                qscale = 1 + rnd() % 31;

            if (tests[i].type == H263)
                init_h263_test(&ref, block_ref, block_last_index, qscale,
                               tests[i].intra);
            else
                init_mpeg12_test(&ref, block_ref, block_last_index, qscale,
                                 tests[i].intra, tests[i].type);

            int n = rnd() % 6;
            ref.block_last_index[n] = block_last_index;

            memcpy(&new, &ref, sizeof(new));
            memcpy(block_new, block_ref, sizeof(block_new));

            call_ref(&ref, block_ref, n, qscale);
            call_new(&new, block_new, n, qscale);

            if (memcmp(&ref, &new, sizeof(new)) || memcmp(block_new, block_ref, sizeof(block_new)))
                fail();

            bench_new(&new, block_new, n, qscale);
        }
    }
}
