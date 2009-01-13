/*
 * AMR Audio decoder stub
 * Copyright (c) 2003 the ffmpeg project
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

 /** @file
 * Adaptive Multi-Rate (AMR) Audio decoder stub.
 *
 * This code implements both an AMR-NarrowBand (AMR-NB) and an AMR-WideBand
 * (AMR-WB) audio encoder/decoder through external reference code from
 * http://www.3gpp.org/. The license of the code from 3gpp is unclear so you
 * have to download the code separately. Two versions exists: One fixed-point
 * and one floating-point. For some reason the float encoder is significantly
 * faster at least on a P4 1.5GHz (0.9s instead of 9.9s on a 30s audio clip
 * at MR102). Both float and fixed point are supported for AMR-NB, but only
 * float for AMR-WB.
 *
 * \section AMR-NB
 *
 * \subsection Float
 * The float version (default) can be downloaded from:
 * http://www.3gpp.org/ftp/Specs/archive/26_series/26.104/26104-610.zip
 *
 * \subsection Fixed-point
 * The fixed-point (TS26.073) can be downloaded from:
 * http://www.3gpp.org/ftp/Specs/archive/26_series/26.073/26073-600.zip
 *
 * \subsection Specification
 * The specification for AMR-NB can be found in TS 26.071
 * (http://www.3gpp.org/ftp/Specs/html-info/26071.htm) and some other
 * info at http://www.3gpp.org/ftp/Specs/html-info/26-series.htm.
 *
 * \section AMR-WB
 *
 * \subsection Float
 * The reference code can be downloaded from:
 * http://www.3gpp.org/ftp/Specs/archive/26_series/26.204/26204-600.zip
 *
 * \subsection Fixed-point
 * If someone wants to use the fixed point version it can be downloaded from:
 * http://www.3gpp.org/ftp/Specs/archive/26_series/26.173/26173-571.zip.
 *
 * \subsection Specification
 * The specification for AMR-WB can be found in TS 26.171
 * (http://www.3gpp.org/ftp/Specs/html-info/26171.htm) and some other
 * info at http://www.3gpp.org/ftp/Specs/html-info/26-series.htm.
 *
 */

#include "avcodec.h"

#if CONFIG_LIBAMR_NB_FIXED

#define MMS_IO

#include "amr/sp_dec.h"
#include "amr/d_homing.h"
#include "amr/typedef.h"
#include "amr/sp_enc.h"
#include "amr/sid_sync.h"
#include "amr/e_homing.h"

#else
#include <amrnb/interf_dec.h>
#include <amrnb/interf_enc.h>
#endif

static const char nb_bitrate_unsupported[] =
    "bitrate not supported: use one of 4.75k, 5.15k, 5.9k, 6.7k, 7.4k, 7.95k, 10.2k or 12.2k\n";
static const char wb_bitrate_unsupported[] =
    "bitrate not supported: use one of 6.6k, 8.85k, 12.65k, 14.25k, 15.85k, 18.25k, 19.85k, 23.05k, or 23.85k\n";

/* Common code for fixed and float version*/
typedef struct AMR_bitrates
{
    int rate;
    enum Mode mode;
} AMR_bitrates;

/* Match desired bitrate */
static int getBitrateMode(int bitrate)
{
    /* make the correspondance between bitrate and mode */
    AMR_bitrates rates[]={ {4750,MR475},
                           {5150,MR515},
                           {5900,MR59},
                           {6700,MR67},
                           {7400,MR74},
                           {7950,MR795},
                           {10200,MR102},
                           {12200,MR122},
                         };
    int i;

    for(i=0;i<8;i++)
    {
        if(rates[i].rate==bitrate)
        {
            return rates[i].mode;
        }
    }
    /* no bitrate matching, return an error */
    return -1;
}

