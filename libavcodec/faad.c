/*
 * Faad decoder
 * Copyright (c) 2003 Zdenek Kabelac.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file faad.c
 * AAC decoder.
 *
 * still a bit unfinished - but it plays something
 */

#include "avcodec.h"
#include "faad.h"

/*
 * when CONFIG_FAADBIN is defined the libfaad will be opened at runtime
 */
//#undef CONFIG_FAADBIN
//#define CONFIG_FAADBIN

#ifdef CONFIG_FAADBIN
#include <dlfcn.h>
static const char* libfaadname = "libfaad.so.0";
#else
#define dlopen(a)
#define dlclose(a)
#endif

typedef struct {
    void* handle;		/* dlopen handle */
    void* faac_handle;		/* FAAD library handle */
    int frame_size;
    int sample_size;
    int flags;

    /* faad calls */
    faacDecHandle FAADAPI (*faacDecOpen)(void);
    faacDecConfigurationPtr FAADAPI (*faacDecGetCurrentConfiguration)(faacDecHandle hDecoder);
    unsigned char FAADAPI (*faacDecSetConfiguration)(faacDecHandle hDecoder,
                                                     faacDecConfigurationPtr config);
    long FAADAPI (*faacDecInit)(faacDecHandle hDecoder,
				unsigned char *buffer,
				unsigned long *samplerate,
				unsigned char *channels);
    char FAADAPI (*faacDecInit2)(faacDecHandle hDecoder, unsigned char *pBuffer,
                                 unsigned long SizeOfDecoderSpecificInfo,
                                 unsigned long *samplerate, unsigned char *channels);
    void FAADAPI (*faacDecClose)(faacDecHandle hDecoder);
    void* FAADAPI (*faacDecDecode)(faacDecHandle hDecoder,
                                   faacDecFrameInfo *hInfo,
                                   unsigned char *buffer);
    unsigned char* FAADAPI (*faacDecGetErrorMessage)(unsigned char errcode);
} FAACContext;

static const unsigned long faac_srates[] =
{
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000
};

static int faac_init_mp4(AVCodecContext *avctx)
{
    FAACContext *s = (FAACContext *) avctx->priv_data;
    unsigned long samplerate;
    unsigned char channels;
    int r = 0;

    if (avctx->extradata)
	r = s->faacDecInit2(s->faac_handle, (uint8_t*) avctx->extradata,
			    avctx->extradata_size,
			    &samplerate, &channels);
    // else r = s->faacDecInit(s->faac_handle ... );

    if (r < 0)
	av_log(avctx, AV_LOG_ERROR, "faacDecInit2 failed r:%d   sr:%ld  ch:%d  s:%d\n",
		r, samplerate, channels, avctx->extradata_size);

    return r;
}

static int faac_init_aac(AVCodecContext *avctx)
{
    FAACContext *s = (FAACContext *) avctx->priv_data;
    return 0;
}

static int faac_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    FAACContext *s = (FAACContext *) avctx->priv_data;
    faacDecFrameInfo frame_info;
    void* out = s->faacDecDecode(s->faac_handle, &frame_info, (unsigned char*)buf);
    //printf("DECODE FRAME %d, %d, %d - %p\n", buf_size, frame_info.samples, frame_info.bytesconsumed, out);

    if (frame_info.error > 0) {
	av_log(avctx, AV_LOG_ERROR, "faac: frame decodinf failed: %s\n",
		s->faacDecGetErrorMessage(frame_info.error));
        return 0;
    }

    frame_info.samples *= s->sample_size;
    memcpy(data, out, frame_info.samples); // CHECKME - can we cheat this one

    if (data_size)
	*data_size = frame_info.samples;

    return (buf_size < (int)frame_info.bytesconsumed)
	? buf_size : (int)frame_info.bytesconsumed;
}

static int faac_decode_end(AVCodecContext *avctx)
{
    FAACContext *s = (FAACContext *) avctx->priv_data;

    if (s->faacDecClose)
        s->faacDecClose(s->faac_handle);

    dlclose(s->handle);
    return 0;
}

