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

#pragma shader_stage(compute)

#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require

#define FADE 0
#define WIPELEFT 1
#define WIPERIGHT 2
#define WIPEUP 3
#define WIPEDOWN 4
#define SLIDEDOWN 5
#define SLIDEUP 6
#define SLIDELEFT 7
#define SLIDERIGHT 8
#define CIRCLEOPEN 9
#define CIRCLECLOSE 10
#define DISSOLVE 11
#define PIXELIZE 12
#define WIPETL 13
#define WIPETR 14
#define WIPEBL 15
#define WIPEBR 16

