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

#define COBJMACROS
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif

#include "mf_utils.h"
#include "libavutil/pixdesc.h"

HRESULT ff_MFGetAttributeSize(IMFAttributes *pattr, REFGUID guid,
                              UINT32 *pw, UINT32 *ph)
{
    UINT64 t;
    HRESULT hr = IMFAttributes_GetUINT64(pattr, guid, &t);
    if (!FAILED(hr)) {
        *pw = t >> 32;
        *ph = (UINT32)t;
    }
    return hr;
}

HRESULT ff_MFSetAttributeSize(IMFAttributes *pattr, REFGUID guid,
                              UINT32 uw, UINT32 uh)
{
    UINT64 t = (((UINT64)uw) << 32) | uh;
    return IMFAttributes_SetUINT64(pattr, guid, t);
}

#define ff_MFSetAttributeRatio ff_MFSetAttributeSize
#define ff_MFGetAttributeRatio ff_MFGetAttributeSize

char *ff_hr_str_buf(char *buf, size_t size, HRESULT hr)
{
#define HR(x) case x: return (char *) # x;
    switch (hr) {
    HR(S_OK)
    HR(E_UNEXPECTED)
    HR(MF_E_INVALIDMEDIATYPE)
    HR(MF_E_INVALIDSTREAMNUMBER)
    HR(MF_E_INVALIDTYPE)
    HR(MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING)
    HR(MF_E_TRANSFORM_TYPE_NOT_SET)
    HR(MF_E_UNSUPPORTED_D3D_TYPE)
    HR(MF_E_TRANSFORM_NEED_MORE_INPUT)
    HR(MF_E_TRANSFORM_STREAM_CHANGE)
    HR(MF_E_NOTACCEPTING)
    HR(MF_E_NO_SAMPLE_TIMESTAMP)
    HR(MF_E_NO_SAMPLE_DURATION)
#undef HR
    }
    snprintf(buf, size, "%x", (unsigned)hr);
    return buf;
}

// If fill_data!=NULL, initialize the buffer and set the length. (This is a
// subtle but important difference: some decoders want CurrentLength==0 on
// provided output buffers.)
IMFSample *ff_create_memory_sample(MFFunctions *f,void *fill_data, size_t size,
                                   size_t align)
{
    HRESULT hr;
    IMFSample *sample;
    IMFMediaBuffer *buffer;

    hr = f->MFCreateSample(&sample);
    if (FAILED(hr))
        return NULL;

    align = FFMAX(align, 16); // 16 is "recommended", even if not required

    hr = f->MFCreateAlignedMemoryBuffer(size, align - 1, &buffer);
    if (FAILED(hr))
        return NULL;

    if (fill_data) {
        BYTE *tmp;

        hr = IMFMediaBuffer_Lock(buffer, &tmp, NULL, NULL);
        if (FAILED(hr)) {
            IMFMediaBuffer_Release(buffer);
            IMFSample_Release(sample);
            return NULL;
        }
        memcpy(tmp, fill_data, size);

        IMFMediaBuffer_SetCurrentLength(buffer, size);
        IMFMediaBuffer_Unlock(buffer);
    }

    IMFSample_AddBuffer(sample, buffer);
    IMFMediaBuffer_Release(buffer);

    return sample;
}

enum AVSampleFormat ff_media_type_to_sample_fmt(IMFAttributes *type)
{
    HRESULT hr;
    UINT32 bits;
    GUID subtype;

    hr = IMFAttributes_GetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
    if (FAILED(hr))
        return AV_SAMPLE_FMT_NONE;

    hr = IMFAttributes_GetGUID(type, &MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr))
        return AV_SAMPLE_FMT_NONE;

    if (IsEqualGUID(&subtype, &MFAudioFormat_PCM)) {
        switch (bits) {
        case 8:  return AV_SAMPLE_FMT_U8;
        case 16: return AV_SAMPLE_FMT_S16;
        case 32: return AV_SAMPLE_FMT_S32;
        }
    } else if (IsEqualGUID(&subtype, &MFAudioFormat_Float)) {
        switch (bits) {
        case 32: return AV_SAMPLE_FMT_FLT;
        case 64: return AV_SAMPLE_FMT_DBL;
        }
    }

    return AV_SAMPLE_FMT_NONE;
}

