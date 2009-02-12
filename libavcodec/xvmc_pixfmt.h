/*
 * Copyright (C) 2003 Ivan Kalvachev
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

#ifndef AVCODEC_XVMC_RENDER_H
#define AVCODEC_XVMC_RENDER_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
#include <X11/extensions/XvMClib.h>


//the surface should be shown, the video driver manipulates this
#define MP_XVMC_STATE_DISPLAY_PENDING 1
//the surface is needed for prediction, the codec manipulates this
#define MP_XVMC_STATE_PREDICTION 2
//this surface is needed for subpicture rendering
#define MP_XVMC_STATE_OSD_SOURCE 4
//                     1337    IDCT MCo
#define MP_XVMC_RENDER_MAGIC 0x1DC711C0

struct xvmc_render_state {
    //these are not changed by the decoder!
    int  magic;

    short * data_blocks;
    XvMCMacroBlock * mv_blocks;
    int total_number_of_mv_blocks;
    int total_number_of_data_blocks;
    int mc_type; //XVMC_MPEG1/2/4,XVMC_H263 without XVMC_IDCT
    int idct; //Do we use IDCT acceleration?
    int chroma_format; //420, 422, 444
    int unsigned_intra; //+-128 for intra pictures after clipping
    XvMCSurface* p_surface; //pointer to rendered surface, never changed

    //these are changed by the decoder
    //used by the XvMCRenderSurface function
    XvMCSurface* p_past_surface; //pointer to the past surface
    XvMCSurface* p_future_surface; //pointer to the future prediction surface

    unsigned int picture_structure; //top/bottom fields or frame!
    unsigned int flags; //XVMC_SECOND_FIELD - 1st or 2nd field in the sequence
    unsigned int display_flags; //1,2 or 1+2 fields for XvMCPutSurface

    //these are for internal communication
    int state; //0 - free, 1 - waiting to display, 2 - waiting for prediction
    int start_mv_blocks_num; //offset in the array for the current slice, updated by vo
    int filled_mv_blocks_num; //processed mv block in this slice, changed by decoder

    int next_free_data_block_num; //used in add_mv_block, pointer to next free block
    //extensions
    void * p_osd_target_surface_render; //pointer to the surface where subpicture is rendered

};

#endif /* AVCODEC_XVMC_RENDER_H */
