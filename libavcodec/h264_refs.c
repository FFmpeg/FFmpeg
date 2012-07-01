/*
 * H.26L/H.264/AVC/JVT/14496-10/... reference picture handling
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * @file
 * H.264 / AVC / MPEG4 part10  reference picture handling.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libavutil/avassert.h"
#include "internal.h"
#include "dsputil.h"
#include "avcodec.h"
#include "h264.h"
#include "golomb.h"

//#undef NDEBUG
#include <assert.h>


static void pic_as_field(Picture *pic, const int parity){
    int i;
    for (i = 0; i < 4; ++i) {
        if (parity == PICT_BOTTOM_FIELD)
            pic->f.data[i] += pic->f.linesize[i];
        pic->f.reference    = parity;
        pic->f.linesize[i] *= 2;
    }
    pic->poc= pic->field_poc[parity == PICT_BOTTOM_FIELD];
}

static int split_field_copy(Picture *dest, Picture *src,
                            int parity, int id_add){
    int match = !!(src->f.reference & parity);

    if (match) {
        *dest = *src;
        if(parity != PICT_FRAME){
            pic_as_field(dest, parity);
            dest->pic_id *= 2;
            dest->pic_id += id_add;
        }
    }

    return match;
}

static int build_def_list(Picture *def, Picture **in, int len, int is_long, int sel){
    int i[2]={0};
    int index=0;

    while(i[0]<len || i[1]<len){
        while (i[0] < len && !(in[ i[0] ] && (in[ i[0] ]->f.reference & sel)))
            i[0]++;
        while (i[1] < len && !(in[ i[1] ] && (in[ i[1] ]->f.reference & (sel^3))))
            i[1]++;
        if(i[0] < len){
            in[ i[0] ]->pic_id= is_long ? i[0] : in[ i[0] ]->frame_num;
            split_field_copy(&def[index++], in[ i[0]++ ], sel  , 1);
        }
        if(i[1] < len){
            in[ i[1] ]->pic_id= is_long ? i[1] : in[ i[1] ]->frame_num;
            split_field_copy(&def[index++], in[ i[1]++ ], sel^3, 0);
        }
    }

    return index;
}

static int add_sorted(Picture **sorted, Picture **src, int len, int limit, int dir){
    int i, best_poc;
    int out_i= 0;

    for(;;){
        best_poc= dir ? INT_MIN : INT_MAX;

        for(i=0; i<len; i++){
            const int poc= src[i]->poc;
            if(((poc > limit) ^ dir) && ((poc < best_poc) ^ dir)){
                best_poc= poc;
                sorted[out_i]= src[i];
            }
        }
        if(best_poc == (dir ? INT_MIN : INT_MAX))
            break;
        limit= sorted[out_i++]->poc - dir;
    }
    return out_i;
}

int ff_h264_fill_default_ref_list(H264Context *h){
    MpegEncContext * const s = &h->s;
    int i, len;

    if(h->slice_type_nos==AV_PICTURE_TYPE_B){
        Picture *sorted[32];
        int cur_poc, list;
        int lens[2];

        if(FIELD_PICTURE)
            cur_poc= s->current_picture_ptr->field_poc[ s->picture_structure == PICT_BOTTOM_FIELD ];
        else
            cur_poc= s->current_picture_ptr->poc;

        for(list= 0; list<2; list++){
            len= add_sorted(sorted    , h->short_ref, h->short_ref_count, cur_poc, 1^list);
            len+=add_sorted(sorted+len, h->short_ref, h->short_ref_count, cur_poc, 0^list);
            assert(len<=32);
            len= build_def_list(h->default_ref_list[list]    , sorted     , len, 0, s->picture_structure);
            len+=build_def_list(h->default_ref_list[list]+len, h->long_ref, 16 , 1, s->picture_structure);
            assert(len<=32);

            if(len < h->ref_count[list])
                memset(&h->default_ref_list[list][len], 0, sizeof(Picture)*(h->ref_count[list] - len));
            lens[list]= len;
        }

        if(lens[0] == lens[1] && lens[1] > 1){
            for (i = 0; h->default_ref_list[0][i].f.data[0] == h->default_ref_list[1][i].f.data[0] && i < lens[0]; i++);
            if(i == lens[0])
                FFSWAP(Picture, h->default_ref_list[1][0], h->default_ref_list[1][1]);
        }
    }else{
        len = build_def_list(h->default_ref_list[0]    , h->short_ref, h->short_ref_count, 0, s->picture_structure);
        len+= build_def_list(h->default_ref_list[0]+len, h-> long_ref, 16                , 1, s->picture_structure);
        assert(len <= 32);
        if(len < h->ref_count[0])
            memset(&h->default_ref_list[0][len], 0, sizeof(Picture)*(h->ref_count[0] - len));
    }
#ifdef TRACE
    for (i=0; i<h->ref_count[0]; i++) {
        tprintf(h->s.avctx, "List0: %s fn:%d 0x%p\n", (h->default_ref_list[0][i].long_ref ? "LT" : "ST"), h->default_ref_list[0][i].pic_id, h->default_ref_list[0][i].data[0]);
    }
    if(h->slice_type_nos==AV_PICTURE_TYPE_B){
        for (i=0; i<h->ref_count[1]; i++) {
            tprintf(h->s.avctx, "List1: %s fn:%d 0x%p\n", (h->default_ref_list[1][i].long_ref ? "LT" : "ST"), h->default_ref_list[1][i].pic_id, h->default_ref_list[1][i].data[0]);
        }
    }
#endif
    return 0;
}

static void print_short_term(H264Context *h);
static void print_long_term(H264Context *h);

/**
 * Extract structure information about the picture described by pic_num in
 * the current decoding context (frame or field). Note that pic_num is
 * picture number without wrapping (so, 0<=pic_num<max_pic_num).
 * @param pic_num picture number for which to extract structure information
 * @param structure one of PICT_XXX describing structure of picture
 *                      with pic_num
 * @return frame number (short term) or long term index of picture
 *         described by pic_num
 */
