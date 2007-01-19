/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include "avcodec.h"
#include "mpegaudio.h"

AVBitStreamFilter *first_bitstream_filter= NULL;

void av_register_bitstream_filter(AVBitStreamFilter *bsf){
    bsf->next = first_bitstream_filter;
    first_bitstream_filter= bsf;
}

AVBitStreamFilterContext *av_bitstream_filter_init(const char *name){
    AVBitStreamFilter *bsf= first_bitstream_filter;

    while(bsf){
        if(!strcmp(name, bsf->name)){
            AVBitStreamFilterContext *bsfc= av_mallocz(sizeof(AVBitStreamFilterContext));
            bsfc->filter= bsf;
            bsfc->priv_data= av_mallocz(bsf->priv_data_size);
            return bsfc;
        }
        bsf= bsf->next;
    }
    return NULL;
}

void av_bitstream_filter_close(AVBitStreamFilterContext *bsfc){
    av_freep(&bsfc->priv_data);
    av_parser_close(bsfc->parser);
    av_free(bsfc);
}

int av_bitstream_filter_filter(AVBitStreamFilterContext *bsfc,
                               AVCodecContext *avctx, const char *args,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe){
    *poutbuf= (uint8_t *) buf;
    *poutbuf_size= buf_size;
    return bsfc->filter->filter(bsfc, avctx, args, poutbuf, poutbuf_size, buf, buf_size, keyframe);
}

static int dump_extradata(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe){
    int cmd= args ? *args : 0;
    /* cast to avoid warning about discarding qualifiers */
    if(avctx->extradata){
        if(  (keyframe && (avctx->flags2 & CODEC_FLAG2_LOCAL_HEADER) && cmd=='a')
           ||(keyframe && (cmd=='k' || !cmd))
           ||(cmd=='e')
            /*||(? && (s->flags & PARSER_FLAG_DUMP_EXTRADATA_AT_BEGIN)*/){
            int size= buf_size + avctx->extradata_size;
            *poutbuf_size= size;
            *poutbuf= av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);

            memcpy(*poutbuf, avctx->extradata, avctx->extradata_size);
            memcpy((*poutbuf) + avctx->extradata_size, buf, buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
            return 1;
        }
    }
    return 0;
}

static int remove_extradata(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe){
    int cmd= args ? *args : 0;
    AVCodecParserContext *s;

    if(!bsfc->parser){
        bsfc->parser= av_parser_init(avctx->codec_id);
    }
    s= bsfc->parser;

    if(s && s->parser->split){
        if(  (((avctx->flags & CODEC_FLAG_GLOBAL_HEADER) || (avctx->flags2 & CODEC_FLAG2_LOCAL_HEADER)) && cmd=='a')
           ||(!keyframe && cmd=='k')
           ||(cmd=='e' || !cmd)
          ){
            int i= s->parser->split(avctx, buf, buf_size);
            buf += i;
            buf_size -= i;
        }
    }
    *poutbuf= (uint8_t *) buf;
    *poutbuf_size= buf_size;

    return 0;
}

