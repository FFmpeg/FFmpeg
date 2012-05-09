/*
 * Xvid rate control wrapper for lavc video encoders
 *
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include <xvid.h>
#include <unistd.h>
#if !HAVE_MKSTEMP
#include <fcntl.h>
#endif

#include "avcodec.h"
#include "libxvid.h"
//#include "dsputil.h"
#include "mpegvideo.h"

#undef NDEBUG
#include <assert.h>

/* Wrapper to work around the lack of mkstemp() on mingw.
 * Also, tries to create file in /tmp first, if possible.
 * *prefix can be a character constant; *filename will be allocated internally.
 * @return file descriptor of opened file (or -1 on error)
 * and opened file name in **filename. */
int ff_tempfile(const char *prefix, char **filename) {
    int fd=-1;
#if !HAVE_MKSTEMP
    *filename = tempnam(".", prefix);
#else
    size_t len = strlen(prefix) + 12; /* room for "/tmp/" and "XXXXXX\0" */
    *filename = av_malloc(len);
#endif
    /* -----common section-----*/
    if (*filename == NULL) {
        av_log(NULL, AV_LOG_ERROR, "ff_tempfile: Cannot allocate file name\n");
        return -1;
    }
#if !HAVE_MKSTEMP
    fd = open(*filename, O_RDWR | O_BINARY | O_CREAT, 0444);
#else
    snprintf(*filename, len, "/tmp/%sXXXXXX", prefix);
    fd = mkstemp(*filename);
    if (fd < 0) {
        snprintf(*filename, len, "./%sXXXXXX", prefix);
        fd = mkstemp(*filename);
    }
#endif
    /* -----common section-----*/
    if (fd < 0) {
        av_log(NULL, AV_LOG_ERROR, "ff_tempfile: Cannot open temporary file %s\n", *filename);
        return -1;
    }
    return fd; /* success */
}

int ff_xvid_rate_control_init(MpegEncContext *s){
    char *tmp_name;
    int fd, i;
    xvid_plg_create_t xvid_plg_create = { 0 };
    xvid_plugin_2pass2_t xvid_2pass2  = { 0 };

    fd=ff_tempfile("xvidrc.", &tmp_name);
    if (fd == -1) {
        av_log(NULL, AV_LOG_ERROR, "Can't create temporary pass2 file.\n");
        return -1;
    }

    for(i=0; i<s->rc_context.num_entries; i++){
        static const char frame_types[] = " ipbs";
        char tmp[256];
        RateControlEntry *rce;

        rce= &s->rc_context.entry[i];

        snprintf(tmp, sizeof(tmp), "%c %d %d %d %d %d %d\n",
            frame_types[rce->pict_type], (int)lrintf(rce->qscale / FF_QP2LAMBDA), rce->i_count, s->mb_num - rce->i_count - rce->skip_count,
            rce->skip_count, (rce->i_tex_bits + rce->p_tex_bits + rce->misc_bits+7)/8, (rce->header_bits+rce->mv_bits+7)/8);

        write(fd, tmp, strlen(tmp));
    }

    close(fd);

    xvid_2pass2.version= XVID_MAKE_VERSION(1,1,0);
    xvid_2pass2.filename= tmp_name;
    xvid_2pass2.bitrate= s->avctx->bit_rate;
    xvid_2pass2.vbv_size= s->avctx->rc_buffer_size;
    xvid_2pass2.vbv_maxrate= s->avctx->rc_max_rate;
    xvid_2pass2.vbv_initial= s->avctx->rc_initial_buffer_occupancy;

    xvid_plg_create.version= XVID_MAKE_VERSION(1,1,0);
    xvid_plg_create.fbase= s->avctx->time_base.den;
    xvid_plg_create.fincr= s->avctx->time_base.num;
    xvid_plg_create.param= &xvid_2pass2;

    if(xvid_plugin_2pass2(NULL, XVID_PLG_CREATE, &xvid_plg_create, &s->rc_context.non_lavc_opaque)<0){
        av_log(NULL, AV_LOG_ERROR, "xvid_plugin_2pass2 failed\n");
        return -1;
    }
    return 0;
}

float ff_xvid_rate_estimate_qscale(MpegEncContext *s, int dry_run){
    xvid_plg_data_t xvid_plg_data = { 0 };

    xvid_plg_data.version= XVID_MAKE_VERSION(1,1,0);
    xvid_plg_data.width = s->width;
    xvid_plg_data.height= s->height;
    xvid_plg_data.mb_width = s->mb_width;
    xvid_plg_data.mb_height= s->mb_height;
    xvid_plg_data.fbase= s->avctx->time_base.den;
    xvid_plg_data.fincr= s->avctx->time_base.num;
    xvid_plg_data.min_quant[0]= s->avctx->qmin;
    xvid_plg_data.min_quant[1]= s->avctx->qmin;
    xvid_plg_data.min_quant[2]= s->avctx->qmin; //FIXME i/b factor & offset
    xvid_plg_data.max_quant[0]= s->avctx->qmax;
    xvid_plg_data.max_quant[1]= s->avctx->qmax;
    xvid_plg_data.max_quant[2]= s->avctx->qmax; //FIXME i/b factor & offset
    xvid_plg_data.bquant_offset = 0; //  100 * s->avctx->b_quant_offset;
    xvid_plg_data.bquant_ratio = 100; // * s->avctx->b_quant_factor;

    if(!s->rc_context.dry_run_qscale){
        if(s->picture_number){
            xvid_plg_data.length=
            xvid_plg_data.stats.length= (s->frame_bits + 7)/8;
            xvid_plg_data.frame_num= s->rc_context.last_picture_number;
            xvid_plg_data.quant= s->qscale;

            xvid_plg_data.type= s->last_pict_type;
            if(xvid_plugin_2pass2(s->rc_context.non_lavc_opaque, XVID_PLG_AFTER, &xvid_plg_data, NULL)){
                av_log(s->avctx, AV_LOG_ERROR, "xvid_plugin_2pass2(handle, XVID_PLG_AFTER, ...) FAILED\n");
                return -1;
            }
        }
        s->rc_context.last_picture_number=
        xvid_plg_data.frame_num= s->picture_number;
        xvid_plg_data.quant= 0;
        if(xvid_plugin_2pass2(s->rc_context.non_lavc_opaque, XVID_PLG_BEFORE, &xvid_plg_data, NULL)){
            av_log(s->avctx, AV_LOG_ERROR, "xvid_plugin_2pass2(handle, XVID_PLG_BEFORE, ...) FAILED\n");
            return -1;
        }
        s->rc_context.dry_run_qscale= xvid_plg_data.quant;
    }
    xvid_plg_data.quant= s->rc_context.dry_run_qscale;
    if(!dry_run)
        s->rc_context.dry_run_qscale= 0;

    if(s->pict_type == AV_PICTURE_TYPE_B) //FIXME this is not exactly identical to xvid
        return xvid_plg_data.quant * FF_QP2LAMBDA * s->avctx->b_quant_factor + s->avctx->b_quant_offset;
    else
        return xvid_plg_data.quant * FF_QP2LAMBDA;
}

void ff_xvid_rate_control_uninit(MpegEncContext *s){
    xvid_plg_destroy_t xvid_plg_destroy;

    xvid_plugin_2pass2(s->rc_context.non_lavc_opaque, XVID_PLG_DESTROY, &xvid_plg_destroy, NULL);
}
