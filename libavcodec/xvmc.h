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
#define AV_XVMC_STATE_OSD_SOURCE               4  /**  the surface is needed for subpicture rendering */
#endif
#define AV_XVMC_ID                    0x1DC711C0  /**< special value to ensure that regular pixel routines haven't corrupted the struct
                                                       the number is 1337 speak for the letters IDCT MCo (motion compensation) */

struct xvmc_pix_fmt {
/** Set by the calling application.
    Once set these values are not supposed to be modified.
*/
//@{
    /** The field contains the special constant value AV_XVMC_ID.
        It is used as a test that the application correctly uses the API,
        and that there is no corruption caused by pixel routines.
        - application - set during initialization
        - libavcodec  - unchanged
    */
    int             xvmc_id;

    /** Pointer to the block array allocated by XvMCCreateBlocks()
        it contins differential pixel data (in MoCo mode)
        or coefficients for IDCT.
        - application - set during initialization
        - libavcodec  - unchanged
    */
    short*          data_blocks;

    /** Pointer to the macroblock description array allocated by XvMCCreateMacroBlocks().
        - application - set during initialization
        - libavcodec  - unchanged
    */
    XvMCMacroBlock* mv_blocks;

    /** Number of all MB descriptions that could be stored in the mv_blocks array.
        - application - set during initialization
        - libavcodec  - unchanged
    */
    int             total_number_of_mv_blocks;

    /** Number of all blocks that could be stored at once in the data_blocks array.
        - application - set during initialization
        - libavcodec  - unchanged
    */
    int             total_number_of_data_blocks;

    /** Indicates that the hardware would interpret data_blocks as IDCT coefficients
        and perform IDCT on them.
        - application - set during initialization
        - libavcodec  - unchanged
    */
    int             idct;

    /** In MoCo mode it indicates that Intra MB are assumed to be in unsigned format
        Same as XVMC_INTRA_UNSIGNED flag.
        - application - set during initialization
        - libavcodec  - unchanged
    */
    int             unsigned_intra;

    /** Pointer to the surface allocated by XvMCCreateSurface().
        It identifies the frame on the video hardware and its state.
        - application - set during initialization
        - libavcodec  - unchanged
    */
    XvMCSurface*    p_surface;
//}@

/** Set by the decoder before calling ff_draw_horiz_band(),
    needed by the XvMCRenderSurface function. */
//@{
    /** Pointer to the surface used as past reference
        - application - unchanged
        - libavcodec  - set
    */
    XvMCSurface*    p_past_surface;

    /** Pointer to the surface used as future reference
        - application - unchanged
        - libavcodec  - set
    */
    XvMCSurface*    p_future_surface;

    /** top/bottom field or frame
        - application - unchanged
        - libavcodec  - set
    */
    unsigned int    picture_structure;

    /** XVMC_SECOND_FIELD - 1st or 2nd field in the sequence
        - application - unchanged
        - libavcodec  - set
    */
    unsigned int    flags;
//}@

    /** Offset in the mv array for the current slice.
        Macroblocks described before that offset are assumed to be already passed to the hardware.
        - application - zeroes it on get_buffer().
                        A successful ff_draw_horiz_band() may increment it
                        with filled_mb_block_num or zero both.
        - libavcodec  - unchanged
    */
    int             start_mv_blocks_num;

    /** Number of mv blocks that are filled by libavcodec and have to be passed to the hardware.
        - application - zeroes it on get_buffer() or after successful ff_draw_horiz_band()
        - libavcodec  - increment with one of each stored MB
    */
    int             filled_mv_blocks_num;

    /** Offset to the next free data block. The mv_blocks hold a number pointing to the data blocks.
        - application - zeroes it on get_buffer() and after successful ff_draw_horiz_band()
        - libvcodec   - each macroblock increases it with the number of coded blocks in it.
    */
    int             next_free_data_block_num;

/** extensions may be placed here */
#if LIBAVCODEC_VERSION_MAJOR < 53
//@{
    /** State - used to work around limitations in the MPlayer video system.
        0   - Surface is not used.
        1   - Surface is still held in application to be displayed or is still visible.
        2   - Surface is still held in libavcodec buffer for prediction.
    */
    int             state;
    void*           p_osd_target_surface_render;  ///< pointer to the surface where the subpicture is rendered
//}@
#endif
};

#endif /* AVCODEC_XVMC_H */
