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


#define REGISTER_FILTER(X,x,y) { \
          extern AVFilter avfilter_##y##_##x ; \
          if(CONFIG_##X##_FILTER )  avfilter_register(&avfilter_##y##_##x ); }

void avfilter_register_all(void)
{
    static int initialized;

    if (initialized)
        return;
    initialized = 1;

    REGISTER_FILTER (ASPECT,      aspect,      vf);
    REGISTER_FILTER (CROP,        crop,        vf);
    REGISTER_FILTER (FORMAT,      format,      vf);
    REGISTER_FILTER (NOFORMAT,    noformat,    vf);
    REGISTER_FILTER (NULL,        null,        vf);
    REGISTER_FILTER (PIXELASPECT, pixelaspect, vf);
    REGISTER_FILTER (SCALE,       scale,       vf);
    REGISTER_FILTER (SLICIFY,     slicify,     vf);
    REGISTER_FILTER (UNSHARP,     unsharp,     vf);
    REGISTER_FILTER (VFLIP,       vflip,       vf);

    REGISTER_FILTER (NULLSRC,     nullsrc,     vsrc);

    REGISTER_FILTER (NULLSINK,    nullsink,    vsink);
}
