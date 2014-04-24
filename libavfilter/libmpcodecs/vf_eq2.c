/*
 * Software equalizer (brightness, contrast, gamma, saturation)
 *
 * Hampa Hug <hampa@hampa.ch> (original LUT gamma/contrast/brightness filter)
 * Daniel Moreno <comac@comac.darktech.org> (saturation, R/G/B gamma support)
 * Richard Felker (original MMX contrast/brightness code (vf_eq.c))
 * Michael Niedermayer <michalni@gmx.at> (LUT16)
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#include "config.h"
#include "mp_msg.h"
#include "cpudetect.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

#define LUT16

/* Per channel parameters */
typedef struct eq2_param_t {
  unsigned char lut[256];
#ifdef LUT16
  uint16_t lut16[256*256];
#endif
  int           lut_clean;

  void (*adjust) (struct eq2_param_t *par, unsigned char *dst, unsigned char *src,
    unsigned w, unsigned h, unsigned dstride, unsigned sstride);

  double        c;
  double        b;
  double        g;
  double        w;
} eq2_param_t;

typedef struct vf_priv_s {
  eq2_param_t param[3];

  double        contrast;
  double        brightness;
  double        saturation;

  double        gamma;
  double        gamma_weight;
  double        rgamma;
  double        ggamma;
  double        bgamma;

  unsigned      buf_w[3];
  unsigned      buf_h[3];
  unsigned char *buf[3];
} vf_eq2_t;


static
void create_lut (eq2_param_t *par)
{
  unsigned i;
  double   g, v;
  double   lw, gw;

  g = par->g;
  gw = par->w;
  lw = 1.0 - gw;

  if ((g < 0.001) || (g > 1000.0)) {
    g = 1.0;
  }

  g = 1.0 / g;

  for (i = 0; i < 256; i++) {
    v = (double) i / 255.0;
    v = par->c * (v - 0.5) + 0.5 + par->b;

    if (v <= 0.0) {
      par->lut[i] = 0;
    }
    else {
      v = v*lw + pow(v, g)*gw;

      if (v >= 1.0) {
        par->lut[i] = 255;
      }
      else {
        par->lut[i] = (unsigned char) (256.0 * v);
      }
    }
  }

#ifdef LUT16
  for(i=0; i<256*256; i++){
    par->lut16[i]= par->lut[i&0xFF] + (par->lut[i>>8]<<8);
  }
#endif

  par->lut_clean = 1;
}

#if HAVE_MMX && HAVE_6REGS
static
void affine_1d_MMX (eq2_param_t *par, unsigned char *dst, unsigned char *src,
  unsigned w, unsigned h, unsigned dstride, unsigned sstride)
{
  unsigned i;
  int      contrast, brightness;
  unsigned dstep, sstep;
  int      pel;
  short    brvec[4];
  short    contvec[4];

//  printf("\nmmx: src=%p dst=%p w=%d h=%d ds=%d ss=%d\n",src,dst,w,h,dstride,sstride);

  contrast = (int) (par->c * 256 * 16);
  brightness = ((int) (100.0 * par->b + 100.0) * 511) / 200 - 128 - contrast / 32;

  brvec[0] = brvec[1] = brvec[2] = brvec[3] = brightness;
  contvec[0] = contvec[1] = contvec[2] = contvec[3] = contrast;

  sstep = sstride - w;
  dstep = dstride - w;

  while (h-- > 0) {
    __asm__ volatile (
      "movq (%5), %%mm3 \n\t"
      "movq (%6), %%mm4 \n\t"
      "pxor %%mm0, %%mm0 \n\t"
      "movl %4, %%eax\n\t"
      ASMALIGN(4)
      "1: \n\t"
      "movq (%0), %%mm1 \n\t"
      "movq (%0), %%mm2 \n\t"
      "punpcklbw %%mm0, %%mm1 \n\t"
      "punpckhbw %%mm0, %%mm2 \n\t"
      "psllw $4, %%mm1 \n\t"
      "psllw $4, %%mm2 \n\t"
      "pmulhw %%mm4, %%mm1 \n\t"
      "pmulhw %%mm4, %%mm2 \n\t"
      "paddw %%mm3, %%mm1 \n\t"
      "paddw %%mm3, %%mm2 \n\t"
      "packuswb %%mm2, %%mm1 \n\t"
      "add $8, %0 \n\t"
      "movq %%mm1, (%1) \n\t"
      "add $8, %1 \n\t"
      "decl %%eax \n\t"
      "jnz 1b \n\t"
      : "=r" (src), "=r" (dst)
      : "0" (src), "1" (dst), "r" (w >> 3), "r" (brvec), "r" (contvec)
      : "%eax"
    );

    for (i = w & 7; i > 0; i--) {
      pel = ((*src++ * contrast) >> 12) + brightness;
      if (pel & 768) {
        pel = (-pel) >> 31;
      }
      *dst++ = pel;
    }

    src += sstep;
    dst += dstep;
  }

  __asm__ volatile ( "emms \n\t" ::: "memory" );
}
#endif

