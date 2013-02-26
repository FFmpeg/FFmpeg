// Avisynth C Interface Version 0.20
// Copyright 2003 Kevin Atkinson

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .
//
// As a special exception, I give you permission to link to the
// Avisynth C interface with independent modules that communicate with
// the Avisynth C interface solely through the interfaces defined in
// avisynth_c.h, regardless of the license terms of these independent
// modules, and to copy and distribute the resulting combined work
// under terms of your choice, provided that every copy of the
// combined work is accompanied by a complete copy of the source code
// of the Avisynth C interface and Avisynth itself (with the version
// used to produce the combined work), being distributed under the
// terms of the GNU General Public License plus this exception.  An
// independent module is a module which is not derived from or based
// on Avisynth C Interface, such as 3rd-party filters, import and
// export plugins, or graphical user interfaces.

// NOTE: this is a partial update of the Avisynth C interface to recognize
// new color spaces added in Avisynth 2.60. By no means is this document
// completely Avisynth 2.60 compliant.

#ifndef __AVISYNTH_C__
#define __AVISYNTH_C__

#ifdef __cplusplus
#  define EXTERN_C extern "C"
#else
#  define EXTERN_C
#endif

#define AVSC_USE_STDCALL 1

#ifndef AVSC_USE_STDCALL
#  define AVSC_CC __cdecl
#else
#  define AVSC_CC __stdcall
#endif

#define AVSC_INLINE static __inline

#ifdef AVISYNTH_C_EXPORTS
#  define AVSC_EXPORT EXTERN_C
#  define AVSC_API(ret, name) EXTERN_C __declspec(dllexport) ret AVSC_CC name
#else
#  define AVSC_EXPORT EXTERN_C __declspec(dllexport)
#  ifndef AVSC_NO_DECLSPEC
#    define AVSC_API(ret, name) EXTERN_C __declspec(dllimport) ret AVSC_CC name
#  else
#    define AVSC_API(ret, name) typedef ret (AVSC_CC *name##_func)
#  endif
#endif

typedef unsigned char BYTE;
#ifdef __GNUC__
typedef long long int INT64;
#else
typedef __int64 INT64;
#endif


/////////////////////////////////////////////////////////////////////
//
// Constants
//

#ifndef __AVISYNTH_H__
enum { AVISYNTH_INTERFACE_VERSION = 4 };
#endif

enum {AVS_SAMPLE_INT8  = 1<<0,
      AVS_SAMPLE_INT16 = 1<<1,
      AVS_SAMPLE_INT24 = 1<<2,
      AVS_SAMPLE_INT32 = 1<<3,
      AVS_SAMPLE_FLOAT = 1<<4};

enum {AVS_PLANAR_Y=1<<0,
      AVS_PLANAR_U=1<<1,
      AVS_PLANAR_V=1<<2,
      AVS_PLANAR_ALIGNED=1<<3,
      AVS_PLANAR_Y_ALIGNED=AVS_PLANAR_Y|AVS_PLANAR_ALIGNED,
      AVS_PLANAR_U_ALIGNED=AVS_PLANAR_U|AVS_PLANAR_ALIGNED,
      AVS_PLANAR_V_ALIGNED=AVS_PLANAR_V|AVS_PLANAR_ALIGNED,
      AVS_PLANAR_A=1<<4,
      AVS_PLANAR_R=1<<5,
      AVS_PLANAR_G=1<<6,
      AVS_PLANAR_B=1<<7,
      AVS_PLANAR_A_ALIGNED=AVS_PLANAR_A|AVS_PLANAR_ALIGNED,
      AVS_PLANAR_R_ALIGNED=AVS_PLANAR_R|AVS_PLANAR_ALIGNED,
      AVS_PLANAR_G_ALIGNED=AVS_PLANAR_G|AVS_PLANAR_ALIGNED,
      AVS_PLANAR_B_ALIGNED=AVS_PLANAR_B|AVS_PLANAR_ALIGNED};

  // Colorspace properties.
enum {AVS_CS_BGR = 1<<28,
      AVS_CS_YUV = 1<<29,
      AVS_CS_INTERLEAVED = 1<<30,
      AVS_CS_PLANAR = 1<<31,

      AVS_CS_SHIFT_SUB_WIDTH   = 0,
      AVS_CS_SHIFT_SUB_HEIGHT  = 1 << 3,
      AVS_CS_SHIFT_SAMPLE_BITS = 1 << 4,

      AVS_CS_SUB_WIDTH_MASK    = 7 << AVS_CS_SHIFT_SUB_WIDTH,
      AVS_CS_SUB_WIDTH_1       = 3 << AVS_CS_SHIFT_SUB_WIDTH, // YV24
      AVS_CS_SUB_WIDTH_2       = 0 << AVS_CS_SHIFT_SUB_WIDTH, // YV12, I420, YV16
      AVS_CS_SUB_WIDTH_4       = 1 << AVS_CS_SHIFT_SUB_WIDTH, // YUV9, YV411

      AVS_CS_VPLANEFIRST       = 1 << 3, // YV12, YV16, YV24, YV411, YUV9
      AVS_CS_UPLANEFIRST       = 1 << 4, // I420

      AVS_CS_SUB_HEIGHT_MASK   = 7 << AVS_CS_SHIFT_SUB_HEIGHT,
      AVS_CS_SUB_HEIGHT_1      = 3 << AVS_CS_SHIFT_SUB_HEIGHT, // YV16, YV24, YV411
      AVS_CS_SUB_HEIGHT_2      = 0 << AVS_CS_SHIFT_SUB_HEIGHT, // YV12, I420
      AVS_CS_SUB_HEIGHT_4      = 1 << AVS_CS_SHIFT_SUB_HEIGHT, // YUV9

