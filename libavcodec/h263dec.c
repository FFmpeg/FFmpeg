/*
 * H263 decoder
 * Copyright (c) 2001 Fabrice Bellard.
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
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

//#define DEBUG
//#define PRINT_FRAME_TIME
#ifdef PRINT_FRAME_TIME
static inline long long rdtsc()
{
	long long l;
	asm volatile(	"rdtsc\n\t"
		: "=A" (l)
	);
//	printf("%d\n", int(l/1000));
	return l;
}
#endif

static int h263_decode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

    s->avctx = avctx;
    s->out_format = FMT_H263;

    s->width = avctx->width;
    s->height = avctx->height;
    s->workaround_bugs= avctx->workaround_bugs;

    /* select sub codec */
    switch(avctx->codec->id) {
    case CODEC_ID_H263:
        s->gob_number = 0;
        s->first_slice_line = 0;
        break;
    case CODEC_ID_MPEG4:
        s->time_increment_bits = 4; /* default value for broken headers */
        s->h263_pred = 1;
        s->has_b_frames = 1; //default, might be overriden in the vol header during header parsing
        break;
    case CODEC_ID_MSMPEG4V1:
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->msmpeg4_version=1;
        break;
    case CODEC_ID_MSMPEG4V2:
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->msmpeg4_version=2;
        break;
    case CODEC_ID_MSMPEG4V3:
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->msmpeg4_version=3;
        break;
    case CODEC_ID_WMV1:
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->msmpeg4_version=4;
        break;
    case CODEC_ID_WMV2:
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->msmpeg4_version=5;
        break;
    case CODEC_ID_H263I:
        s->h263_intel = 1;
        break;
    default:
        return -1;
    }
    s->codec_id= avctx->codec->id;

    /* for h263, we allocate the images after having read the header */
    if (avctx->codec->id != CODEC_ID_H263 && avctx->codec->id != CODEC_ID_MPEG4)
        if (MPV_common_init(s) < 0)
            return -1;

    if (s->h263_msmpeg4)
        ff_msmpeg4_decode_init(s);
    else
        h263_decode_init_vlc(s);
    
    return 0;
}

static int h263_decode_end(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

    MPV_common_end(s);
    return 0;
}

/**
 * retunrs the number of bytes consumed for building the current frame
 */
static int get_consumed_bytes(MpegEncContext *s, int buf_size){
    int pos= (get_bits_count(&s->gb)+7)>>3;

    if(s->divx_version>=500){
        //we would have to scan through the whole buf to handle the weird reordering ...
        return buf_size; 
    }else{
        if(pos==0) pos=1; //avoid infinite loops (i doubt thats needed but ...)
        if(pos+10>buf_size) pos=buf_size; // oops ;)

        return pos;
    }
}

