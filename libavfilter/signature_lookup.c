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
 */

#include "signature.h"

#define HOUGH_MAX_OFFSET 90
#define MAX_FRAMERATE 60

#define DIR_PREV 0
#define DIR_NEXT 1
#define DIR_PREV_END 2
#define DIR_NEXT_END 3

#define STATUS_NULL 0
#define STATUS_END_REACHED 1
#define STATUS_BEGIN_REACHED 2

static void fill_l1distlut(uint8_t lut[])
{
    int i, j, tmp_i, tmp_j,count;
    uint8_t dist;

    for (i = 0, count = 0; i < 242; i++) {
        for (j = i + 1; j < 243; j++, count++) {
            /* ternary distance between i and j */
            dist = 0;
            tmp_i = i; tmp_j = j;
            do {
                dist += FFABS((tmp_j % 3) - (tmp_i % 3));
                tmp_j /= 3;
                tmp_i /= 3;
            } while (tmp_i > 0 || tmp_j > 0);
            lut[count] = dist;
        }
    }
}

static unsigned int intersection_word(const uint8_t *first, const uint8_t *second)
{
    unsigned int val=0,i;
    for (i = 0; i < 28; i += 4) {
        val += av_popcount( (first[i]   & second[i]  ) << 24 |
                            (first[i+1] & second[i+1]) << 16 |
                            (first[i+2] & second[i+2]) << 8  |
                            (first[i+3] & second[i+3]) );
    }
    val += av_popcount( (first[28] & second[28]) << 16 |
                        (first[29] & second[29]) << 8  |
                        (first[30] & second[30]) );
    return val;
}

static unsigned int union_word(const uint8_t *first, const uint8_t *second)
{
    unsigned int val=0,i;
    for (i = 0; i < 28; i += 4) {
        val += av_popcount( (first[i]   | second[i]  ) << 24 |
                            (first[i+1] | second[i+1]) << 16 |
                            (first[i+2] | second[i+2]) << 8  |
                            (first[i+3] | second[i+3]) );
    }
    val += av_popcount( (first[28] | second[28]) << 16 |
                        (first[29] | second[29]) << 8  |
                        (first[30] | second[30]) );
    return val;
}

static unsigned int get_l1dist(AVFilterContext *ctx, SignatureContext *sc, const uint8_t *first, const uint8_t *second)
{
    unsigned int i;
    unsigned int dist = 0;
    uint8_t f, s;

    for (i = 0; i < SIGELEM_SIZE/5; i++) {
        if (first[i] != second[i]) {
            f = first[i];
            s = second[i];
            if (f > s) {
                /* little variation of gauss sum formula */
                dist += sc->l1distlut[243*242/2 - (243-s)*(242-s)/2 + f - s - 1];
            } else {
                dist += sc->l1distlut[243*242/2 - (243-f)*(242-f)/2 + s - f - 1];
            }
        }
    }
    return dist;
}

/**
 * calculates the jaccard distance and evaluates a pair of coarse signatures as good
 * @return 0 if pair is bad, 1 otherwise
 */
static int get_jaccarddist(SignatureContext *sc, CoarseSignature *first, CoarseSignature *second)
{
    int jaccarddist, i, composdist = 0, cwthcount = 0;
    for (i = 0; i < 5; i++) {
        if ((jaccarddist = intersection_word(first->data[i], second->data[i])) > 0) {
            jaccarddist /= union_word(first->data[i], second->data[i]);
        }
        if (jaccarddist >= sc->thworddist) {
            if (++cwthcount > 2) {
                /* more than half (5/2) of distances are too wide */
                return 0;
            }
        }
        composdist += jaccarddist;
        if (composdist > sc->thcomposdist) {
            return 0;
        }
    }
    return 1;
}

/**
 * step through the coarsesignatures as long as a good candidate is found
 * @return 0 if no candidate is found, 1 otherwise
 */
static int find_next_coarsecandidate(SignatureContext *sc, CoarseSignature *secondstart, CoarseSignature **first, CoarseSignature **second, int start)
{
    /* go one coarsesignature foreword */
    if (!start) {
        if ((*second)->next) {
            *second = (*second)->next;
        } else if ((*first)->next) {
            *second = secondstart;
            *first = (*first)->next;
        } else {
            return 0;
        }
    }

    while (1) {
        if (get_jaccarddist(sc, *first, *second))
            return 1;

        /* next signature */
        if ((*second)->next) {
            *second = (*second)->next;
        } else if ((*first)->next) {
            *second = secondstart;
            *first = (*first)->next;
        } else {
            return 0;
        }
    }
}

/**
 * compares framesignatures and sorts out signatures with a l1 distance above a given threshold.
 * Then tries to find out offset and differences between framerates with a hough transformation
 */