      AVS_CS_SAMPLE_BITS_MASK  = 7 << AVS_CS_SHIFT_SAMPLE_BITS,
      AVS_CS_SAMPLE_BITS_8     = 0 << AVS_CS_SHIFT_SAMPLE_BITS,
      AVS_CS_SAMPLE_BITS_16    = 1 << AVS_CS_SHIFT_SAMPLE_BITS,
      AVS_CS_SAMPLE_BITS_32    = 2 << AVS_CS_SHIFT_SAMPLE_BITS,

      AVS_CS_PLANAR_MASK       = AVS_CS_PLANAR | AVS_CS_INTERLEAVED | AVS_CS_YUV | AVS_CS_BGR | AVS_CS_SAMPLE_BITS_MASK | AVS_CS_SUB_HEIGHT_MASK | AVS_CS_SUB_WIDTH_MASK,
      AVS_CS_PLANAR_FILTER     = ~( AVS_CS_VPLANEFIRST | AVS_CS_UPLANEFIRST )};

  // Specific colorformats
enum {
  AVS_CS_UNKNOWN = 0,
  AVS_CS_BGR24 = 1<<0 | AVS_CS_BGR | AVS_CS_INTERLEAVED,
  AVS_CS_BGR32 = 1<<1 | AVS_CS_BGR | AVS_CS_INTERLEAVED,
  AVS_CS_YUY2 = 1<<2 | AVS_CS_YUV | AVS_CS_INTERLEAVED,
  //  AVS_CS_YV12  = 1<<3  Reserved
  //  AVS_CS_I420  = 1<<4  Reserved
  AVS_CS_RAW32 = 1<<5 | AVS_CS_INTERLEAVED,

  AVS_CS_YV24  = AVS_CS_PLANAR | AVS_CS_YUV | AVS_CS_SAMPLE_BITS_8 | AVS_CS_VPLANEFIRST | AVS_CS_SUB_HEIGHT_1 | AVS_CS_SUB_WIDTH_1,  // YVU 4:4:4 planar
  AVS_CS_YV16  = AVS_CS_PLANAR | AVS_CS_YUV | AVS_CS_SAMPLE_BITS_8 | AVS_CS_VPLANEFIRST | AVS_CS_SUB_HEIGHT_1 | AVS_CS_SUB_WIDTH_2,  // YVU 4:2:2 planar
  AVS_CS_YV12  = AVS_CS_PLANAR | AVS_CS_YUV | AVS_CS_SAMPLE_BITS_8 | AVS_CS_VPLANEFIRST | AVS_CS_SUB_HEIGHT_2 | AVS_CS_SUB_WIDTH_2,  // YVU 4:2:0 planar
  AVS_CS_I420  = AVS_CS_PLANAR | AVS_CS_YUV | AVS_CS_SAMPLE_BITS_8 | AVS_CS_UPLANEFIRST | AVS_CS_SUB_HEIGHT_2 | AVS_CS_SUB_WIDTH_2,  // YUV 4:2:0 planar
  AVS_CS_IYUV  = AVS_CS_I420,
  AVS_CS_YV411 = AVS_CS_PLANAR | AVS_CS_YUV | AVS_CS_SAMPLE_BITS_8 | AVS_CS_VPLANEFIRST | AVS_CS_SUB_HEIGHT_1 | AVS_CS_SUB_WIDTH_4,  // YVU 4:1:1 planar
  AVS_CS_YUV9  = AVS_CS_PLANAR | AVS_CS_YUV | AVS_CS_SAMPLE_BITS_8 | AVS_CS_VPLANEFIRST | AVS_CS_SUB_HEIGHT_4 | AVS_CS_SUB_WIDTH_4,  // YVU 4:1:0 planar
  AVS_CS_Y8    = AVS_CS_PLANAR | AVS_CS_INTERLEAVED | AVS_CS_YUV | AVS_CS_SAMPLE_BITS_8                                              // Y   4:0:0 planar
};

enum {
  AVS_IT_BFF = 1<<0,
  AVS_IT_TFF = 1<<1,
  AVS_IT_FIELDBASED = 1<<2};

enum {
  AVS_FILTER_TYPE=1,
  AVS_FILTER_INPUT_COLORSPACE=2,
  AVS_FILTER_OUTPUT_TYPE=9,
  AVS_FILTER_NAME=4,
  AVS_FILTER_AUTHOR=5,
  AVS_FILTER_VERSION=6,
  AVS_FILTER_ARGS=7,
  AVS_FILTER_ARGS_INFO=8,
  AVS_FILTER_ARGS_DESCRIPTION=10,
  AVS_FILTER_DESCRIPTION=11};

enum {  //SUBTYPES
  AVS_FILTER_TYPE_AUDIO=1,
  AVS_FILTER_TYPE_VIDEO=2,
  AVS_FILTER_OUTPUT_TYPE_SAME=3,
  AVS_FILTER_OUTPUT_TYPE_DIFFERENT=4};

enum {
  AVS_CACHE_NOTHING=0,
  AVS_CACHE_RANGE=1,
  AVS_CACHE_ALL=2,
  AVS_CACHE_AUDIO=3,
  AVS_CACHE_AUDIO_NONE=4,
  AVS_CACHE_AUDIO_AUTO=5
  };

#define AVS_FRAME_ALIGN 16

typedef struct AVS_Clip AVS_Clip;
typedef struct AVS_ScriptEnvironment AVS_ScriptEnvironment;

