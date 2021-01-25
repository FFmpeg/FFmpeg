/*
 * Copyright (C) 2017 foo86
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

#ifndef AVCODEC_DOLBY_E_PARSER_H
#define AVCODEC_DOLBY_E_PARSER_H

#include "dolby_e.h"

typedef struct DBEParseContext {
    ParseContext pc;
    DBEContext dectx;

    DolbyEHeaderInfo metadata;
} DBEParseContext;

static const uint8_t nb_programs_tab[MAX_PROG_CONF + 1] = {
    2, 3, 2, 3, 4, 5, 4, 5, 6, 7, 8, 1, 2, 3, 3, 4, 5, 6, 1, 2, 3, 4, 1, 1
};

static const uint8_t nb_channels_tab[MAX_PROG_CONF + 1] = {
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 6, 6, 6, 6, 6, 6, 6, 4, 4, 4, 4, 8, 8
};

#endif
