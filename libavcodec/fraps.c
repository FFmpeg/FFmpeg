/*
 * Fraps FPS1 decoder
 * Copyright (c) 2005 Roine Gustafsson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
 
/**
 * @file fraps.c
 * Lossless Fraps 'FPS1' decoder
 * @author Roine Gustafsson <roine at users sf net>
 * 
 * Only decodes version 0 and 1 files.
 * Codec algorithm for version 0 is taken from Transcode <www.transcoding.org>
 *
 * Version 2 files, which are the most commonly found Fraps files, cannot be
 * decoded yet.
 */
 
#include "avcodec.h"

#define FPS_TAG MKTAG('F', 'P', 'S', 'x')

/**
 * local variable storage
 */
typedef struct FrapsContext{
    AVCodecContext *avctx;
    AVFrame frame;
} FrapsContext;


/**
 * initializes decoder
 * @param avctx codec context
 * @return 0 on success or negative if fails
 */
static int decode_init(AVCodecContext *avctx)
{
    FrapsContext * const s = avctx->priv_data;

    avctx->coded_frame = (AVFrame*)&s->frame;
    avctx->has_b_frames = 0;
    avctx->pix_fmt= PIX_FMT_NONE; /* set in decode_frame */

    s->avctx = avctx;
    s->frame.data[0] = NULL;    

    return 0;
}


/**
 * decode a frame
 * @param avctx codec context
 * @param data output AVFrame
 * @param data_size size of output data or 0 if no picture is returned
 * @param buf input data frame
 * @param buf_size size of input data frame
 * @return number of consumed bytes on success or negative if decode fails
 */
static int decode_frame(AVCodecContext *avctx, 
                        void *data, int *data_size,
                        uint8_t *buf, int buf_size)
{
    FrapsContext * const s = avctx->priv_data;
    AVFrame *frame = data;
    AVFrame * const f = (AVFrame*)&s->frame;
    uint32_t header;
    unsigned int version,header_size;
    unsigned int x, y;
    uint32_t *buf32;
    uint32_t *luma1,*luma2,*cb,*cr;


    header = LE_32(buf);
    version = header & 0xff;
    header_size = (header & (1<<30))? 8 : 4; /* bit 30 means pad to 8 bytes */

    if (version > 1) {
        av_log(avctx, AV_LOG_ERROR, 
               "This file is encoded with Fraps version %d. " \
               "This codec can only decode version 0 and 1.\n", version);
        return -1;
    }

    buf+=4;
    if (header_size == 8)
        buf+=4;
        
    switch(version) {
    case 0:
    default:
        /* Fraps v0 is a reordered YUV420 */
        avctx->pix_fmt = PIX_FMT_YUV420P;

        if ( (buf_size != avctx->width*avctx->height*3/2+header_size) && 
             (buf_size != header_size) ) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid frame length %d (should be %d)\n", 
                   buf_size, avctx->width*avctx->height*3/2+header_size);
            return -1;
        }
        
        if (( (avctx->width % 8) != 0) || ( (avctx->height % 2) != 0 )) {
            av_log(avctx, AV_LOG_ERROR, "Invalid frame size %dx%d\n", 
                   avctx->width, avctx->height);
            return -1;
        }

        f->reference = 1; 
        f->buffer_hints = FF_BUFFER_HINTS_VALID | 
                          FF_BUFFER_HINTS_PRESERVE | 
                          FF_BUFFER_HINTS_REUSABLE;
        if (avctx->reget_buffer(avctx, f)) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return -1;
        }        
        /* bit 31 means same as previous pic */
        f->pict_type = (header & (1<<31))? FF_P_TYPE : FF_I_TYPE; 
        f->key_frame = f->pict_type == FF_I_TYPE;

        if (f->pict_type == FF_I_TYPE) { 
            buf32=(uint32_t*)buf;
            for(y=0; y<avctx->height/2; y++){
                luma1=(uint32_t*)&f->data[0][ y*2*f->linesize[0] ];
                luma2=(uint32_t*)&f->data[0][ (y*2+1)*f->linesize[0] ];
                cr=(uint32_t*)&f->data[1][ y*f->linesize[1] ];
                cb=(uint32_t*)&f->data[2][ y*f->linesize[2] ];
                for(x=0; x<avctx->width; x+=8){
                    *(luma1++) = *(buf32++);
                    *(luma1++) = *(buf32++);
                    *(luma2++) = *(buf32++);
                    *(luma2++) = *(buf32++);
                    *(cr++) = *(buf32++);
                    *(cb++) = *(buf32++);
                }
            }
        }
        break;

    case 1:
        /* Fraps v1 is an upside-down BGR24 */
        avctx->pix_fmt = PIX_FMT_BGR24;

        if ( (buf_size != avctx->width*avctx->height*3+header_size) && 
             (buf_size != header_size) ) {
            av_log(avctx, AV_LOG_ERROR, 
                   "Invalid frame length %d (should be %d)\n",
                   buf_size, avctx->width*avctx->height*3+header_size);
            return -1;
        }

        f->reference = 1;
        f->buffer_hints = FF_BUFFER_HINTS_VALID |
                          FF_BUFFER_HINTS_PRESERVE |
                          FF_BUFFER_HINTS_REUSABLE;
        if (avctx->reget_buffer(avctx, f)) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return -1;
        }
        /* bit 31 means same as previous pic */
        f->pict_type = (header & (1<<31))? FF_P_TYPE : FF_I_TYPE;
        f->key_frame = f->pict_type == FF_I_TYPE;

        if (f->pict_type == FF_I_TYPE) {
            for(y=0; y<avctx->height; y++)
                memcpy(&f->data[0][ (avctx->height-y)*f->linesize[0] ],
                       &buf[y*avctx->width*3],
                       f->linesize[0]);
        }
        break;

    case 2:
        /**
         * Fraps v2 sub-header description. All numbers are little-endian:
         * (this is all guesswork)
         *
         *       0:     DWORD 'FPSx'
         *       4:     DWORD 0x00000010   unknown, perhaps flags
         *       8:     DWORD off_2        offset to plane 2
         *      12:     DWORD off_3        offset to plane 3
         *      16: 256xDWORD freqtbl_1    frequency table for plane 1
         *    1040:           plane_1
         *     ...
         *   off_2: 256xDWORD freqtbl_2    frequency table for plane 2
         *                    plane_2
         *    ...
         *   off_3: 256xDWORD freqtbl_3    frequency table for plane 3
         *                    plane_3
         */
        if ((BE_32(buf) != FPS_TAG)||(buf_size < (3*1024 + 8))) {
            av_log(avctx, AV_LOG_ERROR, "Fraps: error in data stream\n");
            return -1;
        }

        /* NOT FINISHED */

        break;
    }

    *frame = *f;
    *data_size = sizeof(AVFrame);

    return buf_size;
}


/**
 * closes decoder
 * @param avctx codec context
 * @return 0 on success or negative if fails
 */
static int decode_end(AVCodecContext *avctx)
{
    FrapsContext *s = (FrapsContext*)avctx->priv_data;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}


AVCodec fraps_decoder = {
    "fraps",
    CODEC_TYPE_VIDEO,
    CODEC_ID_FRAPS,
    sizeof(FrapsContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1,
};
