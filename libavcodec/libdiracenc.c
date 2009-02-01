/*
 * Dirac encoding support via libdirac library
 * Copyright (c) 2005 BBC, Andrew Kennedy <dirac at rd dot bbc dot co dot uk>
 * Copyright (c) 2006-2008 BBC, Anuradha Suraparaju <asuraparaju at gmail dot com >
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

/**
* @file libavcodec/libdiracenc.c
* Dirac encoding support via libdirac library; more details about the
* Dirac project can be found at http://dirac.sourceforge.net/.
* The libdirac_encoder library implements Dirac specification version 2.2
* (http://dirac.sourceforge.net/specification.html).
*/

#include "libdirac_libschro.h"
#include "libdirac.h"

#undef NDEBUG
#include <assert.h>


#include <libdirac_encoder/dirac_encoder.h>

/** Dirac encoder private data */
typedef struct FfmpegDiracEncoderParams
{
    /** Dirac encoder context */
    dirac_encoder_context_t enc_ctx;

    /** frame being encoded */
    AVFrame picture;

    /** frame size */
    int frame_size;

    /** Dirac encoder handle */
    dirac_encoder_t* p_encoder;

    /** input frame buffer */
    unsigned char *p_in_frame_buf;

    /** buffer to store encoder output before writing it to the frame queue */
    unsigned char *enc_buf;

    /** size of encoder buffer */
    int enc_buf_size;

    /** queue storing encoded frames */
    FfmpegDiracSchroQueue enc_frame_queue;

    /** end of sequence signalled by user, 0 - false, 1 - true */
    int eos_signalled;

    /** end of sequence returned by encoder, 0 - false, 1 - true */
    int eos_pulled;
} FfmpegDiracEncoderParams;

/**
* Works out Dirac-compatible chroma format.
*/
static dirac_chroma_t GetDiracChromaFormat(enum PixelFormat ff_pix_fmt)
{
    int num_formats = sizeof(ffmpeg_dirac_pixel_format_map) /
                      sizeof(ffmpeg_dirac_pixel_format_map[0]);
    int idx;

    for (idx = 0; idx < num_formats; ++idx) {
        if (ffmpeg_dirac_pixel_format_map[idx].ff_pix_fmt == ff_pix_fmt) {
            return ffmpeg_dirac_pixel_format_map[idx].dirac_pix_fmt;
        }
    }
    return formatNK;
}

/**
* Dirac video preset table. Ensure that this tables matches up correctly
* with the ff_dirac_schro_video_format_info table in libdirac_libschro.c.
*/
static const VideoFormat ff_dirac_video_formats[]={
    VIDEO_FORMAT_CUSTOM           ,
    VIDEO_FORMAT_QSIF525          ,
    VIDEO_FORMAT_QCIF             ,
    VIDEO_FORMAT_SIF525           ,
    VIDEO_FORMAT_CIF              ,
    VIDEO_FORMAT_4SIF525          ,
    VIDEO_FORMAT_4CIF             ,
    VIDEO_FORMAT_SD_480I60        ,
    VIDEO_FORMAT_SD_576I50        ,
    VIDEO_FORMAT_HD_720P60        ,
    VIDEO_FORMAT_HD_720P50        ,
    VIDEO_FORMAT_HD_1080I60       ,
    VIDEO_FORMAT_HD_1080I50       ,
    VIDEO_FORMAT_HD_1080P60       ,
    VIDEO_FORMAT_HD_1080P50       ,
    VIDEO_FORMAT_DIGI_CINEMA_2K24 ,
    VIDEO_FORMAT_DIGI_CINEMA_4K24 ,
};

/**
* Returns the video format preset matching the input video dimensions and
* time base.
*/
static VideoFormat GetDiracVideoFormatPreset (AVCodecContext *avccontext)
{
    unsigned int num_formats = sizeof(ff_dirac_video_formats) /
                               sizeof(ff_dirac_video_formats[0]);

    unsigned int idx = ff_dirac_schro_get_video_format_idx (avccontext);

    return (idx < num_formats) ?
                 ff_dirac_video_formats[idx] : VIDEO_FORMAT_CUSTOM;
}