static void amr_decode_fix_avctx(AVCodecContext * avctx)
{
    const int is_amr_wb = 1 + (avctx->codec_id == CODEC_ID_AMR_WB);

    if(avctx->sample_rate == 0)
    {
        avctx->sample_rate = 8000 * is_amr_wb;
    }

    if(avctx->channels == 0)
    {
        avctx->channels = 1;
    }

    avctx->frame_size = 160 * is_amr_wb;
    avctx->sample_fmt = SAMPLE_FMT_S16;
}

#if CONFIG_LIBAMR_NB_FIXED
/* fixed point version*/
/* frame size in serial bitstream file (frame type + serial stream + flags) */
#define SERIAL_FRAMESIZE (1+MAX_SERIAL_SIZE+5)

typedef struct AMRContext {
    int frameCount;
    Speech_Decode_FrameState *speech_decoder_state;
    enum RXFrameType rx_type;
    enum Mode mode;
    Word16 reset_flag;
    Word16 reset_flag_old;

    int enc_bitrate;
    Speech_Encode_FrameState *enstate;
    sid_syncState *sidstate;
    enum TXFrameType tx_frametype;
} AMRContext;

static int amr_nb_decode_init(AVCodecContext * avctx)
{
    AMRContext *s = avctx->priv_data;

    s->frameCount=0;
    s->speech_decoder_state=NULL;
    s->rx_type = (enum RXFrameType)0;
    s->mode= (enum Mode)0;
    s->reset_flag=0;
    s->reset_flag_old=1;

    if(Speech_Decode_Frame_init(&s->speech_decoder_state, "Decoder"))
    {
        av_log(avctx, AV_LOG_ERROR, "Speech_Decode_Frame_init error\n");
        return -1;
    }

    amr_decode_fix_avctx(avctx);

    if(avctx->channels > 1)
    {
        av_log(avctx, AV_LOG_ERROR, "amr_nb: multichannel decoding not supported\n");
        return -1;
    }

    return 0;
}

static int amr_nb_encode_init(AVCodecContext * avctx)
{
    AMRContext *s = avctx->priv_data;

    s->frameCount=0;
    s->speech_decoder_state=NULL;
    s->rx_type = (enum RXFrameType)0;
    s->mode= (enum Mode)0;
    s->reset_flag=0;
    s->reset_flag_old=1;

    if(avctx->sample_rate!=8000)
    {
        av_log(avctx, AV_LOG_ERROR, "Only 8000Hz sample rate supported\n");
        return -1;
    }

    if(avctx->channels!=1)
    {
        av_log(avctx, AV_LOG_ERROR, "Only mono supported\n");
        return -1;
    }

    avctx->frame_size=160;
    avctx->coded_frame= avcodec_alloc_frame();

    if(Speech_Encode_Frame_init(&s->enstate, 0, "encoder") || sid_sync_init (&s->sidstate))
    {
        av_log(avctx, AV_LOG_ERROR, "Speech_Encode_Frame_init error\n");
        return -1;
    }

    if((s->enc_bitrate=getBitrateMode(avctx->bit_rate))<0)
    {
        av_log(avctx, AV_LOG_ERROR, nb_bitrate_unsupported);
        return -1;
    }

    return 0;
}

static int amr_nb_encode_close(AVCodecContext * avctx)
{
    AMRContext *s = avctx->priv_data;

    Speech_Encode_Frame_exit(&s->enstate);
    sid_sync_exit (&s->sidstate);
    av_freep(&avctx->coded_frame);
    return 0;
}

static int amr_nb_decode_close(AVCodecContext * avctx)
{
    AMRContext *s = avctx->priv_data;

    Speech_Decode_Frame_exit(&s->speech_decoder_state);
    return 0;
}