static int pic_num_extract(H264Context *h, int pic_num, int *structure){
    MpegEncContext * const s = &h->s;

    *structure = s->picture_structure;
    if(FIELD_PICTURE){
        if (!(pic_num & 1))
            /* opposite field */
            *structure ^= PICT_FRAME;
        pic_num >>= 1;
    }

    return pic_num;
}

int ff_h264_decode_ref_pic_list_reordering(H264Context *h){
    MpegEncContext * const s = &h->s;
    int list, index, pic_structure;

    print_short_term(h);
    print_long_term(h);

    for(list=0; list<h->list_count; list++){
        memcpy(h->ref_list[list], h->default_ref_list[list], sizeof(Picture)*h->ref_count[list]);

        if(get_bits1(&s->gb)){
            int pred= h->curr_pic_num;

            for(index=0; ; index++){
                unsigned int reordering_of_pic_nums_idc= get_ue_golomb_31(&s->gb);
                unsigned int pic_id;
                int i;
                Picture *ref = NULL;

                if(reordering_of_pic_nums_idc==3)
                    break;

                if(index >= h->ref_count[list]){
                    av_log(h->s.avctx, AV_LOG_ERROR, "reference count overflow\n");
                    return -1;
                }

                if(reordering_of_pic_nums_idc<3){
                    if(reordering_of_pic_nums_idc<2){
                        const unsigned int abs_diff_pic_num= get_ue_golomb(&s->gb) + 1;
                        int frame_num;

                        if(abs_diff_pic_num > h->max_pic_num){
                            av_log(h->s.avctx, AV_LOG_ERROR, "abs_diff_pic_num overflow\n");
                            return -1;
                        }

                        if(reordering_of_pic_nums_idc == 0) pred-= abs_diff_pic_num;
                        else                                pred+= abs_diff_pic_num;
                        pred &= h->max_pic_num - 1;

                        frame_num = pic_num_extract(h, pred, &pic_structure);

                        for(i= h->short_ref_count-1; i>=0; i--){
                            ref = h->short_ref[i];
                            assert(ref->f.reference);
                            assert(!ref->long_ref);
                            if(
                                   ref->frame_num == frame_num &&
                                   (ref->f.reference & pic_structure)
                              )
                                break;
                        }
                        if(i>=0)
                            ref->pic_id= pred;
                    }else{
                        int long_idx;
                        pic_id= get_ue_golomb(&s->gb); //long_term_pic_idx

                        long_idx= pic_num_extract(h, pic_id, &pic_structure);

                        if(long_idx>31){
                            av_log(h->s.avctx, AV_LOG_ERROR, "long_term_pic_idx overflow\n");
                            return -1;
                        }
                        ref = h->long_ref[long_idx];
                        assert(!(ref && !ref->f.reference));
                        if (ref && (ref->f.reference & pic_structure)) {
                            ref->pic_id= pic_id;
                            assert(ref->long_ref);
                            i=0;
                        }else{
                            i=-1;
                        }
                    }

                    if (i < 0) {
                        av_log(h->s.avctx, AV_LOG_ERROR, "reference picture missing during reorder\n");
                        memset(&h->ref_list[list][index], 0, sizeof(Picture)); //FIXME
                    } else {
                        for(i=index; i+1<h->ref_count[list]; i++){
                            if(ref->long_ref == h->ref_list[list][i].long_ref && ref->pic_id == h->ref_list[list][i].pic_id)
                                break;
                        }
                        for(; i > index; i--){
                            h->ref_list[list][i]= h->ref_list[list][i-1];
                        }
                        h->ref_list[list][index]= *ref;
                        if (FIELD_PICTURE){
                            pic_as_field(&h->ref_list[list][index], pic_structure);
                        }
                    }
                }else{
                    av_log(h->s.avctx, AV_LOG_ERROR, "illegal reordering_of_pic_nums_idc\n");
                    return -1;
                }
            }
        }
    }
    for(list=0; list<h->list_count; list++){
        for(index= 0; index < h->ref_count[list]; index++){
            if (!h->ref_list[list][index].f.data[0]) {
                av_log(h->s.avctx, AV_LOG_ERROR, "Missing reference picture\n");
                if (h->default_ref_list[list][0].f.data[0])
                    h->ref_list[list][index]= h->default_ref_list[list][0];
                else
                    return -1;
            }
        }
    }

    return 0;
}

