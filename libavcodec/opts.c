/*
 * LGPL
 */

/**
 * @file opts.c
 * options parser.
 * typical parsed command line:
 * msmpeg4:bitrate=720000:qmax=16
 *
 */

#include "avcodec.h"

const AVOption avoptions_common[] = {
    AVOPTION_CODEC_FLAG("bit_exact", "use only bit-exact stuff", flags, CODEC_FLAG_BITEXACT, 0),
    AVOPTION_CODEC_FLAG("mm_force", "force mm flags", dsp_mask, FF_MM_FORCE, 0),
#ifdef HAVE_MMX
    AVOPTION_CODEC_FLAG("mm_mmx", "mask MMX feature", dsp_mask, FF_MM_MMX, 0),
    AVOPTION_CODEC_FLAG("mm_3dnow", "mask 3DNow feature", dsp_mask, FF_MM_3DNOW, 0),
    AVOPTION_CODEC_FLAG("mm_mmxext", "mask MMXEXT (MMX2) feature", dsp_mask, FF_MM_MMXEXT, 0),
    AVOPTION_CODEC_FLAG("mm_sse", "mask SSE feature", dsp_mask, FF_MM_SSE, 0),
    AVOPTION_CODEC_FLAG("mm_sse2", "mask SSE2 feature", dsp_mask, FF_MM_SSE2, 0),
#endif
    AVOPTION_END()
};

const AVOption avoptions_workaround_bug[] = {
    AVOPTION_CODEC_FLAG("bug_autodetect", "workaround bug autodetection", workaround_bugs, FF_BUG_AUTODETECT, 1),
    AVOPTION_CODEC_FLAG("bug_old_msmpeg4", "workaround old msmpeg4 bug", workaround_bugs, FF_BUG_OLD_MSMPEG4, 0),
    AVOPTION_CODEC_FLAG("bug_xvid_ilace", "workaround XviD interlace bug", workaround_bugs, FF_BUG_XVID_ILACE, 0),
    AVOPTION_CODEC_FLAG("bug_ump4", "workaround ump4 bug", workaround_bugs, FF_BUG_UMP4, 0),
    AVOPTION_CODEC_FLAG("bug_no_padding", "workaround padding bug", workaround_bugs, FF_BUG_NO_PADDING, 0),
    AVOPTION_CODEC_FLAG("bug_ac_vlc", "workaround ac VLC bug", workaround_bugs, FF_BUG_AC_VLC, 0),
    AVOPTION_CODEC_FLAG("bug_qpel_chroma", "workaround qpel chroma bug", workaround_bugs, FF_BUG_QPEL_CHROMA, 0),
    AVOPTION_CODEC_FLAG("bug_std_qpel", "workaround std qpel bug", workaround_bugs, FF_BUG_STD_QPEL, 0),
    AVOPTION_CODEC_FLAG("bug_qpel_chroma2", "workaround qpel chroma2 bug", workaround_bugs, FF_BUG_QPEL_CHROMA2, 0),
    AVOPTION_CODEC_FLAG("bug_direct_blocksize", "workaround direct blocksize bug", workaround_bugs, FF_BUG_DIRECT_BLOCKSIZE, 0),
    AVOPTION_END()
};

/* avoid compatibility problems by redefining it */
static int av_strcasecmp(const char *s1, const char *s2)
{
    signed char val;
    
    for(;;) {
        val = toupper(*s1) - toupper(*s2);
        if (val != 0)
            break;
        if (*s1 != '\0')
            break;
        s1++;
        s2++;
    }
    return val;
}


static int parse_bool(const AVOption *c, char *s, int *var)
{
    int b = 1; /* by default -on- when present */
    if (s) {
	if (!av_strcasecmp(s, "off") || !av_strcasecmp(s, "false")
	    || !strcmp(s, "0"))
	    b = 0;
	else if (!av_strcasecmp(s, "on") || !av_strcasecmp(s, "true")
		 || !strcmp(s, "1"))
	    b = 1;
	else
	    return -1;
    }

    if (c->type == FF_OPT_TYPE_FLAG) {
	if (b)
	    *var |= (int)c->min;
	else
            *var &= ~(int)c->min;
    } else
	*var = b;
    return 0;
}

