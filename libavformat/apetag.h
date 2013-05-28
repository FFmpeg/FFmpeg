/*
 * APE tag handling
 * Copyright (c) 2007 Benjamin Zores <ben@geexbox.org>
 *  based upon libdemac from Dave Chapman.
 *
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

#ifndef AVFORMAT_APETAG_H
#define AVFORMAT_APETAG_H

#include "avformat.h"

/**
 * Read and parse an APE tag
 *
 * @return offset of the tag start in the file
 */
int64_t ff_ape_parse_tag(AVFormatContext *s);

/**
 * Write an APE tag into a file.
 */
int ff_ape_write_tag(AVFormatContext *s);

#endif /* AVFORMAT_APETAG_H */
