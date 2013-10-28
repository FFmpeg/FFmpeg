//  Copyright (c) 2011 FFmpegSource Project
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

/* these are defines/functions that are used and were changed in the switch to 2.6
 * and are needed to maintain full compatility with 2.5 */

enum {
  AVS_CS_YV12_25 = 1<<3 | AVS_CS_YUV | AVS_CS_PLANAR,  // y-v-u, planar
  AVS_CS_I420_25 = 1<<4 | AVS_CS_YUV | AVS_CS_PLANAR,  // y-u-v, planar
};

AVSC_INLINE int avs_get_height_p_25(const AVS_VideoFrame * p, int plane) {
    switch (plane)
    {
        case AVS_PLANAR_U: case AVS_PLANAR_V:
            if (p->pitchUV)
                return p->height>>1;
            return 0;
    }
    return p->height;}

AVSC_INLINE int avs_get_row_size_p_25(const AVS_VideoFrame * p, int plane) {
    int r;
    switch (plane)
    {
    case AVS_PLANAR_U: case AVS_PLANAR_V:
        if (p->pitchUV)
            return p->row_size>>1;
        else
            return 0;
    case AVS_PLANAR_U_ALIGNED: case AVS_PLANAR_V_ALIGNED:
        if (p->pitchUV)
        {
            r = ((p->row_size+AVS_FRAME_ALIGN-1)&(~(AVS_FRAME_ALIGN-1)) )>>1; // Aligned rowsize
            if (r < p->pitchUV)
                return r;
            return p->row_size>>1;
        }
        else
            return 0;
    case AVS_PLANAR_Y_ALIGNED:
        r = (p->row_size+AVS_FRAME_ALIGN-1)&(~(AVS_FRAME_ALIGN-1)); // Aligned rowsize
        if (r <= p->pitch)
            return r;
        return p->row_size;
    }
    return p->row_size;
}

AVSC_INLINE int avs_is_yv12_25(const AVS_VideoInfo * p)
    { return ((p->pixel_type & AVS_CS_YV12_25) == AVS_CS_YV12_25)||((p->pixel_type & AVS_CS_I420_25) == AVS_CS_I420_25); }