static int faac_decode_init(AVCodecContext *avctx)
{
    FAACContext *s = (FAACContext *) avctx->priv_data;
    faacDecConfigurationPtr faac_cfg;

#ifdef CONFIG_FAADBIN
    const char* err = 0;

    s->handle = dlopen(libfaadname, RTLD_LAZY);
    if (!s->handle)
    {
	av_log(avctx, AV_LOG_ERROR, "FAAD library: %s could not be opened! \n%s\n",
		libfaadname, dlerror());
        return -1;
    }
#define dfaac(a, b) \
    do { static const char* n = "faacDec" #a; \
    if ((s->faacDec ## a = b dlsym( s->handle, n )) == NULL) { err = n; break; } } while(0)
    for(;;) {
#else  /* !CONFIG_FAADBIN */
#define dfaac(a, b)     s->faacDec ## a = faacDec ## a
#endif /* CONFIG_FAADBIN */

        // resolve all needed function calls
        dfaac(Open, (faacDecHandle FAADAPI (*)(void)));
	dfaac(GetCurrentConfiguration, (faacDecConfigurationPtr
					FAADAPI (*)(faacDecHandle)));
	dfaac(SetConfiguration, (unsigned char FAADAPI (*)(faacDecHandle,
							   faacDecConfigurationPtr)));

	dfaac(Init, (long FAADAPI (*)(faacDecHandle, unsigned char*,
				     unsigned long*, unsigned char*)));
        dfaac(Init2, (char FAADAPI (*)(faacDecHandle, unsigned char*,
				       unsigned long, unsigned long*,
				       unsigned char*)));
        dfaac(Close, (void FAADAPI (*)(faacDecHandle hDecoder)));
	dfaac(Decode, (void* FAADAPI (*)(faacDecHandle, faacDecFrameInfo*,
					 unsigned char*)));

	dfaac(GetErrorMessage, (unsigned char* FAADAPI (*)(unsigned char)));
#undef dfacc

#ifdef CONFIG_FAADBIN
        break;
    }
    if (err) {
        dlclose(s->handle);
	av_log(avctx, AV_LOG_ERROR, "FAAD library: cannot resolve %s in %s!\n",
		err, libfaadname);
        return -1;
    }
#endif

    s->faac_handle = s->faacDecOpen();
    if (!s->faac_handle) {
        av_log(avctx, AV_LOG_ERROR, "FAAD library: cannot create handler!\n");
        faac_decode_end(avctx);
        return -1;
    }


    faac_cfg = s->faacDecGetCurrentConfiguration(s->faac_handle);

    if (faac_cfg) {
	switch (avctx->bits_per_sample) {
	case 8: av_log(avctx, AV_LOG_ERROR, "FAADlib unsupported bps %d\n", avctx->bits_per_sample); break;
	default:
	case 16:
	    faac_cfg->outputFormat = FAAD_FMT_16BIT;
	    s->sample_size = 2;
	    break;
	case 24:
	    faac_cfg->outputFormat = FAAD_FMT_24BIT;
	    s->sample_size = 3;
	    break;
	case 32:
	    faac_cfg->outputFormat = FAAD_FMT_32BIT;
	    s->sample_size = 4;
	    break;
	}

	faac_cfg->defSampleRate = (!avctx->sample_rate) ? 44100 : avctx->sample_rate;
	faac_cfg->defObjectType = LC;
    }

    s->faacDecSetConfiguration(s->faac_handle, faac_cfg);

    faac_init_mp4(avctx);

    return 0;
}

#define AAC_CODEC(id, name)     \
AVCodec name ## _decoder = {    \
    #name,                      \
    CODEC_TYPE_AUDIO,           \
    id,                         \
    sizeof(FAACContext),        \
    faac_decode_init,           \
    NULL,                       \
    faac_decode_end,            \
    faac_decode_frame,          \
}

// FIXME - raw AAC files - maybe just one entry will be enough
AAC_CODEC(CODEC_ID_AAC, aac);
// If it's mp4 file - usually embeded into Qt Mov
AAC_CODEC(CODEC_ID_MPEG4AAC, mpeg4aac);

#undef AAC_CODEC
