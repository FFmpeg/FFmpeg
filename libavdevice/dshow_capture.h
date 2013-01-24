/*
 * DirectShow capture interface
 * Copyright (c) 2010 Ramiro Polla
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

#ifndef AVDEVICE_DSHOW_H
#define AVDEVICE_DSHOW_H

#define DSHOWDEBUG 0

#include "avdevice.h"

#define COBJMACROS
#include <windows.h>
#define NO_DSHOW_STRSAFE
#include <dshow.h>
#include <dvdmedia.h>

/* EC_DEVICE_LOST is not defined in MinGW dshow headers. */
#ifndef EC_DEVICE_LOST
#define EC_DEVICE_LOST 0x1f
#endif

long ff_copy_dshow_media_type(AM_MEDIA_TYPE *dst, const AM_MEDIA_TYPE *src);
void ff_print_VIDEO_STREAM_CONFIG_CAPS(const VIDEO_STREAM_CONFIG_CAPS *caps);
void ff_print_AUDIO_STREAM_CONFIG_CAPS(const AUDIO_STREAM_CONFIG_CAPS *caps);
void ff_print_AM_MEDIA_TYPE(const AM_MEDIA_TYPE *type);
void ff_printGUID(const GUID *g);

#if DSHOWDEBUG
extern const AVClass *ff_dshow_context_class_ptr;
#define dshowdebug(...) av_log(&ff_dshow_context_class_ptr, AV_LOG_DEBUG, __VA_ARGS__)
#else
#define dshowdebug(...)
#endif

static inline void nothing(void *foo)
{
}

struct GUIDoffset {
    const GUID *iid;
    int offset;
};

enum dshowDeviceType {
    VideoDevice = 0,
    AudioDevice = 1,
};

#define DECLARE_QUERYINTERFACE(class, ...)                                   \
long WINAPI                                                                  \
class##_QueryInterface(class *this, const GUID *riid, void **ppvObject)      \
{                                                                            \
    struct GUIDoffset ifaces[] = __VA_ARGS__;                                \
    int i;                                                                   \
    dshowdebug(AV_STRINGIFY(class)"_QueryInterface(%p, %p, %p)\n", this, riid, ppvObject); \
    ff_printGUID(riid);                                                      \
    if (!ppvObject)                                                          \
        return E_POINTER;                                                    \
    for (i = 0; i < sizeof(ifaces)/sizeof(ifaces[0]); i++) {                 \
        if (IsEqualGUID(riid, ifaces[i].iid)) {                              \
            void *obj = (void *) ((uint8_t *) this + ifaces[i].offset);      \
            class##_AddRef(this);                                            \
            dshowdebug("\tfound %d with offset %d\n", i, ifaces[i].offset);  \
            *ppvObject = (void *) obj;                                       \
            return S_OK;                                                     \
        }                                                                    \
    }                                                                        \
    dshowdebug("\tE_NOINTERFACE\n");                                         \
    *ppvObject = NULL;                                                       \
    return E_NOINTERFACE;                                                    \
}
#define DECLARE_ADDREF(class)                                                \
unsigned long WINAPI                                                         \
class##_AddRef(class *this)                                                  \
{                                                                            \
    dshowdebug(AV_STRINGIFY(class)"_AddRef(%p)\t%ld\n", this, this->ref+1);  \
    return InterlockedIncrement(&this->ref);                                 \
}
#define DECLARE_RELEASE(class)                                               \
unsigned long WINAPI                                                         \
class##_Release(class *this)                                                 \
{                                                                            \
    long ref = InterlockedDecrement(&this->ref);                             \
    dshowdebug(AV_STRINGIFY(class)"_Release(%p)\t%ld\n", this, ref);         \
    if (!ref)                                                                \
        class##_Destroy(this);                                               \
    return ref;                                                              \
}

#define DECLARE_DESTROY(class, func)                                         \
void class##_Destroy(class *this)                                            \
{                                                                            \
    dshowdebug(AV_STRINGIFY(class)"_Destroy(%p)\n", this);                   \
    func(this);                                                              \
    if (this) {                                                              \
        if (this->vtbl)                                                      \
            CoTaskMemFree(this->vtbl);                                       \
        CoTaskMemFree(this);                                                 \
    }                                                                        \
}
#define DECLARE_CREATE(class, setup, ...)                                    \
class *class##_Create(__VA_ARGS__)                                           \
{                                                                            \
    class *this = CoTaskMemAlloc(sizeof(class));                             \
    void  *vtbl = CoTaskMemAlloc(sizeof(*this->vtbl));                       \
    dshowdebug(AV_STRINGIFY(class)"_Create(%p)\n", this);                    \
    if (!this || !vtbl)                                                      \
        goto fail;                                                           \
    ZeroMemory(this, sizeof(class));                                         \
    ZeroMemory(vtbl, sizeof(*this->vtbl));                                   \
    this->ref  = 1;                                                          \
    this->vtbl = vtbl;                                                       \
    if (!setup)                                                              \
        goto fail;                                                           \
    dshowdebug("created "AV_STRINGIFY(class)" %p\n", this);                  \
    return this;                                                             \
fail:                                                                        \
    class##_Destroy(this);                                                   \
    dshowdebug("could not create "AV_STRINGIFY(class)"\n");                  \
    return NULL;                                                             \
}