/////////////////////////////////////////////////////////////////////
//
// AVS_VideoInfo
//

// AVS_VideoInfo is layed out identicly to VideoInfo
typedef struct AVS_VideoInfo {
  int width, height;    // width=0 means no video
  unsigned fps_numerator, fps_denominator;
  int num_frames;

  int pixel_type;

  int audio_samples_per_second;   // 0 means no audio
  int sample_type;
  INT64 num_audio_samples;
  int nchannels;

  // Imagetype properties

  int image_type;
} AVS_VideoInfo;

// useful functions of the above
AVSC_INLINE int avs_has_video(const AVS_VideoInfo * p)
        { return (p->width!=0); }

AVSC_INLINE int avs_has_audio(const AVS_VideoInfo * p)
        { return (p->audio_samples_per_second!=0); }

AVSC_INLINE int avs_is_rgb(const AVS_VideoInfo * p)
        { return !!(p->pixel_type&AVS_CS_BGR); }

AVSC_INLINE int avs_is_rgb24(const AVS_VideoInfo * p)
        { return (p->pixel_type&AVS_CS_BGR24)==AVS_CS_BGR24; } // Clear out additional properties

AVSC_INLINE int avs_is_rgb32(const AVS_VideoInfo * p)
        { return (p->pixel_type & AVS_CS_BGR32) == AVS_CS_BGR32 ; }

AVSC_INLINE int avs_is_yuv(const AVS_VideoInfo * p)
        { return !!(p->pixel_type&AVS_CS_YUV ); }

AVSC_INLINE int avs_is_yuy2(const AVS_VideoInfo * p)
        { return (p->pixel_type & AVS_CS_YUY2) == AVS_CS_YUY2; }

AVSC_INLINE int avs_is_yv24(const AVS_VideoInfo * p)
        { return (p->pixel_type & AVS_CS_PLANAR_MASK) == (AVS_CS_YV24  & AVS_CS_PLANAR_FILTER); }

AVSC_INLINE int avs_is_yv16(const AVS_VideoInfo * p)
        { return (p->pixel_type & AVS_CS_PLANAR_MASK) == (AVS_CS_YV16  & AVS_CS_PLANAR_FILTER); }

AVSC_INLINE int avs_is_yv12(const AVS_VideoInfo * p)
        { return (p->pixel_type & AVS_CS_PLANAR_MASK) == (AVS_CS_YV12  & AVS_CS_PLANAR_FILTER); }

AVSC_INLINE int avs_is_yv411(const AVS_VideoInfo * p)
        { return (p->pixel_type & AVS_CS_PLANAR_MASK) == (AVS_CS_YV411 & AVS_CS_PLANAR_FILTER); }

AVSC_INLINE int avs_is_y8(const AVS_VideoInfo * p)
        { return (p->pixel_type & AVS_CS_PLANAR_MASK) == (AVS_CS_Y8    & AVS_CS_PLANAR_FILTER); }

AVSC_INLINE int avs_is_property(const AVS_VideoInfo * p, int property)
        { return ((p->pixel_type & property)==property ); }

AVSC_INLINE int avs_is_planar(const AVS_VideoInfo * p)
        { return !!(p->pixel_type & AVS_CS_PLANAR); }

AVSC_INLINE int avs_is_color_space(const AVS_VideoInfo * p, int c_space)
        { return avs_is_planar(p) ? ((p->pixel_type & AVS_CS_PLANAR_MASK) == (c_space & AVS_CS_PLANAR_FILTER)) : ((p->pixel_type & c_space) == c_space); }

AVSC_INLINE int avs_is_field_based(const AVS_VideoInfo * p)
        { return !!(p->image_type & AVS_IT_FIELDBASED); }

AVSC_INLINE int avs_is_parity_known(const AVS_VideoInfo * p)
        { return ((p->image_type & AVS_IT_FIELDBASED)&&(p->image_type & (AVS_IT_BFF | AVS_IT_TFF))); }

AVSC_INLINE int avs_is_bff(const AVS_VideoInfo * p)
        { return !!(p->image_type & AVS_IT_BFF); }

AVSC_INLINE int avs_is_tff(const AVS_VideoInfo * p)
        { return !!(p->image_type & AVS_IT_TFF); }

AVSC_INLINE int avs_bits_per_pixel(const AVS_VideoInfo * p)
{
  switch (p->pixel_type) {
      case AVS_CS_BGR24: return 24;
      case AVS_CS_BGR32: return 32;
      case AVS_CS_YUY2:  return 16;
      case AVS_CS_YV12:
      case AVS_CS_I420:  return 12;
      default:           return 0;
    }
}
AVSC_INLINE int avs_bytes_from_pixels(const AVS_VideoInfo * p, int pixels)
        { return pixels * (avs_bits_per_pixel(p)>>3); }   // Will work on planar images, but will return only luma planes

AVSC_INLINE int avs_row_size(const AVS_VideoInfo * p)
        { return avs_bytes_from_pixels(p,p->width); }  // Also only returns first plane on planar images

AVSC_INLINE int avs_bmp_size(const AVS_VideoInfo * vi)
        { if (avs_is_planar(vi)) {int p = vi->height * ((avs_row_size(vi)+3) & ~3); p+=p>>1; return p;  } return vi->height * ((avs_row_size(vi)+3) & ~3); }

AVSC_INLINE int avs_samples_per_second(const AVS_VideoInfo * p)
        { return p->audio_samples_per_second; }


