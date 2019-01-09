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
#ifndef AVFILTER_TRANSPOSE_H
#define AVFILTER_TRANSPOSE_H

enum PassthroughType {
    TRANSPOSE_PT_TYPE_NONE,
    TRANSPOSE_PT_TYPE_LANDSCAPE,
    TRANSPOSE_PT_TYPE_PORTRAIT,
};

enum TransposeDir {
    TRANSPOSE_CCLOCK_FLIP,
    TRANSPOSE_CLOCK,
    TRANSPOSE_CCLOCK,
    TRANSPOSE_CLOCK_FLIP,
};

#endif
