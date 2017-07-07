/*
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

#include "libavutil/atomic.h"
#include "libavutil/avassert.h"

int main(void)
{
    volatile int val      = 1;
    void *tmp1            = (int *)&val;
    void * volatile *tmp2 = &tmp1;
    int res;

    res = avpriv_atomic_int_add_and_fetch(&val, 1);
    av_assert0(res == 2);
    avpriv_atomic_int_set(&val, 3);
    res = avpriv_atomic_int_get(&val);
    av_assert0(res == 3);
    avpriv_atomic_ptr_cas(tmp2, tmp1, &res);
    av_assert0(*tmp2 == &res);

    return 0;
}