static int h263_decode_frame(AVCodecContext *avctx, 
                             void *data, int *data_size,
                             UINT8 *buf, int buf_size)
{
    MpegEncContext *s = avctx->priv_data;
    int ret;
    AVPicture *pict = data; 
#ifdef PRINT_FRAME_TIME
uint64_t time= rdtsc();
#endif
#ifdef DEBUG
    printf("*****frame %d size=%d\n", avctx->frame_number, buf_size);
    printf("bytes=%x %x %x %x\n", buf[0], buf[1], buf[2], buf[3]);
#endif

    s->hurry_up= avctx->hurry_up;
    s->error_resilience= avctx->error_resilience;
    s->workaround_bugs= avctx->workaround_bugs;
    s->flags= avctx->flags;

    *data_size = 0;
   
   /* no supplementary picture */
    if (buf_size == 0) {
        return 0;
    }

    if(s->bitstream_buffer_size && buf_size<20){ //divx 5.01+ frame reorder
        init_get_bits(&s->gb, s->bitstream_buffer, s->bitstream_buffer_size);
    }else
        init_get_bits(&s->gb, buf, buf_size);
    s->bitstream_buffer_size=0;

    /* let's go :-) */
    if (s->h263_msmpeg4) {
        ret = msmpeg4_decode_picture_header(s);
    } else if (s->h263_pred) {
        ret = mpeg4_decode_picture_header(s);
        s->has_b_frames= !s->low_delay;
    } else if (s->h263_intel) {
        ret = intel_h263_decode_picture_header(s);
    } else {
        ret = h263_decode_picture_header(s);
    }
    avctx->has_b_frames= s->has_b_frames;
#if 0 // dump bits per frame / qp / complexity
{
    static FILE *f=NULL;
    if(!f) f=fopen("rate_qp_cplx.txt", "w");
    fprintf(f, "%d %d %f\n", buf_size, s->qscale, buf_size*(double)s->qscale);
}
#endif
       
        /* After H263 & mpeg4 header decode we have the height, width,*/
        /* and other parameters. So then we could init the picture   */
        /* FIXME: By the way H263 decoder is evolving it should have */
        /* an H263EncContext                                         */
    if (s->width != avctx->width || s->height != avctx->height) {
        /* H.263 could change picture size any time */
        MPV_common_end(s);
        s->context_initialized=0;
    }
    if (!s->context_initialized) {
        avctx->width = s->width;
        avctx->height = s->height;
        avctx->aspect_ratio_info= s->aspect_ratio_info;
	if (s->aspect_ratio_info == FF_ASPECT_EXTENDED)
	{
	    avctx->aspected_width = s->aspected_width;
	    avctx->aspected_height = s->aspected_height;
	}
        if (MPV_common_init(s) < 0)
            return -1;
    }

    if(ret==FRAME_SKIPED) return get_consumed_bytes(s, buf_size);
    /* skip if the header was thrashed */
    if (ret < 0){
        fprintf(stderr, "header damaged\n");
        return -1;
    }
    /* skip b frames if we dont have reference frames */
    if(s->num_available_buffers<2 && s->pict_type==B_TYPE) return get_consumed_bytes(s, buf_size);
    /* skip b frames if we are in a hurry */
    if(s->hurry_up && s->pict_type==B_TYPE) return get_consumed_bytes(s, buf_size);
    
    if(s->next_p_frame_damaged){
        if(s->pict_type==B_TYPE)
            return get_consumed_bytes(s, buf_size);
        else
            s->next_p_frame_damaged=0;
    }

    MPV_frame_start(s, avctx);

#ifdef DEBUG
    printf("qscale=%d\n", s->qscale);
#endif

    /* init resync/ error resilience specific variables */
    s->next_resync_qscale= s->qscale;
    s->next_resync_gb= s->gb;
    if(s->resync_marker) s->mb_num_left= 0;
    else                 s->mb_num_left= s->mb_num;

    /* decode each macroblock */
    s->block_wrap[0]=
    s->block_wrap[1]=
    s->block_wrap[2]=
    s->block_wrap[3]= s->mb_width*2 + 2;
    s->block_wrap[4]=
    s->block_wrap[5]= s->mb_width + 2;
    for(s->mb_y=0; s->mb_y < s->mb_height; s->mb_y++) {
        /* Check for GOB headers on H.263 */
        /* FIXME: In the future H.263+ will have intra prediction */
        /* and we are gonna need another way to detect MPEG4      */
        if (s->mb_y && !s->h263_pred) {
            s->first_slice_line = h263_decode_gob_header(s);
        }
        
        if(s->msmpeg4_version==1){
            s->last_dc[0]=
            s->last_dc[1]=
            s->last_dc[2]= 128;
        }

        s->y_dc_scale= s->y_dc_scale_table[ s->qscale ];
        s->c_dc_scale= s->c_dc_scale_table[ s->qscale ];

        s->block_index[0]= s->block_wrap[0]*(s->mb_y*2 + 1) - 1;
        s->block_index[1]= s->block_wrap[0]*(s->mb_y*2 + 1);
        s->block_index[2]= s->block_wrap[0]*(s->mb_y*2 + 2) - 1;
        s->block_index[3]= s->block_wrap[0]*(s->mb_y*2 + 2);
        s->block_index[4]= s->block_wrap[4]*(s->mb_y + 1)                    + s->block_wrap[0]*(s->mb_height*2 + 2);
        s->block_index[5]= s->block_wrap[4]*(s->mb_y + 1 + s->mb_height + 2) + s->block_wrap[0]*(s->mb_height*2 + 2);
        for(s->mb_x=0; s->mb_x < s->mb_width; s->mb_x++) {
            s->block_index[0]+=2;
            s->block_index[1]+=2;
            s->block_index[2]+=2;
            s->block_index[3]+=2;
            s->block_index[4]++;
            s->block_index[5]++;
#ifdef DEBUG
            printf("**mb x=%d y=%d\n", s->mb_x, s->mb_y);
#endif

            if(s->resync_marker){
                if(s->mb_num_left<=0){
                    /* except the first block */
                    if(s->mb_x!=0 || s->mb_y!=0){
                        /* did we miss the next resync marker without noticing an error yet */
                        if(((get_bits_count(&s->gb)+8)&(~7)) != s->next_resync_pos && s->decoding_error==0){
                            fprintf(stderr, "slice end missmatch x:%d y:%d %d %d\n", 
                                    s->mb_x, s->mb_y, get_bits_count(&s->gb), s->next_resync_pos);
                            ff_conceal_past_errors(s, 1);
                        }
                    }
                    s->qscale= s->next_resync_qscale;
                    s->y_dc_scale= s->y_dc_scale_table[ s->qscale ];
                    s->c_dc_scale= s->c_dc_scale_table[ s->qscale ];

                    s->gb= s->next_resync_gb;
                    s->resync_mb_x= s->mb_x; //we know that the marker is here cuz mb_num_left was the distance to it
                    s->resync_mb_y= s->mb_y;
                    s->first_slice_line=1;

                    if(s->codec_id==CODEC_ID_MPEG4){
                        ff_mpeg4_clean_buffers(s);
                        ff_mpeg4_resync(s);
                    }
                }

                if(   s->resync_mb_x==s->mb_x 
                   && s->resync_mb_y==s->mb_y && s->decoding_error!=0){
                    fprintf(stderr, "resynced at %d %d\n", s->mb_x, s->mb_y);
                    s->decoding_error= 0;
                }
            }

            //fprintf(stderr,"\nFrame: %d\tMB: %d",avctx->frame_number, (s->mb_y * s->mb_width) + s->mb_x);
            /* DCT & quantize */
            if(s->decoding_error!=DECODING_DESYNC){
                int last_error= s->decoding_error;
                clear_blocks(s->block[0]);
            
                s->mv_dir = MV_DIR_FORWARD;
                s->mv_type = MV_TYPE_16X16;
                if (s->h263_msmpeg4) {
                    if (msmpeg4_decode_mb(s, s->block) < 0) {
                        fprintf(stderr,"Error at MB: %d\n", (s->mb_y * s->mb_width) + s->mb_x);
                        s->decoding_error=DECODING_DESYNC;
                    }
                } else {
                    if (h263_decode_mb(s, s->block) < 0) {
                        fprintf(stderr,"Error at MB: %d\n", (s->mb_y * s->mb_width) + s->mb_x);
                        s->decoding_error=DECODING_DESYNC;
                    }
                }

                if(s->decoding_error!=last_error){
                    ff_conceal_past_errors(s, 0);
                }
            }

            /* conceal errors */
            if(    s->decoding_error==DECODING_DESYNC
               || (s->decoding_error==DECODING_ACDC_LOST && s->mb_intra)){
                s->mv_dir = MV_DIR_FORWARD;
                s->mv_type = MV_TYPE_16X16;
                s->mb_skiped=0;
                s->mb_intra=0;
                s->mv[0][0][0]=0; //FIXME this is not optimal 
                s->mv[0][0][1]=0;
                clear_blocks(s->block[0]);
            }else if(s->decoding_error && !s->mb_intra){
                clear_blocks(s->block[0]);
            }
            //FIXME remove AC for intra
                        
            MPV_decode_mb(s, s->block);

            s->mb_num_left--;            
        }
        if (    avctx->draw_horiz_band 
            && (s->num_available_buffers>=1 || (!s->has_b_frames)) ) {
            UINT8 *src_ptr[3];
            int y, h, offset;
            y = s->mb_y * 16;
            h = s->height - y;
            if (h > 16)
                h = 16;
            offset = y * s->linesize;
            if(s->pict_type==B_TYPE || (!s->has_b_frames)){
                src_ptr[0] = s->current_picture[0] + offset;
                src_ptr[1] = s->current_picture[1] + (offset >> 2);
                src_ptr[2] = s->current_picture[2] + (offset >> 2);
            } else {
                src_ptr[0] = s->last_picture[0] + offset;
                src_ptr[1] = s->last_picture[1] + (offset >> 2);
                src_ptr[2] = s->last_picture[2] + (offset >> 2);
            }
            avctx->draw_horiz_band(avctx, src_ptr, s->linesize,
                                   y, s->width, h);
        }
    }
    
    if (s->h263_msmpeg4 && s->msmpeg4_version<4 && s->pict_type==I_TYPE)
        if(msmpeg4_decode_ext_header(s, buf_size) < 0) return -1;
    
    /* divx 5.01+ bistream reorder stuff */
    if(s->codec_id==CODEC_ID_MPEG4 && s->bitstream_buffer_size==0 && s->divx_version>=500){
        int current_pos= get_bits_count(&s->gb)>>3;

        if(   buf_size - current_pos > 5 
           && buf_size - current_pos < BITSTREAM_BUFFER_SIZE){
            int i;
            int startcode_found=0;
            for(i=current_pos; i<buf_size-3; i++){
                if(buf[i]==0 && buf[i+1]==0 && buf[i+2]==1 && buf[i+3]==0xB6){
                    startcode_found=1;
                    break;
                }
            }
            if(startcode_found){
                memcpy(s->bitstream_buffer, buf + current_pos, buf_size - current_pos);
                s->bitstream_buffer_size= buf_size - current_pos;
            }
        }
    }

    if(s->bitstream_buffer_size==0 && s->error_resilience>0){
        int left= s->gb.size*8 - get_bits_count(&s->gb);
        int max_extra=8;
        
        if(s->codec_id==CODEC_ID_MPEG4) max_extra+=32;

        if(left>max_extra){
            fprintf(stderr, "discarding %d junk bits at end, next would be %X\n", left, show_bits(&s->gb, 24));
            if(s->decoding_error==0)
                ff_conceal_past_errors(s, 1);
        }
        if(left<0){
            fprintf(stderr, "overreading %d bits\n", -left);
            if(s->decoding_error==0)
                ff_conceal_past_errors(s, 1);
        }
    }
  
    MPV_frame_end(s);
#if 0 //dirty show MVs, we should export the MV tables and write a filter to show them
{
  int mb_y;
  s->has_b_frames=1;
  for(mb_y=0; mb_y<s->mb_height; mb_y++){
    int mb_x;
    int y= mb_y*16 + 8;
    for(mb_x=0; mb_x<s->mb_width; mb_x++){
      int x= mb_x*16 + 8;
      uint8_t *ptr= s->last_picture[0];
      int xy= 1 + mb_x*2 + (mb_y*2 + 1)*(s->mb_width*2 + 2);
      int mx= (s->motion_val[xy][0]>>1) + x;
      int my= (s->motion_val[xy][1]>>1) + y;
      int i;
      int max;

      if(mx<0) mx=0;
      if(my<0) my=0;
      if(mx>=s->width)  mx= s->width -1;
      if(my>=s->height) my= s->height-1;
      max= ABS(mx-x);
      if(ABS(my-y) > max) max= ABS(my-y);
      /* the ugliest linedrawing routine ... */
      for(i=0; i<max; i++){
        int x1= x + (mx-x)*i/max;
        int y1= y + (my-y)*i/max;
        ptr[y1*s->linesize + x1]+=100;
      }
      ptr[y*s->linesize + x]+=100;
      s->mbskip_table[mb_x + mb_y*s->mb_width]=0;
    }
  }

}
#endif    
    if(s->pict_type==B_TYPE || (!s->has_b_frames)){
        pict->data[0] = s->current_picture[0];
        pict->data[1] = s->current_picture[1];
        pict->data[2] = s->current_picture[2];
    } else {
        pict->data[0] = s->last_picture[0];
        pict->data[1] = s->last_picture[1];
        pict->data[2] = s->last_picture[2];
    }
    pict->linesize[0] = s->linesize;
    pict->linesize[1] = s->uvlinesize;
    pict->linesize[2] = s->uvlinesize;

    avctx->quality = s->qscale;

    /* Return the Picture timestamp as the frame number */
    /* we substract 1 because it is added on utils.c    */
    avctx->frame_number = s->picture_number - 1;

    /* dont output the last pic after seeking 
       note we allready added +1 for the current pix in MPV_frame_end(s) */
    if(s->num_available_buffers>=2 || (!s->has_b_frames))
        *data_size = sizeof(AVPicture);
#ifdef PRINT_FRAME_TIME
printf("%Ld\n", rdtsc()-time);
#endif
    return get_consumed_bytes(s, buf_size);
}