#define SETVTBL(vtbl, class, fn) \
    do { (vtbl)->fn = (void *) class##_##fn; } while(0)

/*****************************************************************************
 * Forward Declarations
 ****************************************************************************/
typedef struct libAVPin libAVPin;
typedef struct libAVMemInputPin libAVMemInputPin;
typedef struct libAVEnumPins libAVEnumPins;
typedef struct libAVEnumMediaTypes libAVEnumMediaTypes;
typedef struct libAVFilter libAVFilter;

/*****************************************************************************
 * libAVPin
 ****************************************************************************/
struct libAVPin {
    IPinVtbl *vtbl;
    long ref;
    libAVFilter *filter;
    IPin *connectedto;
    AM_MEDIA_TYPE type;
    IMemInputPinVtbl *imemvtbl;
};

long          WINAPI libAVPin_QueryInterface          (libAVPin *, const GUID *, void **);
unsigned long WINAPI libAVPin_AddRef                  (libAVPin *);
unsigned long WINAPI libAVPin_Release                 (libAVPin *);
long          WINAPI libAVPin_Connect                 (libAVPin *, IPin *, const AM_MEDIA_TYPE *);
long          WINAPI libAVPin_ReceiveConnection       (libAVPin *, IPin *, const AM_MEDIA_TYPE *);
long          WINAPI libAVPin_Disconnect              (libAVPin *);
long          WINAPI libAVPin_ConnectedTo             (libAVPin *, IPin **);
long          WINAPI libAVPin_ConnectionMediaType     (libAVPin *, AM_MEDIA_TYPE *);
long          WINAPI libAVPin_QueryPinInfo            (libAVPin *, PIN_INFO *);
long          WINAPI libAVPin_QueryDirection          (libAVPin *, PIN_DIRECTION *);
long          WINAPI libAVPin_QueryId                 (libAVPin *, wchar_t **);
long          WINAPI libAVPin_QueryAccept             (libAVPin *, const AM_MEDIA_TYPE *);
long          WINAPI libAVPin_EnumMediaTypes          (libAVPin *, IEnumMediaTypes **);
long          WINAPI libAVPin_QueryInternalConnections(libAVPin *, IPin **, unsigned long *);
long          WINAPI libAVPin_EndOfStream             (libAVPin *);
long          WINAPI libAVPin_BeginFlush              (libAVPin *);
long          WINAPI libAVPin_EndFlush                (libAVPin *);
long          WINAPI libAVPin_NewSegment              (libAVPin *, REFERENCE_TIME, REFERENCE_TIME, double);

long          WINAPI libAVMemInputPin_QueryInterface          (libAVMemInputPin *, const GUID *, void **);
unsigned long WINAPI libAVMemInputPin_AddRef                  (libAVMemInputPin *);
unsigned long WINAPI libAVMemInputPin_Release                 (libAVMemInputPin *);
long          WINAPI libAVMemInputPin_GetAllocator            (libAVMemInputPin *, IMemAllocator **);
long          WINAPI libAVMemInputPin_NotifyAllocator         (libAVMemInputPin *, IMemAllocator *, BOOL);
long          WINAPI libAVMemInputPin_GetAllocatorRequirements(libAVMemInputPin *, ALLOCATOR_PROPERTIES *);
long          WINAPI libAVMemInputPin_Receive                 (libAVMemInputPin *, IMediaSample *);
long          WINAPI libAVMemInputPin_ReceiveMultiple         (libAVMemInputPin *, IMediaSample **, long, long *);
long          WINAPI libAVMemInputPin_ReceiveCanBlock         (libAVMemInputPin *);

void                 libAVPin_Destroy(libAVPin *);
libAVPin            *libAVPin_Create (libAVFilter *filter);

void                 libAVMemInputPin_Destroy(libAVMemInputPin *);

/*****************************************************************************
 * libAVEnumPins
 ****************************************************************************/
struct libAVEnumPins {
    IEnumPinsVtbl *vtbl;
    long ref;
    int pos;
    libAVPin *pin;
    libAVFilter *filter;
};

long          WINAPI libAVEnumPins_QueryInterface(libAVEnumPins *, const GUID *, void **);
unsigned long WINAPI libAVEnumPins_AddRef        (libAVEnumPins *);
unsigned long WINAPI libAVEnumPins_Release       (libAVEnumPins *);
long          WINAPI libAVEnumPins_Next          (libAVEnumPins *, unsigned long, IPin **, unsigned long *);
long          WINAPI libAVEnumPins_Skip          (libAVEnumPins *, unsigned long);
long          WINAPI libAVEnumPins_Reset         (libAVEnumPins *);
long          WINAPI libAVEnumPins_Clone         (libAVEnumPins *, libAVEnumPins **);

void                 libAVEnumPins_Destroy(libAVEnumPins *);
libAVEnumPins       *libAVEnumPins_Create (libAVPin *pin, libAVFilter *filter);

/*****************************************************************************
 * libAVEnumMediaTypes
 ****************************************************************************/
struct libAVEnumMediaTypes {
    IEnumPinsVtbl *vtbl;
    long ref;
    int pos;
    AM_MEDIA_TYPE type;
};

long          WINAPI libAVEnumMediaTypes_QueryInterface(libAVEnumMediaTypes *, const GUID *, void **);
unsigned long WINAPI libAVEnumMediaTypes_AddRef        (libAVEnumMediaTypes *);
unsigned long WINAPI libAVEnumMediaTypes_Release       (libAVEnumMediaTypes *);
long          WINAPI libAVEnumMediaTypes_Next          (libAVEnumMediaTypes *, unsigned long, AM_MEDIA_TYPE **, unsigned long *);
long          WINAPI libAVEnumMediaTypes_Skip          (libAVEnumMediaTypes *, unsigned long);
long          WINAPI libAVEnumMediaTypes_Reset         (libAVEnumMediaTypes *);
long          WINAPI libAVEnumMediaTypes_Clone         (libAVEnumMediaTypes *, libAVEnumMediaTypes **);

void                 libAVEnumMediaTypes_Destroy(libAVEnumMediaTypes *);
libAVEnumMediaTypes *libAVEnumMediaTypes_Create(const AM_MEDIA_TYPE *type);

/*****************************************************************************
 * libAVFilter
 ****************************************************************************/
struct libAVFilter {
    IBaseFilterVtbl *vtbl;
    long ref;
    const wchar_t *name;
    libAVPin *pin;
    FILTER_INFO info;
    FILTER_STATE state;
    IReferenceClock *clock;
    enum dshowDeviceType type;
    void *priv_data;
    int stream_index;
    int64_t start_time;
    void (*callback)(void *priv_data, int index, uint8_t *buf, int buf_size, int64_t time);
};

long          WINAPI libAVFilter_QueryInterface (libAVFilter *, const GUID *, void **);
unsigned long WINAPI libAVFilter_AddRef         (libAVFilter *);
unsigned long WINAPI libAVFilter_Release        (libAVFilter *);
long          WINAPI libAVFilter_GetClassID     (libAVFilter *, CLSID *);
long          WINAPI libAVFilter_Stop           (libAVFilter *);
long          WINAPI libAVFilter_Pause          (libAVFilter *);
long          WINAPI libAVFilter_Run            (libAVFilter *, REFERENCE_TIME);
long          WINAPI libAVFilter_GetState       (libAVFilter *, DWORD, FILTER_STATE *);
long          WINAPI libAVFilter_SetSyncSource  (libAVFilter *, IReferenceClock *);
long          WINAPI libAVFilter_GetSyncSource  (libAVFilter *, IReferenceClock **);
long          WINAPI libAVFilter_EnumPins       (libAVFilter *, IEnumPins **);
long          WINAPI libAVFilter_FindPin        (libAVFilter *, const wchar_t *, IPin **);
long          WINAPI libAVFilter_QueryFilterInfo(libAVFilter *, FILTER_INFO *);
long          WINAPI libAVFilter_JoinFilterGraph(libAVFilter *, IFilterGraph *, const wchar_t *);
long          WINAPI libAVFilter_QueryVendorInfo(libAVFilter *, wchar_t **);

void                 libAVFilter_Destroy(libAVFilter *);
libAVFilter         *libAVFilter_Create (void *, void *, enum dshowDeviceType);

#endif /* AVDEVICE_DSHOW_H */
