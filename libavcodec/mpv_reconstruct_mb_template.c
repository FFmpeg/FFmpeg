/*
 * MPEG macroblock reconstruction
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

#define NOT_MPEG12_H261        0
#define MAY_BE_MPEG12_H261     1
#define DEFINITELY_MPEG12_H261 2

/* put block[] to dest[] */
static inline void put_dct(MpegEncContext *s,
                           int16_t *block, int i, uint8_t *dest, int line_size, int qscale)
{
    s->dct_unquantize_intra(s, block, i, qscale);
    s->idsp.idct_put(dest, line_size, block);
}

static inline void add_dequant_dct(MpegEncContext *s,
                           int16_t *block, int i, uint8_t *dest, int line_size, int qscale)
{
    if (s->block_last_index[i] >= 0) {
        s->dct_unquantize_inter(s, block, i, qscale);

        s->idsp.idct_add(dest, line_size, block);
    }
}

/* generic function called after a macroblock has been parsed by the
   decoder or after it has been encoded by the encoder.

   Important variables used:
   s->mb_intra : true if intra macroblock
   s->mv_dir   : motion vector direction
   s->mv_type  : motion vector type
   s->mv       : motion vector
   s->interlaced_dct : true if interlaced dct used (mpeg2)
 */
static av_always_inline
void mpv_reconstruct_mb_internal(MpegEncContext *s, int16_t block[12][64],
                                 int lowres_flag, int is_mpeg12)
{
#define IS_MPEG12_H261(s) (is_mpeg12 == MAY_BE_MPEG12_H261 ? ((s)->out_format <= FMT_H261) : is_mpeg12)
    const int mb_xy = s->mb_y * s->mb_stride + s->mb_x;

    s->cur_pic.qscale_table[mb_xy] = s->qscale;

    /* update DC predictors for P macroblocks */
    if (!s->mb_intra) {
        if (is_mpeg12 != DEFINITELY_MPEG12_H261 && (s->h263_pred || s->h263_aic)) {
            if (s->mbintra_table[mb_xy])
                ff_clean_intra_table_entries(s);
        } else {
            s->last_dc[0] =
            s->last_dc[1] =
            s->last_dc[2] = 128 << s->intra_dc_precision;
        }
    } else if (is_mpeg12 != DEFINITELY_MPEG12_H261 && (s->h263_pred || s->h263_aic))
        s->mbintra_table[mb_xy] = 1;

#if IS_ENCODER
    if ((s->avctx->flags & AV_CODEC_FLAG_PSNR) || s->frame_skip_threshold || s->frame_skip_factor ||
        !((s->intra_only || s->pict_type == AV_PICTURE_TYPE_B) &&
          s->avctx->mb_decision != FF_MB_DECISION_RD))  // FIXME precalc
#endif /* IS_ENCODER */
    {
        uint8_t *dest_y = s->dest[0], *dest_cb = s->dest[1], *dest_cr = s->dest[2];
        int dct_linesize, dct_offset;
        const int linesize   = s->cur_pic.linesize[0]; //not s->linesize as this would be wrong for field pics
        const int uvlinesize = s->cur_pic.linesize[1];
        const int block_size = lowres_flag ? 8 >> s->avctx->lowres : 8;

        /* avoid copy if macroblock skipped in last frame too */
        /* skip only during decoding as we might trash the buffers during encoding a bit */
        if (!IS_ENCODER) {
            uint8_t *mbskip_ptr = &s->mbskip_table[mb_xy];

            if (s->mb_skipped) {
                s->mb_skipped = 0;
                av_assert2(s->pict_type!=AV_PICTURE_TYPE_I);
                *mbskip_ptr = 1;
            } else if (!s->cur_pic.reference) {
                *mbskip_ptr = 1;
            } else{
                *mbskip_ptr = 0; /* not skipped */
            }
        }

        dct_linesize = linesize << s->interlaced_dct;
        dct_offset   = s->interlaced_dct ? linesize : linesize * block_size;

        if (!s->mb_intra) {
            /* motion handling */
            /* decoding or more than one mb_type (MC was already done otherwise) */

#if !IS_ENCODER
            if (HAVE_THREADS && is_mpeg12 != DEFINITELY_MPEG12_H261 &&
                s->avctx->active_thread_type & FF_THREAD_FRAME) {
                if (s->mv_dir & MV_DIR_FORWARD) {
                    ff_thread_progress_await(&s->last_pic.ptr->progress,
                                             lowest_referenced_row(s, 0));
                }
                if (s->mv_dir & MV_DIR_BACKWARD) {
                    ff_thread_progress_await(&s->next_pic.ptr->progress,
                                             lowest_referenced_row(s, 1));
                }
            }

            if (lowres_flag) {
                const h264_chroma_mc_func *op_pix = s->h264chroma.put_h264_chroma_pixels_tab;

                if (s->mv_dir & MV_DIR_FORWARD) {
                    MPV_motion_lowres(s, dest_y, dest_cb, dest_cr, 0, s->last_pic.data, op_pix);
                    op_pix = s->h264chroma.avg_h264_chroma_pixels_tab;
                }
                if (s->mv_dir & MV_DIR_BACKWARD) {
                    MPV_motion_lowres(s, dest_y, dest_cb, dest_cr, 1, s->next_pic.data, op_pix);
                }
            } else {
                const op_pixels_func (*op_pix)[4];
                const qpel_mc_func (*op_qpix)[16];

                if ((is_mpeg12 == DEFINITELY_MPEG12_H261 || !s->no_rounding) || s->pict_type == AV_PICTURE_TYPE_B) {
                    op_pix = s->hdsp.put_pixels_tab;
                    op_qpix = s->qdsp.put_qpel_pixels_tab;
                } else {
                    op_pix = s->hdsp.put_no_rnd_pixels_tab;
                    op_qpix = s->qdsp.put_no_rnd_qpel_pixels_tab;
                }
                if (s->mv_dir & MV_DIR_FORWARD) {
                    ff_mpv_motion(s, dest_y, dest_cb, dest_cr, 0, s->last_pic.data, op_pix, op_qpix);
                    op_pix  = s->hdsp.avg_pixels_tab;
                    op_qpix = s->qdsp.avg_qpel_pixels_tab;
                }
                if (s->mv_dir & MV_DIR_BACKWARD) {
                    ff_mpv_motion(s, dest_y, dest_cb, dest_cr, 1, s->next_pic.data, op_pix, op_qpix);
                }
            }

            /* skip dequant / idct if we are really late ;) */
            if (s->avctx->skip_idct) {
                if(  (s->avctx->skip_idct >= AVDISCARD_NONREF && s->pict_type == AV_PICTURE_TYPE_B)
                   ||(s->avctx->skip_idct >= AVDISCARD_NONKEY && s->pict_type != AV_PICTURE_TYPE_I)
                   || s->avctx->skip_idct >= AVDISCARD_ALL)
                    return;
            }

            /* add dct residue */
            if (!(IS_MPEG12_H261(s) || s->msmpeg4_version != MSMP4_UNUSED ||
                  (s->codec_id == AV_CODEC_ID_MPEG4 && !s->mpeg_quant)))
#endif /* !IS_ENCODER */
            {
                add_dequant_dct(s, block[0], 0, dest_y                          , dct_linesize, s->qscale);
                add_dequant_dct(s, block[1], 1, dest_y              + block_size, dct_linesize, s->qscale);
                add_dequant_dct(s, block[2], 2, dest_y + dct_offset             , dct_linesize, s->qscale);
                add_dequant_dct(s, block[3], 3, dest_y + dct_offset + block_size, dct_linesize, s->qscale);

                if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
                    av_assert2(IS_ENCODER || s->chroma_y_shift);
                    if (!IS_ENCODER || s->chroma_y_shift) {
                        add_dequant_dct(s, block[4], 4, dest_cb, uvlinesize, s->chroma_qscale);
                        add_dequant_dct(s, block[5], 5, dest_cr, uvlinesize, s->chroma_qscale);
                    } else {
                        dct_linesize >>= 1;
                        dct_offset   >>= 1;
                        add_dequant_dct(s, block[4], 4, dest_cb,              dct_linesize, s->chroma_qscale);
                        add_dequant_dct(s, block[5], 5, dest_cr,              dct_linesize, s->chroma_qscale);
                        add_dequant_dct(s, block[6], 6, dest_cb + dct_offset, dct_linesize, s->chroma_qscale);
                        add_dequant_dct(s, block[7], 7, dest_cr + dct_offset, dct_linesize, s->chroma_qscale);
                    }
                }
            }
#if !IS_ENCODER
              else if (is_mpeg12 == DEFINITELY_MPEG12_H261 || lowres_flag || (s->codec_id != AV_CODEC_ID_WMV2)) {
                add_dct(s, block[0], 0, dest_y                          , dct_linesize);
                add_dct(s, block[1], 1, dest_y              + block_size, dct_linesize);
                add_dct(s, block[2], 2, dest_y + dct_offset             , dct_linesize);
                add_dct(s, block[3], 3, dest_y + dct_offset + block_size, dct_linesize);

                if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
                    if (s->chroma_y_shift) {//Chroma420
                        add_dct(s, block[4], 4, dest_cb, uvlinesize);
                        add_dct(s, block[5], 5, dest_cr, uvlinesize);
                    } else {
                        //chroma422
                        dct_linesize = uvlinesize << s->interlaced_dct;
                        dct_offset   = s->interlaced_dct ? uvlinesize : uvlinesize*block_size;

                        add_dct(s, block[4], 4, dest_cb, dct_linesize);
                        add_dct(s, block[5], 5, dest_cr, dct_linesize);
                        add_dct(s, block[6], 6, dest_cb+dct_offset, dct_linesize);
                        add_dct(s, block[7], 7, dest_cr+dct_offset, dct_linesize);
                        if (!s->chroma_x_shift) {//Chroma444
                            add_dct(s, block[8], 8, dest_cb+block_size, dct_linesize);
                            add_dct(s, block[9], 9, dest_cr+block_size, dct_linesize);
                            add_dct(s, block[10], 10, dest_cb+block_size+dct_offset, dct_linesize);
                            add_dct(s, block[11], 11, dest_cr+block_size+dct_offset, dct_linesize);
                        }
                    }
                } //fi gray
            } else if (CONFIG_WMV2_DECODER) {
                ff_wmv2_add_mb(s, block, dest_y, dest_cb, dest_cr);
            }
#endif /* !IS_ENCODER */
        } else {
#if !IS_ENCODER
            /* Only MPEG-4 Simple Studio Profile is supported in > 8-bit mode.
               TODO: Integrate 10-bit properly into mpegvideo.c so that ER works properly */
            if (is_mpeg12 != DEFINITELY_MPEG12_H261 && CONFIG_MPEG4_DECODER &&
                /* s->codec_id == AV_CODEC_ID_MPEG4 && */
                s->avctx->bits_per_raw_sample > 8) {
                ff_mpeg4_decode_studio(s, dest_y, dest_cb, dest_cr, block_size,
                                       uvlinesize, dct_linesize, dct_offset);
            } else if (!IS_MPEG12_H261(s))
#endif /* !IS_ENCODER */
            {
                /* dct only in intra block */
                put_dct(s, block[0], 0, dest_y                          , dct_linesize, s->qscale);
                put_dct(s, block[1], 1, dest_y              + block_size, dct_linesize, s->qscale);
                put_dct(s, block[2], 2, dest_y + dct_offset             , dct_linesize, s->qscale);
                put_dct(s, block[3], 3, dest_y + dct_offset + block_size, dct_linesize, s->qscale);

                if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
                    if (s->chroma_y_shift) {
                        put_dct(s, block[4], 4, dest_cb, uvlinesize, s->chroma_qscale);
                        put_dct(s, block[5], 5, dest_cr, uvlinesize, s->chroma_qscale);
                    } else {
                        dct_offset >>=1;
                        dct_linesize >>=1;
                        put_dct(s, block[4], 4, dest_cb,              dct_linesize, s->chroma_qscale);
                        put_dct(s, block[5], 5, dest_cr,              dct_linesize, s->chroma_qscale);
                        put_dct(s, block[6], 6, dest_cb + dct_offset, dct_linesize, s->chroma_qscale);
                        put_dct(s, block[7], 7, dest_cr + dct_offset, dct_linesize, s->chroma_qscale);
                    }
                }
            }
