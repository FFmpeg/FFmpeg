/*
 * MetaSound decoder
 * Copyright (c) 2013 Konstantin Shishkov
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

#ifndef AVCODEC_METASOUND_DATA_H
#define AVCODEC_METASOUND_DATA_H

#include <stddef.h>
#include <stdint.h>

#include "twinvq.h"

extern const TwinVQModeTab ff_metasound_mode0806;
extern const TwinVQModeTab ff_metasound_mode0806s;
extern const TwinVQModeTab ff_metasound_mode0808;
extern const TwinVQModeTab ff_metasound_mode0808s;
extern const TwinVQModeTab ff_metasound_mode1110;
extern const TwinVQModeTab ff_metasound_mode1110s;
extern const TwinVQModeTab ff_metasound_mode1616;
extern const TwinVQModeTab ff_metasound_mode1616s;
extern const TwinVQModeTab ff_metasound_mode2224;
extern const TwinVQModeTab ff_metasound_mode2224s;
extern const TwinVQModeTab ff_metasound_mode2232;
extern const TwinVQModeTab ff_metasound_mode2232s;
extern const TwinVQModeTab ff_metasound_mode4432;
extern const TwinVQModeTab ff_metasound_mode4432s;
extern const TwinVQModeTab ff_metasound_mode4440;
extern const TwinVQModeTab ff_metasound_mode4440s;
extern const TwinVQModeTab ff_metasound_mode4448;
extern const TwinVQModeTab ff_metasound_mode4448s;

#endif /* AVCODEC_METASOUND_DATA_H */
