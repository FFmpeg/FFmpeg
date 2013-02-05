/*
 * Copyright (C) 2010 Gordon Schmidt <gordon.schmidt <at> s2000.tu-chemnitz.de>
 *
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

//==includes==//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "mp_msg.h"
#include "help_mp.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

#include "libavutil/common.h"
#include "libvo/fastmemcpy.h"

//==types==//
typedef enum stereo_code {
    ANAGLYPH_RC_GRAY,   //anaglyph red/cyan gray
    ANAGLYPH_RC_HALF,   //anaglyph red/cyan half colored
    ANAGLYPH_RC_COLOR,  //anaglyph red/cyan colored
    ANAGLYPH_RC_DUBOIS, //anaglyph red/cyan dubois
    ANAGLYPH_GM_GRAY,   //anaglyph green/magenta gray
    ANAGLYPH_GM_HALF,   //anaglyph green/magenta half colored
    ANAGLYPH_GM_COLOR,  //anaglyph green/magenta colored
    ANAGLYPH_GM_DUBOIS, //anaglyph green/magenta dubois
    ANAGLYPH_YB_GRAY,   //anaglyph yellow/blue gray
    ANAGLYPH_YB_HALF,   //anaglyph yellow/blue half colored
    ANAGLYPH_YB_COLOR,  //anaglyph yellow/blue colored
    ANAGLYPH_YB_DUBOIS, //anaglyph yellow/blue dubois
    MONO_L,             //mono output for debugging (left eye only)
    MONO_R,             //mono output for debugging (right eye only)
    SIDE_BY_SIDE_LR,    //side by side parallel (left eye left, right eye right)
    SIDE_BY_SIDE_RL,    //side by side crosseye (right eye left, left eye right)
    SIDE_BY_SIDE_2_LR,  //side by side parallel with half width resolution
    SIDE_BY_SIDE_2_RL,  //side by side crosseye with half width resolution
    ABOVE_BELOW_LR,     //above-below (left eye above, right eye below)
    ABOVE_BELOW_RL,     //above-below (right eye above, left eye below)
    ABOVE_BELOW_2_LR,   //above-below with half height resolution
    ABOVE_BELOW_2_RL,   //above-below with half height resolution
    INTERLEAVE_ROWS_LR, //row-interleave (left eye has top row)
    INTERLEAVE_ROWS_RL, //row-interleave (right eye has top row)
    STEREO_CODE_COUNT   //no value set - TODO: needs autodetection
} stereo_code;

typedef struct component {
    stereo_code  fmt;
    unsigned int width;
    unsigned int height;
    unsigned int off_left;
    unsigned int off_right;
    unsigned int row_left;
    unsigned int row_right;
} component;

//==global variables==//
static const int ana_coeff[][3][6] = {
  [ANAGLYPH_RC_GRAY]   =
    {{19595, 38470,  7471,     0,     0,     0},
     {    0,     0,     0, 19595, 38470,  7471},
     {    0,     0,     0, 19595, 38470,  7471}},
  [ANAGLYPH_RC_HALF]   =
    {{19595, 38470,  7471,     0,     0,     0},
     {    0,     0,     0,     0, 65536,     0},
     {    0,     0,     0,     0,     0, 65536}},
  [ANAGLYPH_RC_COLOR]  =
    {{65536,     0,     0,     0,     0,     0},
     {    0,     0,     0,     0, 65536,     0},
     {    0,     0,     0,     0,     0, 65536}},
  [ANAGLYPH_RC_DUBOIS] =
    {{29891, 32800, 11559, -2849, -5763,  -102},
     {-2627, -2479, -1033, 24804, 48080, -1209},
     { -997, -1350,  -358, -4729, -7403, 80373}},
  [ANAGLYPH_GM_GRAY]   =
    {{    0,     0,     0, 19595, 38470,  7471},
     {19595, 38470,  7471,     0,     0,     0},
     {    0,     0,     0, 19595, 38470,  7471}},
  [ANAGLYPH_GM_HALF]   =
    {{    0,     0,     0, 65536,     0,     0},
     {19595, 38470,  7471,     0,     0,     0},
     {    0,     0,     0,     0,     0, 65536}},
  [ANAGLYPH_GM_COLOR]  =
    {{    0,     0,     0, 65536,     0,     0},
     {    0, 65536,     0,     0,     0,     0},
     {    0,     0,     0,     0,     0, 65536}},
  [ANAGLYPH_GM_DUBOIS]  =
    {{-4063,-10354, -2556, 34669, 46203,  1573},
     {18612, 43778,  9372, -1049,  -983, -4260},
     { -983, -1769,  1376,   590,  4915, 61407}},
  [ANAGLYPH_YB_GRAY]   =
    {{    0,     0,     0, 19595, 38470,  7471},
     {    0,     0,     0, 19595, 38470,  7471},
     {19595, 38470,  7471,     0,     0,     0}},
  [ANAGLYPH_YB_HALF]   =
    {{    0,     0,     0, 65536,     0,     0},
     {    0,     0,     0,     0, 65536,     0},
     {19595, 38470,  7471,     0,     0,     0}},
  [ANAGLYPH_YB_COLOR]  =
    {{    0,     0,     0, 65536,     0,     0},
     {    0,     0,     0,     0, 65536,     0},
     {    0,     0, 65536,     0,     0,     0}},
  [ANAGLYPH_YB_DUBOIS] =
    {{65535,-12650,18451,   -987, -7590, -1049},
     {-1604, 56032, 4196,    370,  3826, -1049},
     {-2345,-10676, 1358,   5801, 11416, 56217}},
};

struct vf_priv_s {
    component in;
    component out;
    int ana_matrix[3][6];
    unsigned int width;
    unsigned int height;
    unsigned int row_step;
} const ff_vf_priv_default = {
  {SIDE_BY_SIDE_LR},
  {ANAGLYPH_RC_DUBOIS}
};

//==functions==//
static inline uint8_t ana_convert(int coeff[6], uint8_t left[3], uint8_t right[3])
{
    int sum;

    sum  = coeff[0] * left[0] + coeff[3] * right[0]; //red in
    sum += coeff[1] * left[1] + coeff[4] * right[1]; //green in
    sum += coeff[2] * left[2] + coeff[5] * right[2]; //blue in
    return av_clip_uint8(sum >> 16);
}

static int config(struct vf_instance *vf, int width, int height, int d_width,
                  int d_height, unsigned int flags, unsigned int outfmt)
{
    if ((width & 1) || (height & 1)) {
        ff_mp_msg(MSGT_VFILTER, MSGL_WARN, "[stereo3d] invalid height or width\n");
        return 0;
    }
    //default input values
    vf->priv->width             = width;
    vf->priv->height            = height;
    vf->priv->row_step          = 1;
    vf->priv->in.width          = width;
    vf->priv->in.height         = height;
    vf->priv->in.off_left       = 0;
    vf->priv->in.off_right      = 0;
    vf->priv->in.row_left       = 0;
    vf->priv->in.row_right      = 0;

    //check input format
    switch (vf->priv->in.fmt) {
    case SIDE_BY_SIDE_2_LR:
        d_width                *= 2;
    case SIDE_BY_SIDE_LR:
        vf->priv->width         = width / 2;
        vf->priv->in.off_right  = vf->priv->width * 3;
        break;
    case SIDE_BY_SIDE_2_RL:
        d_width                *= 2;
    case SIDE_BY_SIDE_RL:
        vf->priv->width         = width / 2;
        vf->priv->in.off_left   = vf->priv->width * 3;
        break;
    case ABOVE_BELOW_2_LR:
        d_height               *= 2;
    case ABOVE_BELOW_LR:
        vf->priv->height        = height / 2;
        vf->priv->in.row_right  = vf->priv->height;
        break;
    case ABOVE_BELOW_2_RL:
        d_height               *= 2;
    case ABOVE_BELOW_RL:
        vf->priv->height        = height / 2;
        vf->priv->in.row_left   = vf->priv->height;
        break;
    default:
        ff_mp_msg(MSGT_VFILTER, MSGL_WARN,
               "[stereo3d] stereo format of input is not supported\n");
        return 0;
        break;
    }
    //default output values
    vf->priv->out.width         = vf->priv->width;
    vf->priv->out.height        = vf->priv->height;
    vf->priv->out.off_left      = 0;
    vf->priv->out.off_right     = 0;
    vf->priv->out.row_left      = 0;
    vf->priv->out.row_right     = 0;

    //check output format
    switch (vf->priv->out.fmt) {
    case ANAGLYPH_RC_GRAY:
    case ANAGLYPH_RC_HALF:
    case ANAGLYPH_RC_COLOR:
    case ANAGLYPH_RC_DUBOIS:
    case ANAGLYPH_GM_GRAY:
    case ANAGLYPH_GM_HALF:
    case ANAGLYPH_GM_COLOR:
    case ANAGLYPH_GM_DUBOIS:
    case ANAGLYPH_YB_GRAY:
    case ANAGLYPH_YB_HALF:
    case ANAGLYPH_YB_COLOR:
    case ANAGLYPH_YB_DUBOIS:
        memcpy(vf->priv->ana_matrix, ana_coeff[vf->priv->out.fmt],
               sizeof(vf->priv->ana_matrix));
        break;
    case SIDE_BY_SIDE_2_LR:
        d_width                /= 2;
    case SIDE_BY_SIDE_LR:
        vf->priv->out.width     = vf->priv->width * 2;
        vf->priv->out.off_right = vf->priv->width * 3;
        break;
    case SIDE_BY_SIDE_2_RL:
        d_width                /= 2;
    case SIDE_BY_SIDE_RL:
        vf->priv->out.width     = vf->priv->width * 2;
        vf->priv->out.off_left  = vf->priv->width * 3;
        break;
    case ABOVE_BELOW_2_LR:
        d_height               /= 2;
    case ABOVE_BELOW_LR:
        vf->priv->out.height    = vf->priv->height * 2;
        vf->priv->out.row_right = vf->priv->height;
        break;
    case ABOVE_BELOW_2_RL:
        d_height               /= 2;
    case ABOVE_BELOW_RL:
        vf->priv->out.height    = vf->priv->height * 2;
        vf->priv->out.row_left  = vf->priv->height;
        break;
    case INTERLEAVE_ROWS_LR:
        vf->priv->row_step      = 2;
        vf->priv->height        = vf->priv->height / 2;
        vf->priv->out.off_right = vf->priv->width * 3;
        vf->priv->in.off_right += vf->priv->in.width * 3;
        break;
    case INTERLEAVE_ROWS_RL:
        vf->priv->row_step      = 2;
        vf->priv->height        = vf->priv->height / 2;
        vf->priv->out.off_left  = vf->priv->width * 3;
        vf->priv->in.off_left  += vf->priv->in.width * 3;
        break;
    case MONO_R:
        //same as MONO_L only needs switching of input offsets
        vf->priv->in.off_left   = vf->priv->in.off_right;
        vf->priv->in.row_left   = vf->priv->in.row_right;
        //nobreak;
    case MONO_L:
        //use default settings
        break;
    default:
        ff_mp_msg(MSGT_VFILTER, MSGL_WARN,
            "[stereo3d] stereo format of output is not supported\n");
        return 0;
        break;
    }
//    if (!opt_screen_size_x && !opt_screen_size_y) {
        d_width     = d_width  * vf->priv->out.width  / width;
        d_height    = d_height * vf->priv->out.height / height;
//    }

    return ff_vf_next_config(vf, vf->priv->out.width, vf->priv->out.height,
                          d_width, d_height, flags, outfmt);
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    mp_image_t *dmpi;
    if (vf->priv->in.fmt == vf->priv->out.fmt) { //nothing to do
        dmpi = mpi;
    } else {
        int out_off_left, out_off_right;
        int in_off_left  = vf->priv->in.row_left   * mpi->stride[0]  +
                           vf->priv->in.off_left;
        int in_off_right = vf->priv->in.row_right  * mpi->stride[0]  +
                           vf->priv->in.off_right;

        dmpi = ff_vf_get_image(vf->next, IMGFMT_RGB24, MP_IMGTYPE_TEMP,
                            MP_IMGFLAG_ACCEPT_STRIDE,
                            vf->priv->out.width, vf->priv->out.height);
        out_off_left   = vf->priv->out.row_left  * dmpi->stride[0] +
                         vf->priv->out.off_left;
        out_off_right  = vf->priv->out.row_right * dmpi->stride[0] +
                         vf->priv->out.off_right;

        switch (vf->priv->out.fmt) {
        case SIDE_BY_SIDE_LR:
        case SIDE_BY_SIDE_RL:
        case SIDE_BY_SIDE_2_LR:
        case SIDE_BY_SIDE_2_RL:
        case ABOVE_BELOW_LR:
        case ABOVE_BELOW_RL:
        case ABOVE_BELOW_2_LR:
        case ABOVE_BELOW_2_RL:
        case INTERLEAVE_ROWS_LR:
        case INTERLEAVE_ROWS_RL:
            memcpy_pic2(dmpi->planes[0] + out_off_left,
                       mpi->planes[0] + in_off_left,
                       3 * vf->priv->width,
                       vf->priv->height,
                       dmpi->stride[0] * vf->priv->row_step,
                       mpi->stride[0] * vf->priv->row_step,
                       vf->priv->row_step != 1);
            memcpy_pic2(dmpi->planes[0] + out_off_right,
                       mpi->planes[0] + in_off_right,
                       3 * vf->priv->width,
                       vf->priv->height,
                       dmpi->stride[0] * vf->priv->row_step,
                       mpi->stride[0] * vf->priv->row_step,
                       vf->priv->row_step != 1);
            break;
        case MONO_L:
        case MONO_R:
            memcpy_pic(dmpi->planes[0],
                       mpi->planes[0] + in_off_left,
                       3 * vf->priv->width,
                       vf->priv->height,
                       dmpi->stride[0],
                       mpi->stride[0]);
            break;
        case ANAGLYPH_RC_GRAY:
        case ANAGLYPH_RC_HALF:
        case ANAGLYPH_RC_COLOR:
        case ANAGLYPH_RC_DUBOIS:
        case ANAGLYPH_GM_GRAY:
        case ANAGLYPH_GM_HALF:
        case ANAGLYPH_GM_COLOR:
        case ANAGLYPH_GM_DUBOIS:
        case ANAGLYPH_YB_GRAY:
        case ANAGLYPH_YB_HALF:
        case ANAGLYPH_YB_COLOR:
        case ANAGLYPH_YB_DUBOIS: {
            int i,x,y,il,ir,o;
            unsigned char *source     = mpi->planes[0];
            unsigned char *dest       = dmpi->planes[0];
            unsigned int   out_width  = vf->priv->out.width;
            int           *ana_matrix[3];

            for(i = 0; i < 3; i++)
                ana_matrix[i] = vf->priv->ana_matrix[i];

            for (y = 0; y < vf->priv->out.height; y++) {
                o   = dmpi->stride[0] * y;
                il  = in_off_left  + y * mpi->stride[0];
                ir  = in_off_right + y * mpi->stride[0];
                for (x = 0; x < out_width; x++) {
                    dest[o    ]  = ana_convert(
                                   ana_matrix[0], source + il, source + ir); //red out
                    dest[o + 1]  = ana_convert(
                                   ana_matrix[1], source + il, source + ir); //green out
                    dest[o + 2]  = ana_convert(
                                   ana_matrix[2], source + il, source + ir); //blue out
                    il += 3;
                    ir += 3;
                    o  += 3;
                }
            }
            break;
        }
        default:
            ff_mp_msg(MSGT_VFILTER, MSGL_WARN,
                   "[stereo3d] stereo format of output is not supported\n");
            return 0;
            break;
        }
    }
    return ff_vf_next_put_image(vf, dmpi, pts);
}

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
    switch (fmt)
    case IMGFMT_RGB24:
        return ff_vf_next_query_format(vf, fmt);
    return 0;
}

static void uninit(vf_instance_t *vf)
{
    free(vf->priv);
}

static int vf_open(vf_instance_t *vf, char *args)
{
    vf->config          = config;
    vf->uninit          = uninit;
    vf->put_image       = put_image;
    vf->query_format    = query_format;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));

    vf->priv->in.fmt = SIDE_BY_SIDE_LR;
    vf->priv->out.fmt= ANAGLYPH_RC_DUBOIS;
    if (args) sscanf(args, "%d:%d", &vf->priv->in.fmt, &vf->priv->out.fmt);

    return 1;
}
#if 0
///Presets usage
static const struct format_preset {
  char* name;
  stereo_code scode;
} vf_format_presets_defs[] = {
    {"arcg",                             ANAGLYPH_RC_GRAY},
    {"anaglyph_red_cyan_gray",           ANAGLYPH_RC_GRAY},
    {"arch",                             ANAGLYPH_RC_HALF},
    {"anaglyph_red_cyan_half_color",     ANAGLYPH_RC_HALF},
    {"arcc",                             ANAGLYPH_RC_COLOR},
    {"anaglyph_red_cyan_color",          ANAGLYPH_RC_COLOR},
    {"arcd",                             ANAGLYPH_RC_DUBOIS},
    {"anaglyph_red_cyan_dubios",         ANAGLYPH_RC_DUBOIS},
    {"agmg",                             ANAGLYPH_GM_GRAY},
    {"anaglyph_green_magenta_gray",      ANAGLYPH_GM_GRAY},
    {"agmh",                             ANAGLYPH_GM_HALF},
    {"anaglyph_green_magenta_half_color",ANAGLYPH_GM_HALF},
    {"agmc",                             ANAGLYPH_GM_COLOR},
    {"anaglyph_green_magenta_color",     ANAGLYPH_GM_COLOR},
    {"agmd",                             ANAGLYPH_GM_DUBOIS},
    {"anaglyph_green_magenta_dubois",    ANAGLYPH_GM_DUBOIS},
    {"aybg",                             ANAGLYPH_YB_GRAY},
    {"anaglyph_yellow_blue_gray",        ANAGLYPH_YB_GRAY},
    {"aybh",                             ANAGLYPH_YB_HALF},
    {"anaglyph_yellow_blue_half_color",  ANAGLYPH_YB_HALF},
    {"aybc",                             ANAGLYPH_YB_COLOR},
    {"anaglyph_yellow_blue_color",       ANAGLYPH_YB_COLOR},
    {"aybd",                             ANAGLYPH_YB_DUBOIS},
    {"anaglyph_yellow_blue_dubois",      ANAGLYPH_YB_DUBOIS},
    {"ml",                               MONO_L},
    {"mono_left",                        MONO_L},
    {"mr",                               MONO_R},
    {"mono_right",                       MONO_R},
    {"sbsl",                             SIDE_BY_SIDE_LR},
    {"side_by_side_left_first",          SIDE_BY_SIDE_LR},
    {"sbsr",                             SIDE_BY_SIDE_RL},
    {"side_by_side_right_first",         SIDE_BY_SIDE_RL},
    {"sbs2l",                              SIDE_BY_SIDE_2_LR},
    {"side_by_side_half_width_left_first", SIDE_BY_SIDE_2_LR},
    {"sbs2r",                              SIDE_BY_SIDE_2_RL},
    {"side_by_side_half_width_right_first",SIDE_BY_SIDE_2_RL},
    {"abl",                              ABOVE_BELOW_LR},
    {"above_below_left_first",           ABOVE_BELOW_LR},
    {"abr",                              ABOVE_BELOW_RL},
    {"above_below_right_first",          ABOVE_BELOW_RL},
    {"ab2l",                               ABOVE_BELOW_2_LR},
    {"above_below_half_height_left_first", ABOVE_BELOW_2_LR},
    {"ab2r",                               ABOVE_BELOW_2_RL},
    {"above_below_half_height_right_first",ABOVE_BELOW_2_RL},
    {"irl",                                INTERLEAVE_ROWS_LR},
    {"interleave_rows_left_first",         INTERLEAVE_ROWS_LR},
    {"irr",                                INTERLEAVE_ROWS_RL},
    {"interleave_rows_right_first",        INTERLEAVE_ROWS_RL},
    { NULL, 0}
};

#define ST_OFF(f) M_ST_OFF(struct format_preset,f)
static const m_option_t vf_format_preset_fields_in[] = {
  {"in", ST_OFF(scode), CONF_TYPE_INT, 0,0,0, NULL},
  { NULL, NULL, 0, 0, 0, 0,  NULL }
};
static const m_option_t vf_format_preset_fields_out[] = {
  {"out", ST_OFF(scode), CONF_TYPE_INT, 0,0,0, NULL},
  { NULL, NULL, 0, 0, 0, 0,  NULL }
};

static const m_struct_t vf_format_preset_in = {
  "stereo_format_preset_in",
  sizeof(struct format_preset),
  NULL,
  vf_format_preset_fields_in
};
static const m_struct_t vf_format_preset_out = {
  "stereo_format_preset_out",
  sizeof(struct format_preset),
  NULL,
  vf_format_preset_fields_out
};

static const m_struct_t vf_opts;
static const m_obj_presets_t format_preset_in = {
  (struct m_struct_st*)&vf_format_preset_in,
  (struct m_struct_st*)&vf_opts,
  (struct format_preset*)vf_format_presets_defs,
  ST_OFF(name)
};
static const m_obj_presets_t format_preset_out = {
  (struct m_struct_st*)&vf_format_preset_out,
  (struct m_struct_st*)&vf_opts,
  (struct format_preset*)vf_format_presets_defs,
  ST_OFF(name)
};

/// Now the options
#undef ST_OFF
#define ST_OFF(f) M_ST_OFF(struct vf_priv_s,f)
static const m_option_t vf_opts_fields[] = {
  {"stereo_in", 0, CONF_TYPE_OBJ_PRESETS, 0, 0, 0,
                   (m_obj_presets_t*)&format_preset_in},
  {"stereo_out", 0, CONF_TYPE_OBJ_PRESETS, 0, 0, 0,
                    (m_obj_presets_t*)&format_preset_out},
  {"in", ST_OFF(in.fmt), CONF_TYPE_INT, 0,0,0, NULL},
  {"out", ST_OFF(out.fmt), CONF_TYPE_INT, 0,0,0, NULL},
  { NULL, NULL, 0, 0, 0, 0,  NULL }
};

static const m_struct_t vf_opts = {
  "stereo3d",
  sizeof(struct vf_priv_s),
  &ff_vf_priv_default,
  vf_opts_fields
};
#endif

//==info struct==//
const vf_info_t ff_vf_info_stereo3d = {
    "stereoscopic 3d view",
    "stereo3d",
    "Gordon Schmidt",
    "view stereoscopic videos",
    vf_open,
//    &vf_opts
};
