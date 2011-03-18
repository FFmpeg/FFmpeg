/*
 * SVQ1 decoder
 * ported to MPlayer by Arpi <arpi@thot.banki.hu>
 * ported to libavcodec by Nick Kurshev <nickols_k@mail.ru>
 *
 * Copyright (C) 2002 the xine project
 * Copyright (C) 2002 the ffmpeg project
 *
 * SVQ1 Encoder (c) 2004 Mike Melanson <melanson@pcisys.net>
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

/**
 * @file
 * Sorenson Vector Quantizer #1 (SVQ1) video codec.
 * For more information of the SVQ1 algorithm, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#include "svq1.h"
#include "svq1_cb.h"
#include "svq1_vlc.h"

/* standard video sizes */
const struct svq1_frame_size ff_svq1_frame_size_table[7] = {
  { 160, 120 }, { 128,  96 }, { 176, 144 }, { 352, 288 },
  { 704, 576 }, { 240, 180 }, { 320, 240 }
};
