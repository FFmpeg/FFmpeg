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

#ifndef AVCODEC_CODEC_INTERNAL_H
#define AVCODEC_CODEC_INTERNAL_H

/**
 * The codec does not modify any global variables in the init function,
 * allowing to call the init function without locking any global mutexes.
 */
#define FF_CODEC_CAP_INIT_THREADSAFE        (1 << 0)
/**
 * The codec allows calling the close function for deallocation even if
 * the init function returned a failure. Without this capability flag, a
 * codec does such cleanup internally when returning failures from the
 * init function and does not expect the close function to be called at
 * all.
 */
#define FF_CODEC_CAP_INIT_CLEANUP           (1 << 1)
/**
 * Decoders marked with FF_CODEC_CAP_SETS_PKT_DTS want to set
 * AVFrame.pkt_dts manually. If the flag is set, decode.c won't overwrite
 * this field. If it's unset, decode.c tries to guess the pkt_dts field
 * from the input AVPacket.
 */
#define FF_CODEC_CAP_SETS_PKT_DTS           (1 << 2)
/**
 * The decoder extracts and fills its parameters even if the frame is
 * skipped due to the skip_frame setting.
 */
#define FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM  (1 << 3)
/**
 * The decoder sets the cropping fields in the output frames manually.
 * If this cap is set, the generic code will initialize output frame
 * dimensions to coded rather than display values.
 */
#define FF_CODEC_CAP_EXPORTS_CROPPING       (1 << 4)
/**
 * Codec initializes slice-based threading with a main function
 */
#define FF_CODEC_CAP_SLICE_THREAD_HAS_MF    (1 << 5)
/*
 * The codec supports frame threading and has inter-frame dependencies, so it
 * uses ff_thread_report/await_progress().
 */
#define FF_CODEC_CAP_ALLOCATE_PROGRESS      (1 << 6)
/**
 * Codec handles avctx->thread_count == 0 (auto) internally.
 */
#define FF_CODEC_CAP_AUTO_THREADS           (1 << 7)
/**
 * Codec handles output frame properties internally instead of letting the
 * internal logic derive them from AVCodecInternal.last_pkt_props.
 */
#define FF_CODEC_CAP_SETS_FRAME_PROPS       (1 << 8)

/**
 * AVCodec.codec_tags termination value
 */
#define FF_CODEC_TAGS_END -1

struct AVCodecDefault {
    const char *key;
    const char *value;
};

#endif /* AVCODEC_CODEC_INTERNAL_H */