static int parse_double(const AVOption *c, char *s, double *var)
{
    double d;
    if (!s)
        return -1;
    d = atof(s);
    if (c->min != c->max) {
	if (d < c->min || d > c->max) {
	    av_log(NULL, AV_LOG_ERROR, "Option: %s double value: %f out of range <%f, %f>\n",
		    c->name, d, c->min, c->max);
	    return -1;
	}
    }
    *var = d;
    return 0;
}

static int parse_int(const AVOption* c, char* s, int* var)
{
    int i;
    if (!s)
        return -1;
    i = atoi(s);
    if (c->min != c->max) {
	if (i < (int)c->min || i > (int)c->max) {
	    av_log(NULL, AV_LOG_ERROR, "Option: %s integer value: %d out of range <%d, %d>\n",
		    c->name, i, (int)c->min, (int)c->max);
	    return -1;
	}
    }
    *var = i;
    return 0;
}

static int parse_string(const AVOption *c, char *s, void* strct, char **var)
{
    if (!s)
	return -1;

    if (c->type == FF_OPT_TYPE_RCOVERRIDE) {
	int sf, ef, qs;
	float qf;
	if (sscanf(s, "%d,%d,%d,%f", &sf, &ef, &qs, &qf) == 4 && sf < ef) {
	    AVCodecContext *avctx = (AVCodecContext *) strct;
	    RcOverride *o;
	    avctx->rc_override = av_realloc(avctx->rc_override,
					    sizeof(RcOverride) * (avctx->rc_override_count + 1));
	    o = avctx->rc_override + avctx->rc_override_count++;
	    o->start_frame = sf;
	    o->end_frame = ef;
	    o->qscale = qs;
	    o->quality_factor = qf;

	    //printf("parsed Rc:  %d,%d,%d,%f  (%d)\n", sf,ef,qs,qf, avctx->rc_override_count);
	} else {
	    av_log(NULL, AV_LOG_ERROR, "incorrect/unparsable Rc: \"%s\"\n", s);
	}
    } else
	*var = av_strdup(s);
    return 0;
}

int avoption_parse(void* strct, const AVOption* list, const char *opts)
{
    int r = 0;
    char* dopts = av_strdup(opts);
    if (dopts) {
        char *str = dopts;

	while (str && *str && r == 0) {
	    const AVOption *stack[FF_OPT_MAX_DEPTH];
	    const AVOption *c = list;
	    int depth = 0;
	    char* e = strchr(str, ':');
	    char* p;
	    if (e)
		*e++ = 0;

	    p = strchr(str, '=');
	    if (p)
		*p++ = 0;

            // going through option structures
	    for (;;) {
		if (!c->name) {
		    if (c->help) {
			stack[depth++] = c;
			c = (const AVOption*) c->help;
			assert(depth > FF_OPT_MAX_DEPTH);
		    } else {
			if (depth == 0)
			    break; // finished
			c = stack[--depth];
                        c++;
		    }
		} else {
		    if (!strcmp(c->name, str)) {
			void* ptr = (char*)strct + c->offset;

			switch (c->type & FF_OPT_TYPE_MASK) {
			case FF_OPT_TYPE_BOOL:
			    r = parse_bool(c, p, (int*)ptr);
			    break;
			case FF_OPT_TYPE_DOUBLE:
			    r = parse_double(c, p, (double*)ptr);
			    break;
			case FF_OPT_TYPE_INT:
			    r = parse_int(c, p, (int*)ptr);
			    break;
			case FF_OPT_TYPE_STRING:
			    r = parse_string(c, p, strct, (char**)ptr);
			    break;
			default:
			    assert(0 == 1);
			}
		    }
		    c++;
		}
	    }
	    str = e;
	}
	av_free(dopts);
    }
    return r;
}