static int libdirac_encode_init(AVCodecContext *avccontext)
{

    FfmpegDiracEncoderParams* p_dirac_params = avccontext->priv_data;
    int no_local = 1;
    int verbose = avccontext->debug;
    VideoFormat preset;

    /* get Dirac preset */
    preset = GetDiracVideoFormatPreset(avccontext);

    /* initialize the encoder context */
    dirac_encoder_context_init (&(p_dirac_params->enc_ctx), preset);

    p_dirac_params->enc_ctx.src_params.chroma =
                                GetDiracChromaFormat(avccontext->pix_fmt);

    if (p_dirac_params->enc_ctx.src_params.chroma == formatNK) {
        av_log (avccontext, AV_LOG_ERROR,
                "Unsupported pixel format %d. This codec supports only "
                "Planar YUV formats (yuv420p, yuv422p, yuv444p\n",
                avccontext->pix_fmt);
        return -1;
    }

    p_dirac_params->enc_ctx.src_params.frame_rate.numerator   =
                                             avccontext->time_base.den;
    p_dirac_params->enc_ctx.src_params.frame_rate.denominator =
                                             avccontext->time_base.num;

    p_dirac_params->enc_ctx.src_params.width  = avccontext->width;
    p_dirac_params->enc_ctx.src_params.height = avccontext->height;

    p_dirac_params->frame_size = avpicture_get_size(avccontext->pix_fmt,
                                                    avccontext->width,
                                                    avccontext->height);

    avccontext->coded_frame = &p_dirac_params->picture;

    if (no_local) {
        p_dirac_params->enc_ctx.decode_flag = 0;
        p_dirac_params->enc_ctx.instr_flag  = 0;
    } else {
        p_dirac_params->enc_ctx.decode_flag = 1;
        p_dirac_params->enc_ctx.instr_flag  = 1;
    }

    /* Intra-only sequence */
    if (avccontext->gop_size == 0 ) {
        p_dirac_params->enc_ctx.enc_params.num_L1 = 0;
        if (avccontext->coder_type == FF_CODER_TYPE_VLC)
            p_dirac_params->enc_ctx.enc_params.using_ac = 0;
    } else
        avccontext->has_b_frames = 1;

    if (avccontext->flags & CODEC_FLAG_QSCALE) {
        if (avccontext->global_quality != 0) {
            p_dirac_params->enc_ctx.enc_params.qf =
                            avccontext->global_quality / (FF_QP2LAMBDA*10.0);
            /* if it is not default bitrate then send target rate. */
            if (avccontext->bit_rate >= 1000 &&
                avccontext->bit_rate != 200000) {
               p_dirac_params->enc_ctx.enc_params.trate =
                             avccontext->bit_rate / 1000;
            }
        } else
            p_dirac_params->enc_ctx.enc_params.lossless = 1;
    } else if (avccontext->bit_rate >= 1000)
        p_dirac_params->enc_ctx.enc_params.trate = avccontext->bit_rate / 1000;

    if ((preset > VIDEO_FORMAT_QCIF || preset < VIDEO_FORMAT_QSIF525) &&
         avccontext->bit_rate == 200000) {
        p_dirac_params->enc_ctx.enc_params.trate = 0;
    }

    if (avccontext->flags & CODEC_FLAG_INTERLACED_ME) {
        /* all material can be coded as interlaced or progressive
         * irrespective of the type of source material */
        p_dirac_params->enc_ctx.enc_params.picture_coding_mode = 1;
    }

    p_dirac_params->p_encoder = dirac_encoder_init (&(p_dirac_params->enc_ctx),
                                                    verbose );

    if (!p_dirac_params->p_encoder) {
        av_log(avccontext, AV_LOG_ERROR,
               "Unrecoverable Error: dirac_encoder_init failed. ");
        return EXIT_FAILURE;
    }

    /* allocate enough memory for the incoming data */
    p_dirac_params->p_in_frame_buf = av_malloc(p_dirac_params->frame_size);

    /* initialize the encoded frame queue */
    ff_dirac_schro_queue_init(&p_dirac_params->enc_frame_queue);

    return 0 ;
}

