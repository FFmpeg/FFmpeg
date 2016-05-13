/*
 * Copyright (c) 2015 Imagination Technologies Ltd
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

/**
 * @file
 * MIPS assembly defines from sys/asm.h but rewritten for use with C inline
 * assembly (rather than from within .s files).
 */

#ifndef AVUTIL_MIPS_ASMDEFS_H
#define AVUTIL_MIPS_ASMDEFS_H

#if defined(_ABI64) && _MIPS_SIM == _ABI64
# define mips_reg       int64_t
# define PTRSIZE        " 8 "
# define PTRLOG         " 3 "
# define PTR_ADDU       "daddu "
# define PTR_ADDIU      "daddiu "
# define PTR_ADDI       "daddi "
# define PTR_SUBU       "dsubu "
# define PTR_L          "ld "
# define PTR_S          "sd "
# define PTR_SRA        "dsra "
# define PTR_SRL        "dsrl "
# define PTR_SLL        "dsll "
#else
# define mips_reg       int32_t
# define PTRSIZE        " 4 "
# define PTRLOG         " 2 "
# define PTR_ADDU       "addu "
# define PTR_ADDIU      "addiu "
# define PTR_ADDI       "addi "
# define PTR_SUBU       "subu "
# define PTR_L          "lw "
# define PTR_S          "sw "
# define PTR_SRA        "sra "
# define PTR_SRL        "srl "
# define PTR_SLL        "sll "
#endif

#endif /* AVCODEC_MIPS_ASMDEFS_H */
