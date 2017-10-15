/*
 * Blackmagic DeckLink output
 * Copyright (c) 2013-2014 Ramiro Polla, Luca Barbato, Deti Fliegl
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

/* Include internal.h first to avoid conflict between winsock.h (used by
 * DeckLink headers) and winsock2.h (used by libavformat) in MSVC++ builds */
extern "C" {
#include "libavformat/internal.h"
}

#include <DeckLinkAPI.h>
#ifdef _WIN32
#include <DeckLinkAPI_i.c>
#else
#include <DeckLinkAPIDispatch.cpp>
#endif

extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/bswap.h"
#include "avdevice.h"
}

#include "decklink_common.h"

#ifdef _WIN32
IDeckLinkIterator *CreateDeckLinkIteratorInstance(void)
{
    IDeckLinkIterator *iter;

    if (CoInitialize(NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "COM initialization failed.\n");
        return NULL;
    }

    if (CoCreateInstance(CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL,
                         IID_IDeckLinkIterator, (void**) &iter) != S_OK) {
        av_log(NULL, AV_LOG_ERROR, "DeckLink drivers not installed.\n");
        return NULL;
    }

    return iter;
}
#endif

#ifdef _WIN32
static char *dup_wchar_to_utf8(wchar_t *w)
{
    char *s = NULL;
    int l = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    s = (char *) av_malloc(l);
    if (s)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s, l, 0, 0);
    return s;
}
#define DECKLINK_STR    OLECHAR *
#define DECKLINK_STRDUP dup_wchar_to_utf8
#define DECKLINK_FREE(s) SysFreeString(s)
#define DECKLINK_BOOL BOOL
#elif defined(__APPLE__)
static char *dup_cfstring_to_utf8(CFStringRef w)
{
    char s[256];
    CFStringGetCString(w, s, 255, kCFStringEncodingUTF8);
    return av_strdup(s);
}
#define DECKLINK_STR    const __CFString *
#define DECKLINK_STRDUP dup_cfstring_to_utf8
#define DECKLINK_FREE(s) CFRelease(s)
#define DECKLINK_BOOL bool
#else
#define DECKLINK_STR    const char *
#define DECKLINK_STRDUP av_strdup
/* free() is needed for a string returned by the DeckLink SDL. */
#define DECKLINK_FREE(s) free((void *) s)
#define DECKLINK_BOOL bool
#endif

HRESULT ff_decklink_get_display_name(IDeckLink *This, const char **displayName)
{
    DECKLINK_STR tmpDisplayName;
    HRESULT hr = This->GetDisplayName(&tmpDisplayName);
    if (hr != S_OK)
        return hr;
    *displayName = DECKLINK_STRDUP(tmpDisplayName);
    DECKLINK_FREE(tmpDisplayName);
    return hr;
}

static int decklink_select_input(AVFormatContext *avctx, BMDDeckLinkConfigurationID cfg_id)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *)avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *)cctx->ctx;
    BMDDeckLinkAttributeID attr_id = (cfg_id == bmdDeckLinkConfigAudioInputConnection) ? BMDDeckLinkAudioInputConnections : BMDDeckLinkVideoInputConnections;
    int64_t bmd_input              = (cfg_id == bmdDeckLinkConfigAudioInputConnection) ? (int64_t)ctx->audio_input : (int64_t)ctx->video_input;
    const char *type_name          = (cfg_id == bmdDeckLinkConfigAudioInputConnection) ? "audio" : "video";
    int64_t supported_connections = 0;
    HRESULT res;

    if (bmd_input) {
        res = ctx->attr->GetInt(attr_id, &supported_connections);
        if (res != S_OK) {
            av_log(avctx, AV_LOG_ERROR, "Failed to query supported %s inputs.\n", type_name);
            return AVERROR_EXTERNAL;
        }
        if ((supported_connections & bmd_input) != bmd_input) {
            av_log(avctx, AV_LOG_ERROR, "Device does not support selected %s input.\n", type_name);
            return AVERROR(ENOSYS);
        }
        res = ctx->cfg->SetInt(cfg_id, bmd_input);
        if (res != S_OK) {
            av_log(avctx, AV_LOG_ERROR, "Failed to select %s input.\n", type_name);
            return AVERROR_EXTERNAL;
        }
    }
    return 0;
}

