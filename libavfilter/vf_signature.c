/*
 * Copyright (c) 2017 Gerion Entrup
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * MPEG-7 video signature calculation and lookup filter
 * @see http://epubs.surrey.ac.uk/531590/1/MPEG-7%20Video%20Signature%20Author%27s%20Copy.pdf
 */

#include <float.h>
#include "libavcodec/put_bits.h"
#include "libavformat/avformat.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/timestamp.h"
#include "avfilter.h"
#include "internal.h"
#include "signature.h"
#include "signature_lookup.c"

#define OFFSET(x) offsetof(SignatureContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
#define BLOCK_LCM (int64_t) 476985600

static const AVOption signature_options[] = {
    { "detectmode", "set the detectmode",
        OFFSET(mode),         AV_OPT_TYPE_INT,    {.i64 = MODE_OFF}, 0, NB_LOOKUP_MODE-1, FLAGS, "mode" },
        { "off",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = MODE_OFF},  0, 0, .flags = FLAGS, "mode" },
        { "full", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = MODE_FULL}, 0, 0, .flags = FLAGS, "mode" },
        { "fast", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = MODE_FAST}, 0, 0, .flags = FLAGS, "mode" },
    { "nb_inputs",  "number of inputs",
        OFFSET(nb_inputs),    AV_OPT_TYPE_INT,    {.i64 = 1},        1, INT_MAX,          FLAGS },
    { "filename",   "filename for output files",
        OFFSET(filename),     AV_OPT_TYPE_STRING, {.str = ""},       0, NB_FORMATS-1,     FLAGS },
    { "format",     "set output format",
        OFFSET(format),       AV_OPT_TYPE_INT,    {.i64 = FORMAT_BINARY}, 0, 1,           FLAGS , "format" },
        { "binary", 0, 0, AV_OPT_TYPE_CONST, {.i64=FORMAT_BINARY}, 0, 0, FLAGS, "format" },
        { "xml",    0, 0, AV_OPT_TYPE_CONST, {.i64=FORMAT_XML},    0, 0, FLAGS, "format" },
    { "th_d",       "threshold to detect one word as similar",
        OFFSET(thworddist),   AV_OPT_TYPE_INT,    {.i64 = 9000},     1, INT_MAX,          FLAGS },
    { "th_dc",      "threshold to detect all words as similar",
        OFFSET(thcomposdist), AV_OPT_TYPE_INT,    {.i64 = 60000},    1, INT_MAX,          FLAGS },
    { "th_xh",      "threshold to detect frames as similar",
        OFFSET(thl1),         AV_OPT_TYPE_INT,    {.i64 = 116},      1, INT_MAX,          FLAGS },
    { "th_di",      "minimum length of matching sequence in frames",
        OFFSET(thdi),         AV_OPT_TYPE_INT,    {.i64 = 0},        0, INT_MAX,          FLAGS },
    { "th_it",      "threshold for relation of good to all frames",
        OFFSET(thit),         AV_OPT_TYPE_DOUBLE, {.dbl = 0.5},    0.0, 1.0,              FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(signature);

static int query_formats(AVFilterContext *ctx)
{
    /* all formats with a separate gray value */
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_NV12, AV_PIX_FMT_NV21,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    SignatureContext *sic = ctx->priv;
    StreamContext *sc = &(sic->streamcontexts[FF_INLINK_IDX(inlink)]);

    sc->time_base = inlink->time_base;
    /* test for overflow */
    sc->divide = (((uint64_t) inlink->w/32) * (inlink->w/32 + 1) * (inlink->h/32 * inlink->h/32 + 1) > INT64_MAX / (BLOCK_LCM * 255));
    if (sc->divide) {
        av_log(ctx, AV_LOG_WARNING, "Input dimension too high for precise calculation, numbers will be rounded.\n");
    }
    sc->w = inlink->w;
    sc->h = inlink->h;
    return 0;
}

static int get_block_size(const Block *b)
{
    return (b->to.y - b->up.y + 1) * (b->to.x - b->up.x + 1);
}

static uint64_t get_block_sum(StreamContext *sc, uint64_t intpic[32][32], const Block *b)
{
    uint64_t sum = 0;

    int x0, y0, x1, y1;

    x0 = b->up.x;
    y0 = b->up.y;
    x1 = b->to.x;
    y1 = b->to.y;

    if (x0-1 >= 0 && y0-1 >= 0) {
        sum = intpic[y1][x1] + intpic[y0-1][x0-1] - intpic[y1][x0-1] - intpic[y0-1][x1];
    } else if (x0-1 >= 0) {
        sum = intpic[y1][x1] - intpic[y1][x0-1];
    } else if (y0-1 >= 0) {
        sum = intpic[y1][x1] - intpic[y0-1][x1];
    } else {
        sum = intpic[y1][x1];
    }
    return sum;
}

static int cmp(const uint64_t *a, const uint64_t *b)
{
    return *a < *b ? -1 : ( *a > *b ? 1 : 0 );
}

/**
 * sets the bit at position pos to 1 in data
 */
static void set_bit(uint8_t* data, size_t pos)
{
    uint8_t mask = 1 << 7-(pos%8);
    data[pos/8] |= mask;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *picref)
{
    AVFilterContext *ctx = inlink->dst;
    SignatureContext *sic = ctx->priv;
    StreamContext *sc = &(sic->streamcontexts[FF_INLINK_IDX(inlink)]);
    FineSignature* fs;

    static const uint8_t pot3[5] = { 3*3*3*3, 3*3*3, 3*3, 3, 1 };
    /* indexes of words : 210,217,219,274,334  44,175,233,270,273  57,70,103,237,269  100,285,295,337,354  101,102,111,275,296
    s2usw = sorted to unsorted wordvec: 44 is at index 5, 57 at index 10...
    */
    static const unsigned int wordvec[25] = {44,57,70,100,101,102,103,111,175,210,217,219,233,237,269,270,273,274,275,285,295,296,334,337,354};
    static const uint8_t      s2usw[25]   = { 5,10,11, 15, 20, 21, 12, 22,  6,  0,  1,  2,  7, 13, 14,  8,  9,  3, 23, 16, 17, 24,  4, 18, 19};

    uint8_t wordt2b[5] = { 0, 0, 0, 0, 0 }; /* word ternary to binary */
    uint64_t intpic[32][32];
    uint64_t rowcount;
    uint8_t *p = picref->data[0];
    int inti, intj;
    int *intjlut;

    uint64_t conflist[DIFFELEM_SIZE];
    int f = 0, g = 0, w = 0;
    int32_t dh1 = 1, dh2 = 1, dw1 = 1, dw2 = 1, a, b;
    int64_t denom;
    int i, j, k, ternary;
    uint64_t blocksum;
    int blocksize;
    int64_t th; /* threshold */
    int64_t sum;

    int64_t precfactor = (sc->divide) ? 65536 : BLOCK_LCM;

    /* initialize fs */
    if (sc->curfinesig) {
        fs = av_mallocz(sizeof(FineSignature));
        if (!fs)
            return AVERROR(ENOMEM);
        sc->curfinesig->next = fs;
        fs->prev = sc->curfinesig;
        sc->curfinesig = fs;
    } else {
        fs = sc->curfinesig = sc->finesiglist;
        sc->curcoarsesig1->first = fs;
    }

    fs->pts = picref->pts;
    fs->index = sc->lastindex++;

    memset(intpic, 0, sizeof(uint64_t)*32*32);
    intjlut = av_malloc_array(inlink->w, sizeof(int));
    if (!intjlut)
        return AVERROR(ENOMEM);
    for (i = 0; i < inlink->w; i++) {
        intjlut[i] = (i*32)/inlink->w;
    }

    for (i = 0; i < inlink->h; i++) {
        inti = (i*32)/inlink->h;
        for (j = 0; j < inlink->w; j++) {
            intj = intjlut[j];
            intpic[inti][intj] += p[j];
        }
        p += picref->linesize[0];
    }
    av_freep(&intjlut);

    /* The following calculates a summed area table (intpic) and brings the numbers
     * in intpic to the same denominator.
     * So you only have to handle the numinator in the following sections.
     */
    dh1 = inlink->h / 32;
    if (inlink->h % 32)
        dh2 = dh1 + 1;
    dw1 = inlink->w / 32;
    if (inlink->w % 32)
        dw2 = dw1 + 1;
    denom = (sc->divide) ? dh1 * dh2 * dw1 * dw2 : 1;

    for (i = 0; i < 32; i++) {
        rowcount = 0;
        a = 1;
        if (dh2 > 1) {
            a = ((inlink->h*(i+1))%32 == 0) ? (inlink->h*(i+1))/32 - 1 : (inlink->h*(i+1))/32;
            a -= ((inlink->h*i)%32 == 0) ? (inlink->h*i)/32 - 1 : (inlink->h*i)/32;
            a = (a == dh1)? dh2 : dh1;
        }
        for (j = 0; j < 32; j++) {
            b = 1;
            if (dw2 > 1) {
                b = ((inlink->w*(j+1))%32 == 0) ? (inlink->w*(j+1))/32 - 1 : (inlink->w*(j+1))/32;
                b -= ((inlink->w*j)%32 == 0) ? (inlink->w*j)/32 - 1 : (inlink->w*j)/32;
                b = (b == dw1)? dw2 : dw1;
            }
            rowcount += intpic[i][j] * a * b * precfactor / denom;
            if (i > 0) {
                intpic[i][j] = intpic[i-1][j] + rowcount;
            } else {
                intpic[i][j] = rowcount;
            }
        }
    }

    denom = (sc->divide) ? 1 : dh1 * dh2 * dw1 * dw2;

    for (i = 0; i < ELEMENT_COUNT; i++) {
        const ElemCat* elemcat = elements[i];
        int64_t* elemsignature;
        uint64_t* sortsignature;

        elemsignature = av_malloc_array(elemcat->elem_count, sizeof(int64_t));
        if (!elemsignature)
            return AVERROR(ENOMEM);
        sortsignature = av_malloc_array(elemcat->elem_count, sizeof(int64_t));
        if (!sortsignature) {
            av_freep(&elemsignature);
            return AVERROR(ENOMEM);
        }

        for (j = 0; j < elemcat->elem_count; j++) {
            blocksum = 0;
            blocksize = 0;
            for (k = 0; k < elemcat->left_count; k++) {
                blocksum += get_block_sum(sc, intpic, &elemcat->blocks[j*elemcat->block_count+k]);
                blocksize += get_block_size(&elemcat->blocks[j*elemcat->block_count+k]);
            }
            sum = blocksum / blocksize;
            if (elemcat->av_elem) {
                sum -= 128 * precfactor * denom;
            } else {
                blocksum = 0;
                blocksize = 0;
                for (; k < elemcat->block_count; k++) {
                    blocksum += get_block_sum(sc, intpic, &elemcat->blocks[j*elemcat->block_count+k]);
                    blocksize += get_block_size(&elemcat->blocks[j*elemcat->block_count+k]);
                }
                sum -= blocksum / blocksize;
                conflist[g++] = FFABS(sum * 8 / (precfactor * denom));
            }

            elemsignature[j] = sum;
            sortsignature[j] = FFABS(sum);
        }

        /* get threshold */
        qsort(sortsignature, elemcat->elem_count, sizeof(uint64_t), (void*) cmp);
        th = sortsignature[(int) (elemcat->elem_count*0.333)];

        /* ternarize */
        for (j = 0; j < elemcat->elem_count; j++) {
            if (elemsignature[j] < -th) {
                ternary = 0;
            } else if (elemsignature[j] <= th) {
                ternary = 1;
            } else {
                ternary = 2;
            }
            fs->framesig[f/5] += ternary * pot3[f%5];

            if (f == wordvec[w]) {
                fs->words[s2usw[w]/5] += ternary * pot3[wordt2b[s2usw[w]/5]++];
                if (w < 24)
                    w++;
            }
            f++;
        }
        av_freep(&elemsignature);
        av_freep(&sortsignature);
    }

    /* confidence */
    qsort(conflist, DIFFELEM_SIZE, sizeof(uint64_t), (void*) cmp);
    fs->confidence = FFMIN(conflist[DIFFELEM_SIZE/2], 255);

    /* coarsesignature */
    if (sc->coarsecount == 0) {
        if (sc->curcoarsesig2) {
            sc->curcoarsesig1 = av_mallocz(sizeof(CoarseSignature));
            if (!sc->curcoarsesig1)
                return AVERROR(ENOMEM);
            sc->curcoarsesig1->first = fs;
            sc->curcoarsesig2->next = sc->curcoarsesig1;
            sc->coarseend = sc->curcoarsesig1;
        }
    }
    if (sc->coarsecount == 45) {
        sc->midcoarse = 1;
        sc->curcoarsesig2 = av_mallocz(sizeof(CoarseSignature));
        if (!sc->curcoarsesig2)
            return AVERROR(ENOMEM);
        sc->curcoarsesig2->first = fs;
        sc->curcoarsesig1->next = sc->curcoarsesig2;
        sc->coarseend = sc->curcoarsesig2;
    }
    for (i = 0; i < 5; i++) {
        set_bit(sc->curcoarsesig1->data[i], fs->words[i]);
    }
    /* assuming the actual frame is the last */
    sc->curcoarsesig1->last = fs;
    if (sc->midcoarse) {
        for (i = 0; i < 5; i++) {
            set_bit(sc->curcoarsesig2->data[i], fs->words[i]);
        }
        sc->curcoarsesig2->last = fs;
    }

    sc->coarsecount = (sc->coarsecount+1)%90;

    /* debug printing finesignature */
    if (av_log_get_level() == AV_LOG_DEBUG) {
        av_log(ctx, AV_LOG_DEBUG, "input %d, confidence: %d\n", FF_INLINK_IDX(inlink), fs->confidence);

        av_log(ctx, AV_LOG_DEBUG, "words:");
        for (i = 0; i < 5; i++) {
            av_log(ctx, AV_LOG_DEBUG, " %d:", fs->words[i] );
            av_log(ctx, AV_LOG_DEBUG, " %d", fs->words[i] / pot3[0] );
            for (j = 1; j < 5; j++)
                av_log(ctx, AV_LOG_DEBUG, ",%d", fs->words[i] % pot3[j-1] / pot3[j] );
            av_log(ctx, AV_LOG_DEBUG, ";");
        }
        av_log(ctx, AV_LOG_DEBUG, "\n");

        av_log(ctx, AV_LOG_DEBUG, "framesignature:");
        for (i = 0; i < SIGELEM_SIZE/5; i++) {
            av_log(ctx, AV_LOG_DEBUG, " %d", fs->framesig[i] / pot3[0] );
            for (j = 1; j < 5; j++)
                av_log(ctx, AV_LOG_DEBUG, ",%d", fs->framesig[i] % pot3[j-1] / pot3[j] );
        }
        av_log(ctx, AV_LOG_DEBUG, "\n");
    }

    if (FF_INLINK_IDX(inlink) == 0)
        return ff_filter_frame(inlink->dst->outputs[0], picref);
    return 1;
}

static int xml_export(AVFilterContext *ctx, StreamContext *sc, const char* filename)
{
    FineSignature* fs;
    CoarseSignature* cs;
    int i, j;
    FILE* f;
    unsigned int pot3[5] = { 3*3*3*3, 3*3*3, 3*3, 3, 1 };

    f = fopen(filename, "w");
    if (!f) {
        int err = AVERROR(EINVAL);
        char buf[128];
        av_strerror(err, buf, sizeof(buf));
        av_log(ctx, AV_LOG_ERROR, "cannot open xml file %s: %s\n", filename, buf);
        return err;
    }

    /* header */
    fprintf(f, "<?xml version='1.0' encoding='ASCII' ?>\n");
    fprintf(f, "<Mpeg7 xmlns=\"urn:mpeg:mpeg7:schema:2001\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"urn:mpeg:mpeg7:schema:2001 schema/Mpeg7-2001.xsd\">\n");
    fprintf(f, "  <DescriptionUnit xsi:type=\"DescriptorCollectionType\">\n");
    fprintf(f, "    <Descriptor xsi:type=\"VideoSignatureType\">\n");
    fprintf(f, "      <VideoSignatureRegion>\n");
    fprintf(f, "        <VideoSignatureSpatialRegion>\n");
    fprintf(f, "          <Pixel>0 0 </Pixel>\n");
    fprintf(f, "          <Pixel>%d %d </Pixel>\n", sc->w - 1, sc->h - 1);
    fprintf(f, "        </VideoSignatureSpatialRegion>\n");
    fprintf(f, "        <StartFrameOfSpatialRegion>0</StartFrameOfSpatialRegion>\n");
    /* hoping num is 1, other values are vague */
    fprintf(f, "        <MediaTimeUnit>%d</MediaTimeUnit>\n", sc->time_base.den / sc->time_base.num);
    fprintf(f, "        <MediaTimeOfSpatialRegion>\n");
    fprintf(f, "          <StartMediaTimeOfSpatialRegion>0</StartMediaTimeOfSpatialRegion>\n");
    fprintf(f, "          <EndMediaTimeOfSpatialRegion>%" PRIu64 "</EndMediaTimeOfSpatialRegion>\n", sc->coarseend->last->pts);
    fprintf(f, "        </MediaTimeOfSpatialRegion>\n");

    /* coarsesignatures */
    for (cs = sc->coarsesiglist; cs; cs = cs->next) {
        fprintf(f, "        <VSVideoSegment>\n");
        fprintf(f, "          <StartFrameOfSegment>%" PRIu32 "</StartFrameOfSegment>\n", cs->first->index);
        fprintf(f, "          <EndFrameOfSegment>%" PRIu32 "</EndFrameOfSegment>\n", cs->last->index);
        fprintf(f, "          <MediaTimeOfSegment>\n");
        fprintf(f, "            <StartMediaTimeOfSegment>%" PRIu64 "</StartMediaTimeOfSegment>\n", cs->first->pts);
        fprintf(f, "            <EndMediaTimeOfSegment>%" PRIu64 "</EndMediaTimeOfSegment>\n", cs->last->pts);
        fprintf(f, "          </MediaTimeOfSegment>\n");
        for (i = 0; i < 5; i++) {
            fprintf(f, "          <BagOfWords>");
            for (j = 0; j < 31; j++) {
                uint8_t n = cs->data[i][j];
                if (j < 30) {
                    fprintf(f, "%d  %d  %d  %d  %d  %d  %d  %d  ", (n & 0x80) >> 7,
                                                                   (n & 0x40) >> 6,
                                                                   (n & 0x20) >> 5,
                                                                   (n & 0x10) >> 4,
                                                                   (n & 0x08) >> 3,
                                                                   (n & 0x04) >> 2,
                                                                   (n & 0x02) >> 1,
                                                                   (n & 0x01));
                } else {
                    /* print only 3 bit in last byte */
                    fprintf(f, "%d  %d  %d ", (n & 0x80) >> 7,
                                              (n & 0x40) >> 6,
                                              (n & 0x20) >> 5);
                }
            }
            fprintf(f, "</BagOfWords>\n");
        }
        fprintf(f, "        </VSVideoSegment>\n");
    }

    /* finesignatures */
    for (fs = sc->finesiglist; fs; fs = fs->next) {
        fprintf(f, "        <VideoFrame>\n");
        fprintf(f, "          <MediaTimeOfFrame>%" PRIu64 "</MediaTimeOfFrame>\n", fs->pts);
        /* confidence */
        fprintf(f, "          <FrameConfidence>%d</FrameConfidence>\n", fs->confidence);
        /* words */
        fprintf(f, "          <Word>");
        for (i = 0; i < 5; i++) {
            fprintf(f, "%d ", fs->words[i]);
            if (i < 4) {
                fprintf(f, " ");
            }
        }
        fprintf(f, "</Word>\n");
        /* framesignature */
        fprintf(f, "          <FrameSignature>");
        for (i = 0; i< SIGELEM_SIZE/5; i++) {
            if (i > 0) {
                fprintf(f, " ");
            }
            fprintf(f, "%d ", fs->framesig[i] / pot3[0]);
            for (j = 1; j < 5; j++)
                fprintf(f, " %d ", fs->framesig[i] % pot3[j-1] / pot3[j] );
        }
        fprintf(f, "</FrameSignature>\n");
        fprintf(f, "        </VideoFrame>\n");
    }
    fprintf(f, "      </VideoSignatureRegion>\n");
    fprintf(f, "    </Descriptor>\n");
    fprintf(f, "  </DescriptionUnit>\n");
    fprintf(f, "</Mpeg7>\n");

    fclose(f);
    return 0;
}

static int binary_export(AVFilterContext *ctx, StreamContext *sc, const char* filename)
{
    FILE* f;
    FineSignature* fs;
    CoarseSignature* cs;
    uint32_t numofsegments = (sc->lastindex + 44)/45;
    int i, j;
    PutBitContext buf;
    /* buffer + header + coarsesignatures + finesignature */
    int len = (512 + 6 * 32 + 3*16 + 2 +
        numofsegments * (4*32 + 1 + 5*243) +
        sc->lastindex * (2 + 32 + 6*8 + 608)) / 8;
    uint8_t* buffer = av_malloc_array(len, sizeof(uint8_t));
    if (!buffer)
        return AVERROR(ENOMEM);

    f = fopen(filename, "wb");
    if (!f) {
        int err = AVERROR(EINVAL);
        char buf[128];
        av_strerror(err, buf, sizeof(buf));
        av_log(ctx, AV_LOG_ERROR, "cannot open file %s: %s\n", filename, buf);
        av_freep(&buffer);
        return err;
    }
    init_put_bits(&buf, buffer, len);

    put_bits32(&buf, 1); /* NumOfSpatial Regions, only 1 supported */
    put_bits(&buf, 1, 1); /* SpatialLocationFlag, always the whole image */
    put_bits32(&buf, 0); /* PixelX,1 PixelY,1, 0,0 */
    put_bits(&buf, 16, sc->w-1 & 0xFFFF); /* PixelX,2 */
    put_bits(&buf, 16, sc->h-1 & 0xFFFF); /* PixelY,2 */
    put_bits32(&buf, 0); /* StartFrameOfSpatialRegion */
    put_bits32(&buf, sc->lastindex); /* NumOfFrames */
    /* hoping num is 1, other values are vague */
    /* den/num might be greater than 16 bit, so cutting it */
    put_bits(&buf, 16, 0xFFFF & (sc->time_base.den / sc->time_base.num)); /* MediaTimeUnit */
    put_bits(&buf, 1, 1); /* MediaTimeFlagOfSpatialRegion */
    put_bits32(&buf, 0); /* StartMediaTimeOfSpatialRegion */
    put_bits32(&buf, 0xFFFFFFFF & sc->coarseend->last->pts); /* EndMediaTimeOfSpatialRegion */
    put_bits32(&buf, numofsegments); /* NumOfSegments */
    /* coarsesignatures */
    for (cs = sc->coarsesiglist; cs; cs = cs->next) {
        put_bits32(&buf, cs->first->index); /* StartFrameOfSegment */
        put_bits32(&buf, cs->last->index); /* EndFrameOfSegment */
        put_bits(&buf, 1, 1); /* MediaTimeFlagOfSegment */
        put_bits32(&buf, 0xFFFFFFFF & cs->first->pts); /* StartMediaTimeOfSegment */
        put_bits32(&buf, 0xFFFFFFFF & cs->last->pts); /* EndMediaTimeOfSegment */
        for (i = 0; i < 5; i++) {
            /* put 243 bits ( = 7 * 32 + 19 = 8 * 28 + 19) into buffer */
            for (j = 0; j < 30; j++) {
                put_bits(&buf, 8, cs->data[i][j]);
            }
            put_bits(&buf, 3, cs->data[i][30] >> 5);
        }
    }
    /* finesignatures */
    put_bits(&buf, 1, 0); /* CompressionFlag, only 0 supported */
    for (fs = sc->finesiglist; fs; fs = fs->next) {
        put_bits(&buf, 1, 1); /* MediaTimeFlagOfFrame */
        put_bits32(&buf, 0xFFFFFFFF & fs->pts); /* MediaTimeOfFrame */
        put_bits(&buf, 8, fs->confidence); /* FrameConfidence */
        for (i = 0; i < 5; i++) {
            put_bits(&buf, 8, fs->words[i]); /* Words */
        }
        /* framesignature */
        for (i = 0; i < SIGELEM_SIZE/5; i++) {
            put_bits(&buf, 8, fs->framesig[i]);
        }
    }

    avpriv_align_put_bits(&buf);
    flush_put_bits(&buf);
    fwrite(buffer, 1, put_bits_count(&buf)/8, f);
    fclose(f);
    av_freep(&buffer);
    return 0;
}

static int export(AVFilterContext *ctx, StreamContext *sc, int input)
{
    SignatureContext* sic = ctx->priv;
    char filename[1024];

    if (sic->nb_inputs > 1) {
        /* error already handled */
        av_assert0(av_get_frame_filename(filename, sizeof(filename), sic->filename, input) == 0);
    } else {
        strcpy(filename, sic->filename);
    }
    if (sic->format == FORMAT_XML) {
        return xml_export(ctx, sc, filename);
    } else {
        return binary_export(ctx, sc, filename);
    }
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SignatureContext *sic = ctx->priv;
    StreamContext *sc, *sc2;
    MatchingInfo match;
    int i, j, ret;
    int lookup = 1; /* indicates wheather EOF of all files is reached */

    /* process all inputs */
    for (i = 0; i < sic->nb_inputs; i++){
        sc = &(sic->streamcontexts[i]);

        ret = ff_request_frame(ctx->inputs[i]);

        /* return if unexpected error occurs in input stream */
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;

        /* export signature at EOF */
        if (ret == AVERROR_EOF && !sc->exported) {
            /* export if wanted */
            if (strlen(sic->filename) > 0) {
                if (export(ctx, sc, i) < 0)
                    return ret;
            }
            sc->exported = 1;
        }
        lookup &= sc->exported;
    }

    /* signature lookup */
    if (lookup && sic->mode != MODE_OFF) {
        /* iterate over every pair */
        for (i = 0; i < sic->nb_inputs; i++) {
            sc = &(sic->streamcontexts[i]);
            for (j = i+1; j < sic->nb_inputs; j++) {
                sc2 = &(sic->streamcontexts[j]);
                match = lookup_signatures(ctx, sic, sc, sc2, sic->mode);
                if (match.score != 0) {
                    av_log(ctx, AV_LOG_INFO, "matching of video %d at %f and %d at %f, %d frames matching\n",
                            i, ((double) match.first->pts * sc->time_base.num) / sc->time_base.den,
                            j, ((double) match.second->pts * sc2->time_base.num) / sc2->time_base.den,
                            match.matchframes);
                    if (match.whole)
                        av_log(ctx, AV_LOG_INFO, "whole video matching\n");
                } else {
                    av_log(ctx, AV_LOG_INFO, "no matching of video %d and %d\n", i, j);
                }
            }
        }
    }

    return ret;
}

static av_cold int init(AVFilterContext *ctx)
{

    SignatureContext *sic = ctx->priv;
    StreamContext *sc;
    int i, ret;
    char tmp[1024];

    sic->streamcontexts = av_mallocz(sic->nb_inputs * sizeof(StreamContext));
    if (!sic->streamcontexts)
        return AVERROR(ENOMEM);

    for (i = 0; i < sic->nb_inputs; i++) {
        AVFilterPad pad = {
            .type = AVMEDIA_TYPE_VIDEO,
            .name = av_asprintf("in%d", i),
            .config_props = config_input,
            .filter_frame = filter_frame,
        };

        if (!pad.name)
            return AVERROR(ENOMEM);

        sc = &(sic->streamcontexts[i]);

        sc->lastindex = 0;
        sc->finesiglist = av_mallocz(sizeof(FineSignature));
        if (!sc->finesiglist)
            return AVERROR(ENOMEM);
        sc->curfinesig = NULL;

        sc->coarsesiglist = av_mallocz(sizeof(CoarseSignature));
        if (!sc->coarsesiglist)
            return AVERROR(ENOMEM);
        sc->curcoarsesig1 = sc->coarsesiglist;
        sc->coarseend = sc->coarsesiglist;
        sc->coarsecount = 0;
        sc->midcoarse = 0;

        if ((ret = ff_insert_inpad(ctx, i, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    /* check filename */
    if (sic->nb_inputs > 1 && strlen(sic->filename) > 0 && av_get_frame_filename(tmp, sizeof(tmp), sic->filename, 0) == -1) {
        av_log(ctx, AV_LOG_ERROR, "The filename must contain %%d or %%0nd, if you have more than one input.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}



static av_cold void uninit(AVFilterContext *ctx)
{
    SignatureContext *sic = ctx->priv;
    StreamContext *sc;
    void* tmp;
    FineSignature* finsig;
    CoarseSignature* cousig;
    int i;


    /* free the lists */
    if (sic->streamcontexts != NULL) {
        for (i = 0; i < sic->nb_inputs; i++) {
            sc = &(sic->streamcontexts[i]);
            finsig = sc->finesiglist;
            cousig = sc->coarsesiglist;

            while (finsig) {
                tmp = finsig;
                finsig = finsig->next;
                av_freep(&tmp);
            }
            sc->finesiglist = NULL;

            while (cousig) {
                tmp = cousig;
                cousig = cousig->next;
                av_freep(&tmp);
            }
            sc->coarsesiglist = NULL;
        }
        av_freep(&sic->streamcontexts);
    }
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];

    outlink->time_base = inlink->time_base;
    outlink->frame_rate = inlink->frame_rate;
    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    outlink->w = inlink->w;
    outlink->h = inlink->h;

    return 0;
}

static const AVFilterPad signature_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_signature = {
    .name          = "signature",
    .description   = NULL_IF_CONFIG_SMALL("Calculate the MPEG-7 video signature"),
    .priv_size     = sizeof(SignatureContext),
    .priv_class    = &signature_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .outputs       = signature_outputs,
    .inputs        = NULL,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS,
};