static int noise(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe){
    int amount= args ? atoi(args) : 10000;
    unsigned int *state= bsfc->priv_data;
    int i;

    *poutbuf= av_malloc(buf_size + FF_INPUT_BUFFER_PADDING_SIZE);

    memcpy(*poutbuf, buf, buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
    for(i=0; i<buf_size; i++){
        (*state) += (*poutbuf)[i] + 1;
        if(*state % amount == 0)
            (*poutbuf)[i] = *state;
    }
    return 1;
}

#define MP3_MASK 0xFFFE0CCF

static int mp3_header_compress(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe){
    uint32_t header, extraheader;
    int mode_extension, header_size;

    if(avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL){
        av_log(avctx, AV_LOG_ERROR, "not standards compliant\n");
        return -1;
    }

    header = AV_RB32(buf);
    mode_extension= (header>>4)&3;

    if(ff_mpa_check_header(header) < 0 || (header&0x60000) != 0x20000){
output_unchanged:
        *poutbuf= (uint8_t *) buf;
        *poutbuf_size= buf_size;

        av_log(avctx, AV_LOG_INFO, "cannot compress %08X\n", header);
        return 0;
    }

    if(avctx->extradata_size == 0){
        avctx->extradata_size=15;
        avctx->extradata= av_malloc(avctx->extradata_size);
        strcpy(avctx->extradata, "FFCMP3 0.0");
        memcpy(avctx->extradata+11, buf, 4);
    }
    if(avctx->extradata_size != 15){
        av_log(avctx, AV_LOG_ERROR, "Extradata invalid\n");
        return -1;
    }
    extraheader = AV_RB32(avctx->extradata+11);
    if((extraheader&MP3_MASK) != (header&MP3_MASK))
        goto output_unchanged;

    header_size= (header&0x10000) ? 4 : 6;

    *poutbuf_size= buf_size - header_size;
    *poutbuf= av_malloc(buf_size - header_size + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(*poutbuf, buf + header_size, buf_size - header_size + FF_INPUT_BUFFER_PADDING_SIZE);

    if(avctx->channels==2){
        if((header & (3<<19)) != 3<<19){
            (*poutbuf)[1] &= 0x3F;
            (*poutbuf)[1] |= mode_extension<<6;
            FFSWAP(int, (*poutbuf)[1], (*poutbuf)[2]);
        }else{
            (*poutbuf)[1] &= 0x8F;
            (*poutbuf)[1] |= mode_extension<<4;
        }
    }

    return 1;
}

static int mp3_header_decompress(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe){
    uint32_t header;
    int sample_rate= avctx->sample_rate;
    int sample_rate_index=0;
    int lsf, mpeg25, bitrate_index, frame_size;

    header = AV_RB32(buf);
    if(ff_mpa_check_header(header) >= 0){
        *poutbuf= (uint8_t *) buf;
        *poutbuf_size= buf_size;

        return 0;
    }

    if(avctx->extradata_size != 15 || strcmp(avctx->extradata, "FFCMP3 0.0")){
        av_log(avctx, AV_LOG_ERROR, "Extradata invalid %d\n", avctx->extradata_size);
        return -1;
    }

    header= AV_RB32(avctx->extradata+11) & MP3_MASK;

    lsf     = sample_rate < (24000+32000)/2;
    mpeg25  = sample_rate < (12000+16000)/2;
    sample_rate_index= (header>>10)&3;
    sample_rate= mpa_freq_tab[sample_rate_index] >> (lsf + mpeg25); //in case sample rate is a little off

    for(bitrate_index=2; bitrate_index<30; bitrate_index++){
        frame_size = mpa_bitrate_tab[lsf][2][bitrate_index>>1];
        frame_size = (frame_size * 144000) / (sample_rate << lsf) + (bitrate_index&1);
        if(frame_size == buf_size + 4)
            break;
        if(frame_size == buf_size + 6)
            break;
    }
    if(bitrate_index == 30){
        av_log(avctx, AV_LOG_ERROR, "couldnt find bitrate_index\n");
        return -1;
    }

    header |= (bitrate_index&1)<<9;
    header |= (bitrate_index>>1)<<12;
    header |= (frame_size == buf_size + 4)<<16; //FIXME actually set a correct crc instead of 0

    *poutbuf_size= frame_size;
    *poutbuf= av_malloc(frame_size + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(*poutbuf + frame_size - buf_size, buf, buf_size + FF_INPUT_BUFFER_PADDING_SIZE);

    if(avctx->channels==2){
        uint8_t *p= *poutbuf + frame_size - buf_size;
        if(lsf){
            FFSWAP(int, p[1], p[2]);
            header |= (p[1] & 0xC0)>>2;
            p[1] &= 0x3F;
        }else{
            header |= p[1] & 0x30;
            p[1] &= 0xCF;
        }
    }

    (*poutbuf)[0]= header>>24;
    (*poutbuf)[1]= header>>16;
    (*poutbuf)[2]= header>> 8;
    (*poutbuf)[3]= header    ;

    return 1;
}

AVBitStreamFilter dump_extradata_bsf={
    "dump_extra",
    0,
    dump_extradata,
};

AVBitStreamFilter remove_extradata_bsf={
    "remove_extra",
    0,
    remove_extradata,
};

AVBitStreamFilter noise_bsf={
    "noise",
    sizeof(int),
    noise,
};

AVBitStreamFilter mp3_header_compress_bsf={
    "mp3comp",
    0,
    mp3_header_compress,
};

AVBitStreamFilter mp3_header_decompress_bsf={
    "mp3decomp",
    0,
    mp3_header_decompress,
};