struct mf_pix_fmt_entry {
    const GUID *guid;
    enum AVPixelFormat pix_fmt;
};

static const struct mf_pix_fmt_entry mf_pix_fmts[] = {
    {&MFVideoFormat_IYUV, AV_PIX_FMT_YUV420P},
    {&MFVideoFormat_I420, AV_PIX_FMT_YUV420P},
    {&MFVideoFormat_NV12, AV_PIX_FMT_NV12},
    {&MFVideoFormat_P010, AV_PIX_FMT_P010},
    {&MFVideoFormat_P016, AV_PIX_FMT_P010}, // not equal, but compatible
    {&MFVideoFormat_YUY2, AV_PIX_FMT_YUYV422},
};

enum AVPixelFormat ff_media_type_to_pix_fmt(IMFAttributes *type)
{
    HRESULT hr;
    GUID subtype;
    int i;

    hr = IMFAttributes_GetGUID(type, &MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr))
        return AV_PIX_FMT_NONE;

    for (i = 0; i < FF_ARRAY_ELEMS(mf_pix_fmts); i++) {
        if (IsEqualGUID(&subtype, mf_pix_fmts[i].guid))
            return mf_pix_fmts[i].pix_fmt;
    }

    return AV_PIX_FMT_NONE;
}

const GUID *ff_pix_fmt_to_guid(enum AVPixelFormat pix_fmt)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(mf_pix_fmts); i++) {
        if (mf_pix_fmts[i].pix_fmt == pix_fmt)
            return mf_pix_fmts[i].guid;
    }

    return NULL;
}

// If this GUID is of the form XXXXXXXX-0000-0010-8000-00AA00389B71, then
// extract the XXXXXXXX prefix as FourCC (oh the pain).
int ff_fourcc_from_guid(const GUID *guid, uint32_t *out_fourcc)
{
    if (guid->Data2 == 0 && guid->Data3 == 0x0010 &&
        guid->Data4[0] == 0x80 &&
        guid->Data4[1] == 0x00 &&
        guid->Data4[2] == 0x00 &&
        guid->Data4[3] == 0xAA &&
        guid->Data4[4] == 0x00 &&
        guid->Data4[5] == 0x38 &&
        guid->Data4[6] == 0x9B &&
        guid->Data4[7] == 0x71) {
        *out_fourcc = guid->Data1;
        return 0;
    }

    *out_fourcc = 0;
    return AVERROR_UNKNOWN;
}

struct GUID_Entry {
    const GUID *guid;
    const char *name;
};

#define GUID_ENTRY(var) {&(var), # var}

