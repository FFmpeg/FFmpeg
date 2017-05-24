/*
 * Copyright (c) 2016 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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

#include "config.h"
#include "libavutil/attributes.h"
#include "wmv2dsp_mips.h"

#if HAVE_MMI
static av_cold void wmv2dsp_init_mmi(WMV2DSPContext *c)
{
    c->idct_add  = ff_wmv2_idct_add_mmi;
    c->idct_put  = ff_wmv2_idct_put_mmi;
}
#endif /* HAVE_MMI */

av_cold void ff_wmv2dsp_init_mips(WMV2DSPContext *c)
{
#if HAVE_MMI
    wmv2dsp_init_mmi(c);
#endif /* HAVE_MMI */
}
