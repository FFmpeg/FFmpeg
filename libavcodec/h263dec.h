/*
 * H.263 decoder internal header
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
#ifndef AVCODEC_H263DEC_H
#define AVCODEC_H263DEC_H

#include "get_bits.h"
#include "mpegvideo.h"
#include "vlc.h"
#include "libavutil/mem_internal.h"

// The defines below define the number of bits that are read at once for
// reading vlc values. Changing these may improve speed and data cache needs
// be aware though that decreasing them may need the number of stages that is
// passed to get_vlc* to be increased.
#define H263_MV_VLC_BITS     9
#define INTRA_MCBPC_VLC_BITS 6
#define INTER_MCBPC_VLC_BITS 7
#define CBPY_VLC_BITS 6
#define TEX_VLC_BITS 9

extern VLCElem ff_h263_intra_MCBPC_vlc[];
extern VLCElem ff_h263_inter_MCBPC_vlc[];
extern VLCElem ff_h263_cbpy_vlc[];
extern VLCElem ff_h263_mv_vlc[];

typedef struct H263DecContext {
    MPVContext c;

    GetBitContext gb;

    int mb_num_left;            ///< number of MBs left in this video packet (for partitioned slices only)

    int picture_number;

    int pb_frame;     ///< PB-frame mode (0 = none, 1 = base, 2 = improved)

    /* motion compensation */
    int h263_long_vectors;      ///< use horrible H.263v1 long vector mode

    /* FLV specific */
    int flv;                    ///< use flv H.263 header

    /* H.263 specific */
    int ehc_mode;
    int gob_index;

    /* H.263+ specific */
    int custom_pcf;
    int umvplus;                ///< == H.263+ && unrestricted_mv
    int h263_slice_structured;
    int alt_inter_vlc;          ///< alternative inter vlc
    int loop_filter;
    int modified_quant;

    /* MPEG-4 specific */
    int padding_bug_score;      ///< used to detect the VERY common padding bug in MPEG-4
    int skipped_last_frame;
    int divx_packed;            ///< divx specific, used to workaround (many) bugs in divx5
    int data_partitioning;      ///< data partitioning flag from header
    int partitioned_frame;      ///< is current frame partitioned

    /* MSMPEG4 specific */
    int slice_height;           ///< in macroblocks

    /* RV10 specific */
    int rv10_version; ///< RV10 version: 0 or 3
    int rv10_first_dc_coded[3];

    int (*decode_header)(struct H263DecContext *const h);
#define FRAME_SKIPPED 100 ///< Frame is not coded

    int (*decode_mb)(struct H263DecContext *h);
#define SLICE_OK         0
#define SLICE_ERROR     -1
#define SLICE_END       -2 ///<end marker found
#define SLICE_NOEND     -3 ///<no end marker or error found but mb count exceeded

    GetBitContext last_resync_gb;    ///< used to search for the next resync marker

    DECLARE_ALIGNED_32(int16_t, block)[6][64];
} H263DecContext;

int ff_h263_decode_motion(H263DecContext *const h, int pred, int f_code);
int ff_h263_decode_init(AVCodecContext *avctx);
int ff_h263_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                         int *got_frame, AVPacket *avpkt);
void ff_h263_decode_init_vlc(void);
int ff_h263_decode_picture_header(H263DecContext *const h);
int ff_h263_decode_gob_header(H263DecContext *const h);
int ff_h263_decode_mba(H263DecContext *const h);

/**
 * Print picture info if FF_DEBUG_PICT_INFO is set.
 */
void ff_h263_show_pict_info(H263DecContext *const h, int h263_plus);

int ff_intel_h263_decode_picture_header(H263DecContext *const h);
int ff_h263_decode_mb(H263DecContext *const h);

int ff_h263_resync(H263DecContext *const h);

#endif
