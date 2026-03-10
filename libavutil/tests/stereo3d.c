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

#include <stdio.h>

#include "libavutil/frame.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/stereo3d.h"

int main(void)
{
    AVStereo3D *s3d;
    AVFrame *frame;
    size_t size;
    int ret;

    /* av_stereo3d_alloc_size with size output */
    printf("Testing av_stereo3d_alloc_size()\n");
    s3d = av_stereo3d_alloc_size(&size);
    printf("alloc_size: %s, size>0: %s\n",
           s3d ? "OK" : "FAIL", size > 0 ? "yes" : "no");
    av_free(s3d);

    /* av_stereo3d_alloc (no size) */
    s3d = av_stereo3d_alloc();
    printf("alloc: %s\n", s3d ? "OK" : "FAIL");
    av_free(s3d);

    /* av_stereo3d_create_side_data */
    printf("\nTesting av_stereo3d_create_side_data()\n");
    frame = av_frame_alloc();
    s3d = av_stereo3d_create_side_data(frame);
    printf("create_side_data: %s\n", s3d ? "OK" : "FAIL");
    av_frame_free(&frame);

    /* av_stereo3d_type_name - all valid types */
    printf("\nTesting av_stereo3d_type_name()\n");
    for (int i = 0; i <= AV_STEREO3D_UNSPEC; i++)
        printf("type %d: %s\n", i, av_stereo3d_type_name(i));
    printf("out of range: %s\n", av_stereo3d_type_name(100));

    /* av_stereo3d_from_name - all valid names and unknown */
    printf("\nTesting av_stereo3d_from_name()\n");
    {
        static const char * const names[] = {
            "2D", "side by side", "top and bottom", "frame alternate",
            "checkerboard", "side by side (quincunx subsampling)",
            "interleaved lines", "interleaved columns", "unspecified",
        };
        for (int i = 0; i < FF_ARRAY_ELEMS(names); i++)
            printf("%s: %d\n", names[i], av_stereo3d_from_name(names[i]));
    }
    ret = av_stereo3d_from_name("nonexistent");
    printf("nonexistent: %d\n", ret);

    /* av_stereo3d_type_name / av_stereo3d_from_name round-trip */
    printf("\nTesting type name round-trip\n");
    for (int i = 0; i <= AV_STEREO3D_UNSPEC; i++) {
        const char *name = av_stereo3d_type_name(i);
        int rt = av_stereo3d_from_name(name);
        printf("type roundtrip %d (%s): %s\n", i, name, rt == i ? "OK" : "FAIL");
    }

    /* av_stereo3d_view_name - all valid views */
    printf("\nTesting av_stereo3d_view_name()\n");
    for (int i = 0; i <= AV_STEREO3D_VIEW_UNSPEC; i++)
        printf("view %d: %s\n", i, av_stereo3d_view_name(i));
    printf("out of range: %s\n", av_stereo3d_view_name(100));

    /* av_stereo3d_view_name / av_stereo3d_view_from_name round-trip */
    printf("\nTesting view name round-trip\n");
    for (int i = 0; i <= AV_STEREO3D_VIEW_UNSPEC; i++) {
        const char *name = av_stereo3d_view_name(i);
        int rt = av_stereo3d_view_from_name(name);
        printf("view roundtrip %d (%s): %s\n", i, name, rt == i ? "OK" : "FAIL");
    }

    /* av_stereo3d_view_from_name */
    printf("\nTesting av_stereo3d_view_from_name()\n");
    printf("packed: %d\n", av_stereo3d_view_from_name("packed"));
    printf("left: %d\n", av_stereo3d_view_from_name("left"));
    printf("right: %d\n", av_stereo3d_view_from_name("right"));
    printf("unspecified: %d\n", av_stereo3d_view_from_name("unspecified"));
    ret = av_stereo3d_view_from_name("nonexistent");
    printf("nonexistent: %d\n", ret);

    /* av_stereo3d_primary_eye_name - all valid values */
    printf("\nTesting av_stereo3d_primary_eye_name()\n");
    for (int i = 0; i <= AV_PRIMARY_EYE_RIGHT; i++)
        printf("eye %d: %s\n", i, av_stereo3d_primary_eye_name(i));
    printf("out of range: %s\n", av_stereo3d_primary_eye_name(100));

    /* av_stereo3d_primary_eye_from_name */
    printf("\nTesting av_stereo3d_primary_eye_from_name()\n");
    printf("none: %d\n", av_stereo3d_primary_eye_from_name("none"));
    printf("left: %d\n", av_stereo3d_primary_eye_from_name("left"));
    printf("right: %d\n", av_stereo3d_primary_eye_from_name("right"));
    ret = av_stereo3d_primary_eye_from_name("nonexistent");
    printf("nonexistent: %d\n", ret);

    return 0;
}
