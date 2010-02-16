/*
 * AVI common data
 * Copyright (c) 2010 Anton Khirnov
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

#include "avi.h"

const AVMetadataConv ff_avi_metadata_conv[] = {
    { "IART", "artist"    },
    { "ICMT", "comment"   },
    { "ICOP", "copyright" },
    { "ICRD", "date"      },
    { "IGNR", "genre"     },
    { "ILNG", "language"  },
    { "INAM", "title"     },
    { "IPRD", "album"     },
    { "IPRT", "track"     },
    { "ISFT", "encoder"   },
    { "ITCH", "encoded_by"},
    { "strn", "title"     },
    { 0 },
};

const char ff_avi_tags[][5] = {
    "IARL", "IART", "ICMS", "ICMT", "ICOP", "ICRD", "ICRP", "IDIM", "IDPI",
    "IENG", "IGNR", "IKEY", "ILGT", "ILNG", "IMED", "INAM", "IPLT", "IPRD",
    "IPRT", "ISBJ", "ISFT", "ISHP", "ISRC", "ISRF", "ITCH",
    {0}
};