void ff_h264_fill_mbaff_ref_list(H264Context *h){
    int list, i, j;
    for(list=0; list<h->list_count; list++){
        for(i=0; i<h->ref_count[list]; i++){
            Picture *frame = &h->ref_list[list][i];
            Picture *field = &h->ref_list[list][16+2*i];
            field[0] = *frame;
            for(j=0; j<3; j++)
                field[0].f.linesize[j] <<= 1;
            field[0].f.reference = PICT_TOP_FIELD;
            field[0].poc= field[0].field_poc[0];
            field[1] = field[0];
            for(j=0; j<3; j++)
                field[1].f.data[j] += frame->f.linesize[j];
            field[1].f.reference = PICT_BOTTOM_FIELD;
            field[1].poc= field[1].field_poc[1];

            h->luma_weight[16+2*i][list][0] = h->luma_weight[16+2*i+1][list][0] = h->luma_weight[i][list][0];
            h->luma_weight[16+2*i][list][1] = h->luma_weight[16+2*i+1][list][1] = h->luma_weight[i][list][1];
            for(j=0; j<2; j++){
                h->chroma_weight[16+2*i][list][j][0] = h->chroma_weight[16+2*i+1][list][j][0] = h->chroma_weight[i][list][j][0];
                h->chroma_weight[16+2*i][list][j][1] = h->chroma_weight[16+2*i+1][list][j][1] = h->chroma_weight[i][list][j][1];
            }
        }
    }
}

