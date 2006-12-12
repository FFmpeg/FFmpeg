/*
 * X11 video grab interface
 * Copyright (C) 2006 Clemens Fruhwirth
 *
 * A quick note on licensing. This file is a mixture of LGPL code
 * (ffmpeg) and GPL code (xvidcap). The result is a file that must
 * abid both licenses. As they are compatible and GPL is more
 * strict, this code has an "effective" GPL license.
 * 
 * This file contains code from grab.c:
 * Copyright (c) 2000, 2001 Fabrice Bellard 
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
 *
 * This file contains code from the xvidcap project:
 * Copyright (C) 1997-98 Rasca, Berlin
 * Copyright (C) 2003,04 Karl H. Beckers, Frankfurt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "avformat.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#define _LINUX_TIME_H 1
#include <time.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

typedef struct
{
	Display *dpy;
	int frame_format;
	int frame_size;
	int frame_rate;
	int frame_rate_base;
	int64_t time_frame;

	int height;
	int width;
	int x_off;
	int y_off;
	XImage *image;
	int use_shm;
	XShmSegmentInfo shminfo;
	int mouse_wanted;
} X11Grab;

static int
x11grab_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
	X11Grab *x11grab = s1->priv_data;
	Display *dpy;
	AVStream *st = NULL;
	int width, height;
	int frame_rate, frame_rate_base, frame_size;
	int input_pixfmt;
	XImage *image;
	int x_off=0; int y_off = 0;
	int use_shm;

	dpy = XOpenDisplay(NULL);
	if(!dpy) {
		goto fail;
	}

	sscanf(ap->device, "x11:%d,%d", &x_off, &y_off);
	av_log(s1, AV_LOG_INFO, "device: %s -> x: %d y: %d width: %d height: %d\n", ap->device, x_off, y_off, ap->width, ap->height);
  
	if (!ap || ap->width <= 0 || ap->height <= 0 || ap->time_base.den <= 0) {
		av_log(s1, AV_LOG_ERROR, "AVParameters don't have any video size. Use -s.\n");
		return AVERROR_IO;  
	}

	width = ap->width;
	height = ap->height;
	frame_rate = ap->time_base.den;
	frame_rate_base = ap->time_base.num;

	st = av_new_stream(s1, 0);
	if (!st) {
		return -ENOMEM;
	}
	av_set_pts_info(st, 48, 1, 1000000); /* 48 bits pts in us */

	use_shm = XShmQueryExtension(dpy);
	av_log(s1, AV_LOG_INFO, "shared memory extension %s\n", use_shm ? "found" : "not found");

	if(use_shm) {
		int scr = XDefaultScreen(dpy);
		image = XShmCreateImage(dpy,
					DefaultVisual(dpy,scr),
					DefaultDepth(dpy,scr),
					ZPixmap,
					NULL,
					&x11grab->shminfo,
					ap->width, ap->height);
		x11grab->shminfo.shmid = shmget(IPC_PRIVATE,
						image->bytes_per_line * image->height,
						IPC_CREAT|0777);
		if (x11grab->shminfo.shmid == -1) {
			av_log(s1, AV_LOG_ERROR, "Fatal: Can't get shared memory!\n");
			return -ENOMEM;
		}
		x11grab->shminfo.shmaddr = image->data = shmat(x11grab->shminfo.shmid, 0, 0);
		x11grab->shminfo.readOnly = False;
            
		if (!XShmAttach(dpy, &x11grab->shminfo)) {
			av_log(s1, AV_LOG_ERROR, "Fatal: Failed to attach shared memory!\n");
			/* needs some better error subroutine :) */
			return AVERROR_IO;  
		}
	} else {
		image = XGetImage(dpy, RootWindow(dpy, DefaultScreen(dpy)),
				  x_off,y_off,
				  ap->width,ap->height,
				  AllPlanes, ZPixmap); 
	}
  
	switch (image->bits_per_pixel) {
	case 8:
		av_log (s1, AV_LOG_DEBUG, "8 bit pallete\n");
		input_pixfmt = PIX_FMT_PAL8;
		break;
	case 16:
		if ( image->red_mask == 0xF800 && image->green_mask == 0x07E0
		     && image->blue_mask == 0x1F ) {
			av_log (s1, AV_LOG_DEBUG, "16 bit RGB565\n");
			input_pixfmt = PIX_FMT_RGB565;
		} else if ( image->red_mask == 0x7C00 && 
			    image->green_mask == 0x03E0 && 
			    image->blue_mask == 0x1F ) {
			av_log(s1, AV_LOG_DEBUG, "16 bit RGB555\n");
			input_pixfmt = PIX_FMT_RGB555;
		} else {
			av_log(s1, AV_LOG_ERROR, "RGB ordering at image depth %i not supported ... aborting\n", image->bits_per_pixel);
			av_log(s1, AV_LOG_ERROR, "color masks: r 0x%.6lx g 0x%.6lx b 0x%.6lx\n", image->red_mask, image->green_mask, image->blue_mask);
			return AVERROR_IO;  
		}
		break;
	case 24:
		if ( image->red_mask == 0xFF0000 && 
		     image->green_mask == 0xFF00
		     && image->blue_mask == 0xFF ) {
			input_pixfmt = PIX_FMT_BGR24;
		} else if ( image->red_mask == 0xFF && image->green_mask == 0xFF00
			    && image->blue_mask == 0xFF0000 ) {
			input_pixfmt = PIX_FMT_RGB24;
		} else {
			av_log(s1, AV_LOG_ERROR,"rgb ordering at image depth %i not supported ... aborting\n", image->bits_per_pixel);
			av_log(s1, AV_LOG_ERROR, "color masks: r 0x%.6lx g 0x%.6lx b 0x%.6lx\n", image->red_mask, image->green_mask, image->blue_mask);
			return AVERROR_IO;  
		}
		break;
	case 32:
#if 0
		GetColorInfo (image, &c_info);
		if ( c_info.alpha_mask == 0xFF000000 && image->green_mask == 0xFF00 ) {
			// byte order is relevant here, not endianness
			// endianness is handled by avcodec, but atm no such thing
			// as having ABGR, instead of ARGB in a word. Since we
			// need this for Solaris/SPARC, but need to do the conversion
			// for every frame we do it outside of this loop, cf. below
			// this matches both ARGB32 and ABGR32
			input_pixfmt = PIX_FMT_ARGB32;
		}  else {
			av_log(s1, AV_LOG_ERROR,"image depth %i not supported ... aborting\n", image->bits_per_pixel);
			return AVERROR_IO;  
		}
#endif
		input_pixfmt = PIX_FMT_RGBA32;
		break;
	default:
		av_log(s1, AV_LOG_ERROR, "image depth %i not supported ... aborting\n", image->bits_per_pixel);
		return -1;
	}

	frame_size = width * height * image->bits_per_pixel/8;
	x11grab->frame_size = frame_size;
	x11grab->dpy = dpy;
	x11grab->width = ap->width;
	x11grab->height = ap->height;
	x11grab->frame_rate      = frame_rate;
	x11grab->frame_rate_base = frame_rate_base;
	x11grab->time_frame = av_gettime() * frame_rate / frame_rate_base;
	x11grab->x_off = x_off;
	x11grab->y_off = y_off;
	x11grab->image = image;
	x11grab->use_shm = use_shm;
	x11grab->mouse_wanted = 1;

	st->codec->codec_type = CODEC_TYPE_VIDEO;
	st->codec->codec_id = CODEC_ID_RAWVIDEO;
	st->codec->width = width;
	st->codec->height = height;
	st->codec->pix_fmt = input_pixfmt;
	st->codec->time_base.den = frame_rate;
	st->codec->time_base.num = frame_rate_base;
	st->codec->bit_rate = frame_size * 1/av_q2d(st->codec->time_base) * 8;
  
	return 0;