static DECKLINK_BOOL field_order_eq(enum AVFieldOrder field_order, BMDFieldDominance bmd_field_order)
{
    if (field_order == AV_FIELD_UNKNOWN)
        return true;
    if ((field_order == AV_FIELD_TT || field_order == AV_FIELD_TB) && bmd_field_order == bmdUpperFieldFirst)
        return true;
    if ((field_order == AV_FIELD_BB || field_order == AV_FIELD_BT) && bmd_field_order == bmdLowerFieldFirst)
        return true;
    if (field_order == AV_FIELD_PROGRESSIVE && (bmd_field_order == bmdProgressiveFrame || bmd_field_order == bmdProgressiveSegmentedFrame))
        return true;
    return false;
}

int ff_decklink_set_format(AVFormatContext *avctx,
                               int width, int height,
                               int tb_num, int tb_den,
                               enum AVFieldOrder field_order,
                               decklink_direction_t direction, int num)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *)avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *)cctx->ctx;
    BMDDisplayModeSupport support;
    IDeckLinkDisplayModeIterator *itermode;
    IDeckLinkDisplayMode *mode;
    int i = 1;
    HRESULT res;

    av_log(avctx, AV_LOG_DEBUG, "Trying to find mode for frame size %dx%d, frame timing %d/%d, field order %d, direction %d, mode number %d, format code %s\n",
        width, height, tb_num, tb_den, field_order, direction, num, (cctx->format_code) ? cctx->format_code : "(unset)");

    if (ctx->duplex_mode) {
        DECKLINK_BOOL duplex_supported = false;

        if (ctx->attr->GetFlag(BMDDeckLinkSupportsDuplexModeConfiguration, &duplex_supported) != S_OK)
            duplex_supported = false;

        if (duplex_supported) {
            res = ctx->cfg->SetInt(bmdDeckLinkConfigDuplexMode, ctx->duplex_mode == 2 ? bmdDuplexModeFull : bmdDuplexModeHalf);
            if (res != S_OK)
                av_log(avctx, AV_LOG_WARNING, "Setting duplex mode failed.\n");
            else
                av_log(avctx, AV_LOG_VERBOSE, "Successfully set duplex mode to %s duplex.\n", ctx->duplex_mode == 2 ? "full" : "half");
        } else {
            av_log(avctx, AV_LOG_WARNING, "Unable to set duplex mode, because it is not supported.\n");
        }
    }

    if (direction == DIRECTION_IN) {
        int ret;
        ret = decklink_select_input(avctx, bmdDeckLinkConfigAudioInputConnection);
        if (ret < 0)
            return ret;
        ret = decklink_select_input(avctx, bmdDeckLinkConfigVideoInputConnection);
        if (ret < 0)
            return ret;
        res = ctx->dli->GetDisplayModeIterator (&itermode);
    } else {
        res = ctx->dlo->GetDisplayModeIterator (&itermode);
    }

    if (res!= S_OK) {
            av_log(avctx, AV_LOG_ERROR, "Could not get Display Mode Iterator\n");
            return AVERROR(EIO);
    }

    char format_buf[] = "    ";
    if (cctx->format_code)
        memcpy(format_buf, cctx->format_code, FFMIN(strlen(cctx->format_code), sizeof(format_buf)));
    BMDDisplayMode target_mode = (BMDDisplayMode)AV_RB32(format_buf);
    AVRational target_tb = av_make_q(tb_num, tb_den);
    ctx->bmd_mode = bmdModeUnknown;
    while ((ctx->bmd_mode == bmdModeUnknown) && itermode->Next(&mode) == S_OK) {
        BMDTimeValue bmd_tb_num, bmd_tb_den;
        int bmd_width  = mode->GetWidth();
        int bmd_height = mode->GetHeight();
        BMDDisplayMode bmd_mode = mode->GetDisplayMode();
        BMDFieldDominance bmd_field_dominance = mode->GetFieldDominance();

        mode->GetFrameRate(&bmd_tb_num, &bmd_tb_den);
        AVRational mode_tb = av_make_q(bmd_tb_num, bmd_tb_den);

        if ((bmd_width == width &&
             bmd_height == height &&
             !av_cmp_q(mode_tb, target_tb) &&
             field_order_eq(field_order, bmd_field_dominance))
             || i == num
             || target_mode == bmd_mode) {
            ctx->bmd_mode   = bmd_mode;
            ctx->bmd_width  = bmd_width;
            ctx->bmd_height = bmd_height;
            ctx->bmd_tb_den = bmd_tb_den;
            ctx->bmd_tb_num = bmd_tb_num;
            ctx->bmd_field_dominance = bmd_field_dominance;
            av_log(avctx, AV_LOG_INFO, "Found Decklink mode %d x %d with rate %.2f%s\n",
                bmd_width, bmd_height, 1/av_q2d(mode_tb),
                (ctx->bmd_field_dominance==bmdLowerFieldFirst || ctx->bmd_field_dominance==bmdUpperFieldFirst)?"(i)":"");
        }

        mode->Release();
        i++;
    }

    itermode->Release();

    if (ctx->bmd_mode == bmdModeUnknown)
        return -1;
    if (direction == DIRECTION_IN) {
        if (ctx->dli->DoesSupportVideoMode(ctx->bmd_mode, (BMDPixelFormat) cctx->raw_format,
                                           bmdVideoOutputFlagDefault,
                                           &support, NULL) != S_OK)
            return -1;
    } else {
        if (ctx->dlo->DoesSupportVideoMode(ctx->bmd_mode, bmdFormat8BitYUV,
                                           bmdVideoOutputFlagDefault,
                                           &support, NULL) != S_OK)
        return -1;
    }
    if (support == bmdDisplayModeSupported)
        return 0;

    return -1;
}

