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

#ifndef AVCODEC_AMFENC_H
#define AVCODEC_AMFENC_H

#include <AMF/core/Factory.h>

#include <AMF/components/VideoEncoderVCE.h>
#include <AMF/components/VideoEncoderHEVC.h>

#include "libavutil/fifo.h"

#include "avcodec.h"
#include "hwconfig.h"


/**
* AMF trace writer callback class
* Used to capture all AMF logging
*/

typedef struct AmfTraceWriter {
    AMFTraceWriterVtbl *vtbl;
    AVCodecContext     *avctx;
} AmfTraceWriter;

/**
* AMF encoder context
*/

typedef struct AmfContext {
    AVClass            *avclass;
    // access to AMF runtime
    amf_handle          library; ///< handle to DLL library
    AMFFactory         *factory; ///< pointer to AMF factory
    AMFDebug           *debug;   ///< pointer to AMF debug interface
    AMFTrace           *trace;   ///< pointer to AMF trace interface

    amf_uint64          version; ///< version of AMF runtime
    AmfTraceWriter      tracer;  ///< AMF writer registered with AMF
    AMFContext         *context; ///< AMF context
    //encoder
    AMFComponent       *encoder; ///< AMF encoder object
    amf_bool            eof;     ///< flag indicating EOF happened
    AMF_SURFACE_FORMAT  format;  ///< AMF surface format

    AVBufferRef        *hw_device_ctx; ///< pointer to HW accelerator (decoder)
    AVBufferRef        *hw_frames_ctx; ///< pointer to HW accelerator (frame allocator)

    int                 hwsurfaces_in_queue;
    int                 hwsurfaces_in_queue_max;

    // helpers to handle async calls
    int                 delayed_drain;
    AMFSurface         *delayed_surface;
    AVFrame            *delayed_frame;

    // shift dts back by max_b_frames in timing
    AVFifo             *timestamp_list;
    int64_t             dts_delay;

    // common encoder option options

    int                 log_to_dbg;

    // Static options, have to be set before Init() call
    int                 usage;
    int                 profile;
    int                 level;
    int                 preanalysis;
    int                 quality;
    int                 b_frame_delta_qp;
    int                 ref_b_frame_delta_qp;

    // Dynamic options, can be set after Init() call

    int                 rate_control_mode;
    int                 enforce_hrd;
    int                 filler_data;
    int                 enable_vbaq;
    int                 skip_frame;
    int                 qp_i;
    int                 qp_p;
    int                 qp_b;
    int                 max_au_size;
    int                 header_spacing;
    int                 b_frame_ref;
    int                 intra_refresh_mb;
    int                 coding_mode;
    int                 me_half_pel;
    int                 me_quarter_pel;
    int                 aud;

    // HEVC - specific options

    int                 gops_per_idr;
    int                 header_insertion_mode;
    int                 min_qp_i;
    int                 max_qp_i;
    int                 min_qp_p;
    int                 max_qp_p;
    int                 tier;
} AmfContext;

extern const AVCodecHWConfigInternal *const ff_amfenc_hw_configs[];

/**
* Common encoder initization function
*/
int ff_amf_encode_init(AVCodecContext *avctx);
/**
* Common encoder termination function
*/
int ff_amf_encode_close(AVCodecContext *avctx);

/**
* Ecoding one frame - common function for all AMF encoders
*/
int ff_amf_receive_packet(AVCodecContext *avctx, AVPacket *avpkt);

/**
* Supported formats
*/
extern const enum AVPixelFormat ff_amf_pix_fmts[];

/**
* Error handling helper
*/
#define AMF_RETURN_IF_FALSE(avctx, exp, ret_value, /*message,*/ ...) \
    if (!(exp)) { \
        av_log(avctx, AV_LOG_ERROR, __VA_ARGS__); \
        return ret_value; \
    }

#endif //AVCODEC_AMFENC_H