fail:
	av_free(st);
	return AVERROR_IO;  
}

static const uint16_t mousePointerBlack[] =
{
	0, 49152, 40960, 36864, 34816, 33792, 33280, 33024, 32896, 32832,
	33728, 37376, 43264, 51456, 1152, 1152, 576, 576, 448, 0
};

static const uint16_t mousePointerWhite[] =
{
	0, 0, 16384, 24576, 28672, 30720, 31744, 32256, 32512, 32640, 31744,
	27648, 17920, 1536, 768, 768, 384, 384, 0, 0
};

static void
getCurrentPointer(AVFormatContext *s1, X11Grab *s, int *x, int *y)
{
	Window mrootwindow, childwindow;
	int dummy;
	Display *dpy = s->dpy;
  
	mrootwindow = DefaultRootWindow(dpy);
  
	if (XQueryPointer(dpy, mrootwindow, &mrootwindow, &childwindow,
			  x, y, &dummy, &dummy, (unsigned int*)&dummy)) {
	} else {
		av_log(s1, AV_LOG_INFO, "couldn't find mouse pointer\n");
		*x = -1;
		*y = -1;
	}
}

static void
paintMousePointer(AVFormatContext *s1, X11Grab *s, int *x, int *y, XImage *image)
{
	int x_off = s->x_off;
	int y_off = s->y_off;
	int width = s->width;
	int height = s->height;

	if (   (*x - x_off) >= 0 && *x < (width + x_off)
	    && (*y - y_off) >= 0 && *y < (height + y_off) ) { 
		int line;
		uint8_t *im_data = (uint8_t*)image->data;
    
		im_data += (image->bytes_per_line * (*y - y_off)); // shift to right line
		im_data += (image->bits_per_pixel / 8 * (*x - x_off)); // shift to right pixel
    
		switch(image->bits_per_pixel) {
		case 32: 
		{
			uint32_t *cursor;
			int width_cursor;
			uint16_t bm_b, bm_w, mask;
      
			for (line = 0; line < min(20, (y_off + height) - *y); line++ ) {
				if (s->mouse_wanted == 1) {
					bm_b = mousePointerBlack[line];
					bm_w = mousePointerWhite[line];
				} else {
					bm_b = mousePointerWhite[line];
					bm_w = mousePointerBlack[line];
				}
				mask = (0x0001 << 15);
	  
				for (cursor = (uint32_t*) im_data, width_cursor = 0; 
				     ((width_cursor + *x) < (width + x_off) && width_cursor < 16);
				     cursor++, width_cursor++) {
					// Boolean pointer_b_bit, pointer_w_bit;	  
					// pointer_b_bit = ( ( bm_b & mask ) > 0 );
					// pointer_w_bit = ( ( bm_w & mask ) > 0 );
					// printf("%i ", pointer_b_bit, pointer_w_bit );
					if (( bm_b & mask) > 0 ) {
						*cursor &= (  (~image->red_mask)
							    & (~image->green_mask)
							    & (~image->blue_mask));
					} else if (( bm_w & mask) > 0 ) {
						*cursor |= (  image->red_mask
							    | image->green_mask
							    | image->blue_mask );
					}
					mask >>= 1;
				}
				//						printf("\n");
				im_data += image->bytes_per_line;
			}
		}
		break;
#if 0
		case 24: // not sure this can occur at all ..........
			av_log(s1, AV_LOG_ERROR, "input image bits_per_pixel %i not implemented with mouse pointer capture ... aborting!\n",
				image->bits_per_pixel);
			av_log(s1, AV_LOG_ERROR, "Please file a bug at http://www.sourceforge.net/projects/xvidcap/\n");
			exit(1);
			break;
		case 16:
		{
			uint16_t *cursor;
			int width;
			uint16_t bm_b, bm_w, mask;
                
			for (line = 0; line < 16; line++) {
				if (mjob->mouseWanted == 1) {
					bm_b = mousePointerBlack[line];
					bm_w = mousePointerWhite[line];
				} else {
					bm_b = mousePointerWhite[line];
					bm_w = mousePointerBlack[line];
				}
				mask = (0x0001 << 15);
                    
				for (cursor = (uint16_t*) im_data, width = 0;
				     ((width + *x) < (mjob->area->width + mjob->area->x)&&width < 6);
				     cursor++, width++) {
					// Boolean pointer_b_bit, pointer_w_bit;
					// pointer_b_bit = ( ( bm_b & mask ) > 0 );
					// pointer_w_bit = ( ( bm_w & mask ) > 0 );
					// printf("%i ", pointer_b_bit, pointer_w_bit );
					if (( bm_b & mask ) > 0 ) {
						*cursor &= (  (~image->red_mask)
							    & (~image->green_mask)
							    & (~image->blue_mask));
					} else if (( bm_w & mask ) > 0 ) {
						*cursor |= (  image->red_mask
							    | image->green_mask
							    | image->blue_mask );
					}
					mask >>= 1;
				}
				// printf("\n");
                    
				im_data += image->bytes_per_line;
			}
		}
			break;
		case 8:
		{
			uint8_t *cursor;
			int width;
			uint16_t bm_b, bm_w, mask;
                
			for (line = 0; line < 16; line++ ) {
				if (mjob->mouseWanted == 1) {
					bm_b = mousePointerBlack[line];
					bm_w = mousePointerWhite[line];
				} else {
					bm_b = mousePointerWhite[line];
					bm_w = mousePointerBlack[line];
				}
				mask = ( 0x0001 << 15 );
                    
				for (cursor = im_data, width = 0;
				     ((width + *x) < (mjob->area->width + mjob->area->x)&&width < 6);
				     cursor++, width++) {
					// Boolean pointer_b_bit, pointer_w_bit;
					// pointer_b_bit = ( ( bm_b & mask ) > 0 );
					// pointer_w_bit = ( ( bm_w & mask ) > 0 );
					// printf("%i ", pointer_b_bit, pointer_w_bit );
					if ((bm_b & mask) > 0 ) {
						*cursor = 0;
					} else if (( bm_w & mask) > 0 ) {
						*cursor = 1;
					}
					mask >>= 1;
                        
				}
				// printf("\n");
                    
				im_data += image->bytes_per_line;
			}
		}
			break;
		default:
			av_log(s1, AV_LOG_ERROR, "input image bits_per_pixel %i not supported with mouse pointer capture ... aborting!\n",
			       image->bits_per_pixel);
			exit(1);
                
#endif        
		}
	}
}


