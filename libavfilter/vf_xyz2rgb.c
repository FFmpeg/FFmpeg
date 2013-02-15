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
 * xyz2rgb filter
 * Converts from XYZ to RGB space
 * Userful to convert jpeg2000 files from MXF containers in DCP
 * The filter has no parameters
 * Original work by Belle-Nuit Montage, Lausanne, http://www.belle-nuit.com/install-ffmpeg-with-xyz2rgb-video-filter
 * Update for Ffmpeg 1.1 by Michael Cinquin
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
    int xyzgamma[4096];
    int rgbgamma[4096];
    int matrix[3][3];
} XYZ2RGBContext;


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
    XYZ2RGBContext *settings = inlink->dst->priv;
    int i;
    double xyzgamma = 2.6; 
    double rgbgamma = 1.0/2.2;

    for (i = 0; i < 4096; i++)
    {
    	settings->xyzgamma[i] = (int)(pow(i/4095.0,xyzgamma)*4095.0+0.5);
    	settings->rgbgamma[i] = (int)(pow(i/4095.0,rgbgamma)*4095.0+0.5);
    }
  
    
     
    settings->matrix[0][0] = (int)(3.2404542 * 4095.0 + 0.5);
    settings->matrix[0][1] = (int)(- 1.5371385 * 4095.0 - 0.5);
    settings->matrix[0][2] = (int)(- 0.4985314 * 4095.0 - 0.5);
    settings->matrix[1][0] = (int)(- 0.9692660 * 4095.0 - 0.5);
    settings->matrix[1][1] = (int)(1.8760108 * 4095.0 + 0.5);
    settings->matrix[1][2] = (int)(0.0415560 * 4095.0 + 0.5);
    settings->matrix[2][0] = (int)(0.0556434 * 4095.0 + 0.5);
    settings->matrix[2][1] = (int)(- 0.2040259 * 4095.0 - 0.5);
    settings->matrix[2][2] = (int)(1.0572252  * 4095.0 + 0.5);
    return 0;
  
}


static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *in)
{

  AVFilterContext *ctx  = inlink->dst;
     XYZ2RGBContext *settings = ctx->priv;
     AVFilterLink *outlink = ctx->outputs[0];
     AVFilterBufferRef *out;
     uint8_t *inrow, *outrow;
     int i, j;
     int r,g,b,x,y,z;


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
				x = inrow[j] << 4; 
				y = inrow[j+1] << 4;
				z = inrow[j+2] << 4;
				
				// convert from RGB to XYZlinear
				x = settings->xyzgamma[x]; 
				y = settings->xyzgamma[y];
				z = settings->xyzgamma[z];
				
				// convert from XYZlinear to sRGBlinear
				r = settings->matrix[0][0] * x + settings->matrix[0][1] * y + settings->matrix[0][2] * z >> 12;
				g = settings->matrix[1][0] * x + settings->matrix[1][1] * y + settings->matrix[1][2] * z >> 12;
				b = settings->matrix[2][0] * x + settings->matrix[1][2] * y + settings->matrix[2][2] * z >> 12;
								
				// limit values to 12bit legal values [0..0495]
				if (r > 4095) r = 4095; if (r < 0) r = 0;
				if (g > 4095) g = 4095; if (g < 0) g = 0;
				if (b > 4095) b = 4095; if (b < 0) b = 0;
				
				// convert from sRGBlinear to RGB and scale from 12bit to 8bit
				r = settings->rgbgamma[r] >> 4; // /16 12bit->8bit
				g = settings->rgbgamma[g] >> 4;
				b = settings->rgbgamma[b] >> 4;
			
				outrow[j] = (uint8_t)r;
				outrow[j+1] = (uint8_t)g;
				outrow[j+2] = (uint8_t)b;
			}
		}
		else // PIX_FMT_RGB48LE  16bit low endian
		{
			for(j = 0; j < inlink->w * 6; j += 6)
			{
				// read low endian and scale from 16bit to 12bit
				x = (inrow[j] + (inrow[j+1]<<8)) >> 4; 
				y = (inrow[j+2] + (inrow[j+3]<<8)) >> 4;
				z = (inrow[j+4] + (inrow[j+5]<<8)) >> 4;
				
				// convert from XYZ to XYZlinear
				x = settings->xyzgamma[x]; 
				y = settings->xyzgamma[y];
				z = settings->xyzgamma[z];
				
				// convert from XYZlinear to sRGBlinear
				r = settings->matrix[0][0] * x + settings->matrix[0][1] * y + settings->matrix[0][2] * z >> 12;
				g = settings->matrix[1][0] * x + settings->matrix[1][1] * y + settings->matrix[1][2] * z >> 12;
				b = settings->matrix[2][0] * x + settings->matrix[1][2] * y + settings->matrix[2][2] * z >> 12;
								
				// limit values to 12bit legal values [0..0495]
				if (r > 4095) r = 4095; if (r < 0) r = 0;
				if (g > 4095) g = 4095; if (g < 0) g = 0;
				if (b > 4095) b = 4095; if (b < 0) b = 0;
				
				// convert from sRGBlinear to RGB and scale from 12bit to 16bit
				r = settings->rgbgamma[r] << 4; 
				g = settings->rgbgamma[g] << 4;
				b = settings->rgbgamma[b] << 4;
			
				// write low endian
				outrow[j] = (uint8_t)(r & 255); outrow[j+1] = (uint8_t)(r >> 8);
				outrow[j+2] = (uint8_t)(g & 255); outrow[j+3] = (uint8_t)(g >> 8);
				outrow[j+4] = (uint8_t)(b & 255); outrow[j+5] = (uint8_t)(b >> 8);
			}
		}
        inrow  += in->linesize[0];
        outrow += out->linesize[0];
    }
    avfilter_unref_bufferp(&in);
    return ff_filter_frame(outlink, out);

}


static const AVFilterPad avfilter_vf_xyz2rgb_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
        .min_perms    = AV_PERM_READ,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_xyz2rgb_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};


AVFilter avfilter_vf_xyz2rgb = {
    .name      = "xyz2rgb", 
    .description = NULL_IF_CONFIG_SMALL("Converts XYZ to RGB."),
    .priv_size = sizeof(XYZ2RGBContext),
    .query_formats = query_formats,

    .inputs    = avfilter_vf_xyz2rgb_inputs,
    .outputs   = avfilter_vf_xyz2rgb_outputs,

};