AVSC_INLINE int avs_bytes_per_channel_sample(const AVS_VideoInfo * p)
{
    switch (p->sample_type) {
      case AVS_SAMPLE_INT8:  return sizeof(signed char);
      case AVS_SAMPLE_INT16: return sizeof(signed short);
      case AVS_SAMPLE_INT24: return 3;
      case AVS_SAMPLE_INT32: return sizeof(signed int);
      case AVS_SAMPLE_FLOAT: return sizeof(float);
      default: return 0;
    }
}
AVSC_INLINE int avs_bytes_per_audio_sample(const AVS_VideoInfo * p)
        { return p->nchannels*avs_bytes_per_channel_sample(p);}

AVSC_INLINE INT64 avs_audio_samples_from_frames(const AVS_VideoInfo * p, INT64 frames)
        { return ((INT64)(frames) * p->audio_samples_per_second * p->fps_denominator / p->fps_numerator); }

AVSC_INLINE int avs_frames_from_audio_samples(const AVS_VideoInfo * p, INT64 samples)
        { return (int)(samples * (INT64)p->fps_numerator / (INT64)p->fps_denominator / (INT64)p->audio_samples_per_second); }

AVSC_INLINE INT64 avs_audio_samples_from_bytes(const AVS_VideoInfo * p, INT64 bytes)
        { return bytes / avs_bytes_per_audio_sample(p); }

AVSC_INLINE INT64 avs_bytes_from_audio_samples(const AVS_VideoInfo * p, INT64 samples)
        { return samples * avs_bytes_per_audio_sample(p); }

AVSC_INLINE int avs_audio_channels(const AVS_VideoInfo * p)
        { return p->nchannels; }

AVSC_INLINE int avs_sample_type(const AVS_VideoInfo * p)
        { return p->sample_type;}

// useful mutator
AVSC_INLINE void avs_set_property(AVS_VideoInfo * p, int property)
        { p->image_type|=property; }

AVSC_INLINE void avs_clear_property(AVS_VideoInfo * p, int property)
        { p->image_type&=~property; }

AVSC_INLINE void avs_set_field_based(AVS_VideoInfo * p, int isfieldbased)
        { if (isfieldbased) p->image_type|=AVS_IT_FIELDBASED; else p->image_type&=~AVS_IT_FIELDBASED; }

AVSC_INLINE void avs_set_fps(AVS_VideoInfo * p, unsigned numerator, unsigned denominator)
{
    unsigned x=numerator, y=denominator;
    while (y) {   // find gcd
      unsigned t = x%y; x = y; y = t;
    }
    p->fps_numerator = numerator/x;
    p->fps_denominator = denominator/x;
}

AVSC_INLINE int avs_is_same_colorspace(AVS_VideoInfo * x, AVS_VideoInfo * y)
{
        return (x->pixel_type == y->pixel_type)
                || (avs_is_yv12(x) && avs_is_yv12(y));
}

/////////////////////////////////////////////////////////////////////
//
// AVS_VideoFrame
//

// VideoFrameBuffer holds information about a memory block which is used
// for video data.  For efficiency, instances of this class are not deleted
// when the refcount reaches zero; instead they're stored in a linked list
// to be reused.  The instances are deleted when the corresponding AVS
// file is closed.

// AVS_VideoFrameBuffer is layed out identicly to VideoFrameBuffer
// DO NOT USE THIS STRUCTURE DIRECTLY
typedef struct AVS_VideoFrameBuffer {
  BYTE * data;
  int data_size;
  // sequence_number is incremented every time the buffer is changed, so
  // that stale views can tell they're no longer valid.
  volatile long sequence_number;

  volatile long refcount;
} AVS_VideoFrameBuffer;

// VideoFrame holds a "window" into a VideoFrameBuffer.

// AVS_VideoFrame is layed out identicly to IVideoFrame
// DO NOT USE THIS STRUCTURE DIRECTLY
typedef struct AVS_VideoFrame {
  volatile long refcount;
  AVS_VideoFrameBuffer * vfb;
  int offset, pitch, row_size, height, offsetU, offsetV, pitchUV;  // U&V offsets are from top of picture.
  int row_sizeUV, heightUV;
} AVS_VideoFrame;

// Access functions for AVS_VideoFrame
AVSC_INLINE int avs_get_pitch(const AVS_VideoFrame * p) {
        return p->pitch;}

AVSC_INLINE int avs_get_pitch_p(const AVS_VideoFrame * p, int plane) {
  switch (plane) {
  case AVS_PLANAR_U: case AVS_PLANAR_V: return p->pitchUV;}
  return p->pitch;}

AVSC_INLINE int avs_get_row_size(const AVS_VideoFrame * p) {
        return p->row_size; }

AVSC_INLINE int avs_get_row_size_p(const AVS_VideoFrame * p, int plane) {
        int r;
    switch (plane) {
    case AVS_PLANAR_U: case AVS_PLANAR_V:
                if (p->pitchUV) return p->row_sizeUV;
                else            return 0;
    case AVS_PLANAR_U_ALIGNED: case AVS_PLANAR_V_ALIGNED:
                if (p->pitchUV) {
                        r = (p->row_sizeUV+AVS_FRAME_ALIGN-1)&(~(AVS_FRAME_ALIGN-1)); // Aligned rowsize
                        if (r < p->pitchUV)
                                return r;
                        return p->row_sizeUV;
                } else return 0;
    case AVS_PLANAR_Y_ALIGNED:
                r = (p->row_size+AVS_FRAME_ALIGN-1)&(~(AVS_FRAME_ALIGN-1)); // Aligned rowsize
                if (r <= p->pitch)
                        return r;
                return p->row_size;
    }
    return p->row_size;
}