static struct GUID_Entry guid_names[] = {
    GUID_ENTRY(MFT_FRIENDLY_NAME_Attribute),
    GUID_ENTRY(MFT_TRANSFORM_CLSID_Attribute),
    GUID_ENTRY(MFT_ENUM_HARDWARE_URL_Attribute),
    GUID_ENTRY(MFT_CONNECTED_STREAM_ATTRIBUTE),
    GUID_ENTRY(MFT_CONNECTED_TO_HW_STREAM),
    GUID_ENTRY(MF_SA_D3D_AWARE),
    GUID_ENTRY(ff_MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT),
    GUID_ENTRY(ff_MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT_PROGRESSIVE),
    GUID_ENTRY(ff_MF_SA_D3D11_BINDFLAGS),
    GUID_ENTRY(ff_MF_SA_D3D11_USAGE),
    GUID_ENTRY(ff_MF_SA_D3D11_AWARE),
    GUID_ENTRY(ff_MF_SA_D3D11_SHARED),
    GUID_ENTRY(ff_MF_SA_D3D11_SHARED_WITHOUT_MUTEX),
    GUID_ENTRY(MF_MT_SUBTYPE),
    GUID_ENTRY(MF_MT_MAJOR_TYPE),
    GUID_ENTRY(MF_MT_AUDIO_SAMPLES_PER_SECOND),
    GUID_ENTRY(MF_MT_AUDIO_NUM_CHANNELS),
    GUID_ENTRY(MF_MT_AUDIO_CHANNEL_MASK),
    GUID_ENTRY(MF_MT_FRAME_SIZE),
    GUID_ENTRY(MF_MT_INTERLACE_MODE),
    GUID_ENTRY(MF_MT_USER_DATA),
    GUID_ENTRY(MF_MT_PIXEL_ASPECT_RATIO),
    GUID_ENTRY(MFMediaType_Audio),
    GUID_ENTRY(MFMediaType_Video),
    GUID_ENTRY(MFAudioFormat_PCM),
    GUID_ENTRY(MFAudioFormat_Float),
    GUID_ENTRY(ff_MFVideoFormat_AV1),
    GUID_ENTRY(MFVideoFormat_H264),
    GUID_ENTRY(MFVideoFormat_H264_ES),
    GUID_ENTRY(ff_MFVideoFormat_HEVC),
    GUID_ENTRY(ff_MFVideoFormat_HEVC_ES),
    GUID_ENTRY(MFVideoFormat_MPEG2),
    GUID_ENTRY(MFVideoFormat_MP43),
    GUID_ENTRY(MFVideoFormat_MP4V),
    GUID_ENTRY(MFVideoFormat_WMV1),
    GUID_ENTRY(MFVideoFormat_WMV2),
    GUID_ENTRY(MFVideoFormat_WMV3),
    GUID_ENTRY(MFVideoFormat_WVC1),
    GUID_ENTRY(MFAudioFormat_Dolby_AC3),
    GUID_ENTRY(MFAudioFormat_Dolby_DDPlus),
    GUID_ENTRY(MFAudioFormat_AAC),
    GUID_ENTRY(MFAudioFormat_MP3),
    GUID_ENTRY(MFAudioFormat_MSP1),
    GUID_ENTRY(MFAudioFormat_WMAudioV8),
    GUID_ENTRY(MFAudioFormat_WMAudioV9),
    GUID_ENTRY(MFAudioFormat_WMAudio_Lossless),
    GUID_ENTRY(MF_MT_ALL_SAMPLES_INDEPENDENT),
    GUID_ENTRY(MF_MT_COMPRESSED),
    GUID_ENTRY(MF_MT_FIXED_SIZE_SAMPLES),
    GUID_ENTRY(MF_MT_SAMPLE_SIZE),
    GUID_ENTRY(MF_MT_WRAPPED_TYPE),
    GUID_ENTRY(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION),
    GUID_ENTRY(MF_MT_AAC_PAYLOAD_TYPE),
    GUID_ENTRY(MF_MT_AUDIO_AVG_BYTES_PER_SECOND),
    GUID_ENTRY(MF_MT_AUDIO_BITS_PER_SAMPLE),
    GUID_ENTRY(MF_MT_AUDIO_BLOCK_ALIGNMENT),
    GUID_ENTRY(MF_MT_AUDIO_CHANNEL_MASK),
    GUID_ENTRY(MF_MT_AUDIO_FLOAT_SAMPLES_PER_SECOND),
    GUID_ENTRY(MF_MT_AUDIO_FOLDDOWN_MATRIX),
    GUID_ENTRY(MF_MT_AUDIO_NUM_CHANNELS),
    GUID_ENTRY(MF_MT_AUDIO_PREFER_WAVEFORMATEX),
    GUID_ENTRY(MF_MT_AUDIO_SAMPLES_PER_BLOCK),
    GUID_ENTRY(MF_MT_AUDIO_SAMPLES_PER_SECOND),
    GUID_ENTRY(MF_MT_AUDIO_VALID_BITS_PER_SAMPLE),
    GUID_ENTRY(MF_MT_AUDIO_WMADRC_AVGREF),
    GUID_ENTRY(MF_MT_AUDIO_WMADRC_AVGTARGET),
    GUID_ENTRY(MF_MT_AUDIO_WMADRC_PEAKREF),
    GUID_ENTRY(MF_MT_AUDIO_WMADRC_PEAKTARGET),
    GUID_ENTRY(MF_MT_AVG_BIT_ERROR_RATE),
    GUID_ENTRY(MF_MT_AVG_BITRATE),
    GUID_ENTRY(MF_MT_DEFAULT_STRIDE),
    GUID_ENTRY(MF_MT_DRM_FLAGS),
    GUID_ENTRY(MF_MT_FRAME_RATE),
    GUID_ENTRY(MF_MT_FRAME_RATE_RANGE_MAX),
    GUID_ENTRY(MF_MT_FRAME_RATE_RANGE_MIN),
    GUID_ENTRY(MF_MT_FRAME_SIZE),
    GUID_ENTRY(MF_MT_GEOMETRIC_APERTURE),
    GUID_ENTRY(MF_MT_INTERLACE_MODE),
    GUID_ENTRY(MF_MT_MAX_KEYFRAME_SPACING),
    GUID_ENTRY(MF_MT_MINIMUM_DISPLAY_APERTURE),
    GUID_ENTRY(MF_MT_MPEG_SEQUENCE_HEADER),
    GUID_ENTRY(MF_MT_MPEG_START_TIME_CODE),
    GUID_ENTRY(MF_MT_MPEG2_FLAGS),
    GUID_ENTRY(MF_MT_MPEG2_LEVEL),
    GUID_ENTRY(MF_MT_MPEG2_PROFILE),
    GUID_ENTRY(MF_MT_PAD_CONTROL_FLAGS),
    GUID_ENTRY(MF_MT_PALETTE),
    GUID_ENTRY(MF_MT_PAN_SCAN_APERTURE),
    GUID_ENTRY(MF_MT_PAN_SCAN_ENABLED),
    GUID_ENTRY(MF_MT_PIXEL_ASPECT_RATIO),
    GUID_ENTRY(MF_MT_SOURCE_CONTENT_HINT),
    GUID_ENTRY(MF_MT_TRANSFER_FUNCTION),
    GUID_ENTRY(MF_MT_VIDEO_CHROMA_SITING),
    GUID_ENTRY(MF_MT_VIDEO_LIGHTING),
    GUID_ENTRY(MF_MT_VIDEO_NOMINAL_RANGE),
    GUID_ENTRY(MF_MT_VIDEO_PRIMARIES),
    GUID_ENTRY(MF_MT_VIDEO_ROTATION),
    GUID_ENTRY(MF_MT_YUV_MATRIX),
    GUID_ENTRY(ff_CODECAPI_AVDecVideoThumbnailGenerationMode),
    GUID_ENTRY(ff_CODECAPI_AVDecVideoDropPicWithMissingRef),
    GUID_ENTRY(ff_CODECAPI_AVDecVideoSoftwareDeinterlaceMode),
    GUID_ENTRY(ff_CODECAPI_AVDecVideoFastDecodeMode),
    GUID_ENTRY(ff_CODECAPI_AVLowLatencyMode),
    GUID_ENTRY(ff_CODECAPI_AVDecVideoH264ErrorConcealment),
    GUID_ENTRY(ff_CODECAPI_AVDecVideoMPEG2ErrorConcealment),
    GUID_ENTRY(ff_CODECAPI_AVDecVideoCodecType),
    GUID_ENTRY(ff_CODECAPI_AVDecVideoDXVAMode),
    GUID_ENTRY(ff_CODECAPI_AVDecVideoDXVABusEncryption),
    GUID_ENTRY(ff_CODECAPI_AVDecVideoSWPowerLevel),
    GUID_ENTRY(ff_CODECAPI_AVDecVideoMaxCodedWidth),
    GUID_ENTRY(ff_CODECAPI_AVDecVideoMaxCodedHeight),
    GUID_ENTRY(ff_CODECAPI_AVDecNumWorkerThreads),
    GUID_ENTRY(ff_CODECAPI_AVDecSoftwareDynamicFormatChange),
    GUID_ENTRY(ff_CODECAPI_AVDecDisableVideoPostProcessing),
};