/**
 * Mark a picture as no longer needed for reference. The refmask
 * argument allows unreferencing of individual fields or the whole frame.
 * If the picture becomes entirely unreferenced, but is being held for
 * display purposes, it is marked as such.
 * @param refmask mask of fields to unreference; the mask is bitwise
 *                anded with the reference marking of pic
 * @return non-zero if pic becomes entirely unreferenced (except possibly
 *         for display purposes) zero if one of the fields remains in
 *         reference
 */
static inline int unreference_pic(H264Context *h, Picture *pic, int refmask){
    int i;
    if (pic->f.reference &= refmask) {
        return 0;
    } else {
        for(i = 0; h->delayed_pic[i]; i++)
            if(pic == h->delayed_pic[i]){
                pic->f.reference = DELAYED_PIC_REF;
                break;
            }
        return 1;
    }
}

/**
 * Find a Picture in the short term reference list by frame number.
 * @param frame_num frame number to search for
 * @param idx the index into h->short_ref where returned picture is found
 *            undefined if no picture found.
 * @return pointer to the found picture, or NULL if no pic with the provided
 *                 frame number is found
 */
static Picture * find_short(H264Context *h, int frame_num, int *idx){
    MpegEncContext * const s = &h->s;
    int i;

    for(i=0; i<h->short_ref_count; i++){
        Picture *pic= h->short_ref[i];
        if(s->avctx->debug&FF_DEBUG_MMCO)
            av_log(h->s.avctx, AV_LOG_DEBUG, "%d %d %p\n", i, pic->frame_num, pic);
        if(pic->frame_num == frame_num) {
            *idx = i;
            return pic;
        }
    }
    return NULL;
}

/**
 * Remove a picture from the short term reference list by its index in
 * that list.  This does no checking on the provided index; it is assumed
 * to be valid. Other list entries are shifted down.
 * @param i index into h->short_ref of picture to remove.
 */
static void remove_short_at_index(H264Context *h, int i){
    assert(i >= 0 && i < h->short_ref_count);
    h->short_ref[i]= NULL;
    if (--h->short_ref_count)
        memmove(&h->short_ref[i], &h->short_ref[i+1], (h->short_ref_count - i)*sizeof(Picture*));
}

/**
 *
 * @return the removed picture or NULL if an error occurs
 */
static Picture * remove_short(H264Context *h, int frame_num, int ref_mask){
    MpegEncContext * const s = &h->s;
    Picture *pic;
    int i;

    if(s->avctx->debug&FF_DEBUG_MMCO)
        av_log(h->s.avctx, AV_LOG_DEBUG, "remove short %d count %d\n", frame_num, h->short_ref_count);

    pic = find_short(h, frame_num, &i);
    if (pic){
        if(unreference_pic(h, pic, ref_mask))
        remove_short_at_index(h, i);
    }

    return pic;
}

/**
 * Remove a picture from the long term reference list by its index in
 * that list.
 * @return the removed picture or NULL if an error occurs
 */
static Picture * remove_long(H264Context *h, int i, int ref_mask){
    Picture *pic;

    pic= h->long_ref[i];
    if (pic){
        if(unreference_pic(h, pic, ref_mask)){
            assert(h->long_ref[i]->long_ref == 1);
            h->long_ref[i]->long_ref= 0;
            h->long_ref[i]= NULL;
            h->long_ref_count--;
        }
    }

    return pic;
}

void ff_h264_remove_all_refs(H264Context *h){
    int i;

    for(i=0; i<16; i++){
        remove_long(h, i, 0);
    }
    assert(h->long_ref_count==0);

    for(i=0; i<h->short_ref_count; i++){
        unreference_pic(h, h->short_ref[i], 0);
        h->short_ref[i]= NULL;
    }
    h->short_ref_count=0;

    memset(h->default_ref_list, 0, sizeof(h->default_ref_list));
    memset(h->ref_list, 0, sizeof(h->ref_list));
}