AVSC_INLINE int avs_get_height(const AVS_VideoFrame * p) {
        return p->height;}

AVSC_INLINE int avs_get_height_p(const AVS_VideoFrame * p, int plane) {
        switch (plane) {
                case AVS_PLANAR_U: case AVS_PLANAR_V:
                        if (p->pitchUV) return p->heightUV;
                        return 0;
        }
        return p->height;}

AVSC_INLINE const BYTE* avs_get_read_ptr(const AVS_VideoFrame * p) {
        return p->vfb->data + p->offset;}

AVSC_INLINE const BYTE* avs_get_read_ptr_p(const AVS_VideoFrame * p, int plane)
{
        switch (plane) {
                case AVS_PLANAR_U: return p->vfb->data + p->offsetU;
                case AVS_PLANAR_V: return p->vfb->data + p->offsetV;
                default:           return p->vfb->data + p->offset;}
}

AVSC_INLINE int avs_is_writable(const AVS_VideoFrame * p) {
        return (p->refcount == 1 && p->vfb->refcount == 1);}

AVSC_INLINE BYTE* avs_get_write_ptr(const AVS_VideoFrame * p)
{
        if (avs_is_writable(p)) {
                ++p->vfb->sequence_number;
                return p->vfb->data + p->offset;
        } else
                return 0;
}

AVSC_INLINE BYTE* avs_get_write_ptr_p(const AVS_VideoFrame * p, int plane)
{
        if (plane==AVS_PLANAR_Y && avs_is_writable(p)) {
                ++p->vfb->sequence_number;
                return p->vfb->data + p->offset;
        } else if (plane==AVS_PLANAR_Y) {
                return 0;
        } else {
                switch (plane) {
                        case AVS_PLANAR_U: return p->vfb->data + p->offsetU;
                        case AVS_PLANAR_V: return p->vfb->data + p->offsetV;
                        default:       return p->vfb->data + p->offset;
                }
        }
}


AVSC_API(void, avs_release_video_frame)(AVS_VideoFrame *);
// makes a shallow copy of a video frame
AVSC_API(AVS_VideoFrame *, avs_copy_video_frame)(AVS_VideoFrame *);

#ifndef AVSC_NO_DECLSPEC
AVSC_INLINE void avs_release_frame(AVS_VideoFrame * f)
  {avs_release_video_frame(f);}
AVSC_INLINE AVS_VideoFrame * avs_copy_frame(AVS_VideoFrame * f)
  {return avs_copy_video_frame(f);}
#endif

/////////////////////////////////////////////////////////////////////
//
// AVS_Value
//

// Treat AVS_Value as a fat pointer.  That is use avs_copy_value
// and avs_release_value appropiaty as you would if AVS_Value was
// a pointer.

// To maintain source code compatibility with future versions of the
// avisynth_c API don't use the AVS_Value directly.  Use the helper
// functions below.

// AVS_Value is layed out identicly to AVSValue
typedef struct AVS_Value AVS_Value;
struct AVS_Value {
  short type;  // 'a'rray, 'c'lip, 'b'ool, 'i'nt, 'f'loat, 's'tring, 'v'oid, or 'l'ong
               // for some function e'rror
  short array_size;
  union {
    void * clip; // do not use directly, use avs_take_clip
    char boolean;
    int integer;
    float floating_pt;
    const char * string;
    const AVS_Value * array;
  } d;
};

// AVS_Value should be initilized with avs_void.
// Should also set to avs_void after the value is released
// with avs_copy_value.  Consider it the equalvent of setting
// a pointer to NULL
static const AVS_Value avs_void = {'v'};

AVSC_API(void, avs_copy_value)(AVS_Value * dest, AVS_Value src);
AVSC_API(void, avs_release_value)(AVS_Value);

AVSC_INLINE int avs_defined(AVS_Value v) { return v.type != 'v'; }
AVSC_INLINE int avs_is_clip(AVS_Value v) { return v.type == 'c'; }
AVSC_INLINE int avs_is_bool(AVS_Value v) { return v.type == 'b'; }
AVSC_INLINE int avs_is_int(AVS_Value v) { return v.type == 'i'; }
AVSC_INLINE int avs_is_float(AVS_Value v) { return v.type == 'f' || v.type == 'i'; }
AVSC_INLINE int avs_is_string(AVS_Value v) { return v.type == 's'; }
AVSC_INLINE int avs_is_array(AVS_Value v) { return v.type == 'a'; }
AVSC_INLINE int avs_is_error(AVS_Value v) { return v.type == 'e'; }

AVSC_API(AVS_Clip *, avs_take_clip)(AVS_Value, AVS_ScriptEnvironment *);
AVSC_API(void, avs_set_to_clip)(AVS_Value *, AVS_Clip *);

AVSC_INLINE int avs_as_bool(AVS_Value v)
        { return v.d.boolean; }
AVSC_INLINE int avs_as_int(AVS_Value v)
        { return v.d.integer; }
AVSC_INLINE const char * avs_as_string(AVS_Value v)
        { return avs_is_error(v) || avs_is_string(v) ? v.d.string : 0; }
AVSC_INLINE double avs_as_float(AVS_Value v)
        { return avs_is_int(v) ? v.d.integer : v.d.floating_pt; }
AVSC_INLINE const char * avs_as_error(AVS_Value v)
        { return avs_is_error(v) ? v.d.string : 0; }
AVSC_INLINE const AVS_Value * avs_as_array(AVS_Value v)
        { return v.d.array; }