static MatchingInfo* get_matching_parameters(AVFilterContext *ctx, SignatureContext *sc, FineSignature *first, FineSignature *second)
{
    FineSignature *f, *s;
    size_t i, j, k, l, hmax = 0, score;
    int framerate, offset, l1dist;
    double m;
    MatchingInfo *cands = NULL, *c = NULL;

    struct {
        uint8_t size;
        unsigned int dist;
        FineSignature *a;
        uint8_t b_pos[COARSE_SIZE];
        FineSignature *b[COARSE_SIZE];
    } pairs[COARSE_SIZE];

    typedef struct {
        int dist;
        size_t score;
        FineSignature *a;
        FineSignature *b;
    } hspace_elem;

    /* houghspace */
    hspace_elem** hspace = av_malloc_array(MAX_FRAMERATE, sizeof(hspace_elem *));

    /* initialize houghspace */
    for (i = 0; i < MAX_FRAMERATE; i++) {
        hspace[i] = av_malloc_array(2 * HOUGH_MAX_OFFSET + 1, sizeof(hspace_elem));
        for (j = 0; j < HOUGH_MAX_OFFSET; j++) {
            hspace[i][j].score = 0;
            hspace[i][j].dist = 99999;
        }
    }

    /* l1 distances */
    for (i = 0, f = first; i < COARSE_SIZE && f->next; i++, f = f->next) {
        pairs[i].size = 0;
        pairs[i].dist = 99999;
        pairs[i].a = f;
        for (j = 0, s = second; j < COARSE_SIZE && s->next; j++, s = s->next) {
            /* l1 distance of finesignature */
            l1dist = get_l1dist(ctx, sc, f->framesig, s->framesig);
            if (l1dist < sc->thl1) {
                if (l1dist < pairs[i].dist) {
                    pairs[i].size = 1;
                    pairs[i].dist = l1dist;
                    pairs[i].b_pos[0] = j;
                    pairs[i].b[0] = s;
                } else if (l1dist == pairs[i].dist) {
                    pairs[i].b[pairs[i].size] = s;
                    pairs[i].b_pos[pairs[i].size] = j;
                    pairs[i].size++;
                }
            }
        }
    }
    /* last incomplete coarsesignature */
    if (f->next == NULL) {
        for (; i < COARSE_SIZE; i++) {
            pairs[i].size = 0;
            pairs[i].dist = 99999;
        }
    }

    /* hough transformation */
    for (i = 0; i < COARSE_SIZE; i++) {
        for (j = 0; j < pairs[i].size; j++) {
            for (k = i + 1; k < COARSE_SIZE; k++) {
                for (l = 0; l < pairs[k].size; l++) {
                    if (pairs[i].b[j] != pairs[k].b[l]) {
                        /* linear regression */
                        m = (pairs[k].b_pos[l]-pairs[i].b_pos[j]) / (k-i); /* good value between 0.0 - 2.0 */
                        framerate = (int) m*30 + 0.5; /* round up to 0 - 60 */
                        if (framerate>0 && framerate <= MAX_FRAMERATE) {
                            offset = pairs[i].b_pos[j] - ((int) m*i + 0.5); /* only second part has to be rounded up */
                            if (offset > -HOUGH_MAX_OFFSET && offset < HOUGH_MAX_OFFSET) {
                                if (pairs[i].dist < pairs[k].dist) {
                                    if (pairs[i].dist < hspace[framerate-1][offset+HOUGH_MAX_OFFSET].dist) {
                                        hspace[framerate-1][offset+HOUGH_MAX_OFFSET].dist = pairs[i].dist;
                                        hspace[framerate-1][offset+HOUGH_MAX_OFFSET].a = pairs[i].a;
                                        hspace[framerate-1][offset+HOUGH_MAX_OFFSET].b = pairs[i].b[j];
                                    }
                                } else {
                                    if (pairs[k].dist < hspace[framerate-1][offset+HOUGH_MAX_OFFSET].dist) {
                                        hspace[framerate-1][offset+HOUGH_MAX_OFFSET].dist = pairs[k].dist;
                                        hspace[framerate-1][offset+HOUGH_MAX_OFFSET].a = pairs[k].a;
                                        hspace[framerate-1][offset+HOUGH_MAX_OFFSET].b = pairs[k].b[l];
                                    }
                                }

                                score = hspace[framerate-1][offset+HOUGH_MAX_OFFSET].score + 1;
                                if (score > hmax )
                                    hmax = score;
                                hspace[framerate-1][offset+HOUGH_MAX_OFFSET].score = score;
                            }
                        }
                    }
                }
            }
        }
    }

    if (hmax > 0) {
        hmax = (int) (0.7*hmax);
        for (i = 0; i < MAX_FRAMERATE; i++) {
            for (j = 0; j < HOUGH_MAX_OFFSET; j++) {
                if (hmax < hspace[i][j].score) {
                    if (c == NULL) {
                        c = av_malloc(sizeof(MatchingInfo));
                        if (!c)
                            av_log(ctx, AV_LOG_FATAL, "Could not allocate memory");
                        cands = c;
                    } else {
                        c->next = av_malloc(sizeof(MatchingInfo));
                        if (!c->next)
                            av_log(ctx, AV_LOG_FATAL, "Could not allocate memory");
                        c = c->next;
                    }
                    c->framerateratio = (i+1.0) / 30;
                    c->score = hspace[i][j].score;
                    c->offset = j-90;
                    c->first = hspace[i][j].a;
                    c->second = hspace[i][j].b;
                    c->next = NULL;

                    /* not used */
                    c->meandist = 0;
                    c->matchframes = 0;
                    c->whole = 0;
                }
            }
        }
    }
    for (i = 0; i < MAX_FRAMERATE; i++) {
        av_freep(&hspace[i]);
    }
    av_freep(&hspace);
    return cands;
}

