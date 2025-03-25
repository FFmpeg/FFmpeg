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

#include "libavutil/log.h"
#include "utils.h"

void ff_metal_compute_encoder_dispatch(id<MTLDevice> device,
                                       id<MTLComputePipelineState> pipeline,
                                       id<MTLComputeCommandEncoder> encoder,
                                       NSUInteger width, NSUInteger height)
{
    [encoder setComputePipelineState:pipeline];
    NSUInteger w = pipeline.threadExecutionWidth;
    NSUInteger h = pipeline.maxTotalThreadsPerThreadgroup / w;
    MTLSize threadsPerThreadgroup = MTLSizeMake(w, h, 1);
    // MAC_OS_X_VERSION_10_15 is only defined on SDKs new enough to include its functionality (including iOS, tvOS, etc)
#ifdef MAC_OS_X_VERSION_10_15
    if (@available(macOS 10.15, iOS 13, tvOS 14.5, *)) {
        if ([device supportsFamily:MTLGPUFamilyCommon3]) {
            MTLSize threadsPerGrid = MTLSizeMake(width, height, 1);
            [encoder dispatchThreads:threadsPerGrid threadsPerThreadgroup:threadsPerThreadgroup];
            return;
        }
    }
#endif

    // Fallback path, if we took the above one we already returned so none of this is reached
    {
        MTLSize threadgroups = MTLSizeMake((width + w - 1) / w,
                                           (height + h - 1) / h,
                                           1);
        [encoder dispatchThreadgroups:threadgroups threadsPerThreadgroup:threadsPerThreadgroup];
    }
}

CVMetalTextureRef ff_metal_texture_from_pixbuf(void *ctx,
                                               CVMetalTextureCacheRef textureCache,
                                               CVPixelBufferRef pixbuf,
                                               int plane,
                                               MTLPixelFormat format)
{
    CVMetalTextureRef tex = NULL;
    CVReturn ret;

    ret = CVMetalTextureCacheCreateTextureFromImage(
        NULL,
        textureCache,
        pixbuf,
        NULL,
        format,
        CVPixelBufferGetWidthOfPlane(pixbuf, plane),
        CVPixelBufferGetHeightOfPlane(pixbuf, plane),
        plane,
        &tex
    );
    if (ret != kCVReturnSuccess) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create CVMetalTexture from image: %d\n", ret);
        return NULL;
    }

    return tex;
}