int ff_decklink_set_format(AVFormatContext *avctx, decklink_direction_t direction, int num) {
    return ff_decklink_set_format(avctx, 0, 0, 0, 0, AV_FIELD_UNKNOWN, direction, num);
}

int ff_decklink_list_devices(AVFormatContext *avctx,
                             struct AVDeviceInfoList *device_list,
                             int show_inputs, int show_outputs)
{
    IDeckLink *dl = NULL;
    IDeckLinkIterator *iter = CreateDeckLinkIteratorInstance();
    int ret = 0;

    if (!iter) {
        av_log(avctx, AV_LOG_ERROR, "Could not create DeckLink iterator\n");
        return AVERROR(EIO);
    }

    while (ret == 0 && iter->Next(&dl) == S_OK) {
        IDeckLinkOutput *output_config;
        IDeckLinkInput *input_config;
        const char *displayName;
        AVDeviceInfo *new_device = NULL;
        int add = 0;

        ff_decklink_get_display_name(dl, &displayName);

        if (show_outputs) {
            if (dl->QueryInterface(IID_IDeckLinkOutput, (void **)&output_config) == S_OK) {
                output_config->Release();
                add = 1;
            }
        }

        if (show_inputs) {
            if (dl->QueryInterface(IID_IDeckLinkInput, (void **)&input_config) == S_OK) {
                input_config->Release();
                add = 1;
            }
        }

        if (add == 1) {
            new_device = (AVDeviceInfo *) av_mallocz(sizeof(AVDeviceInfo));
            if (!new_device) {
                ret = AVERROR(ENOMEM);
                goto next;
            }
            new_device->device_name = av_strdup(displayName);
            if (!new_device->device_name) {
                ret = AVERROR(ENOMEM);
                goto next;
            }

            new_device->device_description = av_strdup(displayName);
            if (!new_device->device_description) {
                av_freep(&new_device->device_name);
                ret = AVERROR(ENOMEM);
                goto next;
            }

            if ((ret = av_dynarray_add_nofree(&device_list->devices,
                                              &device_list->nb_devices, new_device)) < 0) {
                av_freep(&new_device->device_name);
                av_freep(&new_device->device_description);
                av_freep(&new_device);
                goto next;
            }
        }

    next:
        av_freep(&displayName);
        dl->Release();
    }
    iter->Release();
    return ret;
}

/* This is a wrapper around the ff_decklink_list_devices() which dumps the
   output to av_log() and exits (for backward compatibility with the
   "-list_devices" argument). */