/**
 * print short term list
 */
static void print_short_term(H264Context *h) {
    uint32_t i;
    if(h->s.avctx->debug&FF_DEBUG_MMCO) {
        av_log(h->s.avctx, AV_LOG_DEBUG, "short term list:\n");
        for(i=0; i<h->short_ref_count; i++){
            Picture *pic= h->short_ref[i];
            av_log(h->s.avctx, AV_LOG_DEBUG, "%d fn:%d poc:%d %p\n",
                   i, pic->frame_num, pic->poc, pic->f.data[0]);
        }
    }
}

/**
 * print long term list
 */
static void print_long_term(H264Context *h) {
    uint32_t i;
    if(h->s.avctx->debug&FF_DEBUG_MMCO) {
        av_log(h->s.avctx, AV_LOG_DEBUG, "long term list:\n");
        for(i = 0; i < 16; i++){
            Picture *pic= h->long_ref[i];
            if (pic) {
                av_log(h->s.avctx, AV_LOG_DEBUG, "%d fn:%d poc:%d %p\n",
                       i, pic->frame_num, pic->poc, pic->f.data[0]);
            }
        }
    }
}

void ff_generate_sliding_window_mmcos(H264Context *h) {
    MpegEncContext * const s = &h->s;

    h->mmco_index= 0;
    if(h->short_ref_count && h->long_ref_count + h->short_ref_count >= h->sps.ref_frame_count &&
            !(FIELD_PICTURE && !s->first_field && s->current_picture_ptr->f.reference)) {
        h->mmco[0].opcode= MMCO_SHORT2UNUSED;
        h->mmco[0].short_pic_num= h->short_ref[ h->short_ref_count - 1 ]->frame_num;
        h->mmco_index= 1;
        if (FIELD_PICTURE) {
            h->mmco[0].short_pic_num *= 2;
            h->mmco[1].opcode= MMCO_SHORT2UNUSED;
            h->mmco[1].short_pic_num= h->mmco[0].short_pic_num + 1;
            h->mmco_index= 2;
        }
    }
}