static
void apply_lut (eq2_param_t *par, unsigned char *dst, unsigned char *src,
  unsigned w, unsigned h, unsigned dstride, unsigned sstride)
{
  unsigned      i, j, w2;
  unsigned char *lut;
  uint16_t *lut16;

  if (!par->lut_clean) {
    create_lut (par);
  }

  lut = par->lut;
#ifdef LUT16
  lut16 = par->lut16;
  w2= (w>>3)<<2;
  for (j = 0; j < h; j++) {
    uint16_t *src16= (uint16_t*)src;
    uint16_t *dst16= (uint16_t*)dst;
    for (i = 0; i < w2; i+=4) {
      dst16[i+0] = lut16[src16[i+0]];
      dst16[i+1] = lut16[src16[i+1]];
      dst16[i+2] = lut16[src16[i+2]];
      dst16[i+3] = lut16[src16[i+3]];
    }
    i <<= 1;
#else
  w2= (w>>3)<<3;
  for (j = 0; j < h; j++) {
    for (i = 0; i < w2; i+=8) {
      dst[i+0] = lut[src[i+0]];
      dst[i+1] = lut[src[i+1]];
      dst[i+2] = lut[src[i+2]];
      dst[i+3] = lut[src[i+3]];
      dst[i+4] = lut[src[i+4]];
      dst[i+5] = lut[src[i+5]];
      dst[i+6] = lut[src[i+6]];
      dst[i+7] = lut[src[i+7]];
    }
#endif
    for (; i < w; i++) {
      dst[i] = lut[src[i]];
    }

    src += sstride;
    dst += dstride;
  }
}

static
int put_image (vf_instance_t *vf, mp_image_t *src, double pts)
{
  unsigned      i;
  vf_eq2_t      *eq2;
  mp_image_t    *dst;
  unsigned long img_n,img_c;

  eq2 = vf->priv;

  if ((eq2->buf_w[0] != src->w) || (eq2->buf_h[0] != src->h)) {
    eq2->buf_w[0] = src->w;
    eq2->buf_h[0] = src->h;
      eq2->buf_w[1] = eq2->buf_w[2] = src->w >> src->chroma_x_shift;
      eq2->buf_h[1] = eq2->buf_h[2] = src->h >> src->chroma_y_shift;
    img_n = eq2->buf_w[0]*eq2->buf_h[0];
    if(src->num_planes>1){
      img_c = eq2->buf_w[1]*eq2->buf_h[1];
      eq2->buf[0] = realloc (eq2->buf[0], img_n + 2*img_c);
      eq2->buf[1] = eq2->buf[0] + img_n;
      eq2->buf[2] = eq2->buf[1] + img_c;
    } else
      eq2->buf[0] = realloc (eq2->buf[0], img_n);
  }

  dst = ff_vf_get_image (vf->next, src->imgfmt, MP_IMGTYPE_EXPORT, 0, src->w, src->h);

  for (i = 0; i < ((src->num_planes>1)?3:1); i++) {
    if (eq2->param[i].adjust != NULL) {
      dst->planes[i] = eq2->buf[i];
      dst->stride[i] = eq2->buf_w[i];

      eq2->param[i].adjust (&eq2->param[i], dst->planes[i], src->planes[i],
        eq2->buf_w[i], eq2->buf_h[i], dst->stride[i], src->stride[i]);
    }
    else {
      dst->planes[i] = src->planes[i];
      dst->stride[i] = src->stride[i];
    }
  }

  return ff_vf_next_put_image (vf, dst, pts);
}

static
void check_values (eq2_param_t *par)
{
  /* yuck! floating point comparisons... */

  if ((par->c == 1.0) && (par->b == 0.0) && (par->g == 1.0)) {
    par->adjust = NULL;
  }
#if HAVE_MMX && HAVE_6REGS
  else if (par->g == 1.0 && ff_gCpuCaps.hasMMX) {
    par->adjust = &affine_1d_MMX;
  }
#endif
  else {
    par->adjust = &apply_lut;
  }
}

static
void print_values (vf_eq2_t *eq2)
{
  ff_mp_msg (MSGT_VFILTER, MSGL_V, "vf_eq2: c=%.2f b=%.2f g=%.4f s=%.2f \n",
    eq2->contrast, eq2->brightness, eq2->gamma, eq2->saturation
  );
}

static
void set_contrast (vf_eq2_t *eq2, double c)
{
  eq2->contrast = c;
  eq2->param[0].c = c;
  eq2->param[0].lut_clean = 0;
  check_values (&eq2->param[0]);
  print_values (eq2);
}

static
void set_brightness (vf_eq2_t *eq2, double b)
{
  eq2->brightness = b;
  eq2->param[0].b = b;
  eq2->param[0].lut_clean = 0;
  check_values (&eq2->param[0]);
  print_values (eq2);
}

static
void set_gamma (vf_eq2_t *eq2, double g)
{
  eq2->gamma = g;

  eq2->param[0].g = eq2->gamma * eq2->ggamma;
  eq2->param[1].g = sqrt (eq2->bgamma / eq2->ggamma);
  eq2->param[2].g = sqrt (eq2->rgamma / eq2->ggamma);
  eq2->param[0].w = eq2->param[1].w = eq2->param[2].w = eq2->gamma_weight;

  eq2->param[0].lut_clean = 0;
  eq2->param[1].lut_clean = 0;
  eq2->param[2].lut_clean = 0;

  check_values (&eq2->param[0]);
  check_values (&eq2->param[1]);
  check_values (&eq2->param[2]);

  print_values (eq2);
}

static
void set_saturation (vf_eq2_t *eq2, double s)
{
  eq2->saturation = s;

  eq2->param[1].c = s;
  eq2->param[2].c = s;

  eq2->param[1].lut_clean = 0;
  eq2->param[2].lut_clean = 0;

  check_values (&eq2->param[1]);
  check_values (&eq2->param[2]);

  print_values (eq2);
}

static
int control (vf_instance_t *vf, int request, void *data)
{
  vf_equalizer_t *eq;

  switch (request) {
    case VFCTRL_SET_EQUALIZER:
      eq = (vf_equalizer_t *) data;

      if (strcmp (eq->item, "gamma") == 0) {
        set_gamma (vf->priv, exp (log (8.0) * eq->value / 100.0));
        return CONTROL_TRUE;
      }
      else if (strcmp (eq->item, "contrast") == 0) {
        set_contrast (vf->priv, (1.0 / 100.0) * (eq->value + 100));
        return CONTROL_TRUE;
      }
      else if (strcmp (eq->item, "brightness") == 0) {
        set_brightness (vf->priv, (1.0 / 100.0) * eq->value);
        return CONTROL_TRUE;
      }
      else if (strcmp (eq->item, "saturation") == 0) {
        set_saturation (vf->priv, (double) (eq->value + 100) / 100.0);
        return CONTROL_TRUE;
      }
      break;

    case VFCTRL_GET_EQUALIZER:
      eq = (vf_equalizer_t *) data;
      if (strcmp (eq->item, "gamma") == 0) {
        eq->value = (int) (100.0 * log (vf->priv->gamma) / log (8.0));
        return CONTROL_TRUE;
      }
      else if (strcmp (eq->item, "contrast") == 0) {
        eq->value = (int) (100.0 * vf->priv->contrast) - 100;
        return CONTROL_TRUE;
      }
      else if (strcmp (eq->item, "brightness") == 0) {
        eq->value = (int) (100.0 * vf->priv->brightness);
        return CONTROL_TRUE;
      }
      else if (strcmp (eq->item, "saturation") == 0) {
        eq->value = (int) (100.0 * vf->priv->saturation) - 100;
        return CONTROL_TRUE;
      }
      break;
  }

  return ff_vf_next_control (vf, request, data);
}

static
int query_format (vf_instance_t *vf, unsigned fmt)
{
  switch (fmt) {
    case IMGFMT_YVU9:
    case IMGFMT_IF09:
    case IMGFMT_YV12:
    case IMGFMT_I420:
    case IMGFMT_IYUV:
    case IMGFMT_Y800:
    case IMGFMT_Y8:
    case IMGFMT_444P:
    case IMGFMT_422P:
    case IMGFMT_411P:
      return ff_vf_next_query_format (vf, fmt);
  }

  return 0;
}

static
void uninit (vf_instance_t *vf)
{
  if (vf->priv != NULL) {
    free (vf->priv->buf[0]);
    free (vf->priv);
  }
}

static
int vf_open(vf_instance_t *vf, char *args)
{
  unsigned i;
  vf_eq2_t *eq2;
  double   par[8];

  vf->control = control;
  vf->query_format = query_format;
  vf->put_image = put_image;
  vf->uninit = uninit;

  vf->priv = malloc (sizeof (vf_eq2_t));
  eq2 = vf->priv;

  for (i = 0; i < 3; i++) {
    eq2->buf[i] = NULL;
    eq2->buf_w[i] = 0;
    eq2->buf_h[i] = 0;

    eq2->param[i].adjust = NULL;
    eq2->param[i].c = 1.0;
    eq2->param[i].b = 0.0;
    eq2->param[i].g = 1.0;
    eq2->param[i].lut_clean = 0;
  }

  eq2->contrast = 1.0;
  eq2->brightness = 0.0;
  eq2->saturation = 1.0;

  eq2->gamma = 1.0;
  eq2->gamma_weight = 1.0;
  eq2->rgamma = 1.0;
  eq2->ggamma = 1.0;
  eq2->bgamma = 1.0;

  if (args != NULL) {
    par[0] = 1.0;
    par[1] = 1.0;
    par[2] = 0.0;
    par[3] = 1.0;
    par[4] = 1.0;
    par[5] = 1.0;
    par[6] = 1.0;
    par[7] = 1.0;
    sscanf (args, "%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf",
      par, par + 1, par + 2, par + 3, par + 4, par + 5, par + 6, par + 7
    );

    eq2->rgamma = par[4];
    eq2->ggamma = par[5];
    eq2->bgamma = par[6];
    eq2->gamma_weight = par[7];

    set_gamma (eq2, par[0]);
    set_contrast (eq2, par[1]);
    set_brightness (eq2, par[2]);
    set_saturation (eq2, par[3]);
  }

  return 1;
}

const vf_info_t ff_vf_info_eq2 = {
  "Software equalizer",
  "eq2",
  "Hampa Hug, Daniel Moreno, Richard Felker",
  "",
  &vf_open,
  NULL
};
