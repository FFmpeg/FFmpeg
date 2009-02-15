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

#ifndef AVCODEC_XVMC_H
#define AVCODEC_XVMC_H

#include <X11/extensions/XvMC.h>

#include "avcodec.h"

#if LIBAVCODEC_VERSION_MAJOR < 53
#define AV_XVMC_STATE_DISPLAY_PENDING          1  /**  the surface should be shown, the video driver manipulates this */
#define AV_XVMC_STATE_PREDICTION               2  /**  the surface is needed for prediction, the codec manipulates this */
#define AV_XVMC_STATE_OSD_SOURCE               4  /**  this surface is needed for subpicture rendering */
#endif
#define AV_XVMC_RENDER_MAGIC          0x1DC711C0  /**< magic value to ensure that regular pixel routines haven't corrupted the struct */
                                                  //   1337 IDCT MCo

struct xvmc_render_state {
/** set by calling application */
//@{
    int             magic_id;                     ///< used as a check against memory corruption by regular pixel routines or other API structure

    short*          data_blocks;
    XvMCMacroBlock* mv_blocks;
    int             total_number_of_mv_blocks;
    int             total_number_of_data_blocks;
    int             idct;                         ///< indicate that IDCT acceleration level is used
    int             unsigned_intra;               ///< +-128 for intra pictures after clipping
    XvMCSurface*    p_surface;                    ///< pointer to rendered surface, never changed
//}@

/** set by the decoder
    used by the XvMCRenderSurface function */
//@{
    XvMCSurface*    p_past_surface;               ///< pointer to the past surface
    XvMCSurface*    p_future_surface;             ///< pointer to the future prediction surface

    unsigned int    picture_structure;            ///< top/bottom fields or frame
    unsigned int    flags;                        ///< XVMC_SECOND_FIELD - 1st or 2nd field in the sequence
//}@

    /** Offset in the mv array for the current slice:
        - application - zeros it on  get_buffer().
                        successful draw_horiz_band() may increment it
                        with filled_mb_block_num or zero both.
        - libavcodec  - unchanged
    */
    int             start_mv_blocks_num;

    /** Processed mv blocks in this slice:
        - application - zeros it on get_buffer() or after successful draw_horiz_band()
        - libavcodec  - increment with one of each stored MB
    */
    int             filled_mv_blocks_num;

    /** Used in add_mv_block, pointer to next free block
        - application - zeroes it on get_buffer() and after successful draw_horiz_band()
        - libvcodec   - each macroblock increases it with the number of coded blocks in it.
    */
    int             next_free_data_block_num;

/** extensions may be placed here */
#if LIBAVCODEC_VERSION_MAJOR < 53
//@{
    /** State - used to workaround limitations in MPlayer vo system.
        0   -Surface not used
        1   -Surface is still hold in application to be displayed or is still visible.
        2   -Surface is still hold in lavcodec buffer for prediction
    */
    int             state;
    void*           p_osd_target_surface_render;  ///< pointer to the surface where subpicture is rendered
//}@
#endif
};

#endif /* AVCODEC_XVMC_H */
