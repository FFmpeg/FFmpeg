/*
 * AMR Audio decoder stub
 * Copyright (c) 2003 the ffmpeg project
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
 */
//#define DEBUG
#define MMS_IO
#include "avcodec.h"

#include "amr/sp_dec.h"
#include "amr/d_homing.h"
#include "amr/typedef.h"

/* frame size in serial bitstream file (frame type + serial stream + flags) */
#define SERIAL_FRAMESIZE (1+MAX_SERIAL_SIZE+5)

typedef struct AMRDecodeContext {
    int frameCount;
    Speech_Decode_FrameState *speech_decoder_state;
    enum RXFrameType rx_type;
    enum Mode mode;
    Word16 reset_flag;
    Word16 reset_flag_old;

} AMRDecodeContext;

static int amr_nb_decode_init(AVCodecContext * avctx)
{
    AMRDecodeContext *s = avctx->priv_data;
    s->frameCount=0;
    s->speech_decoder_state=NULL;
    s->rx_type = (enum RXFrameType)0;
    s->mode= (enum Mode)0;
    s->reset_flag=0;
    s->reset_flag_old=1;
    
    if(Speech_Decode_Frame_init(&s->speech_decoder_state, "Decoder"))
    {
        printf("error\r\n");
        return -1;
    }
    return 0;
}

static int amr_decode_close(AVCodecContext * avctx)
{
    AMRDecodeContext *s = avctx->priv_data;
    Speech_Decode_Frame_exit(&s->speech_decoder_state);
}

static int amr_nb_decode_frame(AVCodecContext * avctx,
            void *data, int *data_size,
            uint8_t * buf, int buf_size)
{
    AMRDecodeContext *s = avctx->priv_data;

    uint8_t*amrData=buf;
    int offset=0;

    UWord8 toc, q, ft;
    
    Word16 serial[SERIAL_FRAMESIZE];   /* coded bits */
    Word16 *synth;
    UWord8 *packed_bits;

    static Word16 packed_size[16] = {12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0};
    int i;

    //printf("amr_decode_frame data=0x%X data_size=%i buf=0x%X buf_size=%d frameCount=%d!!\n",data,&data_size,buf,buf_size,s->frameCount);

    synth=data;

    while(offset<buf_size)
    {
        toc=amrData[offset++];
        /* read rest of the frame based on ToC byte */
        q  = (toc >> 2) & 0x01;
        ft = (toc >> 3) & 0x0F;

        packed_bits=amrData+offset;

        offset+=packed_size[ft];

		//Unsort and unpack bits
        s->rx_type = UnpackBits(q, ft, packed_bits, &s->mode, &serial[1]);

		//We have a new frame
        s->frameCount++;

        if (s->rx_type == RX_NO_DATA) 
        {
            s->mode = s->speech_decoder_state->prev_mode;
        }
        else {
            s->speech_decoder_state->prev_mode = s->mode;
        }
        
        /* if homed: check if this frame is another homing frame */
        if (s->reset_flag_old == 1)
        {
            /* only check until end of first subframe */
            s->reset_flag = decoder_homing_frame_test_first(&serial[1], s->mode);
        }
        /* produce encoder homing frame if homed & input=decoder homing frame */
        if ((s->reset_flag != 0) && (s->reset_flag_old != 0))
        {
            for (i = 0; i < L_FRAME; i++)
            {
                synth[i] = EHF_MASK;
            }
        }
        else
        {     
            /* decode frame */
            Speech_Decode_Frame(s->speech_decoder_state, s->mode, &serial[1], s->rx_type, synth);
        }

		//Each AMR-frame results in 160 16-bit samples
        *data_size+=160*2;
        synth+=160;
        
        /* if not homed: check whether current frame is a homing frame */
        if (s->reset_flag_old == 0)
        {
            /* check whole frame */
            s->reset_flag = decoder_homing_frame_test(&serial[1], s->mode);
        }
        /* reset decoder if current frame is a homing frame */
        if (s->reset_flag != 0)
        {
            Speech_Decode_Frame_reset(s->speech_decoder_state);
        }
        s->reset_flag_old = s->reset_flag;
        
    }
    return buf_size;
}

AVCodec amr_nb_decoder =
{
    "amr_nb",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AMR_NB,
    sizeof(AMRDecodeContext),
    amr_nb_decode_init,
    NULL,
    amr_decode_close,
    amr_nb_decode_frame,
};