static int amr_nb_decode_frame(AVCodecContext * avctx,
            void *data, int *data_size,
            const uint8_t * buf, int buf_size)
{
    AMRContext *s = avctx->priv_data;
    const uint8_t*amrData=buf;
    int offset=0;
    UWord8 toc, q, ft;
    Word16 serial[SERIAL_FRAMESIZE];   /* coded bits */
    Word16 *synth;
    UWord8 *packed_bits;
    static Word16 packed_size[16] = {12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0};
    int i;

    //printf("amr_decode_frame data_size=%i buf=0x%X buf_size=%d frameCount=%d!!\n",*data_size,buf,buf_size,s->frameCount);

    synth=data;

    toc=amrData[offset];
    /* read rest of the frame based on ToC byte */
    q  = (toc >> 2) & 0x01;
    ft = (toc >> 3) & 0x0F;

    //printf("offset=%d, packet_size=%d amrData= 0x%X %X %X %X\n",offset,packed_size[ft],amrData[offset],amrData[offset+1],amrData[offset+2],amrData[offset+3]);

    offset++;

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
    *data_size=160*2;

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

    return offset;
}


static int amr_nb_encode_frame(AVCodecContext *avctx,
                            unsigned char *frame/*out*/, int buf_size, void *data/*in*/)
{
    short serial_data[250] = {0};
    AMRContext *s = avctx->priv_data;
    int written;

    s->reset_flag = encoder_homing_frame_test(data);

    Speech_Encode_Frame(s->enstate, s->enc_bitrate, data, &serial_data[1], &s->mode);

    /* add frame type and mode */
    sid_sync (s->sidstate, s->mode, &s->tx_frametype);

    written = PackBits(s->mode, s->enc_bitrate, s->tx_frametype, &serial_data[1], frame);

    if (s->reset_flag != 0)
    {
        Speech_Encode_Frame_reset(s->enstate);
        sid_sync_reset(s->sidstate);
    }
    return written;
}


#elif CONFIG_LIBAMR_NB /* Float point version*/

typedef struct AMRContext {
    int frameCount;
    void * decState;
    int *enstate;
    int enc_bitrate;
} AMRContext;

static int amr_nb_decode_init(AVCodecContext * avctx)
{
    AMRContext *s = avctx->priv_data;

    s->frameCount=0;
    s->decState=Decoder_Interface_init();
    if(!s->decState)
    {
        av_log(avctx, AV_LOG_ERROR, "Decoder_Interface_init error\r\n");
        return -1;
    }

    amr_decode_fix_avctx(avctx);

    if(avctx->channels > 1)
    {
        av_log(avctx, AV_LOG_ERROR, "amr_nb: multichannel decoding not supported\n");
        return -1;
    }

    return 0;
}

static int amr_nb_encode_init(AVCodecContext * avctx)
{
    AMRContext *s = avctx->priv_data;

    s->frameCount=0;

    if(avctx->sample_rate!=8000)
    {
        av_log(avctx, AV_LOG_ERROR, "Only 8000Hz sample rate supported\n");
        return -1;
    }

    if(avctx->channels!=1)
    {
        av_log(avctx, AV_LOG_ERROR, "Only mono supported\n");
        return -1;
    }

    avctx->frame_size=160;
    avctx->coded_frame= avcodec_alloc_frame();

    s->enstate=Encoder_Interface_init(0);
    if(!s->enstate)
    {
        av_log(avctx, AV_LOG_ERROR, "Encoder_Interface_init error\n");
        return -1;
    }

    if((s->enc_bitrate=getBitrateMode(avctx->bit_rate))<0)
    {
        av_log(avctx, AV_LOG_ERROR, nb_bitrate_unsupported);
        return -1;
    }

    return 0;
}

static int amr_nb_decode_close(AVCodecContext * avctx)
{
    AMRContext *s = avctx->priv_data;

    Decoder_Interface_exit(s->decState);
    return 0;
}

static int amr_nb_encode_close(AVCodecContext * avctx)
{
    AMRContext *s = avctx->priv_data;

    Encoder_Interface_exit(s->enstate);
    av_freep(&avctx->coded_frame);
    return 0;
}

