
#include "avcodec.h"

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
