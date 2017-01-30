/** libswscale DCE definitions
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "config.h"

#include "libswscale/swscale_internal.h"

SwsFunc ff_yuv2rgb_init_ppc(SwsContext *c) {return *(SwsFunc*)(0);}
void ff_get_unscaled_swscale_aarch64(SwsContext *c) {return;}
void ff_get_unscaled_swscale_arm(SwsContext *c) {return;}
void ff_get_unscaled_swscale_ppc(SwsContext *c) {return;}
void ff_sws_init_swscale_aarch64(SwsContext *c) {return;}
void ff_sws_init_swscale_arm(SwsContext *c) {return;}
void ff_sws_init_swscale_ppc(SwsContext *c) {return;}
void ff_yuv2rgb_init_tables_ppc(SwsContext *c, const int inv_table[4],
                                int brightness, int contrast, int saturation) {return;}