void ff_decklink_list_devices_legacy(AVFormatContext *avctx,
                                     int show_inputs, int show_outputs)
{
    struct AVDeviceInfoList *device_list = NULL;
    int ret;

    device_list = (struct AVDeviceInfoList *) av_mallocz(sizeof(AVDeviceInfoList));
    if (!device_list)
        return;

    ret = ff_decklink_list_devices(avctx, device_list, show_inputs, show_outputs);
    if (ret == 0) {
        av_log(avctx, AV_LOG_INFO, "Blackmagic DeckLink %s devices:\n",
               show_inputs ? "input" : "output");
        for (int i = 0; i < device_list->nb_devices; i++) {
            av_log(avctx, AV_LOG_INFO, "\t'%s'\n", device_list->devices[i]->device_name);
        }
    }
    avdevice_free_list_devices(&device_list);
}

int ff_decklink_list_formats(AVFormatContext *avctx, decklink_direction_t direction)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *)avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *)cctx->ctx;
    IDeckLinkDisplayModeIterator *itermode;
    IDeckLinkDisplayMode *mode;
    uint32_t format_code;
    HRESULT res;

    if (direction == DIRECTION_IN) {
        int ret;
        ret = decklink_select_input(avctx, bmdDeckLinkConfigAudioInputConnection);
        if (ret < 0)
            return ret;
        ret = decklink_select_input(avctx, bmdDeckLinkConfigVideoInputConnection);
        if (ret < 0)
            return ret;
        res = ctx->dli->GetDisplayModeIterator (&itermode);
    } else {
        res = ctx->dlo->GetDisplayModeIterator (&itermode);
    }

    if (res!= S_OK) {
            av_log(avctx, AV_LOG_ERROR, "Could not get Display Mode Iterator\n");
            return AVERROR(EIO);
    }

    av_log(avctx, AV_LOG_INFO, "Supported formats for '%s':\n\tformat_code\tdescription",
               avctx->filename);
    while (itermode->Next(&mode) == S_OK) {
        BMDTimeValue tb_num, tb_den;
        mode->GetFrameRate(&tb_num, &tb_den);
        format_code = av_bswap32(mode->GetDisplayMode());
        av_log(avctx, AV_LOG_INFO, "\n\t%.4s\t\t%ldx%ld at %d/%d fps",
                (char*) &format_code, mode->GetWidth(), mode->GetHeight(),
                (int) tb_den, (int) tb_num);
        switch (mode->GetFieldDominance()) {
        case bmdLowerFieldFirst:
        av_log(avctx, AV_LOG_INFO, " (interlaced, lower field first)"); break;
        case bmdUpperFieldFirst:
        av_log(avctx, AV_LOG_INFO, " (interlaced, upper field first)"); break;
        }
        mode->Release();
    }
    av_log(avctx, AV_LOG_INFO, "\n");

    itermode->Release();

    return 0;
}

void ff_decklink_cleanup(AVFormatContext *avctx)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *)avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *)cctx->ctx;

    if (ctx->dli)
        ctx->dli->Release();
    if (ctx->dlo)
        ctx->dlo->Release();
    if (ctx->attr)
        ctx->attr->Release();
    if (ctx->cfg)
        ctx->cfg->Release();
    if (ctx->dl)
        ctx->dl->Release();
}

int ff_decklink_init_device(AVFormatContext *avctx, const char* name)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *)avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *)cctx->ctx;
    IDeckLink *dl = NULL;
    IDeckLinkIterator *iter = CreateDeckLinkIteratorInstance();
    if (!iter) {
        av_log(avctx, AV_LOG_ERROR, "Could not create DeckLink iterator\n");
        return AVERROR_EXTERNAL;
    }

    while (iter->Next(&dl) == S_OK) {
        const char *displayName;
        ff_decklink_get_display_name(dl, &displayName);
        if (!strcmp(name, displayName)) {
            av_free((void *)displayName);
            ctx->dl = dl;
            break;
        }
        av_free((void *)displayName);
        dl->Release();
    }
    iter->Release();
    if (!ctx->dl)
        return AVERROR(ENXIO);

    if (ctx->dl->QueryInterface(IID_IDeckLinkConfiguration, (void **)&ctx->cfg) != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not get configuration interface for '%s'\n", name);
        ff_decklink_cleanup(avctx);
        return AVERROR_EXTERNAL;
    }

    if (ctx->dl->QueryInterface(IID_IDeckLinkAttributes, (void **)&ctx->attr) != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not get attributes interface for '%s'\n", name);
        ff_decklink_cleanup(avctx);
        return AVERROR_EXTERNAL;
    }

    return 0;
}