AVSC_INLINE int avs_array_size(AVS_Value v)
        { return avs_is_array(v) ? v.array_size : 1; }
AVSC_INLINE AVS_Value avs_array_elt(AVS_Value v, int index)
        { return avs_is_array(v) ? v.d.array[index] : v; }

// only use these functions on an AVS_Value that does not already have
// an active value.  Remember, treat AVS_Value as a fat pointer.
AVSC_INLINE AVS_Value avs_new_value_bool(int v0)
        { AVS_Value v; v.type = 'b'; v.d.boolean = v0 == 0 ? 0 : 1; return v; }
AVSC_INLINE AVS_Value avs_new_value_int(int v0)
        { AVS_Value v; v.type = 'i'; v.d.integer = v0; return v; }
AVSC_INLINE AVS_Value avs_new_value_string(const char * v0)
        { AVS_Value v; v.type = 's'; v.d.string = v0; return v; }
AVSC_INLINE AVS_Value avs_new_value_float(float v0)
        { AVS_Value v; v.type = 'f'; v.d.floating_pt = v0; return v;}
AVSC_INLINE AVS_Value avs_new_value_error(const char * v0)
        { AVS_Value v; v.type = 'e'; v.d.string = v0; return v; }
#ifndef AVSC_NO_DECLSPEC
AVSC_INLINE AVS_Value avs_new_value_clip(AVS_Clip * v0)
        { AVS_Value v; avs_set_to_clip(&v, v0); return v; }
#endif
AVSC_INLINE AVS_Value avs_new_value_array(AVS_Value * v0, int size)
        { AVS_Value v; v.type = 'a'; v.d.array = v0; v.array_size = size; return v; }

/////////////////////////////////////////////////////////////////////
//
// AVS_Clip
//

AVSC_API(void, avs_release_clip)(AVS_Clip *);
AVSC_API(AVS_Clip *, avs_copy_clip)(AVS_Clip *);

AVSC_API(const char *, avs_clip_get_error)(AVS_Clip *); // return 0 if no error

AVSC_API(const AVS_VideoInfo *, avs_get_video_info)(AVS_Clip *);

AVSC_API(int, avs_get_version)(AVS_Clip *);

AVSC_API(AVS_VideoFrame *, avs_get_frame)(AVS_Clip *, int n);
// The returned video frame must be released with avs_release_video_frame

AVSC_API(int, avs_get_parity)(AVS_Clip *, int n);
// return field parity if field_based, else parity of first field in frame

AVSC_API(int, avs_get_audio)(AVS_Clip *, void * buf,
                             INT64 start, INT64 count);
// start and count are in samples

AVSC_API(int, avs_set_cache_hints)(AVS_Clip *,
                                   int cachehints, int frame_range);

// This is the callback type used by avs_add_function
typedef AVS_Value (AVSC_CC * AVS_ApplyFunc)
                        (AVS_ScriptEnvironment *, AVS_Value args, void * user_data);

typedef struct AVS_FilterInfo AVS_FilterInfo;
struct AVS_FilterInfo
{
  // these members should not be modified outside of the AVS_ApplyFunc callback
  AVS_Clip * child;
  AVS_VideoInfo vi;
  AVS_ScriptEnvironment * env;
  AVS_VideoFrame * (AVSC_CC * get_frame)(AVS_FilterInfo *, int n);
  int (AVSC_CC * get_parity)(AVS_FilterInfo *, int n);
  int (AVSC_CC * get_audio)(AVS_FilterInfo *, void * buf,
                                  INT64 start, INT64 count);
  int (AVSC_CC * set_cache_hints)(AVS_FilterInfo *, int cachehints,
                                        int frame_range);
  void (AVSC_CC * free_filter)(AVS_FilterInfo *);

  // Should be set when ever there is an error to report.
  // It is cleared before any of the above methods are called
  const char * error;
  // this is to store whatever and may be modified at will
  void * user_data;
};

// Create a new filter
// fi is set to point to the AVS_FilterInfo so that you can
//   modify it once it is initilized.
// store_child should generally be set to true.  If it is not
//    set than ALL methods (the function pointers) must be defined
// If it is set than you do not need to worry about freeing the child
//    clip.
AVSC_API(AVS_Clip *, avs_new_c_filter)(AVS_ScriptEnvironment * e,
                                       AVS_FilterInfo * * fi,
                                       AVS_Value child, int store_child);

/////////////////////////////////////////////////////////////////////
//
// AVS_ScriptEnvironment
//

// For GetCPUFlags.  These are backwards-compatible with those in VirtualDub.
enum {
                                /* slowest CPU to support extension */
  AVS_CPU_FORCE        = 0x01,   // N/A
  AVS_CPU_FPU          = 0x02,   // 386/486DX
  AVS_CPU_MMX          = 0x04,   // P55C, K6, PII
  AVS_CPU_INTEGER_SSE  = 0x08,   // PIII, Athlon
  AVS_CPU_SSE          = 0x10,   // PIII, Athlon XP/MP
  AVS_CPU_SSE2         = 0x20,   // PIV, Hammer
  AVS_CPU_3DNOW        = 0x40,   // K6-2
  AVS_CPU_3DNOW_EXT    = 0x80,   // Athlon
  AVS_CPU_X86_64       = 0xA0,   // Hammer (note: equiv. to 3DNow + SSE2,
                                 // which only Hammer will have anyway)
  AVS_CPUF_SSE3       = 0x100,   //  PIV+, K8 Venice
  AVS_CPUF_SSSE3      = 0x200,   //  Core 2
  AVS_CPUF_SSE4       = 0x400,   //  Penryn, Wolfdale, Yorkfield
  AVS_CPUF_SSE4_1     = 0x400,
  AVS_CPUF_SSE4_2     = 0x800,   //  Nehalem
};