#if !IS_ENCODER
              else {
                s->idsp.idct_put(dest_y,                           dct_linesize, block[0]);
                s->idsp.idct_put(dest_y              + block_size, dct_linesize, block[1]);
                s->idsp.idct_put(dest_y + dct_offset,              dct_linesize, block[2]);
                s->idsp.idct_put(dest_y + dct_offset + block_size, dct_linesize, block[3]);

                if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
                    if (s->chroma_y_shift) {
                        s->idsp.idct_put(dest_cb, uvlinesize, block[4]);
                        s->idsp.idct_put(dest_cr, uvlinesize, block[5]);
                    } else {
                        dct_linesize = uvlinesize << s->interlaced_dct;
                        dct_offset   = s->interlaced_dct ? uvlinesize : uvlinesize*block_size;

                        s->idsp.idct_put(dest_cb,              dct_linesize, block[4]);
                        s->idsp.idct_put(dest_cr,              dct_linesize, block[5]);
                        s->idsp.idct_put(dest_cb + dct_offset, dct_linesize, block[6]);
                        s->idsp.idct_put(dest_cr + dct_offset, dct_linesize, block[7]);
                        if (!s->chroma_x_shift) { //Chroma444
                            s->idsp.idct_put(dest_cb + block_size,              dct_linesize, block[8]);
                            s->idsp.idct_put(dest_cr + block_size,              dct_linesize, block[9]);
                            s->idsp.idct_put(dest_cb + block_size + dct_offset, dct_linesize, block[10]);
                            s->idsp.idct_put(dest_cr + block_size + dct_offset, dct_linesize, block[11]);
                        }
                    }
                } //gray
            }
#endif /* !IS_ENCODER */
        }
    }
}