static int iterate_frame(double frr, FineSignature **a, FineSignature **b, int fcount, int *bcount, int dir)
{
    int step;

    /* between 1 and 2, because frr is between 1 and 2 */
    step = ((int) 0.5 + fcount     * frr) /* current frame */
          -((int) 0.5 + (fcount-1) * frr);/* last frame */

    if (dir == DIR_NEXT) {
        if (frr >= 1.0) {
            if ((*a)->next) {
                *a = (*a)->next;
            } else {
                return DIR_NEXT_END;
            }

            if (step == 1) {
                if ((*b)->next) {
                    *b = (*b)->next;
                    (*bcount)++;
                } else {
                    return DIR_NEXT_END;
                }
            } else {
                if ((*b)->next && (*b)->next->next) {
                    *b = (*b)->next->next;
                    (*bcount)++;
                } else {
                    return DIR_NEXT_END;
                }
            }
        } else {
            if ((*b)->next) {
                *b = (*b)->next;
                (*bcount)++;
            } else {
                return DIR_NEXT_END;
            }

            if (step == 1) {
                if ((*a)->next) {
                    *a = (*a)->next;
                } else {
                    return DIR_NEXT_END;
                }
            } else {
                if ((*a)->next && (*a)->next->next) {
                    *a = (*a)->next->next;
                } else {
                    return DIR_NEXT_END;
                }
            }
        }
        return DIR_NEXT;
    } else {
        if (frr >= 1.0) {
            if ((*a)->prev) {
                *a = (*a)->prev;
            } else {
                return DIR_PREV_END;
            }

            if (step == 1) {
                if ((*b)->prev) {
                    *b = (*b)->prev;
                    (*bcount)++;
                } else {
                    return DIR_PREV_END;
                }
            } else {
                if ((*b)->prev && (*b)->prev->prev) {
                    *b = (*b)->prev->prev;
                    (*bcount)++;
                } else {
                    return DIR_PREV_END;
                }
            }
        } else {
            if ((*b)->prev) {
                *b = (*b)->prev;
                (*bcount)++;
            } else {
                return DIR_PREV_END;
            }

            if (step == 1) {
                if ((*a)->prev) {
                    *a = (*a)->prev;
                } else {
                    return DIR_PREV_END;
                }
            } else {
                if ((*a)->prev && (*a)->prev->prev) {
                    *a = (*a)->prev->prev;
                } else {
                    return DIR_PREV_END;
                }
            }
        }
        return DIR_PREV;
    }
}