static void DiracFreeFrame (void *data)
{
    FfmpegDiracSchroEncodedFrame *enc_frame = data;

    av_freep (&(enc_frame->p_encbuf));
    av_free(enc_frame);
}

static int libdirac_encode_frame(AVCodecContext *avccontext,
                                 unsigned char *frame,
                                 int buf_size, void *data)
{
    int enc_size = 0;
    dirac_encoder_state_t state;
    FfmpegDiracEncoderParams* p_dirac_params = avccontext->priv_data;
    FfmpegDiracSchroEncodedFrame* p_frame_output = NULL;
    FfmpegDiracSchroEncodedFrame* p_next_output_frame = NULL;
    int go = 1;
    int last_frame_in_sequence = 0;

    if (data == NULL) {
        /* push end of sequence if not already signalled */
        if (!p_dirac_params->eos_signalled) {
            dirac_encoder_end_sequence( p_dirac_params->p_encoder );
            p_dirac_params->eos_signalled = 1;
        }
    } else {

        /* Allocate frame data to Dirac input buffer.
         * Input line size may differ from what the codec supports,
         * especially when transcoding from one format to another.
         * So use avpicture_layout to copy the frame. */
        avpicture_layout ((AVPicture *)data, avccontext->pix_fmt,
                          avccontext->width, avccontext->height,
                          p_dirac_params->p_in_frame_buf,
                          p_dirac_params->frame_size);

        /* load next frame */
        if (dirac_encoder_load (p_dirac_params->p_encoder,
                                p_dirac_params->p_in_frame_buf,
                                p_dirac_params->frame_size ) < 0) {
            av_log(avccontext, AV_LOG_ERROR, "Unrecoverable Encoder Error."
                   " dirac_encoder_load failed...\n");
            return -1;
        }
    }

    if (p_dirac_params->eos_pulled)
        go = 0;

    while(go) {
        p_dirac_params->p_encoder->enc_buf.buffer = frame;
        p_dirac_params->p_encoder->enc_buf.size   = buf_size;
        /* process frame */
        state = dirac_encoder_output ( p_dirac_params->p_encoder );

        switch (state)
        {
        case ENC_STATE_AVAIL:
        case ENC_STATE_EOS:
            assert (p_dirac_params->p_encoder->enc_buf.size > 0);

            /* All non-frame data is prepended to actual frame data to
             * be able to set the pts correctly. So we don't write data
             * to the frame output queue until we actually have a frame
             */

            p_dirac_params->enc_buf = av_realloc (
                                      p_dirac_params->enc_buf,
                                      p_dirac_params->enc_buf_size +
                                      p_dirac_params->p_encoder->enc_buf.size
                                      );
            memcpy(p_dirac_params->enc_buf + p_dirac_params->enc_buf_size,
                   p_dirac_params->p_encoder->enc_buf.buffer,
                   p_dirac_params->p_encoder->enc_buf.size);

            p_dirac_params->enc_buf_size +=
                                     p_dirac_params->p_encoder->enc_buf.size;

            if (state == ENC_STATE_EOS) {
                p_dirac_params->eos_pulled = 1;
                go = 0;
            }

            /* If non-frame data, don't output it until it we get an
             * encoded frame back from the encoder. */
            if (p_dirac_params->p_encoder->enc_pparams.pnum == -1)
                break;

            /* create output frame */
            p_frame_output = av_mallocz(sizeof(FfmpegDiracSchroEncodedFrame));
            /* set output data */
            p_frame_output->size     = p_dirac_params->enc_buf_size;
            p_frame_output->p_encbuf = p_dirac_params->enc_buf;
            p_frame_output->frame_num =
                            p_dirac_params->p_encoder->enc_pparams.pnum;

            if (p_dirac_params->p_encoder->enc_pparams.ptype == INTRA_PICTURE &&
                p_dirac_params->p_encoder->enc_pparams.rtype == REFERENCE_PICTURE)
                p_frame_output->key_frame = 1;

            ff_dirac_schro_queue_push_back (&p_dirac_params->enc_frame_queue,
                                            p_frame_output);

            p_dirac_params->enc_buf_size = 0;
            p_dirac_params->enc_buf      = NULL;
            break;

        case ENC_STATE_BUFFER:
            go = 0;
            break;

        case ENC_STATE_INVALID:
            av_log(avccontext, AV_LOG_ERROR,
                   "Unrecoverable Dirac Encoder Error. Quitting...\n");
            return -1;

        default:
            av_log(avccontext, AV_LOG_ERROR, "Unknown Dirac Encoder state\n");
            return -1;
        }
    }

    /* copy 'next' frame in queue */

    if (p_dirac_params->enc_frame_queue.size == 1 &&
        p_dirac_params->eos_pulled)
        last_frame_in_sequence = 1;

    p_next_output_frame =
          ff_dirac_schro_queue_pop(&p_dirac_params->enc_frame_queue);

    if (p_next_output_frame == NULL)
        return 0;

    memcpy(frame, p_next_output_frame->p_encbuf, p_next_output_frame->size);
    avccontext->coded_frame->key_frame = p_next_output_frame->key_frame;
    /* Use the frame number of the encoded frame as the pts. It is OK to do
     * so since Dirac is a constant framerate codec. It expects input to be
     * of constant framerate. */
    avccontext->coded_frame->pts = p_next_output_frame->frame_num;
    enc_size = p_next_output_frame->size;

    /* Append the end of sequence information to the last frame in the
     * sequence. */
    if (last_frame_in_sequence && p_dirac_params->enc_buf_size > 0)
    {
        memcpy (frame + enc_size, p_dirac_params->enc_buf,
                p_dirac_params->enc_buf_size);
        enc_size += p_dirac_params->enc_buf_size;
        av_freep (&p_dirac_params->enc_buf);
        p_dirac_params->enc_buf_size = 0;
    }

    /* free frame */
    DiracFreeFrame(p_next_output_frame);

    return enc_size;
}

