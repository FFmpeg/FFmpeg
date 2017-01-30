/** libavutil DCE definitions
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

#include "libavutil/float_dsp.h"
#include "libavutil/cpu_internal.h"

int ff_get_cpu_flags_aarch64(void) {return 0;}
int ff_get_cpu_flags_arm(void) {return 0;}
int ff_get_cpu_flags_ppc(void) {return 0;}
void ff_float_dsp_init_aarch64(AVFloatDSPContext *fdsp) {return;}
void ff_float_dsp_init_arm(AVFloatDSPContext *fdsp) {return;}
void ff_float_dsp_init_mips(AVFloatDSPContext *fdsp) {return;}
void ff_float_dsp_init_ppc(AVFloatDSPContext *fdsp, int strict) {return;}
