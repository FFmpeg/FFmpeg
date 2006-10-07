/*
 * iWMMXt optimized DSP utils
 * copyright (c) 2004 AGAWA Koji
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

void DEF(put, pixels8)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    int stride = line_size;
    __asm__ __volatile__ (
        "and r12, %[pixels], #7 \n\t"
        "bic %[pixels], %[pixels], #7 \n\t"
        "tmcr wcgr1, r12 \n\t"
        "add r4, %[pixels], %[line_size] \n\t"
        "add r5, %[block], %[line_size] \n\t"
        "mov %[line_size], %[line_size], lsl #1 \n\t"
        "1: \n\t"
        "wldrd wr0, [%[pixels]] \n\t"
        "subs %[h], %[h], #2 \n\t"
        "wldrd wr1, [%[pixels], #8] \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "wldrd wr3, [r4] \n\t"
        "pld [%[pixels]] \n\t"
        "pld [%[pixels], #32] \n\t"
        "wldrd wr4, [r4, #8] \n\t"
        "add r4, r4, %[line_size] \n\t"
        "walignr1 wr8, wr0, wr1 \n\t"
        "pld [r4] \n\t"
        "pld [r4, #32] \n\t"
        "walignr1 wr10, wr3, wr4 \n\t"
        "wstrd wr8, [%[block]] \n\t"
        "add %[block], %[block], %[line_size] \n\t"
        "wstrd wr10, [r5] \n\t"
        "add r5, r5, %[line_size] \n\t"
        "bne 1b \n\t"
        : [block]"+r"(block), [pixels]"+r"(pixels), [line_size]"+r"(stride), [h]"+r"(h)
        :
        : "memory", "r4", "r5", "r12");
}

void DEF(avg, pixels8)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    int stride = line_size;
    __asm__ __volatile__ (
        "and r12, %[pixels], #7 \n\t"
        "bic %[pixels], %[pixels], #7 \n\t"
        "tmcr wcgr1, r12 \n\t"
        "add r4, %[pixels], %[line_size] \n\t"
        "add r5, %[block], %[line_size] \n\t"
        "mov %[line_size], %[line_size], lsl #1 \n\t"
        "1: \n\t"
        "wldrd wr0, [%[pixels]] \n\t"
        "subs %[h], %[h], #2 \n\t"
        "wldrd wr1, [%[pixels], #8] \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "wldrd wr3, [r4] \n\t"
        "pld [%[pixels]] \n\t"
        "pld [%[pixels], #32] \n\t"
        "wldrd wr4, [r4, #8] \n\t"
        "add r4, r4, %[line_size] \n\t"
        "walignr1 wr8, wr0, wr1 \n\t"
        "wldrd wr0, [%[block]] \n\t"
        "wldrd wr2, [r5] \n\t"
        "pld [r4] \n\t"
        "pld [r4, #32] \n\t"
        "walignr1 wr10, wr3, wr4 \n\t"
        WAVG2B" wr8, wr8, wr0 \n\t"
        WAVG2B" wr10, wr10, wr2 \n\t"
        "wstrd wr8, [%[block]] \n\t"
        "add %[block], %[block], %[line_size] \n\t"
        "wstrd wr10, [r5] \n\t"
        "pld [%[block]] \n\t"
        "pld [%[block], #32] \n\t"
        "add r5, r5, %[line_size] \n\t"
        "pld [r5] \n\t"
        "pld [r5, #32] \n\t"
        "bne 1b \n\t"
        : [block]"+r"(block), [pixels]"+r"(pixels), [line_size]"+r"(stride), [h]"+r"(h)
        :
        : "memory", "r4", "r5", "r12");
}

void DEF(put, pixels16)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    int stride = line_size;
    __asm__ __volatile__ (
        "and r12, %[pixels], #7 \n\t"
        "bic %[pixels], %[pixels], #7 \n\t"
        "tmcr wcgr1, r12 \n\t"
        "add r4, %[pixels], %[line_size] \n\t"
        "add r5, %[block], %[line_size] \n\t"
        "mov %[line_size], %[line_size], lsl #1 \n\t"
        "1: \n\t"
        "wldrd wr0, [%[pixels]] \n\t"
        "wldrd wr1, [%[pixels], #8] \n\t"
        "subs %[h], %[h], #2 \n\t"
        "wldrd wr2, [%[pixels], #16] \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "wldrd wr3, [r4] \n\t"
        "pld [%[pixels]] \n\t"
        "pld [%[pixels], #32] \n\t"
        "walignr1 wr8, wr0, wr1 \n\t"
        "wldrd wr4, [r4, #8] \n\t"
        "walignr1 wr9, wr1, wr2 \n\t"
        "wldrd wr5, [r4, #16] \n\t"
        "add r4, r4, %[line_size] \n\t"
        "pld [r4] \n\t"
        "pld [r4, #32] \n\t"
        "walignr1 wr10, wr3, wr4 \n\t"
        "wstrd wr8, [%[block]] \n\t"
        "walignr1 wr11, wr4, wr5 \n\t"
        "wstrd wr9, [%[block], #8] \n\t"
        "add %[block], %[block], %[line_size] \n\t"
        "wstrd wr10, [r5] \n\t"
        "wstrd wr11, [r5, #8] \n\t"
        "add r5, r5, %[line_size] \n\t"
        "bne 1b \n\t"
        : [block]"+r"(block), [pixels]"+r"(pixels), [line_size]"+r"(stride), [h]"+r"(h)
        :
        : "memory", "r4", "r5", "r12");
}

void DEF(avg, pixels16)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    int stride = line_size;
    __asm__ __volatile__ (
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "pld [%[block]]                 \n\t"
        "pld [%[block], #32]            \n\t"
        "and r12, %[pixels], #7         \n\t"
        "bic %[pixels], %[pixels], #7   \n\t"
        "tmcr wcgr1, r12                \n\t"
        "add r4, %[pixels], %[line_size]\n\t"
        "add r5, %[block], %[line_size] \n\t"
        "mov %[line_size], %[line_size], lsl #1 \n\t"
        "1:                             \n\t"
        "wldrd wr0, [%[pixels]]         \n\t"
        "wldrd wr1, [%[pixels], #8]     \n\t"
        "subs %[h], %[h], #2            \n\t"
        "wldrd wr2, [%[pixels], #16]    \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "wldrd wr3, [r4]                \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr8, wr0, wr1         \n\t"
        "wldrd wr4, [r4, #8]            \n\t"
        "walignr1 wr9, wr1, wr2         \n\t"
        "wldrd wr5, [r4, #16]           \n\t"
        "add r4, r4, %[line_size]       \n\t"
        "wldrd wr0, [%[block]]          \n\t"
        "pld [r4]                       \n\t"
        "wldrd wr1, [%[block], #8]      \n\t"
        "pld [r4, #32]                  \n\t"
        "wldrd wr2, [r5]                \n\t"
        "walignr1 wr10, wr3, wr4        \n\t"
        "wldrd wr3, [r5, #8]            \n\t"
        WAVG2B" wr8, wr8, wr0           \n\t"
        WAVG2B" wr9, wr9, wr1           \n\t"
        WAVG2B" wr10, wr10, wr2         \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "walignr1 wr11, wr4, wr5        \n\t"
        WAVG2B" wr11, wr11, wr3         \n\t"
        "wstrd wr9, [%[block], #8]      \n\t"
        "add %[block], %[block], %[line_size] \n\t"
        "wstrd wr10, [r5]               \n\t"
        "pld [%[block]]                 \n\t"
        "pld [%[block], #32]            \n\t"
        "wstrd wr11, [r5, #8]           \n\t"
        "add r5, r5, %[line_size]       \n\t"
        "pld [r5]                       \n\t"
        "pld [r5, #32]                  \n\t"
        "bne 1b \n\t"
        : [block]"+r"(block), [pixels]"+r"(pixels), [line_size]"+r"(stride), [h]"+r"(h)
        :
        : "memory", "r4", "r5", "r12");
}

void DEF(put, pixels8_x2)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    int stride = line_size;
    // [wr0 wr1 wr2 wr3] for previous line
    // [wr4 wr5 wr6 wr7] for current line
    SET_RND(wr15); // =2 for rnd  and  =1 for no_rnd version
    __asm__ __volatile__(
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "and r12, %[pixels], #7         \n\t"
        "bic %[pixels], %[pixels], #7   \n\t"
        "tmcr wcgr1, r12                \n\t"
        "add r12, r12, #1               \n\t"
        "add r4, %[pixels], %[line_size]\n\t"
        "tmcr wcgr2, r12                \n\t"
        "add r5, %[block], %[line_size] \n\t"
        "mov %[line_size], %[line_size], lsl #1 \n\t"

        "1:                             \n\t"
        "wldrd wr10, [%[pixels]]        \n\t"
        "cmp r12, #8                    \n\t"
        "wldrd wr11, [%[pixels], #8]    \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "wldrd wr13, [r4]               \n\t"
        "pld [%[pixels]]                \n\t"
        "wldrd wr14, [r4, #8]           \n\t"
        "pld [%[pixels], #32]           \n\t"
        "add r4, r4, %[line_size]       \n\t"
        "walignr1 wr0, wr10, wr11       \n\t"
        "pld [r4]                       \n\t"
        "pld [r4, #32]                  \n\t"
        "walignr1 wr2, wr13, wr14       \n\t"
        "wmoveq wr4, wr11               \n\t"
        "wmoveq wr6, wr14               \n\t"
        "walignr2ne wr4, wr10, wr11     \n\t"
        "walignr2ne wr6, wr13, wr14     \n\t"
        WAVG2B" wr0, wr0, wr4           \n\t"
        WAVG2B" wr2, wr2, wr6           \n\t"
        "wstrd wr0, [%[block]]          \n\t"
        "subs %[h], %[h], #2            \n\t"
        "wstrd wr2, [r5]                \n\t"
        "add %[block], %[block], %[line_size]   \n\t"
        "add r5, r5, %[line_size]       \n\t"
        "bne 1b                         \n\t"
        : [h]"+r"(h), [pixels]"+r"(pixels), [block]"+r"(block), [line_size]"+r"(stride)
        :
        : "r4", "r5", "r12", "memory");
}

void DEF(put, pixels16_x2)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    int stride = line_size;
    // [wr0 wr1 wr2 wr3] for previous line
    // [wr4 wr5 wr6 wr7] for current line
    SET_RND(wr15); // =2 for rnd  and  =1 for no_rnd version
    __asm__ __volatile__(
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "and r12, %[pixels], #7         \n\t"
        "bic %[pixels], %[pixels], #7   \n\t"
        "tmcr wcgr1, r12                \n\t"
        "add r12, r12, #1               \n\t"
        "add r4, %[pixels], %[line_size]\n\t"
        "tmcr wcgr2, r12                \n\t"
        "add r5, %[block], %[line_size] \n\t"
        "mov %[line_size], %[line_size], lsl #1 \n\t"

        "1:                             \n\t"
        "wldrd wr10, [%[pixels]]        \n\t"
        "cmp r12, #8                    \n\t"
        "wldrd wr11, [%[pixels], #8]    \n\t"
        "wldrd wr12, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "wldrd wr13, [r4]               \n\t"
        "pld [%[pixels]]                \n\t"
        "wldrd wr14, [r4, #8]           \n\t"
        "pld [%[pixels], #32]           \n\t"
        "wldrd wr15, [r4, #16]          \n\t"
        "add r4, r4, %[line_size]       \n\t"
        "walignr1 wr0, wr10, wr11       \n\t"
        "pld [r4]                       \n\t"
        "pld [r4, #32]                  \n\t"
        "walignr1 wr1, wr11, wr12       \n\t"
        "walignr1 wr2, wr13, wr14       \n\t"
        "walignr1 wr3, wr14, wr15       \n\t"
        "wmoveq wr4, wr11               \n\t"
        "wmoveq wr5, wr12               \n\t"
        "wmoveq wr6, wr14               \n\t"
        "wmoveq wr7, wr15               \n\t"
        "walignr2ne wr4, wr10, wr11     \n\t"
        "walignr2ne wr5, wr11, wr12     \n\t"
        "walignr2ne wr6, wr13, wr14     \n\t"
        "walignr2ne wr7, wr14, wr15     \n\t"
        WAVG2B" wr0, wr0, wr4           \n\t"
        WAVG2B" wr1, wr1, wr5           \n\t"
        "wstrd wr0, [%[block]]          \n\t"
        WAVG2B" wr2, wr2, wr6           \n\t"
        "wstrd wr1, [%[block], #8]      \n\t"
        WAVG2B" wr3, wr3, wr7           \n\t"
        "add %[block], %[block], %[line_size]   \n\t"
        "wstrd wr2, [r5]                \n\t"
        "subs %[h], %[h], #2            \n\t"
        "wstrd wr3, [r5, #8]            \n\t"
        "add r5, r5, %[line_size]       \n\t"
        "bne 1b                         \n\t"
        : [h]"+r"(h), [pixels]"+r"(pixels), [block]"+r"(block), [line_size]"+r"(stride)
        :
        : "r4", "r5", "r12", "memory");
}

void DEF(avg, pixels8_x2)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    int stride = line_size;
    // [wr0 wr1 wr2 wr3] for previous line
    // [wr4 wr5 wr6 wr7] for current line
    SET_RND(wr15); // =2 for rnd  and  =1 for no_rnd version
    __asm__ __volatile__(
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "pld [%[block]]                 \n\t"
        "pld [%[block], #32]            \n\t"
        "and r12, %[pixels], #7         \n\t"
        "bic %[pixels], %[pixels], #7   \n\t"
        "tmcr wcgr1, r12                \n\t"
        "add r12, r12, #1               \n\t"
        "add r4, %[pixels], %[line_size]\n\t"
        "tmcr wcgr2, r12                \n\t"
        "add r5, %[block], %[line_size] \n\t"
        "mov %[line_size], %[line_size], lsl #1 \n\t"
        "pld [r5]                       \n\t"
        "pld [r5, #32]                  \n\t"

        "1:                             \n\t"
        "wldrd wr10, [%[pixels]]        \n\t"
        "cmp r12, #8                    \n\t"
        "wldrd wr11, [%[pixels], #8]    \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "wldrd wr13, [r4]               \n\t"
        "pld [%[pixels]]                \n\t"
        "wldrd wr14, [r4, #8]           \n\t"
        "pld [%[pixels], #32]           \n\t"
        "add r4, r4, %[line_size]       \n\t"
        "walignr1 wr0, wr10, wr11       \n\t"
        "pld [r4]                       \n\t"
        "pld [r4, #32]                  \n\t"
        "walignr1 wr2, wr13, wr14       \n\t"
        "wmoveq wr4, wr11               \n\t"
        "wmoveq wr6, wr14               \n\t"
        "walignr2ne wr4, wr10, wr11     \n\t"
        "wldrd wr10, [%[block]]         \n\t"
        "walignr2ne wr6, wr13, wr14     \n\t"
        "wldrd wr12, [r5]               \n\t"
        WAVG2B" wr0, wr0, wr4           \n\t"
        WAVG2B" wr2, wr2, wr6           \n\t"
        WAVG2B" wr0, wr0, wr10          \n\t"
        WAVG2B" wr2, wr2, wr12          \n\t"
        "wstrd wr0, [%[block]]          \n\t"
        "subs %[h], %[h], #2            \n\t"
        "wstrd wr2, [r5]                \n\t"
        "add %[block], %[block], %[line_size]   \n\t"
        "add r5, r5, %[line_size]       \n\t"
        "pld [%[block]]                 \n\t"
        "pld [%[block], #32]            \n\t"
        "pld [r5]                       \n\t"
        "pld [r5, #32]                  \n\t"
        "bne 1b                         \n\t"
        : [h]"+r"(h), [pixels]"+r"(pixels), [block]"+r"(block), [line_size]"+r"(stride)
        :
        : "r4", "r5", "r12", "memory");
}

void DEF(avg, pixels16_x2)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    int stride = line_size;
    // [wr0 wr1 wr2 wr3] for previous line
    // [wr4 wr5 wr6 wr7] for current line
    SET_RND(wr15); // =2 for rnd  and  =1 for no_rnd version
    __asm__ __volatile__(
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "pld [%[block]]                 \n\t"
        "pld [%[block], #32]            \n\t"
        "and r12, %[pixels], #7         \n\t"
        "bic %[pixels], %[pixels], #7   \n\t"
        "tmcr wcgr1, r12                \n\t"
        "add r12, r12, #1               \n\t"
        "add r4, %[pixels], %[line_size]\n\t"
        "tmcr wcgr2, r12                \n\t"
        "add r5, %[block], %[line_size] \n\t"
        "mov %[line_size], %[line_size], lsl #1 \n\t"
        "pld [r5]                       \n\t"
        "pld [r5, #32]                  \n\t"

        "1:                             \n\t"
        "wldrd wr10, [%[pixels]]        \n\t"
        "cmp r12, #8                    \n\t"
        "wldrd wr11, [%[pixels], #8]    \n\t"
        "wldrd wr12, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "wldrd wr13, [r4]               \n\t"
        "pld [%[pixels]]                \n\t"
        "wldrd wr14, [r4, #8]           \n\t"
        "pld [%[pixels], #32]           \n\t"
        "wldrd wr15, [r4, #16]          \n\t"
        "add r4, r4, %[line_size]       \n\t"
        "walignr1 wr0, wr10, wr11       \n\t"
        "pld [r4]                       \n\t"
        "pld [r4, #32]                  \n\t"
        "walignr1 wr1, wr11, wr12       \n\t"
        "walignr1 wr2, wr13, wr14       \n\t"
        "walignr1 wr3, wr14, wr15       \n\t"
        "wmoveq wr4, wr11               \n\t"
        "wmoveq wr5, wr12               \n\t"
        "wmoveq wr6, wr14               \n\t"
        "wmoveq wr7, wr15               \n\t"
        "walignr2ne wr4, wr10, wr11     \n\t"
        "walignr2ne wr5, wr11, wr12     \n\t"
        "walignr2ne wr6, wr13, wr14     \n\t"
        "walignr2ne wr7, wr14, wr15     \n\t"
        "wldrd wr10, [%[block]]         \n\t"
        WAVG2B" wr0, wr0, wr4           \n\t"
        "wldrd wr11, [%[block], #8]     \n\t"
        WAVG2B" wr1, wr1, wr5           \n\t"
        "wldrd wr12, [r5]               \n\t"
        WAVG2B" wr2, wr2, wr6           \n\t"
        "wldrd wr13, [r5, #8]           \n\t"
        WAVG2B" wr3, wr3, wr7           \n\t"
        WAVG2B" wr0, wr0, wr10          \n\t"
        WAVG2B" wr1, wr1, wr11          \n\t"
        WAVG2B" wr2, wr2, wr12          \n\t"
        WAVG2B" wr3, wr3, wr13          \n\t"
        "wstrd wr0, [%[block]]          \n\t"
        "subs %[h], %[h], #2            \n\t"
        "wstrd wr1, [%[block], #8]      \n\t"
        "add %[block], %[block], %[line_size]   \n\t"
        "wstrd wr2, [r5]                \n\t"
        "pld [%[block]]                 \n\t"
        "wstrd wr3, [r5, #8]            \n\t"
        "add r5, r5, %[line_size]       \n\t"
        "pld [%[block], #32]            \n\t"
        "pld [r5]                       \n\t"
        "pld [r5, #32]                  \n\t"
        "bne 1b                         \n\t"
        : [h]"+r"(h), [pixels]"+r"(pixels), [block]"+r"(block), [line_size]"+r"(stride)
        :
        :"r4", "r5", "r12", "memory");
}

void DEF(avg, pixels8_y2)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    int stride = line_size;
    // [wr0 wr1 wr2 wr3] for previous line
    // [wr4 wr5 wr6 wr7] for current line
    __asm__ __volatile__(
        "pld            [%[pixels]]                             \n\t"
        "pld            [%[pixels], #32]                        \n\t"
        "and            r12, %[pixels], #7                      \n\t"
        "tmcr           wcgr1, r12                              \n\t"
        "bic            %[pixels], %[pixels], #7                \n\t"

        "wldrd          wr10, [%[pixels]]                       \n\t"
        "wldrd          wr11, [%[pixels], #8]                   \n\t"
        "pld            [%[block]]                              \n\t"
        "add            %[pixels], %[pixels], %[line_size]      \n\t"
        "walignr1       wr0, wr10, wr11                         \n\t"
        "pld            [%[pixels]]                             \n\t"
        "pld            [%[pixels], #32]                        \n\t"

      "1:                                                       \n\t"
        "wldrd          wr10, [%[pixels]]                       \n\t"
        "wldrd          wr11, [%[pixels], #8]                   \n\t"
        "add            %[pixels], %[pixels], %[line_size]      \n\t"
        "pld            [%[pixels]]                             \n\t"
        "pld            [%[pixels], #32]                        \n\t"
        "walignr1       wr4, wr10, wr11                         \n\t"
        "wldrd          wr10, [%[block]]                        \n\t"
         WAVG2B"        wr8, wr0, wr4                           \n\t"
         WAVG2B"        wr8, wr8, wr10                          \n\t"
        "wstrd          wr8, [%[block]]                         \n\t"
        "add            %[block], %[block], %[line_size]        \n\t"

        "wldrd          wr10, [%[pixels]]                       \n\t"
        "wldrd          wr11, [%[pixels], #8]                   \n\t"
        "pld            [%[block]]                              \n\t"
        "add            %[pixels], %[pixels], %[line_size]      \n\t"
        "pld            [%[pixels]]                             \n\t"
        "pld            [%[pixels], #32]                        \n\t"
        "walignr1       wr0, wr10, wr11                         \n\t"
        "wldrd          wr10, [%[block]]                        \n\t"
         WAVG2B"        wr8, wr0, wr4                           \n\t"
         WAVG2B"        wr8, wr8, wr10                          \n\t"
        "wstrd          wr8, [%[block]]                         \n\t"
        "add            %[block], %[block], %[line_size]        \n\t"

        "subs           %[h], %[h], #2                          \n\t"
        "pld            [%[block]]                              \n\t"
        "bne            1b                                      \n\t"
        : [h]"+r"(h), [pixels]"+r"(pixels), [block]"+r"(block), [line_size]"+r"(stride)
        :
        : "cc", "memory", "r12");
}

void DEF(put, pixels16_y2)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    int stride = line_size;
    // [wr0 wr1 wr2 wr3] for previous line
    // [wr4 wr5 wr6 wr7] for current line
    __asm__ __volatile__(
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "and r12, %[pixels], #7         \n\t"
        "tmcr wcgr1, r12                \n\t"
        "bic %[pixels], %[pixels], #7   \n\t"

        "wldrd wr10, [%[pixels]]        \n\t"
        "wldrd wr11, [%[pixels], #8]    \n\t"
        "wldrd wr12, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr0, wr10, wr11       \n\t"
        "walignr1 wr1, wr11, wr12       \n\t"

        "1:                             \n\t"
        "wldrd wr10, [%[pixels]]        \n\t"
        "wldrd wr11, [%[pixels], #8]    \n\t"
        "wldrd wr12, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr4, wr10, wr11       \n\t"
        "walignr1 wr5, wr11, wr12       \n\t"
        WAVG2B" wr8, wr0, wr4           \n\t"
        WAVG2B" wr9, wr1, wr5           \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "wstrd wr9, [%[block], #8]      \n\t"
        "add %[block], %[block], %[line_size]   \n\t"

        "wldrd wr10, [%[pixels]]        \n\t"
        "wldrd wr11, [%[pixels], #8]    \n\t"
        "wldrd wr12, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr0, wr10, wr11       \n\t"
        "walignr1 wr1, wr11, wr12       \n\t"
        WAVG2B" wr8, wr0, wr4           \n\t"
        WAVG2B" wr9, wr1, wr5           \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "wstrd wr9, [%[block], #8]      \n\t"
        "add %[block], %[block], %[line_size]   \n\t"

        "subs %[h], %[h], #2            \n\t"
        "bne 1b                         \n\t"
        : [h]"+r"(h), [pixels]"+r"(pixels), [block]"+r"(block), [line_size]"+r"(stride)
        :
        : "r4", "r5", "r12", "memory");
}

void DEF(avg, pixels16_y2)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    int stride = line_size;
    // [wr0 wr1 wr2 wr3] for previous line
    // [wr4 wr5 wr6 wr7] for current line
    __asm__ __volatile__(
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "and r12, %[pixels], #7         \n\t"
        "tmcr wcgr1, r12                \n\t"
        "bic %[pixels], %[pixels], #7   \n\t"

        "wldrd wr10, [%[pixels]]        \n\t"
        "wldrd wr11, [%[pixels], #8]    \n\t"
        "pld [%[block]]                 \n\t"
        "wldrd wr12, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr0, wr10, wr11       \n\t"
        "walignr1 wr1, wr11, wr12       \n\t"

        "1:                             \n\t"
        "wldrd wr10, [%[pixels]]        \n\t"
        "wldrd wr11, [%[pixels], #8]    \n\t"
        "wldrd wr12, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr4, wr10, wr11       \n\t"
        "walignr1 wr5, wr11, wr12       \n\t"
        "wldrd wr10, [%[block]]         \n\t"
        "wldrd wr11, [%[block], #8]     \n\t"
        WAVG2B" wr8, wr0, wr4           \n\t"
        WAVG2B" wr9, wr1, wr5           \n\t"
        WAVG2B" wr8, wr8, wr10          \n\t"
        WAVG2B" wr9, wr9, wr11          \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "wstrd wr9, [%[block], #8]      \n\t"
        "add %[block], %[block], %[line_size]   \n\t"

        "wldrd wr10, [%[pixels]]        \n\t"
        "wldrd wr11, [%[pixels], #8]    \n\t"
        "pld [%[block]]                 \n\t"
        "wldrd wr12, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr0, wr10, wr11       \n\t"
        "walignr1 wr1, wr11, wr12       \n\t"
        "wldrd wr10, [%[block]]         \n\t"
        "wldrd wr11, [%[block], #8]     \n\t"
        WAVG2B" wr8, wr0, wr4           \n\t"
        WAVG2B" wr9, wr1, wr5           \n\t"
        WAVG2B" wr8, wr8, wr10          \n\t"
        WAVG2B" wr9, wr9, wr11          \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "wstrd wr9, [%[block], #8]      \n\t"
        "add %[block], %[block], %[line_size]   \n\t"

        "subs %[h], %[h], #2            \n\t"
        "pld [%[block]]                 \n\t"
        "bne 1b                         \n\t"
        : [h]"+r"(h), [pixels]"+r"(pixels), [block]"+r"(block), [line_size]"+r"(stride)
        :
        : "r4", "r5", "r12", "memory");
}

void DEF(put, pixels8_xy2)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    // [wr0 wr1 wr2 wr3] for previous line
    // [wr4 wr5 wr6 wr7] for current line
    SET_RND(wr15); // =2 for rnd  and  =1 for no_rnd version
    __asm__ __volatile__(
        "pld [%[pixels]]                \n\t"
        "mov r12, #2                    \n\t"
        "pld [%[pixels], #32]           \n\t"
        "tmcr wcgr0, r12                \n\t" /* for shift value */
        "and r12, %[pixels], #7         \n\t"
        "bic %[pixels], %[pixels], #7   \n\t"
        "tmcr wcgr1, r12                \n\t"

        // [wr0 wr1 wr2 wr3] <= *
        // [wr4 wr5 wr6 wr7]
        "wldrd wr12, [%[pixels]]        \n\t"
        "add r12, r12, #1               \n\t"
        "wldrd wr13, [%[pixels], #8]    \n\t"
        "tmcr wcgr2, r12                \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "cmp r12, #8                    \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr2, wr12, wr13       \n\t"
        "wmoveq wr10, wr13              \n\t"
        "walignr2ne wr10, wr12, wr13    \n\t"
        "wunpckelub wr0, wr2            \n\t"
        "wunpckehub wr1, wr2            \n\t"
        "wunpckelub wr8, wr10           \n\t"
        "wunpckehub wr9, wr10           \n\t"
        "waddhus wr0, wr0, wr8          \n\t"
        "waddhus wr1, wr1, wr9          \n\t"

        "1:                             \n\t"
        // [wr0 wr1 wr2 wr3]
        // [wr4 wr5 wr6 wr7] <= *
        "wldrd wr12, [%[pixels]]        \n\t"
        "cmp r12, #8                    \n\t"
        "wldrd wr13, [%[pixels], #8]    \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "walignr1 wr6, wr12, wr13       \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "wmoveq wr10, wr13              \n\t"
        "walignr2ne wr10, wr12, wr13    \n\t"
        "wunpckelub wr4, wr6            \n\t"
        "wunpckehub wr5, wr6            \n\t"
        "wunpckelub wr8, wr10           \n\t"
        "wunpckehub wr9, wr10           \n\t"
        "waddhus wr4, wr4, wr8          \n\t"
        "waddhus wr5, wr5, wr9          \n\t"
        "waddhus wr8, wr0, wr4          \n\t"
        "waddhus wr9, wr1, wr5          \n\t"
        "waddhus wr8, wr8, wr15         \n\t"
        "waddhus wr9, wr9, wr15         \n\t"
        "wsrlhg wr8, wr8, wcgr0         \n\t"
        "wsrlhg wr9, wr9, wcgr0         \n\t"
        "wpackhus wr8, wr8, wr9         \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "add %[block], %[block], %[line_size]   \n\t"

        // [wr0 wr1 wr2 wr3] <= *
        // [wr4 wr5 wr6 wr7]
        "wldrd wr12, [%[pixels]]        \n\t"
        "wldrd wr13, [%[pixels], #8]    \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "walignr1 wr2, wr12, wr13       \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "wmoveq wr10, wr13              \n\t"
        "walignr2ne wr10, wr12, wr13    \n\t"
        "wunpckelub wr0, wr2            \n\t"
        "wunpckehub wr1, wr2            \n\t"
        "wunpckelub wr8, wr10           \n\t"
        "wunpckehub wr9, wr10           \n\t"
        "waddhus wr0, wr0, wr8          \n\t"
        "waddhus wr1, wr1, wr9          \n\t"
        "waddhus wr8, wr0, wr4          \n\t"
        "waddhus wr9, wr1, wr5          \n\t"
        "waddhus wr8, wr8, wr15         \n\t"
        "waddhus wr9, wr9, wr15         \n\t"
        "wsrlhg wr8, wr8, wcgr0         \n\t"
        "wsrlhg wr9, wr9, wcgr0         \n\t"
        "wpackhus wr8, wr8, wr9         \n\t"
        "subs %[h], %[h], #2            \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "add %[block], %[block], %[line_size]   \n\t"
        "bne 1b                         \n\t"
        : [h]"+r"(h), [pixels]"+r"(pixels), [block]"+r"(block)
        : [line_size]"r"(line_size)
        : "r12", "memory");
}