AVSC_API(const char *, avs_get_error)(AVS_ScriptEnvironment *); // return 0 if no error

AVSC_API(long, avs_get_cpu_flags)(AVS_ScriptEnvironment *);
AVSC_API(int, avs_check_version)(AVS_ScriptEnvironment *, int version);

AVSC_API(char *, avs_save_string)(AVS_ScriptEnvironment *, const char* s, int length);
AVSC_API(char *, avs_sprintf)(AVS_ScriptEnvironment *, const char * fmt, ...);

AVSC_API(char *, avs_vsprintf)(AVS_ScriptEnvironment *, const char * fmt, void* val);
 // note: val is really a va_list; I hope everyone typedefs va_list to a pointer

AVSC_API(int, avs_add_function)(AVS_ScriptEnvironment *,
                                const char * name, const char * params,
                                AVS_ApplyFunc apply, void * user_data);

AVSC_API(int, avs_function_exists)(AVS_ScriptEnvironment *, const char * name);

AVSC_API(AVS_Value, avs_invoke)(AVS_ScriptEnvironment *, const char * name,
                               AVS_Value args, const char** arg_names);
// The returned value must be be released with avs_release_value

AVSC_API(AVS_Value, avs_get_var)(AVS_ScriptEnvironment *, const char* name);
// The returned value must be be released with avs_release_value

AVSC_API(int, avs_set_var)(AVS_ScriptEnvironment *, const char* name, AVS_Value val);

AVSC_API(int, avs_set_global_var)(AVS_ScriptEnvironment *, const char* name, const AVS_Value val);

//void avs_push_context(AVS_ScriptEnvironment *, int level=0);
//void avs_pop_context(AVS_ScriptEnvironment *);

AVSC_API(AVS_VideoFrame *, avs_new_video_frame_a)(AVS_ScriptEnvironment *,
                                          const AVS_VideoInfo * vi, int align);
// align should be at least 16

#ifndef AVSC_NO_DECLSPEC
AVSC_INLINE
AVS_VideoFrame * avs_new_video_frame(AVS_ScriptEnvironment * env,
                                     const AVS_VideoInfo * vi)
  {return avs_new_video_frame_a(env,vi,AVS_FRAME_ALIGN);}

AVSC_INLINE
AVS_VideoFrame * avs_new_frame(AVS_ScriptEnvironment * env,
                               const AVS_VideoInfo * vi)
  {return avs_new_video_frame_a(env,vi,AVS_FRAME_ALIGN);}
#endif


AVSC_API(int, avs_make_writable)(AVS_ScriptEnvironment *, AVS_VideoFrame * * pvf);

AVSC_API(void, avs_bit_blt)(AVS_ScriptEnvironment *, BYTE* dstp, int dst_pitch, const BYTE* srcp, int src_pitch, int row_size, int height);

typedef void (AVSC_CC *AVS_ShutdownFunc)(void* user_data, AVS_ScriptEnvironment * env);
AVSC_API(void, avs_at_exit)(AVS_ScriptEnvironment *, AVS_ShutdownFunc function, void * user_data);

AVSC_API(AVS_VideoFrame *, avs_subframe)(AVS_ScriptEnvironment *, AVS_VideoFrame * src, int rel_offset, int new_pitch, int new_row_size, int new_height);
// The returned video frame must be be released

AVSC_API(int, avs_set_memory_max)(AVS_ScriptEnvironment *, int mem);

AVSC_API(int, avs_set_working_dir)(AVS_ScriptEnvironment *, const char * newdir);

// avisynth.dll exports this; it's a way to use it as a library, without
// writing an AVS script or without going through AVIFile.
AVSC_API(AVS_ScriptEnvironment *, avs_create_script_environment)(int version);

// this symbol is the entry point for the plugin and must
// be defined
AVSC_EXPORT
const char * AVSC_CC avisynth_c_plugin_init(AVS_ScriptEnvironment* env);


AVSC_API(void, avs_delete_script_environment)(AVS_ScriptEnvironment *);


AVSC_API(AVS_VideoFrame *, avs_subframe_planar)(AVS_ScriptEnvironment *, AVS_VideoFrame * src, int rel_offset, int new_pitch, int new_row_size, int new_height, int rel_offsetU, int rel_offsetV, int new_pitchUV);
// The returned video frame must be be released

#ifdef AVSC_NO_DECLSPEC
// use LoadLibrary and related functions to dynamically load Avisynth instead of declspec(dllimport)
/*
  The following functions needs to have been declared, probably from windows.h

  void* malloc(size_t)
  void free(void*);

  HMODULE LoadLibrary(const char*);
  void* GetProcAddress(HMODULE, const char*);
  FreeLibrary(HMODULE);
*/


typedef struct AVS_Library AVS_Library;

#define AVSC_DECLARE_FUNC(name) name##_func name

struct AVS_Library {
  HMODULE handle;