int ff_h264_execute_ref_pic_marking(H264Context *h, MMCO *mmco, int mmco_count){
    MpegEncContext * const s = &h->s;
    int i, av_uninit(j);
    int current_ref_assigned=0, err=0;
    Picture *av_uninit(pic);

    if((s->avctx->debug&FF_DEBUG_MMCO) && mmco_count==0)
        av_log(h->s.avctx, AV_LOG_DEBUG, "no mmco here\n");

    for(i=0; i<mmco_count; i++){
        int av_uninit(structure), av_uninit(frame_num);
        if(s->avctx->debug&FF_DEBUG_MMCO)
            av_log(h->s.avctx, AV_LOG_DEBUG, "mmco:%d %d %d\n", h->mmco[i].opcode, h->mmco[i].short_pic_num, h->mmco[i].long_arg);

        if(   mmco[i].opcode == MMCO_SHORT2UNUSED
           || mmco[i].opcode == MMCO_SHORT2LONG){
            frame_num = pic_num_extract(h, mmco[i].short_pic_num, &structure);
            pic = find_short(h, frame_num, &j);
            if(!pic){
                if(mmco[i].opcode != MMCO_SHORT2LONG || !h->long_ref[mmco[i].long_arg]
                   || h->long_ref[mmco[i].long_arg]->frame_num != frame_num) {
                    av_log(h->s.avctx, AV_LOG_ERROR, "mmco: unref short failure\n");
                    err = AVERROR_INVALIDDATA;
                }
                continue;
            }
        }

        switch(mmco[i].opcode){
        case MMCO_SHORT2UNUSED:
            if(s->avctx->debug&FF_DEBUG_MMCO)
                av_log(h->s.avctx, AV_LOG_DEBUG, "mmco: unref short %d count %d\n", h->mmco[i].short_pic_num, h->short_ref_count);
            remove_short(h, frame_num, structure ^ PICT_FRAME);
            break;
        case MMCO_SHORT2LONG:
                if (h->long_ref[mmco[i].long_arg] != pic)
                    remove_long(h, mmco[i].long_arg, 0);

                remove_short_at_index(h, j);
                h->long_ref[ mmco[i].long_arg ]= pic;
                if (h->long_ref[ mmco[i].long_arg ]){
                    h->long_ref[ mmco[i].long_arg ]->long_ref=1;
                    h->long_ref_count++;
                }
            break;
        case MMCO_LONG2UNUSED:
            j = pic_num_extract(h, mmco[i].long_arg, &structure);
            pic = h->long_ref[j];
            if (pic) {
                remove_long(h, j, structure ^ PICT_FRAME);
            } else if(s->avctx->debug&FF_DEBUG_MMCO)
                av_log(h->s.avctx, AV_LOG_DEBUG, "mmco: unref long failure\n");
            break;
        case MMCO_LONG:
                    // Comment below left from previous code as it is an interresting note.
                    /* First field in pair is in short term list or
                     * at a different long term index.
                     * This is not allowed; see 7.4.3.3, notes 2 and 3.
                     * Report the problem and keep the pair where it is,
                     * and mark this field valid.
                     */

            if (h->long_ref[mmco[i].long_arg] != s->current_picture_ptr) {
                remove_long(h, mmco[i].long_arg, 0);

                h->long_ref[ mmco[i].long_arg ]= s->current_picture_ptr;
                h->long_ref[ mmco[i].long_arg ]->long_ref=1;
                h->long_ref_count++;
            }

            s->current_picture_ptr->f.reference |= s->picture_structure;
            current_ref_assigned=1;
            break;
        case MMCO_SET_MAX_LONG:
            assert(mmco[i].long_arg <= 16);
            // just remove the long term which index is greater than new max
            for(j = mmco[i].long_arg; j<16; j++){
                remove_long(h, j, 0);
            }
            break;
        case MMCO_RESET:
            while(h->short_ref_count){
                remove_short(h, h->short_ref[0]->frame_num, 0);
            }
            for(j = 0; j < 16; j++) {
                remove_long(h, j, 0);
            }
            h->frame_num=
            s->current_picture_ptr->frame_num= 0;
            h->mmco_reset = 1;
            s->current_picture_ptr->mmco_reset=1;
            for (j = 0; j < MAX_DELAYED_PIC_COUNT; j++)
                h->last_pocs[j] = INT_MIN;
            break;
        default: assert(0);
        }
    }

    if (!current_ref_assigned) {
        /* Second field of complementary field pair; the first field of
         * which is already referenced. If short referenced, it
         * should be first entry in short_ref. If not, it must exist
         * in long_ref; trying to put it on the short list here is an
         * error in the encoded bit stream (ref: 7.4.3.3, NOTE 2 and 3).
         */
        if (h->short_ref_count && h->short_ref[0] == s->current_picture_ptr) {
            /* Just mark the second field valid */
            s->current_picture_ptr->f.reference = PICT_FRAME;
        } else if (s->current_picture_ptr->long_ref) {
            av_log(h->s.avctx, AV_LOG_ERROR, "illegal short term reference "
                                             "assignment for second field "
                                             "in complementary field pair "
                                             "(first field is long term)\n");
            err = AVERROR_INVALIDDATA;
        } else {
            pic= remove_short(h, s->current_picture_ptr->frame_num, 0);
            if(pic){
                av_log(h->s.avctx, AV_LOG_ERROR, "illegal short term buffer state detected\n");
                err = AVERROR_INVALIDDATA;
            }

            if(h->short_ref_count)
                memmove(&h->short_ref[1], &h->short_ref[0], h->short_ref_count*sizeof(Picture*));

            h->short_ref[0]= s->current_picture_ptr;
            h->short_ref_count++;
            s->current_picture_ptr->f.reference |= s->picture_structure;
        }
    }

    if (h->long_ref_count + h->short_ref_count > FFMAX(h->sps.ref_frame_count, 1)){

        /* We have too many reference frames, probably due to corrupted
         * stream. Need to discard one frame. Prevents overrun of the
         * short_ref and long_ref buffers.
         */
        av_log(h->s.avctx, AV_LOG_ERROR,
               "number of reference frames (%d+%d) exceeds max (%d; probably "
               "corrupt input), discarding one\n",
               h->long_ref_count, h->short_ref_count, h->sps.ref_frame_count);
        err = AVERROR_INVALIDDATA;

        if (h->long_ref_count && !h->short_ref_count) {
            for (i = 0; i < 16; ++i)
                if (h->long_ref[i])
                    break;

            assert(i < 16);
            remove_long(h, i, 0);
        } else {
            pic = h->short_ref[h->short_ref_count - 1];
            remove_short(h, pic->frame_num, 0);
        }
    }

    print_short_term(h);
    print_long_term(h);

    if(err >= 0 && h->long_ref_count==0 && h->short_ref_count<=2 && h->pps.ref_count[0]<=1 + (s->picture_structure != PICT_FRAME) && s->current_picture_ptr->f.pict_type == AV_PICTURE_TYPE_I){
        s->current_picture_ptr->sync |= 1;
        if(!h->s.avctx->has_b_frames)
            h->sync = 2;
    }

    return (h->s.avctx->err_recognition & AV_EF_EXPLODE) ? err : 0;
}

