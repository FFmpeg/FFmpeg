/*
 * LGPL
 */

/*
 * typical parsed command line:
 * msmpeg4:bitrate=720000:qmax=16
 *
 */

#include "avcodec.h"
#ifdef OPTS_MAIN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

/*
 * todo - use for decoder options also
 */

static int parse_bool(avc_config_t* c, char* s)
{
    int b = 1; /* by default -on- when present */
    if (s) {
	if (!strcasecmp(s, "off") || !strcasecmp(s, "false")
	    || !strcmp(s, "0"))
	    b = 0;
	else if (!strcasecmp(s, "on") || !strcasecmp(s, "true")
		 || !strcmp(s, "1"))
	    b = 1;
	else
	    return -1;
    }

    if (c && c->val)
	*(int*)(c->val) = b;
    return 0;
}

static int parse_double(avc_config_t* c, char* s)
{
    double d;
    if (!s)
        return -1;
    d = atof(s);
    if (c->min != c->max) {
	if (d < c->min || d > c->max) {
	    fprintf(stderr, "Option: %s double value: %f out of range <%f, %f>\n",
		    c->name, d, c->min, c->max);
	    return -1;
	}
    }
    if (c && c->val)
	*(double*)(c->val) = d;
    return 0;
}

static int parse_int(avc_config_t* c, char* s)
{
    int i;
    if (!s)
        return -1;
    i = atoi(s);
    if (c->min != c->max) {
	if (i < (int)c->min || i > (int)c->max) {
	    fprintf(stderr, "Option: %s integer value: %d out of range <%d, %d>\n",
		    c->name, i, (int)c->min, (int)c->max);
	    return -1;
	}
    }
    if (c && c->val)
	*(int*)(c->val) = i;
    return 0;
}

static int parse_string(AVCodecContext* avctx, avc_config_t* c, char* s)
{
    if (!s)
	return -1;

    if (c->type == FF_CONF_TYPE_RCOVERIDE) {
	int sf, ef, qs;
	float qf;
	if (sscanf(s, "%d,%d,%d,%f", &sf, &ef, &qs, &qf) == 4 && sf < ef) {
	    RcOverride* o;
	    *((RcOverride**)c->val) =
		realloc(*((RcOverride**)c->val),
			sizeof(RcOverride) * (avctx->rc_override_count + 1));
            o = *((RcOverride**)c->val) + avctx->rc_override_count++;
	    o->start_frame = sf;
	    o->end_frame = ef;
	    o->qscale = qs;
	    o->quality_factor = qf;

	    //printf("parsed Rc:  %d,%d,%d,%f  (%d)\n", sf,ef,qs,qf, avctx->rc_override_count);
	} else {
	    printf("incorrect/unparsable Rc: \"%s\"\n", s);
	}
    } else
	(char*)(c->val) = strdup(s);
    return 0;
}

static int parse(AVCodecContext* avctx, avc_config_t* config, char* str)
{
    while (str && *str) {
	avc_config_t* c = config;
	char* e = strchr(str, ':');
        char* p;
	if (e)
            *e++ = 0;

	p = strchr(str, '=');
	if (p)
	    *p++ = 0;

	while (c->name) {
	    if (!strcmp(c->name, str)) {
		switch (c->type & FF_CONF_TYPE_MASK) {
		case FF_CONF_TYPE_BOOL:
                    parse_bool(c, p);
                    break;
		case FF_CONF_TYPE_DOUBLE:
                    parse_double(c, p);
                    break;
		case FF_CONF_TYPE_INT:
                    parse_int(c, p);
                    break;
		case FF_CONF_TYPE_STRING:
		    parse_string(avctx, c, p);
		    break;
		default:
                    abort();
                    break;
		}
	    }
            c++;
	}
	str = e;
    }
    return 0;
}

/**
 *
 * \param avctx  where to store parsed results
 * \param str    string with options for parsing
 *               or selectional string (pick only options appliable
 *               for codec - use  ,msmpeg4, (with commas to avoid mismatch)
 * \param config allocated avc_config_t for external parsing
 *               i.e. external program might learn about all available
 *               options for given codec
 **/
void avcodec_getopt(AVCodecContext* avctx, const char* str, avc_config_t** config)
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
	    FF_CONF_TYPE_RCOVERIDE, &ctx->rc_override, 0, 0, 0, "0,0,0,0", class_h263
	},

        { NULL, NULL, 0, NULL, 0, 0, 0, NULL, NULL }
    };

    if (config) {
	*config = malloc(sizeof(cnf));
	if (*config) {
	    avc_config_t* src = cnf;
	    avc_config_t* dst = *config;
	    while (src->name) {
		if (!str || !src->supported || strstr(src->supported, str))
		    memcpy(dst++, src, sizeof(avc_config_t));
                src++;
	    }
	    memset(dst, 0, sizeof(avc_config_t));
	}
    } else if (str) {
	char* s = strdup(str);
	if (s) {
	    parse(avctx, cnf, s);
            free(s);
	}
    }
}

#ifdef OPTS_MAIN
/*
 * API test -
 * arg1: options
 * arg2: codec type
 *
 * compile standalone: make CFLAGS="-DOPTS_MAIN" opts
 */
int main(int argc, char* argv[])
{
    AVCodecContext avctx;
    avc_config_t* config;
    char* def = malloc(5000);
    const char* col = "";
    int i = 0;

    memset(&avctx, 0, sizeof(avctx));
    *def = 0;
    avcodec_getopt(&avctx, argv[1], NULL);

    avcodec_getopt(NULL, (argc > 2) ? argv[2] : NULL, &config);
    if (config)
	while (config->name) {
            int t = config->type & FF_CONF_TYPE_MASK;
	    printf("Config   %s  %s\n", config->name,
		   t == FF_CONF_TYPE_BOOL ? "bool" :
                   t == FF_CONF_TYPE_DOUBLE ? "double" :
                   t == FF_CONF_TYPE_INT ? "integer" :
		   t == FF_CONF_TYPE_STRING ? "string" :
		   "unknown??");
	    switch (t) {
	    case FF_CONF_TYPE_BOOL:
		i += sprintf(def + i, "%s%s=%s",
			     col, config->name,
			     config->defval != 0. ? "on" : "off");
                break;
	    case FF_CONF_TYPE_DOUBLE:
		i += sprintf(def + i, "%s%s=%f",
			     col, config->name, config->defval);
                break;
	    case FF_CONF_TYPE_INT:
		i += sprintf(def + i, "%s%s=%d",
			     col, config->name, (int) config->defval);
                break;
	    case FF_CONF_TYPE_STRING:
		i += sprintf(def + i, "%s%s=%s",
			     col, config->name, config->defstr);
		break;
	    }
	    col = ":";
	    config++;
	}

    printf("Default Options: %s\n", def);

    return 0;
}
#endif