void DEF(put, pixels16_xy2)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    // [wr0 wr1 wr2 wr3] for previous line
    // [wr4 wr5 wr6 wr7] for current line
    SET_RND(wr15); // =2 for rnd  and  =1 for no_rnd version
    __asm__ __volatile__(
        "pld [%[pixels]]                \n\t"
        "mov r12, #2                    \n\t"
        "pld [%[pixels], #32]           \n\t"
        "tmcr wcgr0, r12                \n\t" /* for shift value */
        /* alignment */
        "and r12, %[pixels], #7         \n\t"
        "bic %[pixels], %[pixels], #7   \n\t"
        "tmcr wcgr1, r12                \n\t"
        "add r12, r12, #1               \n\t"
        "tmcr wcgr2, r12                \n\t"

        // [wr0 wr1 wr2 wr3] <= *
        // [wr4 wr5 wr6 wr7]
        "wldrd wr12, [%[pixels]]        \n\t"
        "cmp r12, #8                    \n\t"
        "wldrd wr13, [%[pixels], #8]    \n\t"
        "wldrd wr14, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "pld [%[pixels]]                \n\t"
        "walignr1 wr2, wr12, wr13       \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr3, wr13, wr14       \n\t"
        "wmoveq wr10, wr13              \n\t"
        "wmoveq wr11, wr14              \n\t"
        "walignr2ne wr10, wr12, wr13    \n\t"
        "walignr2ne wr11, wr13, wr14    \n\t"
        "wunpckelub wr0, wr2            \n\t"
        "wunpckehub wr1, wr2            \n\t"
        "wunpckelub wr2, wr3            \n\t"
        "wunpckehub wr3, wr3            \n\t"
        "wunpckelub wr8, wr10           \n\t"
        "wunpckehub wr9, wr10           \n\t"
        "wunpckelub wr10, wr11          \n\t"
        "wunpckehub wr11, wr11          \n\t"
        "waddhus wr0, wr0, wr8          \n\t"
        "waddhus wr1, wr1, wr9          \n\t"
        "waddhus wr2, wr2, wr10         \n\t"
        "waddhus wr3, wr3, wr11         \n\t"

        "1:                             \n\t"
        // [wr0 wr1 wr2 wr3]
        // [wr4 wr5 wr6 wr7] <= *
        "wldrd wr12, [%[pixels]]        \n\t"
        "cmp r12, #8                    \n\t"
        "wldrd wr13, [%[pixels], #8]    \n\t"
        "wldrd wr14, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "walignr1 wr6, wr12, wr13       \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr7, wr13, wr14       \n\t"
        "wmoveq wr10, wr13              \n\t"
        "wmoveq wr11, wr14              \n\t"
        "walignr2ne wr10, wr12, wr13    \n\t"
        "walignr2ne wr11, wr13, wr14    \n\t"
        "wunpckelub wr4, wr6            \n\t"
        "wunpckehub wr5, wr6            \n\t"
        "wunpckelub wr6, wr7            \n\t"
        "wunpckehub wr7, wr7            \n\t"
        "wunpckelub wr8, wr10           \n\t"
        "wunpckehub wr9, wr10           \n\t"
        "wunpckelub wr10, wr11          \n\t"
        "wunpckehub wr11, wr11          \n\t"
        "waddhus wr4, wr4, wr8          \n\t"
        "waddhus wr5, wr5, wr9          \n\t"
        "waddhus wr6, wr6, wr10         \n\t"
        "waddhus wr7, wr7, wr11         \n\t"
        "waddhus wr8, wr0, wr4          \n\t"
        "waddhus wr9, wr1, wr5          \n\t"
        "waddhus wr10, wr2, wr6         \n\t"
        "waddhus wr11, wr3, wr7         \n\t"
        "waddhus wr8, wr8, wr15         \n\t"
        "waddhus wr9, wr9, wr15         \n\t"
        "waddhus wr10, wr10, wr15       \n\t"
        "waddhus wr11, wr11, wr15       \n\t"
        "wsrlhg wr8, wr8, wcgr0         \n\t"
        "wsrlhg wr9, wr9, wcgr0         \n\t"
        "wsrlhg wr10, wr10, wcgr0       \n\t"
        "wsrlhg wr11, wr11, wcgr0       \n\t"
        "wpackhus wr8, wr8, wr9         \n\t"
        "wpackhus wr9, wr10, wr11       \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "wstrd wr9, [%[block], #8]      \n\t"
        "add %[block], %[block], %[line_size]   \n\t"

        // [wr0 wr1 wr2 wr3] <= *
        // [wr4 wr5 wr6 wr7]
        "wldrd wr12, [%[pixels]]        \n\t"
        "wldrd wr13, [%[pixels], #8]    \n\t"
        "wldrd wr14, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "walignr1 wr2, wr12, wr13       \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr3, wr13, wr14       \n\t"
        "wmoveq wr10, wr13              \n\t"
        "wmoveq wr11, wr14              \n\t"
        "walignr2ne wr10, wr12, wr13    \n\t"
        "walignr2ne wr11, wr13, wr14    \n\t"
        "wunpckelub wr0, wr2            \n\t"
        "wunpckehub wr1, wr2            \n\t"
        "wunpckelub wr2, wr3            \n\t"
        "wunpckehub wr3, wr3            \n\t"
        "wunpckelub wr8, wr10           \n\t"
        "wunpckehub wr9, wr10           \n\t"
        "wunpckelub wr10, wr11          \n\t"
        "wunpckehub wr11, wr11          \n\t"
        "waddhus wr0, wr0, wr8          \n\t"
        "waddhus wr1, wr1, wr9          \n\t"
        "waddhus wr2, wr2, wr10         \n\t"
        "waddhus wr3, wr3, wr11         \n\t"
        "waddhus wr8, wr0, wr4          \n\t"
        "waddhus wr9, wr1, wr5          \n\t"
        "waddhus wr10, wr2, wr6         \n\t"
        "waddhus wr11, wr3, wr7         \n\t"
        "waddhus wr8, wr8, wr15         \n\t"
        "waddhus wr9, wr9, wr15         \n\t"
        "waddhus wr10, wr10, wr15       \n\t"
        "waddhus wr11, wr11, wr15       \n\t"
        "wsrlhg wr8, wr8, wcgr0         \n\t"
        "wsrlhg wr9, wr9, wcgr0         \n\t"
        "wsrlhg wr10, wr10, wcgr0       \n\t"
        "wsrlhg wr11, wr11, wcgr0       \n\t"
        "wpackhus wr8, wr8, wr9         \n\t"
        "wpackhus wr9, wr10, wr11       \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "wstrd wr9, [%[block], #8]      \n\t"
        "add %[block], %[block], %[line_size]   \n\t"

        "subs %[h], %[h], #2            \n\t"
        "bne 1b                         \n\t"
        : [h]"+r"(h), [pixels]"+r"(pixels), [block]"+r"(block)
        : [line_size]"r"(line_size)
        : "r12", "memory");
}

void DEF(avg, pixels8_xy2)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    // [wr0 wr1 wr2 wr3] for previous line
    // [wr4 wr5 wr6 wr7] for current line
    SET_RND(wr15); // =2 for rnd  and  =1 for no_rnd version
    __asm__ __volatile__(
        "pld [%[block]]                 \n\t"
        "pld [%[block], #32]            \n\t"
        "pld [%[pixels]]                \n\t"
        "mov r12, #2                    \n\t"
        "pld [%[pixels], #32]           \n\t"
        "tmcr wcgr0, r12                \n\t" /* for shift value */
        "and r12, %[pixels], #7         \n\t"
        "bic %[pixels], %[pixels], #7   \n\t"
        "tmcr wcgr1, r12                \n\t"

        // [wr0 wr1 wr2 wr3] <= *
        // [wr4 wr5 wr6 wr7]
        "wldrd wr12, [%[pixels]]        \n\t"
        "add r12, r12, #1               \n\t"
        "wldrd wr13, [%[pixels], #8]    \n\t"
        "tmcr wcgr2, r12                \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "cmp r12, #8                    \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr2, wr12, wr13       \n\t"
        "wmoveq wr10, wr13              \n\t"
        "walignr2ne wr10, wr12, wr13    \n\t"
        "wunpckelub wr0, wr2            \n\t"
        "wunpckehub wr1, wr2            \n\t"
        "wunpckelub wr8, wr10           \n\t"
        "wunpckehub wr9, wr10           \n\t"
        "waddhus wr0, wr0, wr8          \n\t"
        "waddhus wr1, wr1, wr9          \n\t"

        "1:                             \n\t"
        // [wr0 wr1 wr2 wr3]
        // [wr4 wr5 wr6 wr7] <= *
        "wldrd wr12, [%[pixels]]        \n\t"
        "cmp r12, #8                    \n\t"
        "wldrd wr13, [%[pixels], #8]    \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "walignr1 wr6, wr12, wr13       \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "wmoveq wr10, wr13              \n\t"
        "walignr2ne wr10, wr12, wr13    \n\t"
        "wunpckelub wr4, wr6            \n\t"
        "wunpckehub wr5, wr6            \n\t"
        "wunpckelub wr8, wr10           \n\t"
        "wunpckehub wr9, wr10           \n\t"
        "waddhus wr4, wr4, wr8          \n\t"
        "waddhus wr5, wr5, wr9          \n\t"
        "waddhus wr8, wr0, wr4          \n\t"
        "waddhus wr9, wr1, wr5          \n\t"
        "waddhus wr8, wr8, wr15         \n\t"
        "waddhus wr9, wr9, wr15         \n\t"
        "wldrd wr12, [%[block]]         \n\t"
        "wsrlhg wr8, wr8, wcgr0         \n\t"
        "wsrlhg wr9, wr9, wcgr0         \n\t"
        "wpackhus wr8, wr8, wr9         \n\t"
        WAVG2B" wr8, wr8, wr12          \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "add %[block], %[block], %[line_size]   \n\t"
        "wldrd wr12, [%[pixels]]        \n\t"
        "pld [%[block]]                 \n\t"
        "pld [%[block], #32]            \n\t"

        // [wr0 wr1 wr2 wr3] <= *
        // [wr4 wr5 wr6 wr7]
        "wldrd wr13, [%[pixels], #8]    \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "walignr1 wr2, wr12, wr13       \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "wmoveq wr10, wr13              \n\t"
        "walignr2ne wr10, wr12, wr13    \n\t"
        "wunpckelub wr0, wr2            \n\t"
        "wunpckehub wr1, wr2            \n\t"
        "wunpckelub wr8, wr10           \n\t"
        "wunpckehub wr9, wr10           \n\t"
        "waddhus wr0, wr0, wr8          \n\t"
        "waddhus wr1, wr1, wr9          \n\t"
        "waddhus wr8, wr0, wr4          \n\t"
        "waddhus wr9, wr1, wr5          \n\t"
        "waddhus wr8, wr8, wr15         \n\t"
        "waddhus wr9, wr9, wr15         \n\t"
        "wldrd wr12, [%[block]]         \n\t"
        "wsrlhg wr8, wr8, wcgr0         \n\t"
        "wsrlhg wr9, wr9, wcgr0         \n\t"
        "wpackhus wr8, wr8, wr9         \n\t"
        "subs %[h], %[h], #2            \n\t"
        WAVG2B" wr8, wr8, wr12          \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "add %[block], %[block], %[line_size]   \n\t"
        "pld [%[block]]                 \n\t"
        "pld [%[block], #32]            \n\t"
        "bne 1b                         \n\t"
        : [h]"+r"(h), [pixels]"+r"(pixels), [block]"+r"(block)
        : [line_size]"r"(line_size)
        : "r12", "memory");
}

