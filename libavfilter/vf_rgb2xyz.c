/*
 * Copyright (c) 2012 Matthias Buercher
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
 * rgb2xyz filter
 * Converts from sRGB to XYZ space
 * Userful to convert videos for DCP
 * The filter has no parameters
 * based on xyz2rgb by Belle-Nuit Montage
 */

#include <string.h>

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavutil/pixdesc.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"

typedef struct {
  int xyzgamma[4096]; /**< xyzgamma will hold values for DCI gamma */
  int rgbgamma[4096]; /**< rgbgamma will hold values for sRGB gamma */
  int matrix[3][3];/**< the color processing matrix */
} RGB2XYZContext;


/**
 * We provide support for only two formats. 
 * RGB24 as general purpose format
 * RGB48LE is the format actually used in j2c streams in DCP files
 */
static int query_formats(AVFilterContext *ctx)
{
  static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_RGB24,    AV_PIX_FMT_RGB48LE,          
    AV_PIX_FMT_NONE
  };

  ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
  return 0;
}

/**
 * The gamma values is precalculated in an array
 * XYZ uses projector gamma 2.6
 * sRGB uses gamma 2.2
 * The gamma function is the inverse power function, calculated in [0..1] and scaled to 12 bitdepth [0.4095]
 * 0.5 is added for rounding
 *
 * The matrix multipliers are precalculated and scaled to 12bit bitdepth (4096 values)
 * For documentation see 
 * http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
 * http://en.wikipedia.org/wiki/SRGB
 */

static int config_props(AVFilterLink *inlink)
{
  RGB2XYZContext *settings = inlink->dst->priv;
  int i;
  double xyzgamma = 1/2.6; 
  double rgbgamma = 2.2;

  for (i = 0; i < 4096; i++)
    {
      settings->xyzgamma[i] = (int)(pow(i/4095.0,xyzgamma)*4095.0+0.5);
      settings->rgbgamma[i] = (int)(pow(i/4095.0,rgbgamma)*4095.0+0.5);
    }
  
    
     

  settings->matrix[0][0] = (int)(0.4124564 * 4095.0 + 0.5);
  settings->matrix[0][1] = (int)(0.3575761 * 4095.0 - 0.5);
  settings->matrix[0][2] = (int)(0.1804375 * 4095.0 - 0.5);
  settings->matrix[1][0] = (int)(0.2126729 * 4095.0 - 0.5);
  settings->matrix[1][1] = (int)(0.7151522 * 4095.0 + 0.5);
  settings->matrix[1][2] = (int)(0.0721750 * 4095.0 + 0.5);
  settings->matrix[2][0] = (int)(0.0193339 * 4095.0 + 0.5);
  settings->matrix[2][1] = (int)(0.1191920 * 4095.0 - 0.5);
  settings->matrix[2][2] = (int)(0.9503041 * 4095.0 + 0.5);
  return 0;
  
}