static int libdirac_encode_close(AVCodecContext *avccontext)
{
    FfmpegDiracEncoderParams* p_dirac_params  = avccontext->priv_data;

    /* close the encoder */
    dirac_encoder_close(p_dirac_params->p_encoder );

    /* free data in the output frame queue */
    ff_dirac_schro_queue_free(&p_dirac_params->enc_frame_queue,
                              DiracFreeFrame);

    /* free the encoder buffer */
    if (p_dirac_params->enc_buf_size)
        av_freep(&p_dirac_params->enc_buf);

    /* free the input frame buffer */
    av_freep(&p_dirac_params->p_in_frame_buf);

    return 0 ;
}


AVCodec libdirac_encoder = {
    "libdirac",
    CODEC_TYPE_VIDEO,
    CODEC_ID_DIRAC,
    sizeof(FfmpegDiracEncoderParams),
    libdirac_encode_init,
    libdirac_encode_frame,
    libdirac_encode_close,
   .capabilities= CODEC_CAP_DELAY,
   .pix_fmts= (enum PixelFormat[]){PIX_FMT_YUV420P, PIX_FMT_YUV422P, PIX_FMT_YUV444P, PIX_FMT_NONE},
   .long_name= NULL_IF_CONFIG_SMALL("libdirac Dirac 2.2"),
} ;