static MatchingInfo evaluate_parameters(AVFilterContext *ctx, SignatureContext *sc, MatchingInfo *infos, MatchingInfo bestmatch, int mode)
{
    int dist, distsum = 0, bcount = 1, dir = DIR_NEXT;
    int fcount = 0, goodfcount = 0, gooda = 0, goodb = 0;
    double meandist, minmeandist = bestmatch.meandist;
    int tolerancecount = 0;
    FineSignature *a, *b, *aprev, *bprev;
    int status = STATUS_NULL;

    for (; infos != NULL; infos = infos->next) {
        a = infos->first;
        b = infos->second;
        while (1) {
            dist = get_l1dist(ctx, sc, a->framesig, b->framesig);

            if (dist > sc->thl1) {
                if (a->confidence >= 1 || b->confidence >= 1) {
                    /* bad frame (because high different information) */
                    tolerancecount++;
                }

                if (tolerancecount > 2) {
                    a = aprev;
                    b = bprev;
                    if (dir == DIR_NEXT) {
                        /* turn around */
                        a = infos->first;
                        b = infos->second;
                        dir = DIR_PREV;
                    } else {
                        break;
                    }
                }
            } else {
                /* good frame */
                distsum += dist;
                goodfcount++;
                tolerancecount=0;

                aprev = a;
                bprev = b;

                if (a->confidence < 1) gooda++;
                if (b->confidence < 1) goodb++;
            }

            fcount++;

            dir = iterate_frame(infos->framerateratio, &a, &b, fcount, &bcount, dir);
            if (dir == DIR_NEXT_END) {
                status = STATUS_END_REACHED;
                a = infos->first;
                b = infos->second;
                dir = iterate_frame(infos->framerateratio, &a, &b, fcount, &bcount, DIR_PREV);
            }

            if (dir == DIR_PREV_END) {
                status |= STATUS_BEGIN_REACHED;
                break;
            }

            if (sc->thdi != 0 && bcount >= sc->thdi) {
                break; /* enough frames found */
            }
        }

        if (bcount < sc->thdi)
            continue; /* matching sequence is too short */
        if ((double) goodfcount / (double) fcount < sc->thit)
            continue;
        if ((double) goodfcount*0.5 < FFMAX(gooda, goodb))
            continue;

        meandist = (double) goodfcount / (double) distsum;

        if (meandist < minmeandist ||
                status == STATUS_END_REACHED | STATUS_BEGIN_REACHED ||
                mode == MODE_FAST){
            minmeandist = meandist;
            /* bestcandidate in this iteration */
            bestmatch.meandist = meandist;
            bestmatch.matchframes = bcount;
            bestmatch.framerateratio = infos->framerateratio;
            bestmatch.score = infos->score;
            bestmatch.offset = infos->offset;
            bestmatch.first = infos->first;
            bestmatch.second = infos->second;
            bestmatch.whole = 0; /* will be set to true later */
            bestmatch.next = NULL;
        }

        /* whole sequence is automatically best match */
        if (status == (STATUS_END_REACHED | STATUS_BEGIN_REACHED)) {
            bestmatch.whole = 1;
            break;
        }

        /* first matching sequence is enough, finding the best one is not necessary */
        if (mode == MODE_FAST) {
            break;
        }
    }
    return bestmatch;
}

static void sll_free(MatchingInfo *sll)
{
    void *tmp;
    while (sll) {
        tmp = sll;
        sll = sll->next;
        av_freep(&tmp);
    }
}

static MatchingInfo lookup_signatures(AVFilterContext *ctx, SignatureContext *sc, StreamContext *first, StreamContext *second, int mode)
{
    CoarseSignature *cs, *cs2;
    MatchingInfo *infos;
    MatchingInfo bestmatch;
    MatchingInfo *i;

    cs = first->coarsesiglist;
    cs2 = second->coarsesiglist;

    /* score of bestmatch is 0, if no match is found */
    bestmatch.score = 0;
    bestmatch.meandist = 99999;
    bestmatch.whole = 0;

    fill_l1distlut(sc->l1distlut);

    /* stage 1: coarsesignature matching */
    if (find_next_coarsecandidate(sc, second->coarsesiglist, &cs, &cs2, 1) == 0)
        return bestmatch; /* no candidate found */
    do {
        av_log(ctx, AV_LOG_DEBUG, "Stage 1: got coarsesignature pair. "
               "indices of first frame: %"PRIu32" and %"PRIu32"\n",
               cs->first->index, cs2->first->index);
        /* stage 2: l1-distance and hough-transform */
        av_log(ctx, AV_LOG_DEBUG, "Stage 2: calculate matching parameters\n");
        infos = get_matching_parameters(ctx, sc, cs->first, cs2->first);
        if (av_log_get_level() == AV_LOG_DEBUG) {
            for (i = infos; i != NULL; i = i->next) {
                av_log(ctx, AV_LOG_DEBUG, "Stage 2: matching pair at %"PRIu32" and %"PRIu32", "
                       "ratio %f, offset %d\n", i->first->index, i->second->index,
                       i->framerateratio, i->offset);
            }
        }
        /* stage 3: evaluation */
        av_log(ctx, AV_LOG_DEBUG, "Stage 3: evaluate\n");
        if (infos) {
            bestmatch = evaluate_parameters(ctx, sc, infos, bestmatch, mode);
            av_log(ctx, AV_LOG_DEBUG, "Stage 3: best matching pair at %"PRIu32" and %"PRIu32", "
                   "ratio %f, offset %d, score %d, %d frames matching\n",
                   bestmatch.first->index, bestmatch.second->index,
                   bestmatch.framerateratio, bestmatch.offset, bestmatch.score, bestmatch.matchframes);
            sll_free(infos);
        }
    } while (find_next_coarsecandidate(sc, second->coarsesiglist, &cs, &cs2, 0) && !bestmatch.whole);
    return bestmatch;

}