/*
 * just read new data in the image structure, the image
 * structure inclusive the data area must be allocated before
 */
static int
XGetZPixmap(Display *dpy, Drawable d, XImage *image, int x, int y)
{
  xGetImageReply rep;
  xGetImageReq *req;
  long nbytes;
  
  if (!image)
    return (False);
  LockDisplay(dpy);
  GetReq(GetImage, req);
  /*
   * first set up the standard stuff in the request
   */
  req->drawable = d;
  req->x = x;
  req->y = y;
  req->width = image->width;
  req->height = image->height;
  req->planeMask = AllPlanes;
  req->format = ZPixmap;
  
  if (_XReply(dpy, (xReply *) &rep, 0, xFalse) == 0 ||
      rep.length == 0) {
    UnlockDisplay(dpy);
    SyncHandle();
    return (False);
  }
  
  nbytes = (long)rep.length << 2;
  _XReadPad(dpy, image->data, nbytes);
  
  UnlockDisplay(dpy);
  SyncHandle();
  return (True);
}

static int
x11grab_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
  X11Grab *s = s1->priv_data;
  Display *dpy = s->dpy;
  XImage *image = s->image;
  int x_off = s->x_off;
  int y_off = s->y_off;

  int64_t curtime, delay;
  struct timespec ts;
  
  /* Calculate the time of the next frame */
  s->time_frame += int64_t_C(1000000);
  
  /* wait based on the frame rate */
  for(;;) {
    curtime = av_gettime();
    delay = s->time_frame  * s->frame_rate_base / s->frame_rate - curtime;
    if (delay <= 0) {
      if (delay < int64_t_C(-1000000) * s->frame_rate_base / s->frame_rate) {
	/* printf("grabbing is %d frames late (dropping)\n", (int) -(delay / 16666)); */
	s->time_frame += int64_t_C(1000000);
      }
      break;
    }    
    ts.tv_sec = delay / 1000000;
    ts.tv_nsec = (delay % 1000000) * 1000;
    nanosleep(&ts, NULL);
  }
  
  if (av_new_packet(pkt, s->frame_size) < 0)
    return AVERROR_IO;
  
  pkt->pts = curtime & ((1LL << 48) - 1);
  
  if(s->use_shm) {
    if (!XShmGetImage(dpy,
		     RootWindow(dpy, DefaultScreen(dpy)),
		      image, x_off, y_off, AllPlanes)) {
      fprintf(stderr,"XShmGetImage() failed\n");
    }
  } 
  else {
    XGetZPixmap(dpy,
		RootWindow(dpy, DefaultScreen(dpy)),
		image, x_off, y_off); 
  };
  {
    int pointer_x, pointer_y;
    getCurrentPointer(s, &pointer_x, &pointer_y);
    paintMousePointer(s, &pointer_x, &pointer_y, image);
  }

#warning FIXME - avoid memcpy
  memcpy(pkt->data, image->data, s->frame_size); 
  return s->frame_size;
}

static int
x11grab_read_close(AVFormatContext *s1)
{
  X11Grab *x11grab = s1->priv_data;
  
  XCloseDisplay(x11grab->dpy);
  return 0;
}

AVInputFormat x11_grab_device_demuxer = {
  "x11grab",
  "X11grab",
  sizeof(X11Grab),
  NULL,
  x11grab_read_header,
  x11grab_read_packet,
  x11grab_read_close,
  .flags = AVFMT_NOFILE,
};
