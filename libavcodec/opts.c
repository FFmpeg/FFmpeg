/*
 * LGPL
 */

/*
 * typical parsed command line:
 * msmpeg4:bitrate=720000:qmax=16
 *
 */

#include "avcodec.h"

/*
 * possible extension - use for decoder options
 *                    - for given codec names filter only correct
 *                      options given (could be passed with 'str')
 */

/**
 * \param avctx  where to store parsed results
 * \param str    string with options for parsing
 * \param config allocated avc_config_t for external parsing
 *               i.e. external program might learn about all available
 *               options for given codec
 **/
void avcodec_getopt(AVCodecContext* avctx, char* str, avc_config_t** config)
{
    AVCodecContext avctx_tmp;
    AVCodecContext* ctx = (avctx) ? avctx : &avctx_tmp;
    static const char* class_h263 = ",msmpeg4,";
    //"huffyuv,wmv1,msmpeg4v2,msmpeg4,mpeg4,mpeg1,mpeg1video,mjpeg,rv10,h263,h263p"

    avc_config_t cnf[] =
    {
	// FIXME: sorted by importance!!!
        // expert option should follow more common ones
	{
	    "bitrate", "desired video bitrate",
	    FF_CONF_TYPE_INT, &ctx->bit_rate, 4, 240000000, 800000, NULL, class_h263
	}, {
	    "vhq", "very high quality",
	    FF_CONF_TYPE_FLAG, &ctx->flags, 0, CODEC_FLAG_HQ, 0, NULL, class_h263
	}, {
	    "ratetol", "number of bits the bitstream is allowed to diverge from the reference"
	    "the reference can be CBR (for CBR pass1) or VBR (for pass2)",
	    FF_CONF_TYPE_INT, &ctx->bit_rate_tolerance, 4, 240000000, 8000, NULL, class_h263
	}, {
	    "qmin", "minimum quantizer", FF_CONF_TYPE_INT, &ctx->qmin, 1, 31, 2, NULL, class_h263
	}, {
	    "qmax", "maximum qunatizer", FF_CONF_TYPE_INT, &ctx->qmax, 1, 31, 31, NULL, class_h263
	}, {
	    "rc_eq", "rate control equation",
	    FF_CONF_TYPE_STRING, &ctx->rc_eq, 0, 0, 0, "tex^qComp" /* FILLME options */, class_h263
	}, {
	    "rc_minrate", "rate control minimum bitrate",
	    FF_CONF_TYPE_INT, &ctx->rc_min_rate, 4, 24000000, 0, NULL, class_h263
	}, {
	    "rc_maxrate", "rate control maximum bitrate",
	    FF_CONF_TYPE_INT, &ctx->rc_max_rate, 4, 24000000, 0, NULL, class_h263
	}, {
	    "psnr", "calculate PSNR of compressed frames",
	    FF_CONF_TYPE_FLAG, &ctx->flags, 0, CODEC_FLAG_PSNR, 0, NULL, class_h263
	}, {
	    "rc_override", "ratecontrol override (=startframe,endframe,qscale,quality_factor)",
	    FF_CONF_TYPE_RCOVERIDE, &ctx->rc_override, 0, 0, 0, NULL, class_h263
	},

        { NULL, NULL, 0, NULL, 0, 0, 0, NULL, NULL }
    };

    if (config)
    {
	*config = malloc(sizeof(cnf));
	if (*config)
            memcpy(*config, cnf, sizeof(cnf));
    }
}