int ff_h264_decode_ref_pic_marking(H264Context *h, GetBitContext *gb){
    MpegEncContext * const s = &h->s;
    int i;

    h->mmco_index= 0;
    if(h->nal_unit_type == NAL_IDR_SLICE){ //FIXME fields
        s->broken_link= get_bits1(gb) -1;
        if(get_bits1(gb)){
            h->mmco[0].opcode= MMCO_LONG;
            h->mmco[0].long_arg= 0;
            h->mmco_index= 1;
        }
    }else{
        if(get_bits1(gb)){ // adaptive_ref_pic_marking_mode_flag
            for(i= 0; i<MAX_MMCO_COUNT; i++) {
                MMCOOpcode opcode= get_ue_golomb_31(gb);

                h->mmco[i].opcode= opcode;
                if(opcode==MMCO_SHORT2UNUSED || opcode==MMCO_SHORT2LONG){
                    h->mmco[i].short_pic_num= (h->curr_pic_num - get_ue_golomb(gb) - 1) & (h->max_pic_num - 1);
/*                    if(h->mmco[i].short_pic_num >= h->short_ref_count || h->short_ref[ h->mmco[i].short_pic_num ] == NULL){
                        av_log(s->avctx, AV_LOG_ERROR, "illegal short ref in memory management control operation %d\n", mmco);
                        return -1;
                    }*/
                }
                if(opcode==MMCO_SHORT2LONG || opcode==MMCO_LONG2UNUSED || opcode==MMCO_LONG || opcode==MMCO_SET_MAX_LONG){
                    unsigned int long_arg= get_ue_golomb_31(gb);
                    if(long_arg >= 32 || (long_arg >= 16 && !(opcode == MMCO_SET_MAX_LONG && long_arg == 16) && !(opcode == MMCO_LONG2UNUSED && FIELD_PICTURE))){
                        av_log(h->s.avctx, AV_LOG_ERROR, "illegal long ref in memory management control operation %d\n", opcode);
                        return -1;
                    }
                    h->mmco[i].long_arg= long_arg;
                }

                if(opcode > (unsigned)MMCO_LONG){
                    av_log(h->s.avctx, AV_LOG_ERROR, "illegal memory management control operation %d\n", opcode);
                    return -1;
                }
                if(opcode == MMCO_END)
                    break;
            }
            h->mmco_index= i;
        }else{
            ff_generate_sliding_window_mmcos(h);
        }
    }

    return 0;
}