static int amr_nb_decode_frame(AVCodecContext * avctx,
            void *data, int *data_size,
            const uint8_t * buf, int buf_size)
{
    AMRContext *s = avctx->priv_data;
    const uint8_t*amrData=buf;
    static const uint8_t block_size[16]={ 12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0 };
    enum Mode dec_mode;
    int packet_size;

    /* av_log(NULL,AV_LOG_DEBUG,"amr_decode_frame buf=%p buf_size=%d frameCount=%d!!\n",buf,buf_size,s->frameCount); */

    dec_mode = (buf[0] >> 3) & 0x000F;
    packet_size = block_size[dec_mode]+1;

    if(packet_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "amr frame too short (%u, should be %u)\n", buf_size, packet_size);
        return -1;
    }

    s->frameCount++;
    /* av_log(NULL,AV_LOG_DEBUG,"packet_size=%d amrData= 0x%X %X %X %X\n",packet_size,amrData[0],amrData[1],amrData[2],amrData[3]); */
    /* call decoder */
    Decoder_Interface_Decode(s->decState, amrData, data, 0);
    *data_size=160*2;

    return packet_size;
}

static int amr_nb_encode_frame(AVCodecContext *avctx,
                            unsigned char *frame/*out*/, int buf_size, void *data/*in*/)
{
    AMRContext *s = avctx->priv_data;
    int written;

    if((s->enc_bitrate=getBitrateMode(avctx->bit_rate))<0)
    {
        av_log(avctx, AV_LOG_ERROR, nb_bitrate_unsupported);
        return -1;
    }

    written = Encoder_Interface_Encode(s->enstate,
        s->enc_bitrate,
        data,
        frame,
        0);
    /* av_log(NULL,AV_LOG_DEBUG,"amr_nb_encode_frame encoded %u bytes, bitrate %u, first byte was %#02x\n",written, s->enc_bitrate, frame[0] ); */

    return written;
}

#endif

#if CONFIG_LIBAMR_NB || CONFIG_LIBAMR_NB_FIXED

AVCodec libamr_nb_decoder =
{
    "libamr_nb",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AMR_NB,
    sizeof(AMRContext),
    amr_nb_decode_init,
    NULL,
    amr_nb_decode_close,
    amr_nb_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("libamr-nb Adaptive Multi-Rate (AMR) Narrow-Band"),
};

