/*
 * filter registration
 * copyright (c) 2008 Vitor Sessak
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

#include "avfilter.h"

#define REGISTER_VF(X,x) { \
          extern AVFilter avfilter_vf_##x ; \
          if(ENABLE_VF_##X )  avfilter_register(&avfilter_vf_##x ); }


#define REGISTER_VSRC(X,x) { \
          extern AVFilter avfilter_vsrc_##x ; \
          if(ENABLE_VSRC_##X )  avfilter_register(&avfilter_vsrc_##x ); }

void avfilter_register_all(void)
{
    static int initialized;

    if (initialized)
        return;
    initialized = 1;

//    REGISTER_VF(CROP,crop);

}
