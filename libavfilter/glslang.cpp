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

#include <pthread.h>

extern "C" {
#include "libavutil/mem.h"
#include "libavutil/avassert.h"
}

#include <glslang/Include/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include "glslang.h"

using namespace glslang;

static pthread_mutex_t glslang_mutex = PTHREAD_MUTEX_INITIALIZER;
static int glslang_refcount = 0;

/* We require Vulkan 1.1 */
#define GLSL_VERSION EShTargetVulkan_1_1

/* Vulkan 1.1 implementations require SPIR-V 1.3 to be implemented */
#define SPIRV_VERSION EShTargetSpv_1_3

// Taken from glslang's examples, which apparently generally bases the choices
// on OpenGL specification limits
static const TBuiltInResource DefaultTBuiltInResource = {
    /* .MaxLights = */ 32,
    /* .MaxClipPlanes = */ 6,
    /* .MaxTextureUnits = */ 32,
    /* .MaxTextureCoords = */ 32,
    /* .MaxVertexAttribs = */ 64,
    /* .MaxVertexUniformComponents = */ 4096,
    /* .MaxVaryingFloats = */ 64,
    /* .MaxVertexTextureImageUnits = */ 32,
    /* .MaxCombinedTextureImageUnits = */ 80,
    /* .MaxTextureImageUnits = */ 32,
    /* .MaxFragmentUniformComponents = */ 4096,
    /* .MaxDrawBuffers = */ 32,
    /* .MaxVertexUniformVectors = */ 128,
    /* .MaxVaryingVectors = */ 8,
    /* .MaxFragmentUniformVectors = */ 16,
    /* .MaxVertexOutputVectors = */ 16,
    /* .MaxFragmentInputVectors = */ 15,
    /* .MinProgramTexelOffset = */ -8,
    /* .MaxProgramTexelOffset = */ 7,
    /* .MaxClipDistances = */ 8,
    /* .MaxComputeWorkGroupCountX = */ 65535,
    /* .MaxComputeWorkGroupCountY = */ 65535,
    /* .MaxComputeWorkGroupCountZ = */ 65535,
    /* .MaxComputeWorkGroupSizeX = */ 1024,
    /* .MaxComputeWorkGroupSizeY = */ 1024,
    /* .MaxComputeWorkGroupSizeZ = */ 64,
    /* .MaxComputeUniformComponents = */ 1024,
    /* .MaxComputeTextureImageUnits = */ 16,
    /* .MaxComputeImageUniforms = */ 8,
    /* .MaxComputeAtomicCounters = */ 8,
    /* .MaxComputeAtomicCounterBuffers = */ 1,
    /* .MaxVaryingComponents = */ 60,
    /* .MaxVertexOutputComponents = */ 64,
    /* .MaxGeometryInputComponents = */ 64,
    /* .MaxGeometryOutputComponents = */ 128,
    /* .MaxFragmentInputComponents = */ 128,
    /* .MaxImageUnits = */ 8,
    /* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
    /* .MaxCombinedShaderOutputResources = */ 8,
    /* .MaxImageSamples = */ 0,
    /* .MaxVertexImageUniforms = */ 0,
    /* .MaxTessControlImageUniforms = */ 0,
    /* .MaxTessEvaluationImageUniforms = */ 0,
    /* .MaxGeometryImageUniforms = */ 0,
    /* .MaxFragmentImageUniforms = */ 8,
    /* .MaxCombinedImageUniforms = */ 8,
    /* .MaxGeometryTextureImageUnits = */ 16,
    /* .MaxGeometryOutputVertices = */ 256,
    /* .MaxGeometryTotalOutputComponents = */ 1024,
    /* .MaxGeometryUniformComponents = */ 1024,
    /* .MaxGeometryVaryingComponents = */ 64,
    /* .MaxTessControlInputComponents = */ 128,
    /* .MaxTessControlOutputComponents = */ 128,
    /* .MaxTessControlTextureImageUnits = */ 16,
    /* .MaxTessControlUniformComponents = */ 1024,
    /* .MaxTessControlTotalOutputComponents = */ 4096,
    /* .MaxTessEvaluationInputComponents = */ 128,
    /* .MaxTessEvaluationOutputComponents = */ 128,
    /* .MaxTessEvaluationTextureImageUnits = */ 16,
    /* .MaxTessEvaluationUniformComponents = */ 1024,
    /* .MaxTessPatchComponents = */ 120,
    /* .MaxPatchVertices = */ 32,
    /* .MaxTessGenLevel = */ 64,
    /* .MaxViewports = */ 16,
    /* .MaxVertexAtomicCounters = */ 0,
    /* .MaxTessControlAtomicCounters = */ 0,
    /* .MaxTessEvaluationAtomicCounters = */ 0,
    /* .MaxGeometryAtomicCounters = */ 0,
    /* .MaxFragmentAtomicCounters = */ 8,
    /* .MaxCombinedAtomicCounters = */ 8,
    /* .MaxAtomicCounterBindings = */ 1,
    /* .MaxVertexAtomicCounterBuffers = */ 0,
    /* .MaxTessControlAtomicCounterBuffers = */ 0,
    /* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
    /* .MaxGeometryAtomicCounterBuffers = */ 0,
    /* .MaxFragmentAtomicCounterBuffers = */ 1,
    /* .MaxCombinedAtomicCounterBuffers = */ 1,
    /* .MaxAtomicCounterBufferSize = */ 16384,
    /* .MaxTransformFeedbackBuffers = */ 4,
    /* .MaxTransformFeedbackInterleavedComponents = */ 64,
    /* .MaxCullDistances = */ 8,
    /* .MaxCombinedClipAndCullDistances = */ 8,
    /* .MaxSamples = */ 4,
    /* .maxMeshOutputVerticesNV = */ 256,
    /* .maxMeshOutputPrimitivesNV = */ 512,
    /* .maxMeshWorkGroupSizeX_NV = */ 32,
    /* .maxMeshWorkGroupSizeY_NV = */ 1,
    /* .maxMeshWorkGroupSizeZ_NV = */ 1,
    /* .maxTaskWorkGroupSizeX_NV = */ 32,
    /* .maxTaskWorkGroupSizeY_NV = */ 1,
    /* .maxTaskWorkGroupSizeZ_NV = */ 1,
    /* .maxMeshViewCountNV = */ 4,

    .limits = {
        /* .nonInductiveForLoops = */ 1,
        /* .whileLoops = */ 1,
        /* .doWhileLoops = */ 1,
        /* .generalUniformIndexing = */ 1,
        /* .generalAttributeMatrixVectorIndexing = */ 1,
        /* .generalVaryingIndexing = */ 1,
        /* .generalSamplerIndexing = */ 1,
        /* .generalVariableIndexing = */ 1,
        /* .generalConstantMatrixVectorIndexing = */ 1,
    }
};