  AVSC_DECLARE_FUNC(avs_add_function);
  AVSC_DECLARE_FUNC(avs_at_exit);
  AVSC_DECLARE_FUNC(avs_bit_blt);
  AVSC_DECLARE_FUNC(avs_check_version);
  AVSC_DECLARE_FUNC(avs_clip_get_error);
  AVSC_DECLARE_FUNC(avs_copy_clip);
  AVSC_DECLARE_FUNC(avs_copy_value);
  AVSC_DECLARE_FUNC(avs_copy_video_frame);
  AVSC_DECLARE_FUNC(avs_create_script_environment);
  AVSC_DECLARE_FUNC(avs_delete_script_environment);
  AVSC_DECLARE_FUNC(avs_function_exists);
  AVSC_DECLARE_FUNC(avs_get_audio);
  AVSC_DECLARE_FUNC(avs_get_cpu_flags);
  AVSC_DECLARE_FUNC(avs_get_error);
  AVSC_DECLARE_FUNC(avs_get_frame);
  AVSC_DECLARE_FUNC(avs_get_parity);
  AVSC_DECLARE_FUNC(avs_get_var);
  AVSC_DECLARE_FUNC(avs_get_version);
  AVSC_DECLARE_FUNC(avs_get_video_info);
  AVSC_DECLARE_FUNC(avs_invoke);
  AVSC_DECLARE_FUNC(avs_make_writable);
  AVSC_DECLARE_FUNC(avs_new_c_filter);
  AVSC_DECLARE_FUNC(avs_new_video_frame_a);
  AVSC_DECLARE_FUNC(avs_release_clip);
  AVSC_DECLARE_FUNC(avs_release_value);
  AVSC_DECLARE_FUNC(avs_release_video_frame);
  AVSC_DECLARE_FUNC(avs_save_string);
  AVSC_DECLARE_FUNC(avs_set_cache_hints);
  AVSC_DECLARE_FUNC(avs_set_global_var);
  AVSC_DECLARE_FUNC(avs_set_memory_max);
  AVSC_DECLARE_FUNC(avs_set_to_clip);
  AVSC_DECLARE_FUNC(avs_set_var);
  AVSC_DECLARE_FUNC(avs_set_working_dir);
  AVSC_DECLARE_FUNC(avs_sprintf);
  AVSC_DECLARE_FUNC(avs_subframe);
  AVSC_DECLARE_FUNC(avs_subframe_planar);
  AVSC_DECLARE_FUNC(avs_take_clip);
  AVSC_DECLARE_FUNC(avs_vsprintf);
};

#undef AVSC_DECLARE_FUNC


AVSC_INLINE AVS_Library * avs_load_library() {
  AVS_Library *library = (AVS_Library *)malloc(sizeof(AVS_Library));
  if (library == NULL)
    return NULL;
  library->handle = LoadLibrary("avisynth");
  if (library->handle == NULL)
    goto fail;

#define __AVSC_STRINGIFY(x) #x
#define AVSC_STRINGIFY(x) __AVSC_STRINGIFY(x)
#define AVSC_LOAD_FUNC(name) {\
  library->name = (name##_func) GetProcAddress(library->handle, AVSC_STRINGIFY(name));\
  if (library->name == NULL)\
    goto fail;\
}

  AVSC_LOAD_FUNC(avs_add_function);
  AVSC_LOAD_FUNC(avs_at_exit);
  AVSC_LOAD_FUNC(avs_bit_blt);
  AVSC_LOAD_FUNC(avs_check_version);
  AVSC_LOAD_FUNC(avs_clip_get_error);
  AVSC_LOAD_FUNC(avs_copy_clip);
  AVSC_LOAD_FUNC(avs_copy_value);
  AVSC_LOAD_FUNC(avs_copy_video_frame);
  AVSC_LOAD_FUNC(avs_create_script_environment);
  AVSC_LOAD_FUNC(avs_delete_script_environment);
  AVSC_LOAD_FUNC(avs_function_exists);
  AVSC_LOAD_FUNC(avs_get_audio);
  AVSC_LOAD_FUNC(avs_get_cpu_flags);
  AVSC_LOAD_FUNC(avs_get_error);
  AVSC_LOAD_FUNC(avs_get_frame);
  AVSC_LOAD_FUNC(avs_get_parity);
  AVSC_LOAD_FUNC(avs_get_var);
  AVSC_LOAD_FUNC(avs_get_version);
  AVSC_LOAD_FUNC(avs_get_video_info);
  AVSC_LOAD_FUNC(avs_invoke);
  AVSC_LOAD_FUNC(avs_make_writable);
  AVSC_LOAD_FUNC(avs_new_c_filter);
  AVSC_LOAD_FUNC(avs_new_video_frame_a);
  AVSC_LOAD_FUNC(avs_release_clip);
  AVSC_LOAD_FUNC(avs_release_value);
  AVSC_LOAD_FUNC(avs_release_video_frame);
  AVSC_LOAD_FUNC(avs_save_string);
  AVSC_LOAD_FUNC(avs_set_cache_hints);
  AVSC_LOAD_FUNC(avs_set_global_var);
  AVSC_LOAD_FUNC(avs_set_memory_max);
  AVSC_LOAD_FUNC(avs_set_to_clip);
  AVSC_LOAD_FUNC(avs_set_var);
  AVSC_LOAD_FUNC(avs_set_working_dir);
  AVSC_LOAD_FUNC(avs_sprintf);
  AVSC_LOAD_FUNC(avs_subframe);
  AVSC_LOAD_FUNC(avs_subframe_planar);
  AVSC_LOAD_FUNC(avs_take_clip);
  AVSC_LOAD_FUNC(avs_vsprintf);

#undef __AVSC_STRINGIFY
#undef AVSC_STRINGIFY
#undef AVSC_LOAD_FUNC

  return library;

fail:
  free(library);
  return NULL;
}

AVSC_INLINE void avs_free_library(AVS_Library *library) {
  if (library == NULL)
    return;
  FreeLibrary(library->handle);
  free(library);
}
#endif

#endif
