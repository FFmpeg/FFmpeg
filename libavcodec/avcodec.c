/**
 * @file avcodec.c
 * avcodec.
 */

#include "errno.h"
#include "avcodec.h"

#ifndef MKTAG
#define MKTAG(a,b,c,d) (a | (b << 8) | (c << 16) | (d << 24))
#endif

// private structure used to hide all internal memory allocations
// and structures used for de/encoding - end user should
// never see any complicated structure
typedef struct private_handle
{
    AVCodec* avcodec;
    AVCodecContext avcontext;
    struct private_handle* next;
    struct private_handle* prev;
} private_handle_t;

static private_handle_t* handle_first = 0;

static AVCodec* avcodec_find_by_fcc(uint32_t fcc)
{
    // translation table
    static const struct fcc_to_avcodecid {
	enum CodecID codec;
	uint32_t list[4]; // maybe we could map more fcc to same codec
    } lc[] = {
	{ CODEC_ID_H263, { MKTAG('U', '2', '6', '3'), 0 } },
	{ CODEC_ID_H263I, { MKTAG('I', '2', '6', '3'), 0 } },
	{ CODEC_ID_MSMPEG4V3, { MKTAG('D', 'I', 'V', '3'), 0 } },
	{ CODEC_ID_MPEG4, { MKTAG('D', 'I', 'V', 'X'),  MKTAG('D', 'X', '5', '0'), 0 } },
	{ CODEC_ID_MSMPEG4V2, { MKTAG('M', 'P', '4', '2'), 0 } },
	{ CODEC_ID_MJPEG, { MKTAG('M', 'J', 'P', 'G'), 0 } },
	{ CODEC_ID_MPEG1VIDEO, { MKTAG('P', 'I', 'M', '1'), 0 } },
	{ CODEC_ID_AC3, { 0x2000, 0 } },
	{ CODEC_ID_MP2, { 0x50, 0x55, 0 } },
	{ CODEC_ID_FLV1, { MKTAG('F', 'L', 'V', '1'), 0 } },

	{ CODEC_ID_NONE, {0}}
    };
    const struct fcc_to_avcodecid* c;

    for (c = lc; c->codec != CODEC_ID_NONE; c++)
    {
	int i = 0;
	while (c->list[i] != 0)
	    if (c->list[i++] == fcc)
		return avcodec_find_decoder(c->codec);
    }

    return NULL;
}

static private_handle_t* create_handle(void)
{
    private_handle_t* t = av_malloc(sizeof(private_handle_t));
    if (!t)
	return NULL;
    memset(t, 0, sizeof(*t));

    // register and fill
    if (!handle_first)
    {
	avcodec_init();
	avcodec_register_all();
        handle_first = t;
    }
    else
    {
        t->prev = handle_first->next;
	handle_first->next = t;
	t->next = handle_first;
    }

    return t;
}

static void destroy_handle(private_handle_t* handle)
{
    if (handle)
    {
	if (handle->avcodec)
	{
	    avcodec_close(&handle->avcontext);
	}
	av_free(handle);

        // count referencies
    }
}

int avcodec(void* handle, avc_cmd_t cmd, void* pin, void* pout)
{
    AVCodecContext* ctx = handle;
    switch (cmd)
    {
    case AVC_OPEN_BY_NAME:
	{
            // pin  char* codec name
	    private_handle_t* h = create_handle();
	    (private_handle_t**)pout = h;
	    if (!h)
		return -ENOMEM;
	    if (!h->avcodec)
	    {
		destroy_handle(h);
		(private_handle_t**)pout = NULL;
		return -1;// better error
	    }
            return 0;
	}
    case AVC_OPEN_BY_CODEC_ID:
	{
            // pin  uint32_t codec fourcc
	    private_handle_t* h = create_handle();
	    (private_handle_t**)pout = h;
	    if (!h)
		return -ENOMEM;

	    if (!h->avcodec)
	    {
		destroy_handle(h);
		(private_handle_t**)pout = NULL;
		return -1;// better error
	    }
            return 0;
	}
    case AVC_OPEN_BY_FOURCC:
	{
            // pin  uint32_t codec fourcc
	    private_handle_t* h = create_handle();
	    (private_handle_t**)pout = h;
	    if (!h)
		return -ENOMEM;
	    h->avcodec = avcodec_find_by_fcc((uint32_t) pin);
	    if (!h->avcodec)
	    {
		destroy_handle(h);
		(private_handle_t**)pout = NULL;
		return -1;// better error
	    }
            return 0;
	}
    case AVC_CLOSE:
	// uninit part
	// eventually close all allocated space if this was last
	// instance
	destroy_handle(handle);
	break;

    case AVC_FLUSH:
	break;

    case AVC_DECODE:
	break;

    case AVC_ENCODE:
	break;

    case AVC_GET_VERSION:
        (int*) pout = 500;
    default:
	return -1;

    }
    return 0;
}
