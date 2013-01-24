/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef MPLAYER_VF_H
#define MPLAYER_VF_H

//#include "m_option.h"
#include "mp_image.h"

//extern m_obj_settings_t* vf_settings;
//extern const m_obj_list_t vf_obj_list;

struct vf_instance;
struct vf_priv_s;

typedef struct vf_info_s {
    const char *info;
    const char *name;
    const char *author;
    const char *comment;
    int (*vf_open)(struct vf_instance *vf,char* args);
    // Ptr to a struct dscribing the options
    const void* opts;
} vf_info_t;

#define NUM_NUMBERED_MPI 50

typedef struct vf_image_context_s {
    mp_image_t* static_images[2];
    mp_image_t* temp_images[1];
    mp_image_t* export_images[1];
    mp_image_t* numbered_images[NUM_NUMBERED_MPI];
    int static_idx;
} vf_image_context_t;

typedef struct vf_format_context_t {
    int have_configured;
    int orig_width, orig_height, orig_fmt;
} vf_format_context_t;

typedef struct vf_instance {
    const vf_info_t* info;
    // funcs:
    int (*config)(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt);
    int (*control)(struct vf_instance *vf,
        int request, void* data);
    int (*query_format)(struct vf_instance *vf,
        unsigned int fmt);
    void (*get_image)(struct vf_instance *vf,
        mp_image_t *mpi);
    int (*put_image)(struct vf_instance *vf,
        mp_image_t *mpi, double pts);
    void (*start_slice)(struct vf_instance *vf,
        mp_image_t *mpi);
    void (*draw_slice)(struct vf_instance *vf,
        unsigned char** src, int* stride, int w,int h, int x, int y);
    void (*uninit)(struct vf_instance *vf);

    int (*continue_buffered_image)(struct vf_instance *vf);
    // caps:
    unsigned int default_caps; // used by default query_format()
    unsigned int default_reqs; // used by default config()
    // data:
    int w, h;
    vf_image_context_t imgctx;
    vf_format_context_t fmt;
    struct vf_instance *next;
    mp_image_t *dmpi;
    struct vf_priv_s* priv;
} vf_instance_t;

// control codes:
#include "mpc_info.h"

typedef struct vf_seteq_s
{
    const char *item;
    int value;
} vf_equalizer_t;

#define VFCTRL_QUERY_MAX_PP_LEVEL 4 /* test for postprocessing support (max level) */
#define VFCTRL_SET_PP_LEVEL 5 /* set postprocessing level */
#define VFCTRL_SET_EQUALIZER 6 /* set color options (brightness,contrast etc) */
#define VFCTRL_GET_EQUALIZER 8 /* gset color options (brightness,contrast etc) */
#define VFCTRL_DRAW_OSD 7
#define VFCTRL_CHANGE_RECTANGLE 9 /* Change the rectangle boundaries */
#define VFCTRL_FLIP_PAGE 10 /* Tell the vo to flip pages */
#define VFCTRL_DUPLICATE_FRAME 11 /* For encoding - encode zero-change frame */
#define VFCTRL_SKIP_NEXT_FRAME 12 /* For encoding - drop the next frame that passes thru */
#define VFCTRL_FLUSH_FRAMES    13 /* For encoding - flush delayed frames */
#define VFCTRL_SCREENSHOT      14 /* Make a screenshot */
#define VFCTRL_INIT_EOSD       15 /* Select EOSD renderer */
#define VFCTRL_DRAW_EOSD       16 /* Render EOSD */
#define VFCTRL_GET_PTS         17 /* Return last pts value that reached vf_vo*/
#define VFCTRL_SET_DEINTERLACE 18 /* Set deinterlacing status */
#define VFCTRL_GET_DEINTERLACE 19 /* Get deinterlacing status */

#include "vfcap.h"

//FIXME this should be in a common header, but i dunno which
#define MP_NOPTS_VALUE (-1LL<<63) //both int64_t and double should be able to represent this exactly


// functions:
void ff_vf_mpi_clear(mp_image_t* mpi,int x0,int y0,int w,int h);
mp_image_t* ff_vf_get_image(vf_instance_t* vf, unsigned int outfmt, int mp_imgtype, int mp_imgflag, int w, int h);

vf_instance_t* vf_open_plugin(const vf_info_t* const* filter_list, vf_instance_t* next, const char *name, char **args);
vf_instance_t* vf_open_filter(vf_instance_t* next, const char *name, char **args);
vf_instance_t* ff_vf_add_before_vo(vf_instance_t **vf, char *name, char **args);
vf_instance_t* vf_open_encoder(vf_instance_t* next, const char *name, char *args);

unsigned int ff_vf_match_csp(vf_instance_t** vfp,const unsigned int* list,unsigned int preferred);
void ff_vf_clone_mpi_attributes(mp_image_t* dst, mp_image_t* src);
void ff_vf_queue_frame(vf_instance_t *vf, int (*)(vf_instance_t *));
int ff_vf_output_queued_frame(vf_instance_t *vf);

// default wrappers:
int ff_vf_next_config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt);
int ff_vf_next_control(struct vf_instance *vf, int request, void* data);
void ff_vf_extra_flip(struct vf_instance *vf);
int ff_vf_next_query_format(struct vf_instance *vf, unsigned int fmt);
int ff_vf_next_put_image(struct vf_instance *vf,mp_image_t *mpi, double pts);
void ff_vf_next_draw_slice (struct vf_instance *vf, unsigned char** src, int* stride, int w,int h, int x, int y);

vf_instance_t* ff_append_filters(vf_instance_t* last);

void ff_vf_uninit_filter(vf_instance_t* vf);
void ff_vf_uninit_filter_chain(vf_instance_t* vf);

int ff_vf_config_wrapper(struct vf_instance *vf,
                      int width, int height, int d_width, int d_height,
                      unsigned int flags, unsigned int outfmt);

static inline int norm_qscale(int qscale, int type)
{
    switch (type) {
    case 0: // MPEG-1
        return qscale;
    case 1: // MPEG-2
        return qscale >> 1;
    case 2: // H264
        return qscale >> 2;
    case 3: // VP56
        return (63 - qscale + 2) >> 2;
    }
    return qscale;
}

#endif /* MPLAYER_VF_H */
