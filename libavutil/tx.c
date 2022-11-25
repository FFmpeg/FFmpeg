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

#include "avassert.h"
#include "intmath.h"
#include "cpu.h"
#include "qsort.h"
#include "bprint.h"

#include "tx_priv.h"

#define TYPE_IS(type, x)               \
    (((x) == AV_TX_FLOAT_ ## type)  || \
     ((x) == AV_TX_DOUBLE_ ## type) || \
     ((x) == AV_TX_INT32_ ## type))

/* Calculates the modular multiplicative inverse */
static av_always_inline int mulinv(int n, int m)
{
    n = n % m;
    for (int x = 1; x < m; x++)
        if (((n * x) % m) == 1)
            return x;
    av_assert0(0); /* Never reached */
    return 0;
}

int ff_tx_gen_pfa_input_map(AVTXContext *s, FFTXCodeletOptions *opts,
                            int d1, int d2)
{
    const int sl = d1*d2;

    s->map = av_malloc(s->len*sizeof(*s->map));
    if (!s->map)
        return AVERROR(ENOMEM);

    for (int k = 0; k < s->len; k += sl) {
        if (s->inv || (opts && opts->map_dir == FF_TX_MAP_SCATTER)) {
            for (int m = 0; m < d2; m++)
                for (int n = 0; n < d1; n++)
                    s->map[k + ((m*d1 + n*d2) % (sl))] = m*d1 + n;
        } else {
            for (int m = 0; m < d2; m++)
                for (int n = 0; n < d1; n++)
                    s->map[k + m*d1 + n] = (m*d1 + n*d2) % (sl);
        }

        if (s->inv)
            for (int w = 1; w <= ((sl) >> 1); w++)
                FFSWAP(int, s->map[k + w], s->map[k + sl - w]);
    }

    s->map_dir = opts ? opts->map_dir : FF_TX_MAP_GATHER;

    return 0;
}

/* Guaranteed to work for any n, m where gcd(n, m) == 1 */
int ff_tx_gen_compound_mapping(AVTXContext *s, FFTXCodeletOptions *opts,
                               int inv, int n, int m)
{
    int *in_map, *out_map;
    const int len = n*m;    /* Will not be equal to s->len for MDCTs */
    int m_inv, n_inv;

    /* Make sure the numbers are coprime */
    if (av_gcd(n, m) != 1)
        return AVERROR(EINVAL);

    m_inv = mulinv(m, n);
    n_inv = mulinv(n, m);

    if (!(s->map = av_malloc(2*len*sizeof(*s->map))))
        return AVERROR(ENOMEM);

    in_map  = s->map;
    out_map = s->map + len;

    /* Ruritanian map for input, CRT map for output, can be swapped */
    if (opts && opts->map_dir == FF_TX_MAP_SCATTER) {
        for (int j = 0; j < m; j++) {
            for (int i = 0; i < n; i++) {
                in_map[(i*m + j*n) % len] = j*n + i;
                out_map[(i*m*m_inv + j*n*n_inv) % len] = i*m + j;
            }
        }
    } else {
        for (int j = 0; j < m; j++) {
            for (int i = 0; i < n; i++) {
                in_map[j*n + i] = (i*m + j*n) % len;
                out_map[(i*m*m_inv + j*n*n_inv) % len] = i*m + j;
            }
        }
    }

    if (inv) {
        for (int i = 0; i < m; i++) {
            int *in = &in_map[i*n + 1]; /* Skip the DC */
            for (int j = 0; j < ((n - 1) >> 1); j++)
                FFSWAP(int, in[j], in[n - j - 2]);
        }
    }

    s->map_dir = opts ? opts->map_dir : FF_TX_MAP_GATHER;

    return 0;
}

static inline int split_radix_permutation(int i, int len, int inv)
{
    len >>= 1;
    if (len <= 1)
        return i & 1;
    if (!(i & len))
        return split_radix_permutation(i, len, inv) * 2;
    len >>= 1;
    return split_radix_permutation(i, len, inv) * 4 + 1 - 2*(!(i & len) ^ inv);
}

int ff_tx_gen_ptwo_revtab(AVTXContext *s, FFTXCodeletOptions *opts)
{
    int len = s->len;

    if (!(s->map = av_malloc(len*sizeof(*s->map))))
        return AVERROR(ENOMEM);

    if (opts && opts->map_dir == FF_TX_MAP_SCATTER) {
        for (int i = 0; i < s->len; i++)
            s->map[-split_radix_permutation(i, len, s->inv) & (len - 1)] = i;
    } else {
        for (int i = 0; i < s->len; i++)
            s->map[i] = -split_radix_permutation(i, len, s->inv) & (len - 1);
    }

    s->map_dir = opts ? opts->map_dir : FF_TX_MAP_GATHER;

    return 0;
}

int ff_tx_gen_inplace_map(AVTXContext *s, int len)
{
    int *src_map, out_map_idx = 0;

    if (!s->sub || !s->sub->map)
        return AVERROR(EINVAL);

    if (!(s->map = av_mallocz(len*sizeof(*s->map))))
        return AVERROR(ENOMEM);

    src_map = s->sub->map;

    /* The first coefficient is always already in-place */
    for (int src = 1; src < s->len; src++) {
        int dst = src_map[src];
        int found = 0;

        if (dst <= src)
            continue;

        /* This just checks if a closed loop has been encountered before,
         * and if so, skips it, since to fully permute a loop we must only
         * enter it once. */
        do {
            for (int j = 0; j < out_map_idx; j++) {
                if (dst == s->map[j]) {
                    found = 1;
                    break;
                }
            }
            dst = src_map[dst];
        } while (dst != src && !found);

        if (!found)
            s->map[out_map_idx++] = src;
    }

    s->map[out_map_idx++] = 0;

    return 0;
}

static void parity_revtab_generator(int *revtab, int n, int inv, int offset,
                                    int is_dual, int dual_high, int len,
                                    int basis, int dual_stride, int inv_lookup)
{
    len >>= 1;

    if (len <= basis) {
        int k1, k2, stride, even_idx, odd_idx;

        is_dual = is_dual && dual_stride;
        dual_high = is_dual & dual_high;
        stride = is_dual ? FFMIN(dual_stride, len) : 0;

        even_idx = offset + dual_high*(stride - 2*len);
        odd_idx  = even_idx + len + (is_dual && !dual_high)*len + dual_high*len;

        for (int i = 0; i < len; i++) {
            k1 = -split_radix_permutation(offset + i*2 + 0, n, inv) & (n - 1);
            k2 = -split_radix_permutation(offset + i*2 + 1, n, inv) & (n - 1);
            if (inv_lookup) {
                revtab[even_idx++] = k1;
                revtab[odd_idx++]  = k2;
            } else {
                revtab[k1] = even_idx++;
                revtab[k2] = odd_idx++;
            }
            if (stride && !((i + 1) % stride)) {
                even_idx += stride;
                odd_idx  += stride;
            }
        }

        return;
    }

    parity_revtab_generator(revtab, n, inv, offset,
                            0, 0, len >> 0, basis, dual_stride, inv_lookup);
    parity_revtab_generator(revtab, n, inv, offset + (len >> 0),
                            1, 0, len >> 1, basis, dual_stride, inv_lookup);
    parity_revtab_generator(revtab, n, inv, offset + (len >> 0) + (len >> 1),
                            1, 1, len >> 1, basis, dual_stride, inv_lookup);
}

int ff_tx_gen_split_radix_parity_revtab(AVTXContext *s, int len, int inv,
                                        FFTXCodeletOptions *opts,
                                        int basis, int dual_stride)
{
    basis >>= 1;
    if (len < basis)
        return AVERROR(EINVAL);

    if (!(s->map = av_mallocz(len*sizeof(*s->map))))
        return AVERROR(ENOMEM);

    av_assert0(!dual_stride || !(dual_stride & (dual_stride - 1)));
    av_assert0(dual_stride <= basis);

    parity_revtab_generator(s->map, len, inv, 0, 0, 0, len,
                            basis, dual_stride,
                            opts ? opts->map_dir == FF_TX_MAP_GATHER : FF_TX_MAP_GATHER);

    s->map_dir = opts ? opts->map_dir : FF_TX_MAP_GATHER;

    return 0;
}

static void reset_ctx(AVTXContext *s, int free_sub)
{
    if (!s)
        return;

    if (s->sub)
        for (int i = 0; i < TX_MAX_SUB; i++)
            reset_ctx(&s->sub[i], free_sub + 1);

    if (s->cd_self && s->cd_self->uninit)
        s->cd_self->uninit(s);

    if (free_sub)
        av_freep(&s->sub);

    av_freep(&s->map);
    av_freep(&s->exp);
    av_freep(&s->tmp);

    /* Nothing else needs to be reset, it gets overwritten if another
     * ff_tx_init_subtx() call is made. */
    s->nb_sub = 0;
    s->opaque = NULL;
    memset(s->fn, 0, sizeof(*s->fn));
}

void ff_tx_clear_ctx(AVTXContext *s)
{
    reset_ctx(s, 0);
}

av_cold void av_tx_uninit(AVTXContext **ctx)
{
    if (!(*ctx))
        return;

    reset_ctx(*ctx, 1);
    av_freep(ctx);
}

static av_cold int ff_tx_null_init(AVTXContext *s, const FFTXCodelet *cd,
                                   uint64_t flags, FFTXCodeletOptions *opts,
                                   int len, int inv, const void *scale)
{
    /* Can only handle one sample+type to one sample+type transforms */
    if (TYPE_IS(MDCT, s->type) || TYPE_IS(RDFT, s->type))
        return AVERROR(EINVAL);
    return 0;
}

/* Null transform when the length is 1 */
static void ff_tx_null(AVTXContext *s, void *_out, void *_in, ptrdiff_t stride)
{
    memcpy(_out, _in, stride);
}

static const FFTXCodelet ff_tx_null_def = {
    .name       = NULL_IF_CONFIG_SMALL("null"),
    .function   = ff_tx_null,
    .type       = TX_TYPE_ANY,
    .flags      = AV_TX_UNALIGNED | FF_TX_ALIGNED |
                  FF_TX_OUT_OF_PLACE | AV_TX_INPLACE,
    .factors[0] = TX_FACTOR_ANY,
    .min_len    = 1,
    .max_len    = 1,
    .init       = ff_tx_null_init,
    .cpu_flags  = FF_TX_CPU_FLAGS_ALL,
    .prio       = FF_TX_PRIO_MAX,
};

static const FFTXCodelet * const ff_tx_null_list[] = {
    &ff_tx_null_def,
    NULL,
};

/* Array of all compiled codelet lists. Order is irrelevant. */
static const FFTXCodelet * const * const codelet_list[] = {
    ff_tx_codelet_list_float_c,
    ff_tx_codelet_list_double_c,
    ff_tx_codelet_list_int32_c,
    ff_tx_null_list,
#if HAVE_X86ASM
    ff_tx_codelet_list_float_x86,
#endif
#if ARCH_AARCH64
    ff_tx_codelet_list_float_aarch64,
#endif
};
static const int codelet_list_num = FF_ARRAY_ELEMS(codelet_list);

static const int cpu_slow_mask = AV_CPU_FLAG_SSE2SLOW | AV_CPU_FLAG_SSE3SLOW  |
                                 AV_CPU_FLAG_ATOM     | AV_CPU_FLAG_SSSE3SLOW |
                                 AV_CPU_FLAG_AVXSLOW  | AV_CPU_FLAG_SLOW_GATHER;

static const int cpu_slow_penalties[][2] = {
    { AV_CPU_FLAG_SSE2SLOW,    1 + 64  },
    { AV_CPU_FLAG_SSE3SLOW,    1 + 64  },
    { AV_CPU_FLAG_SSSE3SLOW,   1 + 64  },
    { AV_CPU_FLAG_ATOM,        1 + 128 },
    { AV_CPU_FLAG_AVXSLOW,     1 + 128 },
    { AV_CPU_FLAG_SLOW_GATHER, 1 + 32  },
};

static int get_codelet_prio(const FFTXCodelet *cd, int cpu_flags, int len)
{
    int prio = cd->prio;
    int max_factor = 0;

    /* If the CPU has a SLOW flag, and the instruction is also flagged
     * as being slow for such, reduce its priority */
    for (int i = 0; i < FF_ARRAY_ELEMS(cpu_slow_penalties); i++) {
        if ((cpu_flags & cd->cpu_flags) & cpu_slow_penalties[i][0])
            prio -= cpu_slow_penalties[i][1];
    }

    /* Prioritize aligned-only codelets */
    if ((cd->flags & FF_TX_ALIGNED) && !(cd->flags & AV_TX_UNALIGNED))
        prio += 64;

    /* Codelets for specific lengths are generally faster */
    if ((len == cd->min_len) && (len == cd->max_len))
        prio += 64;

    /* Forward-only or inverse-only transforms are generally better */
    if ((cd->flags & (FF_TX_FORWARD_ONLY | FF_TX_INVERSE_ONLY)))
        prio += 64;

    /* Larger factors are generally better */
    for (int i = 0; i < TX_MAX_SUB; i++)
        max_factor = FFMAX(cd->factors[i], max_factor);
    if (max_factor)
        prio += 16*max_factor;

    return prio;
}

typedef struct FFTXLenDecomp {
    int len;
    int len2;
    int prio;
    const FFTXCodelet *cd;
} FFTXLenDecomp;

static int cmp_decomp(FFTXLenDecomp *a, FFTXLenDecomp *b)
{
    return FFDIFFSIGN(b->prio, a->prio);
}

int ff_tx_decompose_length(int dst[TX_MAX_DECOMPOSITIONS], enum AVTXType type,
                           int len, int inv)
{
    int nb_decomp = 0;
    FFTXLenDecomp ld[TX_MAX_DECOMPOSITIONS];
    int codelet_list_idx = codelet_list_num;

    const int cpu_flags = av_get_cpu_flags();

    /* Loop through all codelets in all codelet lists to find matches
     * to the requirements */
    while (codelet_list_idx--) {
        const FFTXCodelet * const * list = codelet_list[codelet_list_idx];
        const FFTXCodelet *cd = NULL;

        while ((cd = *list++)) {
            int fl = len;
            int skip = 0, prio;
            int factors_product = 1, factors_mod = 0;

            if (nb_decomp >= TX_MAX_DECOMPOSITIONS)
                goto sort;

            /* Check if the type matches */
            if (cd->type != TX_TYPE_ANY && type != cd->type)
                continue;

            /* Check direction for non-orthogonal codelets */
            if (((cd->flags & FF_TX_FORWARD_ONLY) && inv) ||
                ((cd->flags & (FF_TX_INVERSE_ONLY | AV_TX_FULL_IMDCT)) && !inv))
                continue;

            /* Check if the CPU supports the required ISA */
            if (cd->cpu_flags != FF_TX_CPU_FLAGS_ALL &&
                !(cpu_flags & (cd->cpu_flags & ~cpu_slow_mask)))
                continue;

            for (int i = 0; i < TX_MAX_FACTORS; i++) {
                if (!cd->factors[i] || (fl == 1))
                    break;

                if (cd->factors[i] == TX_FACTOR_ANY) {
                    factors_mod++;
                    factors_product *= fl;
                } else if (!(fl % cd->factors[i])) {
                    factors_mod++;
                    if (cd->factors[i] == 2) {
                        int b = ff_ctz(fl);
                        fl >>= b;
                        factors_product <<= b;
                    } else {
                        do {
                            fl /= cd->factors[i];
                            factors_product *= cd->factors[i];
                        } while (!(fl % cd->factors[i]));
                    }
                }
            }

            /* Disqualify if factor requirements are not satisfied or if trivial */
            if ((factors_mod < cd->nb_factors) || (len == factors_product))
                continue;

            if (av_gcd(factors_product, fl) != 1)
                continue;

            /* Check if length is supported and factorization was successful */
            if ((factors_product < cd->min_len) ||
                (cd->max_len != TX_LEN_UNLIMITED && (factors_product > cd->max_len)))
                continue;

            prio = get_codelet_prio(cd, cpu_flags, factors_product) * factors_product;

            /* Check for duplicates */
            for (int i = 0; i < nb_decomp; i++) {
                if (factors_product == ld[i].len) {
                    /* Update priority if new one is higher */
                    if (prio > ld[i].prio)
                        ld[i].prio = prio;
                    skip = 1;
                    break;
                }
            }

            /* Add decomposition if unique */
            if (!skip) {
                ld[nb_decomp].cd = cd;
                ld[nb_decomp].len = factors_product;
                ld[nb_decomp].len2 = fl;
                ld[nb_decomp].prio = prio;
                nb_decomp++;
            }
        }
    }

    if (!nb_decomp)
        return AVERROR(EINVAL);

sort:
    AV_QSORT(ld, nb_decomp, FFTXLenDecomp, cmp_decomp);

    for (int i = 0; i < nb_decomp; i++) {
        if (ld[i].cd->nb_factors > 1)
            dst[i] = ld[i].len2;
        else
            dst[i] = ld[i].len;
    }

    return nb_decomp;
}

int ff_tx_gen_default_map(AVTXContext *s, FFTXCodeletOptions *opts)
{
    s->map = av_malloc(s->len*sizeof(*s->map));
    if (!s->map)
        return AVERROR(ENOMEM);

    s->map[0] = 0; /* DC is always at the start */
    if (s->inv) /* Reversing the ACs flips the transform direction */
        for (int i = 1; i < s->len; i++)
            s->map[i] = s->len - i;
    else
        for (int i = 1; i < s->len; i++)
            s->map[i] = i;

    s->map_dir = FF_TX_MAP_GATHER;

    return 0;
}

#if !CONFIG_SMALL
static void print_flags(AVBPrint *bp, uint64_t f)
{
    int prev = 0;
    const char *sep = ", ";
    av_bprintf(bp, "flags: [");
    if ((f & FF_TX_ALIGNED) && ++prev)
        av_bprintf(bp, "aligned");
    if ((f & AV_TX_UNALIGNED) && ++prev)
        av_bprintf(bp, "%sunaligned", prev > 1 ? sep : "");
    if ((f & AV_TX_INPLACE) && ++prev)
        av_bprintf(bp, "%sinplace", prev > 1 ? sep : "");
    if ((f & FF_TX_OUT_OF_PLACE) && ++prev)
        av_bprintf(bp, "%sout_of_place", prev > 1 ? sep : "");
    if ((f & FF_TX_FORWARD_ONLY) && ++prev)
        av_bprintf(bp, "%sfwd_only", prev > 1 ? sep : "");
    if ((f & FF_TX_INVERSE_ONLY) && ++prev)
        av_bprintf(bp, "%sinv_only", prev > 1 ? sep : "");
    if ((f & FF_TX_PRESHUFFLE) && ++prev)
        av_bprintf(bp, "%spreshuf", prev > 1 ? sep : "");
    if ((f & AV_TX_FULL_IMDCT) && ++prev)
        av_bprintf(bp, "%simdct_full", prev > 1 ? sep : "");
    if ((f & FF_TX_ASM_CALL) && ++prev)
        av_bprintf(bp, "%sasm_call", prev > 1 ? sep : "");
    av_bprintf(bp, "]");
}

static void print_type(AVBPrint *bp, enum AVTXType type)
{
    av_bprintf(bp, "%s",
               type == TX_TYPE_ANY       ? "any"         :
               type == AV_TX_FLOAT_FFT   ? "fft_float"   :
               type == AV_TX_FLOAT_MDCT  ? "mdct_float"  :
               type == AV_TX_FLOAT_RDFT  ? "rdft_float"  :
               type == AV_TX_DOUBLE_FFT  ? "fft_double"  :
               type == AV_TX_DOUBLE_MDCT ? "mdct_double" :
               type == AV_TX_DOUBLE_RDFT ? "rdft_double" :
               type == AV_TX_INT32_FFT   ? "fft_int32"   :
               type == AV_TX_INT32_MDCT  ? "mdct_int32"  :
               type == AV_TX_INT32_RDFT  ? "rdft_int32"  :
               "unknown");
}

static void print_cd_info(const FFTXCodelet *cd, int prio, int len, int print_prio)
{
    AVBPrint bp = { 0 };
    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);

    av_bprintf(&bp, "%s - type: ", cd->name);

    print_type(&bp, cd->type);

    av_bprintf(&bp, ", len: ");
    if (!len) {
        if (cd->min_len != cd->max_len)
            av_bprintf(&bp, "[%i, ", cd->min_len);

        if (cd->max_len == TX_LEN_UNLIMITED)
            av_bprintf(&bp, "∞");
        else
            av_bprintf(&bp, "%i", cd->max_len);
    } else {
        av_bprintf(&bp, "%i", len);
    }

    if (cd->factors[1]) {
        av_bprintf(&bp, "%s, factors", !len && cd->min_len != cd->max_len ? "]" : "");
        if (!cd->nb_factors)
            av_bprintf(&bp, ": [");
        else
            av_bprintf(&bp, "[%i]: [", cd->nb_factors);

        for (int i = 0; i < TX_MAX_FACTORS; i++) {
            if (i && cd->factors[i])
                av_bprintf(&bp, ", ");
            if (cd->factors[i] == TX_FACTOR_ANY)
                av_bprintf(&bp, "any");
            else if (cd->factors[i])
                av_bprintf(&bp, "%i", cd->factors[i]);
            else
                break;
        }

        av_bprintf(&bp, "], ");
    } else {
        av_bprintf(&bp, "%s, factor: %i, ",
                   !len && cd->min_len != cd->max_len ? "]" : "", cd->factors[0]);
    }
    print_flags(&bp, cd->flags);

    if (print_prio)
        av_bprintf(&bp, ", prio: %i", prio);

    av_log(NULL, AV_LOG_DEBUG, "%s\n", bp.str);
}

static void print_tx_structure(AVTXContext *s, int depth)
{
    const FFTXCodelet *cd = s->cd_self;

    for (int i = 0; i <= depth; i++)
        av_log(NULL, AV_LOG_DEBUG, "    ");

    print_cd_info(cd, cd->prio, s->len, 0);

    for (int i = 0; i < s->nb_sub; i++)
        print_tx_structure(&s->sub[i], depth + 1);
}
#endif /* CONFIG_SMALL */

typedef struct TXCodeletMatch {
    const FFTXCodelet *cd;
    int prio;
} TXCodeletMatch;

static int cmp_matches(TXCodeletMatch *a, TXCodeletMatch *b)
{
    return FFDIFFSIGN(b->prio, a->prio);
}

/* We want all factors to completely cover the length */
static inline int check_cd_factors(const FFTXCodelet *cd, int len)
{
    int matches = 0, any_flag = 0;

    for (int i = 0; i < TX_MAX_FACTORS; i++) {
        int factor = cd->factors[i];

        if (factor == TX_FACTOR_ANY) {
            any_flag = 1;
            matches++;
            continue;
        } else if (len <= 1 || !factor) {
            break;
        } else if (factor == 2) { /* Fast path */
            int bits_2 = ff_ctz(len);
            if (!bits_2)
                continue; /* Factor not supported */

            len >>= bits_2;
            matches++;
        } else {
            int res = len % factor;
            if (res)
                continue; /* Factor not supported */

            while (!res) {
                len /= factor;
                res = len % factor;
            }
            matches++;
        }
    }

    return (cd->nb_factors <= matches) && (any_flag || len == 1);
}

av_cold int ff_tx_init_subtx(AVTXContext *s, enum AVTXType type,
                             uint64_t flags, FFTXCodeletOptions *opts,
                             int len, int inv, const void *scale)
{
    int ret = 0;
    AVTXContext *sub = NULL;
    TXCodeletMatch *cd_tmp, *cd_matches = NULL;
    unsigned int cd_matches_size = 0;
    int codelet_list_idx = codelet_list_num;
    int nb_cd_matches = 0;
#if !CONFIG_SMALL
    AVBPrint bp = { 0 };
#endif

    /* We still accept functions marked with SLOW, even if the CPU is
     * marked with the same flag, but we give them lower priority. */
    const int cpu_flags = av_get_cpu_flags();

    /* Flags the transform wants */
    uint64_t req_flags = flags;

    /* Flags the codelet may require to be present */
    uint64_t inv_req_mask = AV_TX_FULL_IMDCT | FF_TX_PRESHUFFLE | FF_TX_ASM_CALL;

    /* Unaligned codelets are compatible with the aligned flag */
    if (req_flags & FF_TX_ALIGNED)
        req_flags |= AV_TX_UNALIGNED;

    /* If either flag is set, both are okay, so don't check for an exact match */
    if ((req_flags & AV_TX_INPLACE) && (req_flags & FF_TX_OUT_OF_PLACE))
        req_flags &= ~(AV_TX_INPLACE | FF_TX_OUT_OF_PLACE);
    if ((req_flags & FF_TX_ALIGNED) && (req_flags & AV_TX_UNALIGNED))
        req_flags &= ~(FF_TX_ALIGNED | AV_TX_UNALIGNED);

    /* Loop through all codelets in all codelet lists to find matches
     * to the requirements */
    while (codelet_list_idx--) {
        const FFTXCodelet * const * list = codelet_list[codelet_list_idx];
        const FFTXCodelet *cd = NULL;

        while ((cd = *list++)) {
            /* Check if the type matches */
            if (cd->type != TX_TYPE_ANY && type != cd->type)
                continue;

            /* Check direction for non-orthogonal codelets */
            if (((cd->flags & FF_TX_FORWARD_ONLY) && inv) ||
                ((cd->flags & (FF_TX_INVERSE_ONLY | AV_TX_FULL_IMDCT)) && !inv))
                continue;

            /* Check if the requested flags match from both sides */
            if (((req_flags    & cd->flags) != (req_flags)) ||
                ((inv_req_mask & cd->flags) != (req_flags & inv_req_mask)))
                continue;

            /* Check if length is supported */
            if ((len < cd->min_len) || (cd->max_len != -1 && (len > cd->max_len)))
                continue;

            /* Check if the CPU supports the required ISA */
            if (cd->cpu_flags != FF_TX_CPU_FLAGS_ALL &&
                !(cpu_flags & (cd->cpu_flags & ~cpu_slow_mask)))
                continue;

            /* Check for factors */
            if (!check_cd_factors(cd, len))
                continue;

            /* Realloc array and append */
            cd_tmp = av_fast_realloc(cd_matches, &cd_matches_size,
                                     sizeof(*cd_tmp) * (nb_cd_matches + 1));
            if (!cd_tmp) {
                av_free(cd_matches);
                return AVERROR(ENOMEM);
            }

            cd_matches                     = cd_tmp;
            cd_matches[nb_cd_matches].cd   = cd;
            cd_matches[nb_cd_matches].prio = get_codelet_prio(cd, cpu_flags, len);
            nb_cd_matches++;
        }
    }

#if !CONFIG_SMALL
    /* Print debugging info */
    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&bp, "For transform of length %i, %s, ", len,
               inv ? "inverse" : "forward");
    print_type(&bp, type);
    av_bprintf(&bp, ", ");
    print_flags(&bp, flags);
    av_bprintf(&bp, ", found %i matches%s", nb_cd_matches,
               nb_cd_matches ? ":" : ".");
#endif

    /* No matches found */
    if (!nb_cd_matches)
        return AVERROR(ENOSYS);

    /* Sort the list */
    AV_QSORT(cd_matches, nb_cd_matches, TXCodeletMatch, cmp_matches);

#if !CONFIG_SMALL
    av_log(NULL, AV_LOG_DEBUG, "%s\n", bp.str);

    for (int i = 0; i < nb_cd_matches; i++) {
        av_log(NULL, AV_LOG_DEBUG, "    %i: ", i + 1);
        print_cd_info(cd_matches[i].cd, cd_matches[i].prio, 0, 1);
    }
#endif

    if (!s->sub) {
        s->sub = sub = av_mallocz(TX_MAX_SUB*sizeof(*sub));
        if (!sub) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
    }

    /* Attempt to initialize each */
    for (int i = 0; i < nb_cd_matches; i++) {
        const FFTXCodelet *cd = cd_matches[i].cd;
        AVTXContext *sctx = &s->sub[s->nb_sub];

        sctx->len        = len;
        sctx->inv        = inv;
        sctx->type       = type;
        sctx->flags      = cd->flags | flags;
        sctx->cd_self    = cd;

        s->fn[s->nb_sub] = cd->function;
        s->cd[s->nb_sub] = cd;

        ret = 0;
        if (cd->init)
            ret = cd->init(sctx, cd, flags, opts, len, inv, scale);

        if (ret >= 0) {
            if (opts && opts->map_dir != FF_TX_MAP_NONE &&
                sctx->map_dir == FF_TX_MAP_NONE) {
                /* If a specific map direction was requested, and it doesn't
                 * exist, create one.*/
                sctx->map = av_malloc(len*sizeof(*sctx->map));
                if (!sctx->map) {
                    ret = AVERROR(ENOMEM);
                    goto end;
                }

                for (int i = 0; i < len; i++)
                    sctx->map[i] = i;
            } else if (opts && (opts->map_dir != sctx->map_dir)) {
                int *tmp = av_malloc(len*sizeof(*sctx->map));
                if (!tmp) {
                    ret = AVERROR(ENOMEM);
                    goto end;
                }

                memcpy(tmp, sctx->map, len*sizeof(*sctx->map));

                for (int i = 0; i < len; i++)
                    sctx->map[tmp[i]] = i;

                av_free(tmp);
            }

            s->nb_sub++;
            goto end;
        }

        s->fn[s->nb_sub] = NULL;
        s->cd[s->nb_sub] = NULL;

        reset_ctx(sctx, 0);
        if (ret == AVERROR(ENOMEM))
            break;
    }

    if (!s->nb_sub)
        av_freep(&s->sub);

end:
    av_free(cd_matches);
    return ret;
}

av_cold int av_tx_init(AVTXContext **ctx, av_tx_fn *tx, enum AVTXType type,
                       int inv, int len, const void *scale, uint64_t flags)
{
    int ret;
    AVTXContext tmp = { 0 };
    const double default_scale_d = 1.0;
    const float  default_scale_f = 1.0f;

    if (!len || type >= AV_TX_NB || !ctx || !tx)
        return AVERROR(EINVAL);

    if (!(flags & AV_TX_UNALIGNED))
        flags |= FF_TX_ALIGNED;
    if (!(flags & AV_TX_INPLACE))
        flags |= FF_TX_OUT_OF_PLACE;

    if (!scale && ((type == AV_TX_FLOAT_MDCT) || (type == AV_TX_INT32_MDCT)))
        scale = &default_scale_f;
    else if (!scale && (type == AV_TX_DOUBLE_MDCT))
        scale = &default_scale_d;

    ret = ff_tx_init_subtx(&tmp, type, flags, NULL, len, inv, scale);
    if (ret < 0)
        return ret;

    *ctx = &tmp.sub[0];
    *tx  = tmp.fn[0];

#if !CONFIG_SMALL
    av_log(NULL, AV_LOG_DEBUG, "Transform tree:\n");
    print_tx_structure(*ctx, 0);
#endif

    return ret;
}
