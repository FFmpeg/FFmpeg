/*
 * DES encryption/decryption
 * Copyright (c) 2007 Reimar Doeffinger
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

#ifndef FFMPEG_DES_H
#define FFMPEG_DES_H

#include <stdint.h>
#include "common.h"

/**
 * \brief en- or decrypt an 64-bit block of data with DES
 * \param in data to process.
 * \param key key to use for en-/decryption.
 * \param decrypt if 0 encrypt, else decrypt.
 * \return processed data
 *
 * If your input data is in 8-bit blocks treat it as big-endian
 * (use e.g. AV_RB64 and AV_WB64).
 */
uint64_t ff_des_encdec(uint64_t in, uint64_t key, int decrypt) av_const;

#endif /* FFMPEG_DES_H */