static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *in)
{

  AVFilterContext *ctx  = inlink->dst;
  RGB2XYZContext *settings = ctx->priv;
  AVFilterLink *outlink = ctx->outputs[0];
  AVFilterBufferRef *out;
  uint8_t *inrow, *outrow;
  int i, j;
  int x,y,z,r,g,b;


  out = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
  if (!out) {
    avfilter_unref_bufferp(&in);
    return AVERROR(ENOMEM);
  }
  avfilter_copy_buffer_ref_props(out, in);


  /* copy palette if required */
  if (av_pix_fmt_desc_get(inlink->format)->flags & PIX_FMT_PAL)
    memcpy(out->data[1], in->data[1], AVPALETTE_SIZE);



        
  /*    inrow  = in->data[0] + in->video->y0 * in->linesize[0];
	outrow = out->data[0] + in->video->y0 * */

  /*        outrow = out->data[0] + inlink->w ;
	    inrow  = in ->data[0] + inlink->w ; */

  inrow  = in ->data[0];
  outrow = out->data[0];



  for(i = 0; i < in->video->h; i ++) 
    {
        
      /*
       * The calculation is separated on input-format RB24 or RGB48LE
       * In both cases the XYZ-values are scaled to 12bit bitdepth, 
       * then transformed to sRGB and and scaled back to the original bitdepth
       */
        
      if (inlink->format == PIX_FMT_RGB24 )        
        {
	  for(j = 0; j < inlink->w * 3; j += 3)
	    {
                                
	      // scale from 8bit to 12bit
	      r = inrow[j] << 4; 
	      g = inrow[j+1] << 4;
	      b = inrow[j+2] << 4;

	      // convert from RGB to RGBlinear
	      r = settings->rgbgamma[r]; 
	      g = settings->rgbgamma[g];
	      b = settings->rgbgamma[b];
                                
                                
	      // convert from RGBlinear to sXYZlinear
	      x = settings->matrix[0][0] * r + settings->matrix[0][1] * g + settings->matrix[0][2] * b >> 12;
	      y = settings->matrix[1][0] * r + settings->matrix[1][1] * g + settings->matrix[1][2] * b >> 12;
	      z = settings->matrix[2][0] * r + settings->matrix[1][2] * g + settings->matrix[2][2] * b >> 12;
                                                                
	      // limit values to 12bit legal values [0..0495]
	      if (x > 4095) x = 4095; if (x < 0) x = 0;
	      if (y > 4095) y = 4095; if (y < 0) y = 0;
	      if (z > 4095) z = 4095; if (z < 0) z = 0;
                                
	      // convert from sXYZlinear to XYZ and scale from 12bit to 16bit
	      x = settings->xyzgamma[x] << 4; 
	      y = settings->xyzgamma[y] << 4;
	      z = settings->xyzgamma[z] << 4;
                        
                        
	      outrow[j] = (uint8_t)x;
	      outrow[j+1] = (uint8_t)y;
	      outrow[j+2] = (uint8_t)z;
	    }
	}
      else // PIX_FMT_RGB48LE  16bit low endian
	{
	  for(j = 0; j < inlink->w * 6; j += 6)
	    {
	      // read low endian and scale from 16bit to 12bit
	      r = (inrow[j] + (inrow[j+1]<<8)) >> 4; 
	      g = (inrow[j+2] + (inrow[j+3]<<8)) >> 4;
	      b = (inrow[j+4] + (inrow[j+5]<<8)) >> 4;
                                
	      // convert from RGB to RGBlinear
	      r = settings->rgbgamma[r]; 
	      g = settings->rgbgamma[g];
	      b = settings->rgbgamma[b];
                                
	      // convert from RGBlinear to sXYZlinear
	      x = settings->matrix[0][0] * r + settings->matrix[0][1] * g + settings->matrix[0][2] * b >> 12;
	      y = settings->matrix[1][0] * r + settings->matrix[1][1] * g + settings->matrix[1][2] * b >> 12;
	      z = settings->matrix[2][0] * r + settings->matrix[1][2] * g + settings->matrix[2][2] * b >> 12;
                                                                
	      // limit values to 12bit legal values [0..0495]
	      if (x > 4095) x = 4095; if (x < 0) x = 0;
	      if (y > 4095) y = 4095; if (y < 0) y = 0;
	      if (z > 4095) z = 4095; if (z < 0) z = 0;
                                
	      // convert from sXYZlinear to XYZ and scale from 12bit to 16bit
	      x = settings->xyzgamma[x] << 4; 
	      y = settings->xyzgamma[y] << 4;
	      z = settings->xyzgamma[z] << 4;
                        
	      // write low endian
	      outrow[j] = (uint8_t)(x & 255); outrow[j+1] = (uint8_t)(x >> 8);
	      outrow[j+2] = (uint8_t)(y & 255); outrow[j+3] = (uint8_t)(y >> 8);
	      outrow[j+4] = (uint8_t)(z & 255); outrow[j+5] = (uint8_t)(z >> 8);
	    }
	}
      inrow  += in->linesize[0];
      outrow += out->linesize[0];
    }
  avfilter_unref_bufferp(&in);
  return ff_filter_frame(outlink, out);

}


static const AVFilterPad avfilter_vf_rgb2xyz_inputs[] = {
  {
    .name         = "default",
    .type         = AVMEDIA_TYPE_VIDEO,
    .filter_frame = filter_frame,
    .config_props = config_props,
    .min_perms    = AV_PERM_READ,
  },
  { NULL }
};

static const AVFilterPad avfilter_vf_rgb2xyz_outputs[] = {
  {
    .name = "default",
    .type = AVMEDIA_TYPE_VIDEO,
  },
  { NULL }
};


AVFilter avfilter_vf_rgb2xyz = {
  .name      = "rgb2xyz", 
  .description = NULL_IF_CONFIG_SMALL("Converts XYZ to RGB."),
  .priv_size = sizeof(RGB2XYZContext),
  .query_formats = query_formats,

  .inputs    = avfilter_vf_rgb2xyz_inputs,
  .outputs   = avfilter_vf_rgb2xyz_outputs,

};