char *ff_guid_str_buf(char *buf, size_t buf_size, const GUID *guid)
{
    uint32_t fourcc;
    int n;
    for (n = 0; n < FF_ARRAY_ELEMS(guid_names); n++) {
        if (IsEqualGUID(guid, guid_names[n].guid)) {
            snprintf(buf, buf_size, "%s", guid_names[n].name);
            return buf;
        }
    }

    if (ff_fourcc_from_guid(guid, &fourcc) >= 0) {
        snprintf(buf, buf_size, "<FourCC %s>", av_fourcc2str(fourcc));
        return buf;
    }

    snprintf(buf, buf_size,
             "{%8.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x}",
             (unsigned) guid->Data1, guid->Data2, guid->Data3,
             guid->Data4[0], guid->Data4[1],
             guid->Data4[2], guid->Data4[3],
             guid->Data4[4], guid->Data4[5],
             guid->Data4[6], guid->Data4[7]);
    return buf;
}

void ff_attributes_dump(void *log, IMFAttributes *attrs)
{
    HRESULT hr;
    UINT32 count;
    int n;

    hr = IMFAttributes_GetCount(attrs, &count);
    if (FAILED(hr))
        return;

    for (n = 0; n < count; n++) {
        GUID key;
        MF_ATTRIBUTE_TYPE type;
        char extra[80] = {0};
        const char *name = NULL;

        hr = IMFAttributes_GetItemByIndex(attrs, n, &key, NULL);
        if (FAILED(hr))
            goto err;

        name = ff_guid_str(&key);

        if (IsEqualGUID(&key, &MF_MT_AUDIO_CHANNEL_MASK)) {
            UINT32 v;
            hr = IMFAttributes_GetUINT32(attrs, &key, &v);
            if (FAILED(hr))
                goto err;
            snprintf(extra, sizeof(extra), " (0x%x)", (unsigned)v);
        } else if (IsEqualGUID(&key, &MF_MT_FRAME_SIZE)) {
            UINT32 w, h;

            hr = ff_MFGetAttributeSize(attrs, &MF_MT_FRAME_SIZE, &w, &h);
            if (FAILED(hr))
                goto err;
            snprintf(extra, sizeof(extra), " (%dx%d)", (int)w, (int)h);
        } else if (IsEqualGUID(&key, &MF_MT_PIXEL_ASPECT_RATIO) ||
                   IsEqualGUID(&key, &MF_MT_FRAME_RATE)) {
            UINT32 num, den;

            hr = ff_MFGetAttributeRatio(attrs, &key, &num, &den);
            if (FAILED(hr))
                goto err;
            snprintf(extra, sizeof(extra), " (%d:%d)", (int)num, (int)den);
        }

        hr = IMFAttributes_GetItemType(attrs, &key, &type);
        if (FAILED(hr))
            goto err;

        switch (type) {
        case MF_ATTRIBUTE_UINT32: {
            UINT32 v;
            hr = IMFAttributes_GetUINT32(attrs, &key, &v);
            if (FAILED(hr))
                goto err;
            av_log(log, AV_LOG_VERBOSE, "   %s=%d%s\n", name, (int)v, extra);
            break;
        case MF_ATTRIBUTE_UINT64: {
            UINT64 v;
            hr = IMFAttributes_GetUINT64(attrs, &key, &v);
            if (FAILED(hr))
                goto err;
            av_log(log, AV_LOG_VERBOSE, "   %s=%lld%s\n", name, (long long)v, extra);
            break;
        }
        case MF_ATTRIBUTE_DOUBLE: {
            DOUBLE v;
            hr = IMFAttributes_GetDouble(attrs, &key, &v);
            if (FAILED(hr))
                goto err;
            av_log(log, AV_LOG_VERBOSE, "   %s=%f%s\n", name, (double)v, extra);
            break;
        }
        case MF_ATTRIBUTE_STRING: {
            wchar_t s[512]; // being lazy here
            hr = IMFAttributes_GetString(attrs, &key, s, sizeof(s), NULL);
            if (FAILED(hr))
                goto err;
            av_log(log, AV_LOG_VERBOSE, "   %s='%ls'%s\n", name, s, extra);
            break;
        }
        case MF_ATTRIBUTE_GUID: {
            GUID v;
            hr = IMFAttributes_GetGUID(attrs, &key, &v);
            if (FAILED(hr))
                goto err;
            av_log(log, AV_LOG_VERBOSE, "   %s=%s%s\n", name, ff_guid_str(&v), extra);
            break;
        }
        case MF_ATTRIBUTE_BLOB: {
            UINT32 sz;
            UINT8 buffer[100];
            hr = IMFAttributes_GetBlobSize(attrs, &key, &sz);
            if (FAILED(hr))
                goto err;
            if (sz <= sizeof(buffer)) {
                // hex-dump it
                char str[512] = {0};
                size_t pos = 0;
                hr = IMFAttributes_GetBlob(attrs, &key, buffer, sizeof(buffer), &sz);
                if (FAILED(hr))
                    goto err;
                for (pos = 0; pos < sz; pos++) {
                    const char *hex = "0123456789ABCDEF";
                    if (pos * 3 + 3 > sizeof(str))
                        break;
                    str[pos * 3 + 0] = hex[buffer[pos] >> 4];
                    str[pos * 3 + 1] = hex[buffer[pos] & 15];
                    str[pos * 3 + 2] = ' ';
                }
                str[pos * 3 + 0] = 0;
                av_log(log, AV_LOG_VERBOSE, "   %s=<blob size %d: %s>%s\n", name, (int)sz, str, extra);
            } else {
                av_log(log, AV_LOG_VERBOSE, "   %s=<blob size %d>%s\n", name, (int)sz, extra);
            }
            break;
        }
        case MF_ATTRIBUTE_IUNKNOWN: {
            av_log(log, AV_LOG_VERBOSE, "   %s=<IUnknown>%s\n", name, extra);
            break;
        }
        default:
            av_log(log, AV_LOG_VERBOSE, "   %s=<unknown type>%s\n", name, extra);
            break;
        }
        }

        if (IsEqualGUID(&key, &MF_MT_SUBTYPE)) {
            const char *fmt;
            fmt = av_get_sample_fmt_name(ff_media_type_to_sample_fmt(attrs));
            if (fmt)
                av_log(log, AV_LOG_VERBOSE, "   FF-sample-format=%s\n", fmt);

            fmt = av_get_pix_fmt_name(ff_media_type_to_pix_fmt(attrs));
            if (fmt)
                av_log(log, AV_LOG_VERBOSE, "   FF-pixel-format=%s\n", fmt);
        }

        continue;
    err:
        av_log(log, AV_LOG_VERBOSE, "   %s=<failed to get value>\n", name ? name : "?");
    }
}