AVCodec mpeg4_decoder = {
    "mpeg4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG4,
    sizeof(MpegEncContext),
    h263_decode_init,
    NULL,
    h263_decode_end,
    h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
};

AVCodec h263_decoder = {
    "h263",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H263,
    sizeof(MpegEncContext),
    h263_decode_init,
    NULL,
    h263_decode_end,
    h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
};

AVCodec msmpeg4v1_decoder = {
    "msmpeg4v1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V1,
    sizeof(MpegEncContext),
    h263_decode_init,
    NULL,
    h263_decode_end,
    h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
};

AVCodec msmpeg4v2_decoder = {
    "msmpeg4v2",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V2,
    sizeof(MpegEncContext),
    h263_decode_init,
    NULL,
    h263_decode_end,
    h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
};

AVCodec msmpeg4v3_decoder = {
    "msmpeg4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V3,
    sizeof(MpegEncContext),
    h263_decode_init,
    NULL,
    h263_decode_end,
    h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
};

AVCodec wmv1_decoder = {
    "wmv1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_WMV1,
    sizeof(MpegEncContext),
    h263_decode_init,
    NULL,
    h263_decode_end,
    h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
};

AVCodec wmv2_decoder = {
    "wmv2",
    CODEC_TYPE_VIDEO,
    CODEC_ID_WMV2,
    sizeof(MpegEncContext),
    h263_decode_init,
    NULL,
    h263_decode_end,
    h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
};

AVCodec h263i_decoder = {
    "h263i",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H263I,
    sizeof(MpegEncContext),
    h263_decode_init,
    NULL,
    h263_decode_end,
    h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
};