AVCodec libamr_nb_encoder =
{
    "libamr_nb",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AMR_NB,
    sizeof(AMRContext),
    amr_nb_encode_init,
    amr_nb_encode_frame,
    amr_nb_encode_close,
    NULL,
    .sample_fmts = (enum SampleFormat[]){SAMPLE_FMT_S16,SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("libamr-nb Adaptive Multi-Rate (AMR) Narrow-Band"),
};

#endif

/* -----------AMR wideband ------------*/
#if CONFIG_LIBAMR_WB

#ifdef _TYPEDEF_H
//To avoid duplicate typedefs from typedef in amr-nb
#define typedef_h
#endif

#include <amrwb/enc_if.h>
#include <amrwb/dec_if.h>
#include <amrwb/if_rom.h>

/* Common code for fixed and float version*/
typedef struct AMRWB_bitrates
{
    int rate;
    int mode;
} AMRWB_bitrates;

static int getWBBitrateMode(int bitrate)
{
    /* make the correspondance between bitrate and mode */
    AMRWB_bitrates rates[]={ {6600,0},
                           {8850,1},
                           {12650,2},
                           {14250,3},
                           {15850,4},
                           {18250,5},
                           {19850,6},
                           {23050,7},
                           {23850,8},
                         };
    int i;

    for(i=0;i<9;i++)
    {
        if(rates[i].rate==bitrate)
        {
            return rates[i].mode;
        }
    }
    /* no bitrate matching, return an error */
    return -1;
}


typedef struct AMRWBContext {
    int frameCount;
    void *state;
    int mode;
    Word16 allow_dtx;
} AMRWBContext;

static int amr_wb_encode_init(AVCodecContext * avctx)
{
    AMRWBContext *s = avctx->priv_data;

    s->frameCount=0;

    if(avctx->sample_rate!=16000)
    {
        av_log(avctx, AV_LOG_ERROR, "Only 16000Hz sample rate supported\n");
        return -1;
    }

    if(avctx->channels!=1)
    {
        av_log(avctx, AV_LOG_ERROR, "Only mono supported\n");
        return -1;
    }

    if((s->mode=getWBBitrateMode(avctx->bit_rate))<0)
    {
        av_log(avctx, AV_LOG_ERROR, wb_bitrate_unsupported);
        return -1;
    }

    avctx->frame_size=320;
    avctx->coded_frame= avcodec_alloc_frame();

    s->state = E_IF_init();
    s->allow_dtx=0;

    return 0;
}

static int amr_wb_encode_close(AVCodecContext * avctx)
{
    AMRWBContext *s = avctx->priv_data;

    E_IF_exit(s->state);
    av_freep(&avctx->coded_frame);
    s->frameCount++;
    return 0;
}

static int amr_wb_encode_frame(AVCodecContext *avctx,
                            unsigned char *frame/*out*/, int buf_size, void *data/*in*/)
{
    AMRWBContext *s = avctx->priv_data;
    int size;

    if((s->mode=getWBBitrateMode(avctx->bit_rate))<0)
    {
        av_log(avctx, AV_LOG_ERROR, wb_bitrate_unsupported);
        return -1;
    }
    size = E_IF_encode(s->state, s->mode, data, frame, s->allow_dtx);
    return size;
}

static int amr_wb_decode_init(AVCodecContext * avctx)
{
    AMRWBContext *s = avctx->priv_data;

    s->frameCount=0;
    s->state = D_IF_init();

    amr_decode_fix_avctx(avctx);

    if(avctx->channels > 1)
    {
        av_log(avctx, AV_LOG_ERROR, "amr_wb: multichannel decoding not supported\n");
        return -1;
    }

    return 0;
}

static int amr_wb_decode_frame(AVCodecContext * avctx,
            void *data, int *data_size,
            const uint8_t * buf, int buf_size)
{
    AMRWBContext *s = avctx->priv_data;
    const uint8_t*amrData=buf;
    int mode;
    int packet_size;
    static const uint8_t block_size[16] = {18, 23, 33, 37, 41, 47, 51, 59, 61, 6, 6, 0, 0, 0, 1, 1};

    if(buf_size==0) {
        /* nothing to do */
        return 0;
    }

    mode = (amrData[0] >> 3) & 0x000F;
    packet_size = block_size[mode];

    if(packet_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "amr frame too short (%u, should be %u)\n", buf_size, packet_size+1);
        return -1;
    }

    s->frameCount++;
    D_IF_decode( s->state, amrData, data, _good_frame);
    *data_size=320*2;
    return packet_size;
}

static int amr_wb_decode_close(AVCodecContext * avctx)
{
    AMRWBContext *s = avctx->priv_data;

    D_IF_exit(s->state);
    return 0;
}

AVCodec libamr_wb_decoder =
{
    "libamr_wb",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AMR_WB,
    sizeof(AMRWBContext),
    amr_wb_decode_init,
    NULL,
    amr_wb_decode_close,
    amr_wb_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("libamr-wb Adaptive Multi-Rate (AMR) Wide-Band"),
};

AVCodec libamr_wb_encoder =
{
    "libamr_wb",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AMR_WB,
    sizeof(AMRWBContext),
    amr_wb_encode_init,
    amr_wb_encode_frame,
    amr_wb_encode_close,
    NULL,
    .sample_fmts = (enum SampleFormat[]){SAMPLE_FMT_S16,SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("libamr-wb Adaptive Multi-Rate (AMR) Wide-Band"),
};

#endif //CONFIG_LIBAMR_WB