GLSlangResult *glslang_compile(const char *glsl, enum GLSlangStage stage)
{
    GLSlangResult *res = (GLSlangResult *)av_mallocz(sizeof(*res));
    if (!res)
        return NULL;

    static const EShLanguage lang[] = {
        [GLSLANG_VERTEX]   = EShLangVertex,
        [GLSLANG_FRAGMENT] = EShLangFragment,
        [GLSLANG_COMPUTE]  = EShLangCompute,
    };

    assert(glslang_refcount);
    TShader *shader = new TShader(lang[stage]);
    if (!shader) {
        res->rval = AVERROR(ENOMEM);
        return res;
    }

    shader->setEnvClient(EShClientVulkan, GLSL_VERSION);
    shader->setEnvTarget(EShTargetSpv, SPIRV_VERSION);
    shader->setStrings(&glsl, 1);
    if (!shader->parse(&DefaultTBuiltInResource, GLSL_VERSION, true, EShMsgDefault)) {
        res->error_msg = av_strdup(shader->getInfoLog());
        res->rval = AVERROR_EXTERNAL;
        delete shader;
        return res;
    }

    TProgram *prog = new TProgram();
    if (!prog) {
        res->rval = AVERROR(ENOMEM);
        delete shader;
        return res;
    }

    prog->addShader(shader);
    if (!prog->link(EShMsgDefault)) {
        res->error_msg = av_strdup(prog->getInfoLog());
        res->rval = AVERROR_EXTERNAL;
        delete shader;
        delete prog;
        return res;
    }

    std::vector<unsigned int> spirv; /* Result */

    SpvOptions options; /* Options - by default all optimizations are off */
    options.generateDebugInfo = false; /* Makes sense for files but not here */
    options.disassemble = false; /* Will print disassembly on compilation */
    options.validate = false; /* Validates the generated SPIRV, unneeded */
    options.disableOptimizer = false; /* For debugging */
    options.optimizeSize = true; /* Its faster */

    GlslangToSpv(*prog->getIntermediate(lang[stage]), spirv, NULL, &options);

    res->size = spirv.size()*sizeof(unsigned int);
    res->data = av_memdup(spirv.data(), res->size);
    if (!res->data) {
        res->rval = AVERROR(ENOMEM);
        delete shader;
        delete prog;
        return res;
    }

    delete shader;
    delete prog;

    return res;
}

int glslang_init(void)
{
    int ret = 0;

    pthread_mutex_lock(&glslang_mutex);
    if (glslang_refcount++ == 0)
        ret = !InitializeProcess();
    pthread_mutex_unlock(&glslang_mutex);

    return ret;
}

void glslang_uninit(void)
{
    pthread_mutex_lock(&glslang_mutex);
    if (glslang_refcount && (--glslang_refcount == 0))
        FinalizeProcess();
    pthread_mutex_unlock(&glslang_mutex);
}
