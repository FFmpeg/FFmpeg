/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVRESAMPLE_ARM_ASM_OFFSETS_H
#define AVRESAMPLE_ARM_ASM_OFFSETS_H

/* struct ResampleContext */
#define FILTER_BANK                     0x08
#define FILTER_LENGTH                   0x0c
#define SRC_INCR                        0x20
#define PHASE_SHIFT                     0x28
#define PHASE_MASK                      (PHASE_SHIFT + 0x04)

#endif /* AVRESAMPLE_ARM_ASM_OFFSETS_H */
