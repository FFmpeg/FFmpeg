/*
 * Faad decoder
 * Copyright (c) 2003 Zdenek Kabelac.
 * Copyright (c) 2004 Thomas Raivio.
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

#ifndef FAADAPI
#define FAADAPI
#endif

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
#ifndef FAAD2_VERSION
	int FAADAPI (*faacDecSetConfiguration)(faacDecHandle hDecoder,
                                           faacDecConfigurationPtr config);
	int FAADAPI (*faacDecInit)(faacDecHandle hDecoder,
				unsigned char *buffer,
				unsigned long *samplerate,
				unsigned long *channels);
	int FAADAPI (*faacDecInit2)(faacDecHandle hDecoder, unsigned char *pBuffer,
                                unsigned long SizeOfDecoderSpecificInfo,
                                unsigned long *samplerate, unsigned long *channels);
	int FAADAPI (*faacDecDecode)(faacDecHandle hDecoder,
		                unsigned char *buffer,
				unsigned long *bytesconsumed,
				short *sample_buffer,
                                unsigned long *samples);
#else
	unsigned char FAADAPI (*faacDecSetConfiguration)(faacDecHandle hDecoder,
                                                     faacDecConfigurationPtr config);
	long FAADAPI (*faacDecInit)(faacDecHandle hDecoder,
		  		 unsigned char *buffer,
				 unsigned long buffer_size,
				 unsigned long *samplerate,
				 unsigned char *channels);
	char FAADAPI (*faacDecInit2)(faacDecHandle hDecoder, unsigned char *pBuffer,
                                 unsigned long SizeOfDecoderSpecificInfo,
                                 unsigned long *samplerate, unsigned char *channels);
	void *FAADAPI (*faacDecDecode)(faacDecHandle hDecoder,
		                         faacDecFrameInfo *hInfo,
		                         unsigned char *buffer,
								 unsigned long buffer_size);
	unsigned char* FAADAPI (*faacDecGetErrorMessage)(unsigned char errcode);
#endif
    
    void FAADAPI (*faacDecClose)(faacDecHandle hDecoder);
    
    
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
#ifndef FAAD2_VERSION
    unsigned long channels;
#else
    unsigned char channels;
#endif
    int r = 0;

    if (avctx->extradata)
	r = s->faacDecInit2(s->faac_handle, (uint8_t*) avctx->extradata,
			    avctx->extradata_size,
			    &samplerate, &channels);
    // else r = s->faacDecInit(s->faac_handle ... );

    if (r < 0)
	av_log(avctx, AV_LOG_ERROR, "faacDecInit2 failed r:%d   sr:%ld  ch:%ld  s:%d\n",
		r, samplerate, (long)channels, avctx->extradata_size);
    avctx->sample_rate = samplerate;
    avctx->channels = channels;

    return r;
}

static int faac_init_aac(AVCodecContext *avctx)
{
    return 0;
}

static int faac_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    FAACContext *s = (FAACContext *) avctx->priv_data;
#ifndef FAAD2_VERSION
    unsigned long bytesconsumed;
    short *sample_buffer = NULL;
    unsigned long samples;
    int out;
#else
    faacDecFrameInfo frame_info;
    void *out;
#endif
    if(buf_size == 0)
	return 0;
#ifndef FAAD2_VERSION
    out = s->faacDecDecode(s->faac_handle, 
                           (unsigned char*)buf, 
                           &bytesconsumed, 
                           data, 
                           &samples);
    samples *= s->sample_size;
    if (data_size)
	*data_size = samples;
    return (buf_size < (int)bytesconsumed)
	? buf_size : (int)bytesconsumed;
#else
	
    out = s->faacDecDecode(s->faac_handle, &frame_info, (unsigned char*)buf, (unsigned long)buf_size);

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
#endif
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
#ifndef FAAD2_VERSION
	dfaac(SetConfiguration, (int FAADAPI (*)(faacDecHandle,
							   faacDecConfigurationPtr)));

	dfaac(Init, (int FAADAPI (*)(faacDecHandle, unsigned char*,
				     unsigned long*, unsigned long*)));
    dfaac(Init2, (int FAADAPI (*)(faacDecHandle, unsigned char*,
				       unsigned long, unsigned long*,
				       unsigned long*)));
    dfaac(Close, (void FAADAPI (*)(faacDecHandle hDecoder)));
	dfaac(Decode, (int FAADAPI (*)(faacDecHandle, unsigned char*,
		             unsigned long*, short*, unsigned long*)));
#else
	dfaac(SetConfiguration, (unsigned char FAADAPI (*)(faacDecHandle,
							   faacDecConfigurationPtr)));
	dfaac(Init, (long FAADAPI (*)(faacDecHandle, unsigned char*,
				     unsigned long, unsigned long*, unsigned char*)));
	dfaac(Init2, (char FAADAPI (*)(faacDecHandle, unsigned char*,
				       unsigned long, unsigned long*,
				       unsigned char*)));
	dfaac(Decode, (void *FAADAPI (*)(faacDecHandle, faacDecFrameInfo*,
		             unsigned char*, unsigned long)));
	dfaac(GetErrorMessage, (unsigned char* FAADAPI (*)(unsigned char)));
#endif
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
#ifdef FAAD2_VERSION
	    faac_cfg->outputFormat = FAAD_FMT_16BIT;
#endif
	    s->sample_size = 2;
	    break;
	case 24:
#ifdef FAAD2_VERSION
	    faac_cfg->outputFormat = FAAD_FMT_24BIT;
#endif
	    s->sample_size = 3;
	    break;
	case 32:
#ifdef FAAD2_VERSION
	    faac_cfg->outputFormat = FAAD_FMT_32BIT;
#endif
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
