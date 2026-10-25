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

#ifndef AVCODEC_AARCH64_APV_DSP_CONSTANTS_H
#define AVCODEC_AARCH64_APV_DSP_CONSTANTS_H

/* Offsets in the constants buffer used by transquant_neon. */
#define APV_CONST_QMATRIX        0
#define APV_CONST_QP_SHIFT       128
#define APV_CONST_DEQUANT_ROUND  132
#define APV_CONST_NEG_BD_SHIFT   136
#define APV_CONST_OUTPUT_ROUND   140
#define APV_CONST_OUTPUT_MAX     144
#define APV_CONST_OUTPUT_SHIFT   148

#endif /* AVCODEC_AARCH64_APV_DSP_CONSTANTS_H */