void DEF(avg, pixels16_xy2)(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    // [wr0 wr1 wr2 wr3] for previous line
    // [wr4 wr5 wr6 wr7] for current line
    SET_RND(wr15); // =2 for rnd  and  =1 for no_rnd version
    __asm__ __volatile__(
        "pld [%[block]]                 \n\t"
        "pld [%[block], #32]            \n\t"
        "pld [%[pixels]]                \n\t"
        "mov r12, #2                    \n\t"
        "pld [%[pixels], #32]           \n\t"
        "tmcr wcgr0, r12                \n\t" /* for shift value */
        /* alignment */
        "and r12, %[pixels], #7         \n\t"
        "bic %[pixels], %[pixels], #7           \n\t"
        "tmcr wcgr1, r12                \n\t"
        "add r12, r12, #1               \n\t"
        "tmcr wcgr2, r12                \n\t"

        // [wr0 wr1 wr2 wr3] <= *
        // [wr4 wr5 wr6 wr7]
        "wldrd wr12, [%[pixels]]        \n\t"
        "cmp r12, #8                    \n\t"
        "wldrd wr13, [%[pixels], #8]    \n\t"
        "wldrd wr14, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "pld [%[pixels]]                \n\t"
        "walignr1 wr2, wr12, wr13       \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr3, wr13, wr14       \n\t"
        "wmoveq wr10, wr13              \n\t"
        "wmoveq wr11, wr14              \n\t"
        "walignr2ne wr10, wr12, wr13    \n\t"
        "walignr2ne wr11, wr13, wr14    \n\t"
        "wunpckelub wr0, wr2            \n\t"
        "wunpckehub wr1, wr2            \n\t"
        "wunpckelub wr2, wr3            \n\t"
        "wunpckehub wr3, wr3            \n\t"
        "wunpckelub wr8, wr10           \n\t"
        "wunpckehub wr9, wr10           \n\t"
        "wunpckelub wr10, wr11          \n\t"
        "wunpckehub wr11, wr11          \n\t"
        "waddhus wr0, wr0, wr8          \n\t"
        "waddhus wr1, wr1, wr9          \n\t"
        "waddhus wr2, wr2, wr10         \n\t"
        "waddhus wr3, wr3, wr11         \n\t"

        "1:                             \n\t"
        // [wr0 wr1 wr2 wr3]
        // [wr4 wr5 wr6 wr7] <= *
        "wldrd wr12, [%[pixels]]        \n\t"
        "cmp r12, #8                    \n\t"
        "wldrd wr13, [%[pixels], #8]    \n\t"
        "wldrd wr14, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "walignr1 wr6, wr12, wr13       \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr7, wr13, wr14       \n\t"
        "wmoveq wr10, wr13              \n\t"
        "wmoveq wr11, wr14              \n\t"
        "walignr2ne wr10, wr12, wr13    \n\t"
        "walignr2ne wr11, wr13, wr14    \n\t"
        "wunpckelub wr4, wr6            \n\t"
        "wunpckehub wr5, wr6            \n\t"
        "wunpckelub wr6, wr7            \n\t"
        "wunpckehub wr7, wr7            \n\t"
        "wunpckelub wr8, wr10           \n\t"
        "wunpckehub wr9, wr10           \n\t"
        "wunpckelub wr10, wr11          \n\t"
        "wunpckehub wr11, wr11          \n\t"
        "waddhus wr4, wr4, wr8          \n\t"
        "waddhus wr5, wr5, wr9          \n\t"
        "waddhus wr6, wr6, wr10         \n\t"
        "waddhus wr7, wr7, wr11         \n\t"
        "waddhus wr8, wr0, wr4          \n\t"
        "waddhus wr9, wr1, wr5          \n\t"
        "waddhus wr10, wr2, wr6         \n\t"
        "waddhus wr11, wr3, wr7         \n\t"
        "waddhus wr8, wr8, wr15         \n\t"
        "waddhus wr9, wr9, wr15         \n\t"
        "waddhus wr10, wr10, wr15       \n\t"
        "waddhus wr11, wr11, wr15       \n\t"
        "wsrlhg wr8, wr8, wcgr0         \n\t"
        "wsrlhg wr9, wr9, wcgr0         \n\t"
        "wldrd wr12, [%[block]]         \n\t"
        "wldrd wr13, [%[block], #8]     \n\t"
        "wsrlhg wr10, wr10, wcgr0       \n\t"
        "wsrlhg wr11, wr11, wcgr0       \n\t"
        "wpackhus wr8, wr8, wr9         \n\t"
        "wpackhus wr9, wr10, wr11       \n\t"
        WAVG2B" wr8, wr8, wr12          \n\t"
        WAVG2B" wr9, wr9, wr13          \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "wstrd wr9, [%[block], #8]      \n\t"
        "add %[block], %[block], %[line_size]   \n\t"

        // [wr0 wr1 wr2 wr3] <= *
        // [wr4 wr5 wr6 wr7]
        "wldrd wr12, [%[pixels]]        \n\t"
        "pld [%[block]]                 \n\t"
        "wldrd wr13, [%[pixels], #8]    \n\t"
        "pld [%[block], #32]            \n\t"
        "wldrd wr14, [%[pixels], #16]   \n\t"
        "add %[pixels], %[pixels], %[line_size] \n\t"
        "walignr1 wr2, wr12, wr13       \n\t"
        "pld [%[pixels]]                \n\t"
        "pld [%[pixels], #32]           \n\t"
        "walignr1 wr3, wr13, wr14       \n\t"
        "wmoveq wr10, wr13              \n\t"
        "wmoveq wr11, wr14              \n\t"
        "walignr2ne wr10, wr12, wr13    \n\t"
        "walignr2ne wr11, wr13, wr14    \n\t"
        "wunpckelub wr0, wr2            \n\t"
        "wunpckehub wr1, wr2            \n\t"
        "wunpckelub wr2, wr3            \n\t"
        "wunpckehub wr3, wr3            \n\t"
        "wunpckelub wr8, wr10           \n\t"
        "wunpckehub wr9, wr10           \n\t"
        "wunpckelub wr10, wr11          \n\t"
        "wunpckehub wr11, wr11          \n\t"
        "waddhus wr0, wr0, wr8          \n\t"
        "waddhus wr1, wr1, wr9          \n\t"
        "waddhus wr2, wr2, wr10         \n\t"
        "waddhus wr3, wr3, wr11         \n\t"
        "waddhus wr8, wr0, wr4          \n\t"
        "waddhus wr9, wr1, wr5          \n\t"
        "waddhus wr10, wr2, wr6         \n\t"
        "waddhus wr11, wr3, wr7         \n\t"
        "waddhus wr8, wr8, wr15         \n\t"
        "waddhus wr9, wr9, wr15         \n\t"
        "waddhus wr10, wr10, wr15       \n\t"
        "waddhus wr11, wr11, wr15       \n\t"
        "wsrlhg wr8, wr8, wcgr0         \n\t"
        "wsrlhg wr9, wr9, wcgr0         \n\t"
        "wldrd wr12, [%[block]]         \n\t"
        "wldrd wr13, [%[block], #8]     \n\t"
        "wsrlhg wr10, wr10, wcgr0       \n\t"
        "wsrlhg wr11, wr11, wcgr0       \n\t"
        "wpackhus wr8, wr8, wr9         \n\t"
        "wpackhus wr9, wr10, wr11       \n\t"
        WAVG2B" wr8, wr8, wr12          \n\t"
        WAVG2B" wr9, wr9, wr13          \n\t"
        "wstrd wr8, [%[block]]          \n\t"
        "wstrd wr9, [%[block], #8]      \n\t"
        "add %[block], %[block], %[line_size]   \n\t"
        "subs %[h], %[h], #2            \n\t"
        "pld [%[block]]                 \n\t"
        "pld [%[block], #32]            \n\t"
        "bne 1b                         \n\t"
        : [h]"+r"(h), [pixels]"+r"(pixels), [block]"+r"(block)
        : [line_size]"r"(line_size)
        : "r12", "memory");
}
