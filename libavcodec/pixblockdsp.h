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

#ifndef AVCODEC_PIXBLOCKDSP_H
#define AVCODEC_PIXBLOCKDSP_H

#include <stddef.h>
#include <stdint.h>

#define PIXBLOCKDSP_8BPP_GET_PIXELS_SUPPORTS_UNALIGNED \
    !(ARCH_ARM || ARCH_MIPS || ARCH_PPC || ARCH_RISCV)

typedef struct PixblockDSPContext {
    void (*get_pixels)(int16_t *restrict block /* align 16 */,
                       /* align 16 for > 8 bits; align 8 for <= 8 bits
                        * (or 1 if PIXBLOCKDSP_8BPP_GET_PIXELS_SUPPORTS_UNALIGNED is set) */
                       const uint8_t *pixels,
                       ptrdiff_t stride);
    void (*get_pixels_unaligned)(int16_t *restrict block /* align 16 */,
                       const uint8_t *pixels,
                       ptrdiff_t stride);
    void (*diff_pixels)(int16_t *restrict block /* align 16 */,
                        const uint8_t *s1 /* align 8 */,
                        const uint8_t *s2 /* align 8 */,
                        ptrdiff_t stride);
    void (*diff_pixels_unaligned)(int16_t *restrict block /* align 16 */,
                        const uint8_t *s1,
                        const uint8_t *s2,
                        ptrdiff_t stride);

} PixblockDSPContext;

void ff_pixblockdsp_init(PixblockDSPContext *c, int bits_per_raw_sample);
void ff_pixblockdsp_init_aarch64(PixblockDSPContext *c,
                                 unsigned high_bit_depth);
void ff_pixblockdsp_init_arm(PixblockDSPContext *c,
                             unsigned high_bit_depth);
void ff_pixblockdsp_init_ppc(PixblockDSPContext *c,
                             unsigned high_bit_depth);
void ff_pixblockdsp_init_riscv(PixblockDSPContext *c,
                               unsigned high_bit_depth);
void ff_pixblockdsp_init_x86(PixblockDSPContext *c,
                             unsigned high_bit_depth);
void ff_pixblockdsp_init_mips(PixblockDSPContext *c,
                              unsigned high_bit_depth);

#endif /* AVCODEC_PIXBLOCKDSP_H */
