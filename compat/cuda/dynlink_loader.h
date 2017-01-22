/*
 * This copyright notice applies to this header file only:
 *
 * Copyright (c) 2016
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the software, and to permit persons to whom the
 * software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef AV_COMPAT_CUDA_DYNLINK_LOADER_H
#define AV_COMPAT_CUDA_DYNLINK_LOADER_H

#include "compat/cuda/dynlink_cuda.h"
#include "compat/cuda/dynlink_nvcuvid.h"
#include "compat/nvenc/nvEncodeAPI.h"
#include "compat/w32dlfcn.h"

#include "libavutil/log.h"
#include "libavutil/error.h"

#if defined(_WIN32)
# define LIB_HANDLE HMODULE
#else
# define LIB_HANDLE void*
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
# define CUDA_LIBNAME "nvcuda.dll"
# define NVCUVID_LIBNAME "nvcuvid.dll"
# if ARCH_X86_64
#  define NVENC_LIBNAME "nvEncodeAPI64.dll"
# else
#  define NVENC_LIBNAME "nvEncodeAPI.dll"
# endif
#else
# define CUDA_LIBNAME "libcuda.so.1"
# define NVCUVID_LIBNAME "libnvcuvid.so.1"
# define NVENC_LIBNAME "libnvidia-encode.so.1"
#endif

#define LOAD_LIBRARY(l, path)                                     \
    do {                                                          \
        if (!((l) = dlopen(path, RTLD_LAZY))) {                   \
            av_log(NULL, AV_LOG_ERROR, "Cannot load %s\n", path); \
            ret = AVERROR_UNKNOWN;                                \
            goto error;                                           \
        }                                                         \
        av_log(NULL, AV_LOG_TRACE, "Loaded lib: %s\n", path);     \
    } while (0)

#define LOAD_SYMBOL(fun, symbol)                                    \
    do {                                                            \
        if (!((f->fun) = dlsym(f->lib, symbol))) {                  \
            av_log(NULL, AV_LOG_ERROR, "Cannot load %s\n", symbol); \
            ret = AVERROR_UNKNOWN;                                  \
            goto error;                                             \
        }                                                           \
        av_log(NULL, AV_LOG_TRACE, "Loaded sym: %s\n", symbol);     \
    } while (0)

#define GENERIC_LOAD_FUNC_PREAMBLE(T, n, N)  \
    T *f;                                    \
    int ret;                                 \
                                             \
    n##_free_functions(functions);           \
                                             \
    f = *functions = av_mallocz(sizeof(*f)); \
    if (!f)                                  \
        return AVERROR(ENOMEM);              \
                                             \
    LOAD_LIBRARY(f->lib, N);

#define GENERIC_LOAD_FUNC_FINALE(n) \
    return 0;                       \
error:                              \
    n##_free_functions(functions);  \
    return ret;

#define GENERIC_FREE_FUNC()              \
    if (!functions)                      \
        return;                          \
    if (*functions && (*functions)->lib) \
        dlclose((*functions)->lib);      \
    av_freep(functions);

#ifdef AV_COMPAT_DYNLINK_CUDA_H
typedef struct CudaFunctions {
    tcuInit *cuInit;
    tcuDeviceGetCount *cuDeviceGetCount;
    tcuDeviceGet *cuDeviceGet;
    tcuDeviceGetName *cuDeviceGetName;
    tcuDeviceComputeCapability *cuDeviceComputeCapability;
    tcuCtxCreate_v2 *cuCtxCreate;
    tcuCtxPushCurrent_v2 *cuCtxPushCurrent;
    tcuCtxPopCurrent_v2 *cuCtxPopCurrent;
    tcuCtxDestroy_v2 *cuCtxDestroy;
    tcuMemAlloc_v2 *cuMemAlloc;
    tcuMemFree_v2 *cuMemFree;
    tcuMemcpy2D_v2 *cuMemcpy2D;
    tcuGetErrorName *cuGetErrorName;
    tcuGetErrorString *cuGetErrorString;

    LIB_HANDLE lib;
} CudaFunctions;
#else
typedef struct CudaFunctions CudaFunctions;
#endif

typedef struct CuvidFunctions {
    tcuvidCreateDecoder *cuvidCreateDecoder;
    tcuvidDestroyDecoder *cuvidDestroyDecoder;
    tcuvidDecodePicture *cuvidDecodePicture;
    tcuvidMapVideoFrame *cuvidMapVideoFrame;
    tcuvidUnmapVideoFrame *cuvidUnmapVideoFrame;
    tcuvidCtxLockCreate *cuvidCtxLockCreate;
    tcuvidCtxLockDestroy *cuvidCtxLockDestroy;
    tcuvidCtxLock *cuvidCtxLock;
    tcuvidCtxUnlock *cuvidCtxUnlock;

    tcuvidCreateVideoSource *cuvidCreateVideoSource;
    tcuvidCreateVideoSourceW *cuvidCreateVideoSourceW;
    tcuvidDestroyVideoSource *cuvidDestroyVideoSource;
    tcuvidSetVideoSourceState *cuvidSetVideoSourceState;
    tcuvidGetVideoSourceState *cuvidGetVideoSourceState;
    tcuvidGetSourceVideoFormat *cuvidGetSourceVideoFormat;
    tcuvidGetSourceAudioFormat *cuvidGetSourceAudioFormat;
    tcuvidCreateVideoParser *cuvidCreateVideoParser;
    tcuvidParseVideoData *cuvidParseVideoData;
    tcuvidDestroyVideoParser *cuvidDestroyVideoParser;

    LIB_HANDLE lib;
} CuvidFunctions;

typedef struct NvencFunctions {
    NVENCSTATUS (NVENCAPI *NvEncodeAPICreateInstance)(NV_ENCODE_API_FUNCTION_LIST *functionList);
    NVENCSTATUS (NVENCAPI *NvEncodeAPIGetMaxSupportedVersion)(uint32_t* version);

    LIB_HANDLE lib;
} NvencFunctions;

#ifdef AV_COMPAT_DYNLINK_CUDA_H
static inline void cuda_free_functions(CudaFunctions **functions)
{
    GENERIC_FREE_FUNC();
}
#endif

static inline void cuvid_free_functions(CuvidFunctions **functions)
{
    GENERIC_FREE_FUNC();
}

static inline void nvenc_free_functions(NvencFunctions **functions)
{
    GENERIC_FREE_FUNC();
}

#ifdef AV_COMPAT_DYNLINK_CUDA_H
static inline int cuda_load_functions(CudaFunctions **functions)
{
    GENERIC_LOAD_FUNC_PREAMBLE(CudaFunctions, cuda, CUDA_LIBNAME);

    LOAD_SYMBOL(cuInit, "cuInit");
    LOAD_SYMBOL(cuDeviceGetCount, "cuDeviceGetCount");
    LOAD_SYMBOL(cuDeviceGet, "cuDeviceGet");
    LOAD_SYMBOL(cuDeviceGetName, "cuDeviceGetName");
    LOAD_SYMBOL(cuDeviceComputeCapability, "cuDeviceComputeCapability");
    LOAD_SYMBOL(cuCtxCreate, "cuCtxCreate_v2");
    LOAD_SYMBOL(cuCtxPushCurrent, "cuCtxPushCurrent_v2");
    LOAD_SYMBOL(cuCtxPopCurrent, "cuCtxPopCurrent_v2");
    LOAD_SYMBOL(cuCtxDestroy, "cuCtxDestroy_v2");
    LOAD_SYMBOL(cuMemAlloc, "cuMemAlloc_v2");
    LOAD_SYMBOL(cuMemFree, "cuMemFree_v2");
    LOAD_SYMBOL(cuMemcpy2D, "cuMemcpy2D_v2");
    LOAD_SYMBOL(cuGetErrorName, "cuGetErrorName");
    LOAD_SYMBOL(cuGetErrorString, "cuGetErrorString");

    GENERIC_LOAD_FUNC_FINALE(cuda);
}
#endif

static inline int cuvid_load_functions(CuvidFunctions **functions)
{
    GENERIC_LOAD_FUNC_PREAMBLE(CuvidFunctions, cuvid, NVCUVID_LIBNAME);

    LOAD_SYMBOL(cuvidCreateDecoder, "cuvidCreateDecoder");
    LOAD_SYMBOL(cuvidDestroyDecoder, "cuvidDestroyDecoder");
    LOAD_SYMBOL(cuvidDecodePicture, "cuvidDecodePicture");
#ifdef __CUVID_DEVPTR64
    LOAD_SYMBOL(cuvidMapVideoFrame, "cuvidMapVideoFrame64");
    LOAD_SYMBOL(cuvidUnmapVideoFrame, "cuvidUnmapVideoFrame64");
#else
    LOAD_SYMBOL(cuvidMapVideoFrame, "cuvidMapVideoFrame");
    LOAD_SYMBOL(cuvidUnmapVideoFrame, "cuvidUnmapVideoFrame");
#endif
    LOAD_SYMBOL(cuvidCtxLockCreate, "cuvidCtxLockCreate");
    LOAD_SYMBOL(cuvidCtxLockDestroy, "cuvidCtxLockDestroy");
    LOAD_SYMBOL(cuvidCtxLock, "cuvidCtxLock");
    LOAD_SYMBOL(cuvidCtxUnlock, "cuvidCtxUnlock");

    LOAD_SYMBOL(cuvidCreateVideoSource, "cuvidCreateVideoSource");
    LOAD_SYMBOL(cuvidCreateVideoSourceW, "cuvidCreateVideoSourceW");
    LOAD_SYMBOL(cuvidDestroyVideoSource, "cuvidDestroyVideoSource");
    LOAD_SYMBOL(cuvidSetVideoSourceState, "cuvidSetVideoSourceState");
    LOAD_SYMBOL(cuvidGetVideoSourceState, "cuvidGetVideoSourceState");
    LOAD_SYMBOL(cuvidGetSourceVideoFormat, "cuvidGetSourceVideoFormat");
    LOAD_SYMBOL(cuvidGetSourceAudioFormat, "cuvidGetSourceAudioFormat");
    LOAD_SYMBOL(cuvidCreateVideoParser, "cuvidCreateVideoParser");
    LOAD_SYMBOL(cuvidParseVideoData, "cuvidParseVideoData");
    LOAD_SYMBOL(cuvidDestroyVideoParser, "cuvidDestroyVideoParser");

    GENERIC_LOAD_FUNC_FINALE(cuvid);
}

static inline int nvenc_load_functions(NvencFunctions **functions)
{
    GENERIC_LOAD_FUNC_PREAMBLE(NvencFunctions, nvenc, NVENC_LIBNAME);

    LOAD_SYMBOL(NvEncodeAPICreateInstance, "NvEncodeAPICreateInstance");
    LOAD_SYMBOL(NvEncodeAPIGetMaxSupportedVersion, "NvEncodeAPIGetMaxSupportedVersion");

    GENERIC_LOAD_FUNC_FINALE(nvenc);
}

#undef GENERIC_LOAD_FUNC_PREAMBLE
#undef LOAD_LIBRARY
#undef LOAD_SYMBOL
#undef GENERIC_LOAD_FUNC_FINALE
#undef GENERIC_FREE_FUNC
#undef CUDA_LIBNAME
#undef NVCUVID_LIBNAME
#undef NVENC_LIBNAME
#undef LIB_HANDLE

#endif