void ff_media_type_dump(void *log, IMFMediaType *type)
{
    ff_attributes_dump(log, (IMFAttributes *)type);
}

const CLSID *ff_codec_to_mf_subtype(enum AVCodecID codec)
{
    switch (codec) {
    case AV_CODEC_ID_AV1:               return &ff_MFVideoFormat_AV1;
    case AV_CODEC_ID_H264:              return &MFVideoFormat_H264;
    case AV_CODEC_ID_HEVC:              return &ff_MFVideoFormat_HEVC;
    case AV_CODEC_ID_AC3:               return &MFAudioFormat_Dolby_AC3;
    case AV_CODEC_ID_AAC:               return &MFAudioFormat_AAC;
    case AV_CODEC_ID_MP3:               return &MFAudioFormat_MP3;
    default:                            return NULL;
    }
}

static int init_com_mf(void *log, MFFunctions *f)
{
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        av_log(log, AV_LOG_ERROR, "COM must not be in STA mode\n");
        return AVERROR(EINVAL);
    } else if (FAILED(hr)) {
        av_log(log, AV_LOG_ERROR, "could not initialize COM\n");
        return AVERROR(ENOSYS);
    }

    hr = f->MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
        av_log(log, AV_LOG_ERROR, "could not initialize MediaFoundation\n");
        CoUninitialize();
        return AVERROR(ENOSYS);
    }

    return 0;
}

