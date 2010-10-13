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

/**
 * @file
 * targa file common definitions
 *
 * Based on:
 * http://www.gamers.org/dEngine/quake3/TGA.txt
 *
 * and other specs you can find referenced for example in:
 * http://en.wikipedia.org/wiki/Truevision_TGA
 */

enum TargaCompr {
    TGA_NODATA = 0, // no image data
    TGA_PAL    = 1, // palettized
    TGA_RGB    = 2, // true-color
    TGA_BW     = 3, // black & white or grayscale
    TGA_RLE    = 8, // flag pointing that data is RLE-coded
};
