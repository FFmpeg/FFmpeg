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

/* Guaranteed to work for any n, m where gcd(n, m) == 1 */
int ff_tx_gen_compound_mapping(AVTXContext *s, int n, int m)
{
    int *in_map, *out_map;
    const int inv = s->inv;
    const int len = n*m;    /* Will not be equal to s->len for MDCTs */
    const int mdct = TYPE_IS(MDCT, s->type);
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
    for (int j = 0; j < m; j++) {
        for (int i = 0; i < n; i++) {
            /* Shifted by 1 to simplify MDCTs */
            in_map[j*n + i] = ((i*m + j*n) % len) << mdct;
            out_map[(i*m*m_inv + j*n*n_inv) % len] = i*m + j;
        }
    }

    /* Change transform direction by reversing all ACs */
    if (inv) {
        for (int i = 0; i < m; i++) {
            int *in = &in_map[i*n + 1]; /* Skip the DC */
            for (int j = 0; j < ((n - 1) >> 1); j++)
                FFSWAP(int, in[j], in[n - j - 2]);
        }
    }

    /* Our 15-point transform is also a compound one, so embed its input map */
    if (n == 15) {
        for (int k = 0; k < m; k++) {
            int tmp[15];
            memcpy(tmp, &in_map[k*15], 15*sizeof(*tmp));
            for (int i = 0; i < 5; i++) {
                for (int j = 0; j < 3; j++)
                    in_map[k*15 + i*3 + j] = tmp[(i*3 + j*5) % 15];
            }
        }
    }

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

int ff_tx_gen_ptwo_revtab(AVTXContext *s, int invert_lookup)
{
    int len = s->len;

    if (!(s->map = av_malloc(len*sizeof(*s->map))))
        return AVERROR(ENOMEM);

    if (invert_lookup) {
        for (int i = 0; i < s->len; i++)
            s->map[i] = -split_radix_permutation(i, len, s->inv) & (len - 1);
    } else {
        for (int i = 0; i < s->len; i++)
            s->map[-split_radix_permutation(i, len, s->inv) & (len - 1)] = i;
    }

    return 0;
}

int ff_tx_gen_ptwo_inplace_revtab_idx(AVTXContext *s)
{
    int *src_map, out_map_idx = 0, len = s->len;

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

int ff_tx_gen_split_radix_parity_revtab(AVTXContext *s, int invert_lookup,
                                        int basis, int dual_stride)
{
    int len = s->len;
    int inv = s->inv;

    if (!(s->map = av_mallocz(len*sizeof(*s->map))))
        return AVERROR(ENOMEM);

    basis >>= 1;
    if (len < basis)
        return AVERROR(EINVAL);

    av_assert0(!dual_stride || !(dual_stride & (dual_stride - 1)));
    av_assert0(dual_stride <= basis);
    parity_revtab_generator(s->map, len, inv, 0, 0, 0, len,
                            basis, dual_stride, invert_lookup);

    return 0;
}

static void reset_ctx(AVTXContext *s)
{
    if (!s)
        return;

    if (s->sub)
        for (int i = 0; i < s->nb_sub; i++)
            reset_ctx(&s->sub[i]);

    if (s->cd_self->uninit)
        s->cd_self->uninit(s);

    av_freep(&s->sub);
    av_freep(&s->map);
    av_freep(&s->exp);
    av_freep(&s->tmp);

    memset(s, 0, sizeof(*s));
}

av_cold void av_tx_uninit(AVTXContext **ctx)
{
    if (!(*ctx))
        return;

    reset_ctx(*ctx);
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

static void print_cd_info(const FFTXCodelet *cd, int prio, int print_prio)
{
    AVBPrint bp = { 0 };
    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);

    av_bprintf(&bp, "%s - type: ", cd->name);

    print_type(&bp, cd->type);

    av_bprintf(&bp, ", len: ");
    if (cd->min_len != cd->max_len)
        av_bprintf(&bp, "[%i, ", cd->min_len);

    if (cd->max_len == TX_LEN_UNLIMITED)
        av_bprintf(&bp, "∞");
    else
        av_bprintf(&bp, "%i", cd->max_len);

    av_bprintf(&bp, "%s, factors: [", cd->min_len != cd->max_len ? "]" : "");
    for (int i = 0; i < TX_MAX_SUB; i++) {
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
    print_flags(&bp, cd->flags);

    if (print_prio)
        av_bprintf(&bp, ", prio: %i", prio);

    av_log(NULL, AV_LOG_VERBOSE, "%s\n", bp.str);
}

static void print_tx_structure(AVTXContext *s, int depth)
{
    const FFTXCodelet *cd = s->cd_self;

    for (int i = 0; i <= depth; i++)
        av_log(NULL, AV_LOG_VERBOSE, "    ");

    print_cd_info(cd, cd->prio, 0);

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
    int all_flag = 0;

    for (int i = 0; i < TX_MAX_SUB; i++) {
        int factor = cd->factors[i];

        /* Conditions satisfied */
        if (len == 1)
            return 1;

        /* No more factors */
        if (!factor) {
            break;
        } else if (factor == TX_FACTOR_ANY) {
            all_flag = 1;
            continue;
        }

        if (factor == 2) { /* Fast path */
            int bits_2 = ff_ctz(len);
            if (!bits_2)
                return 0; /* Factor not supported */

            len >>= bits_2;
        } else {
            int res = len % factor;
            if (res)
                return 0; /* Factor not supported */

            while (!res) {
                len /= factor;
                res = len % factor;
            }
        }
    }

    return all_flag || (len == 1);
}

av_cold int ff_tx_init_subtx(AVTXContext *s, enum AVTXType type,
                             uint64_t flags, FFTXCodeletOptions *opts,
                             int len, int inv, const void *scale)
{
    int ret = 0;
    AVTXContext *sub = NULL;
    TXCodeletMatch *cd_tmp, *cd_matches = NULL;
    unsigned int cd_matches_size = 0;
    int nb_cd_matches = 0;
#if !CONFIG_SMALL
    AVBPrint bp = { 0 };
#endif

    /* Array of all compiled codelet lists. Order is irrelevant. */
    const FFTXCodelet * const * const codelet_list[] = {
        ff_tx_codelet_list_float_c,
        ff_tx_codelet_list_double_c,
        ff_tx_codelet_list_int32_c,
        ff_tx_null_list,
#if HAVE_X86ASM
        ff_tx_codelet_list_float_x86,
#endif
    };
    int codelet_list_num = FF_ARRAY_ELEMS(codelet_list);

    /* We still accept functions marked with SLOW, even if the CPU is
     * marked with the same flag, but we give them lower priority. */
    const int cpu_flags = av_get_cpu_flags();
    const int slow_mask = AV_CPU_FLAG_SSE2SLOW | AV_CPU_FLAG_SSE3SLOW  |
                          AV_CPU_FLAG_ATOM     | AV_CPU_FLAG_SSSE3SLOW |
                          AV_CPU_FLAG_AVXSLOW  | AV_CPU_FLAG_SLOW_GATHER;

    static const int slow_penalties[][2] = {
        { AV_CPU_FLAG_SSE2SLOW,    1 + 64  },
        { AV_CPU_FLAG_SSE3SLOW,    1 + 64  },
        { AV_CPU_FLAG_SSSE3SLOW,   1 + 64  },
        { AV_CPU_FLAG_ATOM,        1 + 128 },
        { AV_CPU_FLAG_AVXSLOW,     1 + 128 },
        { AV_CPU_FLAG_SLOW_GATHER, 1 + 32  },
    };

    /* Flags the transform wants */
    uint64_t req_flags = flags;

    /* Flags the codelet may require to be present */
    uint64_t inv_req_mask = AV_TX_FULL_IMDCT | FF_TX_PRESHUFFLE;

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
    while (codelet_list_num--) {
        const FFTXCodelet * const * list = codelet_list[codelet_list_num];
        const FFTXCodelet *cd = NULL;

        while ((cd = *list++)) {
            int max_factor = 0;

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
                !(cpu_flags & (cd->cpu_flags & ~slow_mask)))
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
            cd_matches[nb_cd_matches].prio = cd->prio;

            /* If the CPU has a SLOW flag, and the instruction is also flagged
             * as being slow for such, reduce its priority */
            for (int i = 0; i < FF_ARRAY_ELEMS(slow_penalties); i++) {
                if ((cpu_flags & cd->cpu_flags) & slow_penalties[i][0])
                    cd_matches[nb_cd_matches].prio -= slow_penalties[i][1];
            }

            /* Prioritize aligned-only codelets */
            if ((cd->flags & FF_TX_ALIGNED) && !(cd->flags & AV_TX_UNALIGNED))
                cd_matches[nb_cd_matches].prio += 64;

            /* Codelets for specific lengths are generally faster */
            if ((len == cd->min_len) && (len == cd->max_len))
                cd_matches[nb_cd_matches].prio += 64;

            /* Forward-only or inverse-only transforms are generally better */
            if ((cd->flags & (FF_TX_FORWARD_ONLY | FF_TX_INVERSE_ONLY)))
                cd_matches[nb_cd_matches].prio += 64;

            /* Larger factors are generally better */
            for (int i = 0; i < TX_MAX_SUB; i++)
                max_factor = FFMAX(cd->factors[i], max_factor);
            if (max_factor)
                cd_matches[nb_cd_matches].prio += 16*max_factor;

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
    av_log(NULL, AV_LOG_VERBOSE, "%s\n", bp.str);

    for (int i = 0; i < nb_cd_matches; i++) {
        av_log(NULL, AV_LOG_VERBOSE, "    %i: ", i + 1);
        print_cd_info(cd_matches[i].cd, cd_matches[i].prio, 1);
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
        sctx->flags      = flags;
        sctx->cd_self    = cd;

        s->fn[s->nb_sub] = cd->function;
        s->cd[s->nb_sub] = cd;

        ret = 0;
        if (cd->init)
            ret = cd->init(sctx, cd, flags, opts, len, inv, scale);

        if (ret >= 0) {
            s->nb_sub++;
            goto end;
        }

        s->fn[s->nb_sub] = NULL;
        s->cd[s->nb_sub] = NULL;

        reset_ctx(sctx);
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
    av_log(NULL, AV_LOG_VERBOSE, "Transform tree:\n");
    print_tx_structure(*ctx, 0);
#endif

    return ret;
}