static void uninit_com_mf(MFFunctions *f)
{
    f->MFShutdown();
    CoUninitialize();
}

// Find and create a IMFTransform with the given input/output types. When done,
// you should use ff_free_mf() to destroy it, which will also uninit COM.
int ff_instantiate_mf(void *log,
                      MFFunctions *f,
                      GUID category,
                      MFT_REGISTER_TYPE_INFO *in_type,
                      MFT_REGISTER_TYPE_INFO *out_type,
                      int use_hw,
                      IMFTransform **res)
{
    HRESULT hr;
    int n;
    int ret;
    IMFActivate **activate;
    UINT32 num_activate;
    IMFActivate *winner = 0;
    UINT32 flags;

    ret = init_com_mf(log, f);
    if (ret < 0)
        return ret;

    flags = MFT_ENUM_FLAG_SORTANDFILTER;

    if (use_hw) {
        flags |= MFT_ENUM_FLAG_HARDWARE;
    } else {
        flags |= MFT_ENUM_FLAG_SYNCMFT;
    }

    hr = f->MFTEnumEx(category, flags, in_type, out_type, &activate,
                      &num_activate);
    if (FAILED(hr))
        goto error_uninit_mf;

    if (log) {
        if (!num_activate)
            av_log(log, AV_LOG_ERROR, "could not find any MFT for the given media type\n");

        for (n = 0; n < num_activate; n++) {
            av_log(log, AV_LOG_VERBOSE, "MF %d attributes:\n", n);
            ff_attributes_dump(log, (IMFAttributes *)activate[n]);
        }
    }

    *res = NULL;
    for (n = 0; n < num_activate; n++) {
        if (log)
            av_log(log, AV_LOG_VERBOSE, "activate MFT %d\n", n);
        hr = IMFActivate_ActivateObject(activate[n], &IID_IMFTransform,
                                        (void **)res);
        if (*res) {
            winner = activate[n];
            IMFActivate_AddRef(winner);
            break;
        }
    }

    for (n = 0; n < num_activate; n++)
       IMFActivate_Release(activate[n]);
    CoTaskMemFree(activate);

    if (!*res) {
        if (log)
            av_log(log, AV_LOG_ERROR, "could not create MFT\n");
        goto error_uninit_mf;
    }

    if (log) {
        wchar_t s[512]; // being lazy here
        IMFAttributes *attrs;
        hr = IMFTransform_GetAttributes(*res, &attrs);
        if (!FAILED(hr) && attrs) {

            av_log(log, AV_LOG_VERBOSE, "MFT attributes\n");
            ff_attributes_dump(log, attrs);
            IMFAttributes_Release(attrs);
        }

        hr = IMFActivate_GetString(winner, &MFT_FRIENDLY_NAME_Attribute, s,
                                   sizeof(s), NULL);
        if (!FAILED(hr))
            av_log(log, AV_LOG_INFO, "MFT name: '%ls'\n", s);

    }

    IMFActivate_Release(winner);

    return 0;

error_uninit_mf:
    uninit_com_mf(f);
    return AVERROR(ENOSYS);
}

void ff_free_mf(MFFunctions *f, IMFTransform **mft)
{
    if (*mft)
        IMFTransform_Release(*mft);
    *mft = NULL;
    uninit_com_mf(f);
}
