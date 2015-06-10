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

#include <DeckLinkAPI.h>
#ifdef _WIN32
#include <DeckLinkAPI_i.c>
#else
#include <DeckLinkAPIDispatch.cpp>
#endif

#include <pthread.h>
#include <semaphore.h>

extern "C" {
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/imgutils.h"
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
#elif defined(__APPLE__)
static char *dup_cfstring_to_utf8(CFStringRef w)
{
    char s[256];
    CFStringGetCString(w, s, 255, kCFStringEncodingUTF8);
    return av_strdup(s);
}
#define DECKLINK_STR    const __CFString *
#define DECKLINK_STRDUP dup_cfstring_to_utf8
#define DECKLINK_FREE(s) free((void *) s)
#else
#define DECKLINK_STR    const char *
#define DECKLINK_STRDUP av_strdup
/* free() is needed for a string returned by the DeckLink SDL. */
#define DECKLINK_FREE(s) free((void *) s)
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

int ff_decklink_set_format(AVFormatContext *avctx,
                               int width, int height,
                               int tb_num, int tb_den,
                               decklink_direction_t direction, int num)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *)cctx->ctx;
    BMDDisplayModeSupport support;
    IDeckLinkDisplayModeIterator *itermode;
    IDeckLinkDisplayMode *mode;
    int i = 1;
    HRESULT res;

    if (direction == DIRECTION_IN) {
        res = ctx->dli->GetDisplayModeIterator (&itermode);
    } else {
        res = ctx->dlo->GetDisplayModeIterator (&itermode);
    }

    if (res!= S_OK) {
            av_log(avctx, AV_LOG_ERROR, "Could not get Display Mode Iterator\n");
            return AVERROR(EIO);
    }


    if (tb_num == 1) {
        tb_num *= 1000;
        tb_den *= 1000;
    }
    ctx->bmd_mode = bmdModeUnknown;
    while ((ctx->bmd_mode == bmdModeUnknown) && itermode->Next(&mode) == S_OK) {
        BMDTimeValue bmd_tb_num, bmd_tb_den;
        int bmd_width  = mode->GetWidth();
        int bmd_height = mode->GetHeight();

        mode->GetFrameRate(&bmd_tb_num, &bmd_tb_den);

        if ((bmd_width == width && bmd_height == height &&
            bmd_tb_num == tb_num && bmd_tb_den == tb_den) || i == num) {
            ctx->bmd_mode   = mode->GetDisplayMode();
            ctx->bmd_width  = bmd_width;
            ctx->bmd_height = bmd_height;
            ctx->bmd_tb_den = bmd_tb_den;
            ctx->bmd_tb_num = bmd_tb_num;
            ctx->bmd_field_dominance = mode->GetFieldDominance();
            av_log(avctx, AV_LOG_INFO, "Found Decklink mode %d x %d with rate %.2f%s\n",
                bmd_width, bmd_height, (float)bmd_tb_den/(float)bmd_tb_num,
                (ctx->bmd_field_dominance==bmdLowerFieldFirst || ctx->bmd_field_dominance==bmdUpperFieldFirst)?"(i)":"");
        }

        mode->Release();
        i++;
    }

    itermode->Release();

    if (ctx->bmd_mode == bmdModeUnknown)
        return -1;
    if (direction == DIRECTION_IN) {
        if (ctx->dli->DoesSupportVideoMode(ctx->bmd_mode, bmdFormat8BitYUV,
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
    return ff_decklink_set_format(avctx, 0, 0, 0, 0, direction, num);
}

int ff_decklink_list_devices(AVFormatContext *avctx)
{
    IDeckLink *dl = NULL;
    IDeckLinkIterator *iter = CreateDeckLinkIteratorInstance();
    if (!iter) {
        av_log(avctx, AV_LOG_ERROR, "Could not create DeckLink iterator\n");
        return AVERROR(EIO);
    }
    av_log(avctx, AV_LOG_INFO, "Blackmagic DeckLink devices:\n");
    while (iter->Next(&dl) == S_OK) {
        const char *displayName;
        ff_decklink_get_display_name(dl, &displayName);
        av_log(avctx, AV_LOG_INFO, "\t'%s'\n", displayName);
        av_free((void *) displayName);
        dl->Release();
    }
    iter->Release();
    return 0;
}

int ff_decklink_list_formats(AVFormatContext *avctx, decklink_direction_t direction)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *)cctx->ctx;
    IDeckLinkDisplayModeIterator *itermode;
    IDeckLinkDisplayMode *mode;
    int i=0;
    HRESULT res;

    if (direction == DIRECTION_IN) {
        res = ctx->dli->GetDisplayModeIterator (&itermode);
    } else {
        res = ctx->dlo->GetDisplayModeIterator (&itermode);
    }

    if (res!= S_OK) {
            av_log(avctx, AV_LOG_ERROR, "Could not get Display Mode Iterator\n");
            return AVERROR(EIO);
    }

    av_log(avctx, AV_LOG_INFO, "Supported formats for '%s':\n",
               avctx->filename);
    while (itermode->Next(&mode) == S_OK) {
        BMDTimeValue tb_num, tb_den;
        mode->GetFrameRate(&tb_num, &tb_den);
        av_log(avctx, AV_LOG_INFO, "\t%d\t%ldx%ld at %d/%d fps",
                ++i,mode->GetWidth(), mode->GetHeight(),
                (int) tb_den, (int) tb_num);
        switch (mode->GetFieldDominance()) {
        case bmdLowerFieldFirst:
        av_log(avctx, AV_LOG_INFO, " (interlaced, lower field first)"); break;
        case bmdUpperFieldFirst:
        av_log(avctx, AV_LOG_INFO, " (interlaced, upper field first)"); break;
        }
        av_log(avctx, AV_LOG_INFO, "\n");
        mode->Release();
    }

    itermode->Release();

    return 0;
}
