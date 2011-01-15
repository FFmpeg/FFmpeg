/**
 * @file
 * Vorbis I decoder
 * @author Denes Balatoni  ( dbalatoni programozo hu )
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

#undef V_DEBUG
//#define V_DEBUG
//#define AV_DEBUG(...) av_log(NULL, AV_LOG_INFO, __VA_ARGS__)

#include <math.h>

#define ALT_BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"
#include "dsputil.h"
#include "fft.h"

#include "vorbis.h"
#include "xiph.h"

#define V_NB_BITS 8
#define V_NB_BITS2 11
#define V_MAX_VLCS (1 << 16)
#define V_MAX_PARTITIONS (1 << 20)

#ifndef V_DEBUG
#define AV_DEBUG(...)
#endif

#undef NDEBUG
#include <assert.h>

typedef struct {
    uint_fast8_t dimensions;
    uint_fast8_t lookup_type;
    uint_fast8_t maxdepth;
    VLC vlc;
    float *codevectors;
    unsigned int nb_bits;
} vorbis_codebook;

typedef union  vorbis_floor_u  vorbis_floor_data;
typedef struct vorbis_floor0_s vorbis_floor0;
typedef struct vorbis_floor1_s vorbis_floor1;
struct vorbis_context_s;
typedef
int (* vorbis_floor_decode_func)
    (struct vorbis_context_s *, vorbis_floor_data *, float *);
typedef struct {
    uint_fast8_t floor_type;
    vorbis_floor_decode_func decode;
    union vorbis_floor_u {
        struct vorbis_floor0_s {
            uint_fast8_t  order;
            uint_fast16_t rate;
            uint_fast16_t bark_map_size;
            int_fast32_t *map[2];
            uint_fast32_t map_size[2];
            uint_fast8_t  amplitude_bits;
            uint_fast8_t  amplitude_offset;
            uint_fast8_t  num_books;
            uint_fast8_t *book_list;
            float        *lsp;
        } t0;
        struct vorbis_floor1_s {
            uint_fast8_t partitions;
            uint_fast8_t maximum_class;
            uint_fast8_t partition_class[32];
            uint_fast8_t class_dimensions[16];
            uint_fast8_t class_subclasses[16];
            uint_fast8_t class_masterbook[16];
            int_fast16_t subclass_books[16][8];
            uint_fast8_t multiplier;
            uint_fast16_t x_list_dim;
            vorbis_floor1_entry *list;
        } t1;
    } data;
} vorbis_floor;

typedef struct {
    uint_fast16_t type;
    uint_fast32_t begin;
    uint_fast32_t end;
    uint_fast32_t partition_size;
    uint_fast8_t  classifications;
    uint_fast8_t  classbook;
    int_fast16_t  books[64][8];
    uint_fast8_t  maxpass;
} vorbis_residue;

typedef struct {
    uint_fast8_t  submaps;
    uint_fast16_t coupling_steps;
    uint_fast8_t *magnitude;
    uint_fast8_t *angle;
    uint_fast8_t *mux;
    uint_fast8_t  submap_floor[16];
    uint_fast8_t  submap_residue[16];
} vorbis_mapping;

typedef struct {
    uint_fast8_t  blockflag;
    uint_fast16_t windowtype;
    uint_fast16_t transformtype;
    uint_fast8_t  mapping;
} vorbis_mode;

typedef struct vorbis_context_s {
    AVCodecContext *avccontext;
    GetBitContext gb;
    DSPContext dsp;

    FFTContext mdct[2];
    uint_fast8_t  first_frame;
    uint_fast32_t version;
    uint_fast8_t  audio_channels;
    uint_fast32_t audio_samplerate;
    uint_fast32_t bitrate_maximum;
    uint_fast32_t bitrate_nominal;
    uint_fast32_t bitrate_minimum;
    uint_fast32_t blocksize[2];
    const float  *win[2];
    uint_fast16_t codebook_count;
    vorbis_codebook *codebooks;
    uint_fast8_t  floor_count;
    vorbis_floor *floors;
    uint_fast8_t  residue_count;
    vorbis_residue *residues;
    uint_fast8_t  mapping_count;
    vorbis_mapping *mappings;
    uint_fast8_t  mode_count;
    vorbis_mode  *modes;
    uint_fast8_t  mode_number; // mode number for the current packet
    uint_fast8_t  previous_window;
    float        *channel_residues;
    float        *channel_floors;
    float        *saved;
    uint_fast32_t add_bias; // for float->int conversion
    uint_fast32_t exp_bias;
} vorbis_context;

/* Helper functions */

#define BARK(x) \
    (13.1f * atan(0.00074f * (x)) + 2.24f * atan(1.85e-8f * (x) * (x)) + 1e-4f * (x))

static const char idx_err_str[] = "Index value %d out of range (0 - %d) for %s at %s:%i\n";
#define VALIDATE_INDEX(idx, limit) \
    if (idx >= limit) {\
        av_log(vc->avccontext, AV_LOG_ERROR,\
               idx_err_str,\
               (int)(idx), (int)(limit - 1), #idx, __FILE__, __LINE__);\
        return -1;\
    }
#define GET_VALIDATED_INDEX(idx, bits, limit) \
    {\
        idx = get_bits(gb, bits);\
        VALIDATE_INDEX(idx, limit)\
    }

static float vorbisfloat2float(uint_fast32_t val)
{
    double mant = val & 0x1fffff;
    long exp    = (val & 0x7fe00000L) >> 21;
    if (val & 0x80000000)
        mant = -mant;
    return ldexp(mant, exp - 20 - 768);
}


// Free all allocated memory -----------------------------------------

static void vorbis_free(vorbis_context *vc)
{
    int_fast16_t i;

    av_freep(&vc->channel_residues);
    av_freep(&vc->channel_floors);
    av_freep(&vc->saved);

    av_freep(&vc->residues);
    av_freep(&vc->modes);

    ff_mdct_end(&vc->mdct[0]);
    ff_mdct_end(&vc->mdct[1]);

    for (i = 0; i < vc->codebook_count; ++i) {
        av_free(vc->codebooks[i].codevectors);
        free_vlc(&vc->codebooks[i].vlc);
    }
    av_freep(&vc->codebooks);

    for (i = 0; i < vc->floor_count; ++i) {
        if (vc->floors[i].floor_type == 0) {
            av_free(vc->floors[i].data.t0.map[0]);
            av_free(vc->floors[i].data.t0.map[1]);
            av_free(vc->floors[i].data.t0.book_list);
            av_free(vc->floors[i].data.t0.lsp);
        } else {
            av_free(vc->floors[i].data.t1.list);
        }
    }
    av_freep(&vc->floors);

    for (i = 0; i < vc->mapping_count; ++i) {
        av_free(vc->mappings[i].magnitude);
        av_free(vc->mappings[i].angle);
        av_free(vc->mappings[i].mux);
    }
    av_freep(&vc->mappings);
}

// Parse setup header -------------------------------------------------

// Process codebooks part

static int vorbis_parse_setup_hdr_codebooks(vorbis_context *vc)
{
    uint_fast16_t cb;
    uint8_t  *tmp_vlc_bits;
    uint32_t *tmp_vlc_codes;
    GetBitContext *gb = &vc->gb;

    vc->codebook_count = get_bits(gb, 8) + 1;

    AV_DEBUG(" Codebooks: %d \n", vc->codebook_count);

    vc->codebooks = av_mallocz(vc->codebook_count * sizeof(vorbis_codebook));
    tmp_vlc_bits  = av_mallocz(V_MAX_VLCS * sizeof(uint8_t));
    tmp_vlc_codes = av_mallocz(V_MAX_VLCS * sizeof(uint32_t));

    for (cb = 0; cb < vc->codebook_count; ++cb) {
        vorbis_codebook *codebook_setup = &vc->codebooks[cb];
        uint_fast8_t ordered;
        uint_fast32_t t, used_entries = 0;
        uint_fast32_t entries;

        AV_DEBUG(" %d. Codebook \n", cb);

        if (get_bits(gb, 24) != 0x564342) {
            av_log(vc->avccontext, AV_LOG_ERROR, " %"PRIdFAST16". Codebook setup data corrupt. \n", cb);
            goto error;
        }

        codebook_setup->dimensions=get_bits(gb, 16);
        if (codebook_setup->dimensions > 16 || codebook_setup->dimensions == 0) {
            av_log(vc->avccontext, AV_LOG_ERROR, " %"PRIdFAST16". Codebook's dimension is invalid (%d). \n", cb, codebook_setup->dimensions);
            goto error;
        }
        entries = get_bits(gb, 24);
        if (entries > V_MAX_VLCS) {
            av_log(vc->avccontext, AV_LOG_ERROR, " %"PRIdFAST16". Codebook has too many entries (%"PRIdFAST32"). \n", cb, entries);
            goto error;
        }

        ordered = get_bits1(gb);

        AV_DEBUG(" codebook_dimensions %d, codebook_entries %d \n", codebook_setup->dimensions, entries);

        if (!ordered) {
            uint_fast16_t ce;
            uint_fast8_t  flag;
            uint_fast8_t  sparse = get_bits1(gb);

            AV_DEBUG(" not ordered \n");

            if (sparse) {
                AV_DEBUG(" sparse \n");

                used_entries = 0;
                for (ce = 0; ce < entries; ++ce) {
                    flag = get_bits1(gb);
                    if (flag) {
                        tmp_vlc_bits[ce] = get_bits(gb, 5) + 1;
                        ++used_entries;
                    } else
                        tmp_vlc_bits[ce] = 0;
                }
            } else {
                AV_DEBUG(" not sparse \n");

                used_entries = entries;
                for (ce = 0; ce < entries; ++ce)
                    tmp_vlc_bits[ce] = get_bits(gb, 5) + 1;
            }
        } else {
            uint_fast16_t current_entry = 0;
            uint_fast8_t current_length = get_bits(gb, 5)+1;

            AV_DEBUG(" ordered, current length: %d \n", current_length);  //FIXME

            used_entries = entries;
            for (; current_entry < used_entries && current_length <= 32; ++current_length) {
                uint_fast16_t i, number;

                AV_DEBUG(" number bits: %d ", ilog(entries - current_entry));

                number = get_bits(gb, ilog(entries - current_entry));

                AV_DEBUG(" number: %d \n", number);

                for (i = current_entry; i < number+current_entry; ++i)
                    if (i < used_entries)
                        tmp_vlc_bits[i] = current_length;

                current_entry+=number;
            }
            if (current_entry>used_entries) {
                av_log(vc->avccontext, AV_LOG_ERROR, " More codelengths than codes in codebook. \n");
                goto error;
            }
        }

        codebook_setup->lookup_type = get_bits(gb, 4);

        AV_DEBUG(" lookup type: %d : %s \n", codebook_setup->lookup_type, codebook_setup->lookup_type ? "vq" : "no lookup");

// If the codebook is used for (inverse) VQ, calculate codevectors.

        if (codebook_setup->lookup_type == 1) {
            uint_fast16_t i, j, k;
            uint_fast16_t codebook_lookup_values = ff_vorbis_nth_root(entries, codebook_setup->dimensions);
            uint_fast16_t codebook_multiplicands[codebook_lookup_values];

            float codebook_minimum_value = vorbisfloat2float(get_bits_long(gb, 32));
            float codebook_delta_value   = vorbisfloat2float(get_bits_long(gb, 32));
            uint_fast8_t codebook_value_bits = get_bits(gb, 4)+1;
            uint_fast8_t codebook_sequence_p = get_bits1(gb);

            AV_DEBUG(" We expect %d numbers for building the codevectors. \n", codebook_lookup_values);
            AV_DEBUG("  delta %f minmum %f \n", codebook_delta_value, codebook_minimum_value);

            for (i = 0; i < codebook_lookup_values; ++i) {
                codebook_multiplicands[i] = get_bits(gb, codebook_value_bits);

                AV_DEBUG(" multiplicands*delta+minmum : %e \n", (float)codebook_multiplicands[i]*codebook_delta_value+codebook_minimum_value);
                AV_DEBUG(" multiplicand %d \n", codebook_multiplicands[i]);
            }

// Weed out unused vlcs and build codevector vector
            codebook_setup->codevectors = used_entries ? av_mallocz(used_entries*codebook_setup->dimensions * sizeof(float)) : NULL;
            for (j = 0, i = 0; i < entries; ++i) {
                uint_fast8_t dim = codebook_setup->dimensions;

                if (tmp_vlc_bits[i]) {
                    float last = 0.0;
                    uint_fast32_t lookup_offset = i;

#ifdef V_DEBUG
                    av_log(vc->avccontext, AV_LOG_INFO, "Lookup offset %d ,", i);
#endif

                    for (k = 0; k < dim; ++k) {
                        uint_fast32_t multiplicand_offset = lookup_offset % codebook_lookup_values;
                        codebook_setup->codevectors[j * dim + k] = codebook_multiplicands[multiplicand_offset] * codebook_delta_value + codebook_minimum_value + last;
                        if (codebook_sequence_p)
                            last = codebook_setup->codevectors[j * dim + k];
                        lookup_offset/=codebook_lookup_values;
                    }
                    tmp_vlc_bits[j] = tmp_vlc_bits[i];

#ifdef V_DEBUG
                    av_log(vc->avccontext, AV_LOG_INFO, "real lookup offset %d, vector: ", j);
                    for (k = 0; k < dim; ++k)
                        av_log(vc->avccontext, AV_LOG_INFO, " %f ", codebook_setup->codevectors[j * dim + k]);
                    av_log(vc->avccontext, AV_LOG_INFO, "\n");
#endif

                    ++j;
                }
            }
            if (j != used_entries) {
                av_log(vc->avccontext, AV_LOG_ERROR, "Bug in codevector vector building code. \n");
                goto error;
            }
            entries = used_entries;
        } else if (codebook_setup->lookup_type >= 2) {
            av_log(vc->avccontext, AV_LOG_ERROR, "Codebook lookup type not supported. \n");
            goto error;
        }

// Initialize VLC table
        if (ff_vorbis_len2vlc(tmp_vlc_bits, tmp_vlc_codes, entries)) {
            av_log(vc->avccontext, AV_LOG_ERROR, " Invalid code lengths while generating vlcs. \n");
            goto error;
        }
        codebook_setup->maxdepth = 0;
        for (t = 0; t < entries; ++t)
            if (tmp_vlc_bits[t] >= codebook_setup->maxdepth)
                codebook_setup->maxdepth = tmp_vlc_bits[t];

        if (codebook_setup->maxdepth > 3 * V_NB_BITS)
            codebook_setup->nb_bits = V_NB_BITS2;
        else
            codebook_setup->nb_bits = V_NB_BITS;

        codebook_setup->maxdepth = (codebook_setup->maxdepth+codebook_setup->nb_bits - 1) / codebook_setup->nb_bits;

        if (init_vlc(&codebook_setup->vlc, codebook_setup->nb_bits, entries, tmp_vlc_bits, sizeof(*tmp_vlc_bits), sizeof(*tmp_vlc_bits), tmp_vlc_codes, sizeof(*tmp_vlc_codes), sizeof(*tmp_vlc_codes), INIT_VLC_LE)) {
            av_log(vc->avccontext, AV_LOG_ERROR, " Error generating vlc tables. \n");
            goto error;
        }
    }

    av_free(tmp_vlc_bits);
    av_free(tmp_vlc_codes);
    return 0;

// Error:
error:
    av_free(tmp_vlc_bits);
    av_free(tmp_vlc_codes);
    return -1;
}

// Process time domain transforms part (unused in Vorbis I)

static int vorbis_parse_setup_hdr_tdtransforms(vorbis_context *vc)
{
    GetBitContext *gb = &vc->gb;
    uint_fast8_t i;
    uint_fast8_t vorbis_time_count = get_bits(gb, 6) + 1;

    for (i = 0; i < vorbis_time_count; ++i) {
        uint_fast16_t vorbis_tdtransform = get_bits(gb, 16);

        AV_DEBUG(" Vorbis time domain transform %d: %d \n", vorbis_time_count, vorbis_tdtransform);

        if (vorbis_tdtransform) {
            av_log(vc->avccontext, AV_LOG_ERROR, "Vorbis time domain transform data nonzero. \n");
            return -1;
        }
    }
    return 0;
}

// Process floors part

static int vorbis_floor0_decode(vorbis_context *vc,
                                vorbis_floor_data *vfu, float *vec);
static void create_map(vorbis_context *vc, uint_fast8_t floor_number);
static int vorbis_floor1_decode(vorbis_context *vc,
                                vorbis_floor_data *vfu, float *vec);
static int vorbis_parse_setup_hdr_floors(vorbis_context *vc)
{
    GetBitContext *gb = &vc->gb;
    uint_fast16_t i,j,k;

    vc->floor_count = get_bits(gb, 6) + 1;

    vc->floors = av_mallocz(vc->floor_count * sizeof(vorbis_floor));

    for (i = 0; i < vc->floor_count; ++i) {
        vorbis_floor *floor_setup = &vc->floors[i];

        floor_setup->floor_type = get_bits(gb, 16);

        AV_DEBUG(" %d. floor type %d \n", i, floor_setup->floor_type);

        if (floor_setup->floor_type == 1) {
            uint_fast8_t  maximum_class = 0;
            uint_fast8_t  rangebits;
            uint_fast32_t rangemax;
            uint_fast16_t floor1_values = 2;

            floor_setup->decode = vorbis_floor1_decode;

            floor_setup->data.t1.partitions = get_bits(gb, 5);

            AV_DEBUG(" %d.floor: %d partitions \n", i, floor_setup->data.t1.partitions);

            for (j = 0; j < floor_setup->data.t1.partitions; ++j) {
                floor_setup->data.t1.partition_class[j] = get_bits(gb, 4);
                if (floor_setup->data.t1.partition_class[j] > maximum_class)
                    maximum_class = floor_setup->data.t1.partition_class[j];

                AV_DEBUG(" %d. floor %d partition class %d \n", i, j, floor_setup->data.t1.partition_class[j]);

            }

            AV_DEBUG(" maximum class %d \n", maximum_class);

            floor_setup->data.t1.maximum_class = maximum_class;

            for (j = 0; j <= maximum_class; ++j) {
                floor_setup->data.t1.class_dimensions[j] = get_bits(gb, 3) + 1;
                floor_setup->data.t1.class_subclasses[j] = get_bits(gb, 2);

                AV_DEBUG(" %d floor %d class dim: %d subclasses %d \n", i, j, floor_setup->data.t1.class_dimensions[j], floor_setup->data.t1.class_subclasses[j]);

                if (floor_setup->data.t1.class_subclasses[j]) {
                    GET_VALIDATED_INDEX(floor_setup->data.t1.class_masterbook[j], 8, vc->codebook_count)

                    AV_DEBUG("   masterbook: %d \n", floor_setup->data.t1.class_masterbook[j]);
                }

                for (k = 0; k < (1 << floor_setup->data.t1.class_subclasses[j]); ++k) {
                    int16_t bits = get_bits(gb, 8) - 1;
                    if (bits != -1)
                        VALIDATE_INDEX(bits, vc->codebook_count)
                    floor_setup->data.t1.subclass_books[j][k] = bits;

                    AV_DEBUG("    book %d. : %d \n", k, floor_setup->data.t1.subclass_books[j][k]);
                }
            }

            floor_setup->data.t1.multiplier = get_bits(gb, 2) + 1;
            floor_setup->data.t1.x_list_dim = 2;

            for (j = 0; j < floor_setup->data.t1.partitions; ++j)
                floor_setup->data.t1.x_list_dim+=floor_setup->data.t1.class_dimensions[floor_setup->data.t1.partition_class[j]];

            floor_setup->data.t1.list = av_mallocz(floor_setup->data.t1.x_list_dim * sizeof(vorbis_floor1_entry));


            rangebits = get_bits(gb, 4);
            rangemax = (1 << rangebits);
            if (rangemax > vc->blocksize[1] / 2) {
                av_log(vc->avccontext, AV_LOG_ERROR,
                       "Floor value is too large for blocksize: %d (%d)\n",
                       rangemax, vc->blocksize[1] / 2);
                return -1;
            }
            floor_setup->data.t1.list[0].x = 0;
            floor_setup->data.t1.list[1].x = rangemax;

            for (j = 0; j < floor_setup->data.t1.partitions; ++j) {
                for (k = 0; k < floor_setup->data.t1.class_dimensions[floor_setup->data.t1.partition_class[j]]; ++k, ++floor1_values) {
                    floor_setup->data.t1.list[floor1_values].x = get_bits(gb, rangebits);

                    AV_DEBUG(" %d. floor1 Y coord. %d \n", floor1_values, floor_setup->data.t1.list[floor1_values].x);
                }
            }

// Precalculate order of x coordinates - needed for decode
            ff_vorbis_ready_floor1_list(floor_setup->data.t1.list, floor_setup->data.t1.x_list_dim);
        } else if (floor_setup->floor_type == 0) {
            uint_fast8_t max_codebook_dim = 0;

            floor_setup->decode = vorbis_floor0_decode;

            floor_setup->data.t0.order          = get_bits(gb,  8);
            floor_setup->data.t0.rate           = get_bits(gb, 16);
            floor_setup->data.t0.bark_map_size  = get_bits(gb, 16);
            floor_setup->data.t0.amplitude_bits = get_bits(gb,  6);
            /* zero would result in a div by zero later *
             * 2^0 - 1 == 0                             */
            if (floor_setup->data.t0.amplitude_bits == 0) {
              av_log(vc->avccontext, AV_LOG_ERROR,
                     "Floor 0 amplitude bits is 0.\n");
              return -1;
            }
            floor_setup->data.t0.amplitude_offset = get_bits(gb, 8);
            floor_setup->data.t0.num_books        = get_bits(gb, 4) + 1;

            /* allocate mem for booklist */
            floor_setup->data.t0.book_list =
                av_malloc(floor_setup->data.t0.num_books);
            if (!floor_setup->data.t0.book_list)
                return -1;
            /* read book indexes */
            {
                int idx;
                uint_fast8_t book_idx;
                for (idx = 0; idx < floor_setup->data.t0.num_books; ++idx) {
                    GET_VALIDATED_INDEX(floor_setup->data.t0.book_list[idx], 8, vc->codebook_count)
                    if (vc->codebooks[book_idx].dimensions > max_codebook_dim)
                        max_codebook_dim = vc->codebooks[book_idx].dimensions;
                }
            }

            create_map(vc, i);

            /* allocate mem for lsp coefficients */
            {
                /* codebook dim is for padding if codebook dim doesn't *
                 * divide order+1 then we need to read more data       */
                floor_setup->data.t0.lsp =
                    av_malloc((floor_setup->data.t0.order+1 + max_codebook_dim)
                              * sizeof(float));
                if (!floor_setup->data.t0.lsp)
                    return -1;
            }

#ifdef V_DEBUG /* debug output parsed headers */
            AV_DEBUG("floor0 order: %u\n", floor_setup->data.t0.order);
            AV_DEBUG("floor0 rate: %u\n", floor_setup->data.t0.rate);
            AV_DEBUG("floor0 bark map size: %u\n",
              floor_setup->data.t0.bark_map_size);
            AV_DEBUG("floor0 amplitude bits: %u\n",
              floor_setup->data.t0.amplitude_bits);
            AV_DEBUG("floor0 amplitude offset: %u\n",
              floor_setup->data.t0.amplitude_offset);
            AV_DEBUG("floor0 number of books: %u\n",
              floor_setup->data.t0.num_books);
            AV_DEBUG("floor0 book list pointer: %p\n",
              floor_setup->data.t0.book_list);
            {
              int idx;
              for (idx = 0; idx < floor_setup->data.t0.num_books; ++idx) {
                  AV_DEBUG("  Book %d: %u\n",
                           idx+1,
                           floor_setup->data.t0.book_list[idx]);
              }
            }
#endif
        } else {
            av_log(vc->avccontext, AV_LOG_ERROR, "Invalid floor type!\n");
            return -1;
        }
    }
    return 0;
}

// Process residues part

static int vorbis_parse_setup_hdr_residues(vorbis_context *vc)
{
    GetBitContext *gb = &vc->gb;
    uint_fast8_t i, j, k;

    vc->residue_count = get_bits(gb, 6)+1;
    vc->residues      = av_mallocz(vc->residue_count * sizeof(vorbis_residue));

    AV_DEBUG(" There are %d residues. \n", vc->residue_count);

    for (i = 0; i < vc->residue_count; ++i) {
        vorbis_residue *res_setup = &vc->residues[i];
        uint_fast8_t cascade[64];
        uint_fast8_t high_bits;
        uint_fast8_t low_bits;

        res_setup->type = get_bits(gb, 16);

        AV_DEBUG(" %d. residue type %d \n", i, res_setup->type);

        res_setup->begin          = get_bits(gb, 24);
        res_setup->end            = get_bits(gb, 24);
        res_setup->partition_size = get_bits(gb, 24) + 1;
        /* Validations to prevent a buffer overflow later. */
        if (res_setup->begin>res_setup->end ||
            res_setup->end>vc->blocksize[1] / (res_setup->type == 2 ? 1 : 2) ||
            (res_setup->end-res_setup->begin) / res_setup->partition_size > V_MAX_PARTITIONS) {
            av_log(vc->avccontext, AV_LOG_ERROR, "partition out of bounds: type, begin, end, size, blocksize: %"PRIdFAST16", %"PRIdFAST32", %"PRIdFAST32", %"PRIdFAST32", %"PRIdFAST32"\n", res_setup->type, res_setup->begin, res_setup->end, res_setup->partition_size, vc->blocksize[1] / 2);
            return -1;
        }

        res_setup->classifications = get_bits(gb, 6) + 1;
        GET_VALIDATED_INDEX(res_setup->classbook, 8, vc->codebook_count)

        AV_DEBUG("    begin %d end %d part.size %d classif.s %d classbook %d \n", res_setup->begin, res_setup->end, res_setup->partition_size,
          res_setup->classifications, res_setup->classbook);

        for (j = 0; j < res_setup->classifications; ++j) {
            high_bits = 0;
            low_bits  = get_bits(gb, 3);
            if (get_bits1(gb))
                high_bits = get_bits(gb, 5);
            cascade[j] = (high_bits << 3) + low_bits;

            AV_DEBUG("     %d class casscade depth: %d \n", j, ilog(cascade[j]));
        }

        res_setup->maxpass = 0;
        for (j = 0; j < res_setup->classifications; ++j) {
            for (k = 0; k < 8; ++k) {
                if (cascade[j]&(1 << k)) {
                    GET_VALIDATED_INDEX(res_setup->books[j][k], 8, vc->codebook_count)

                    AV_DEBUG("     %d class casscade depth %d book: %d \n", j, k, res_setup->books[j][k]);

                    if (k>res_setup->maxpass)
                        res_setup->maxpass = k;
                } else {
                    res_setup->books[j][k] = -1;
                }
            }
        }
    }
    return 0;
}

// Process mappings part

static int vorbis_parse_setup_hdr_mappings(vorbis_context *vc)
{
    GetBitContext *gb = &vc->gb;
    uint_fast8_t i, j;

    vc->mapping_count = get_bits(gb, 6)+1;
    vc->mappings      = av_mallocz(vc->mapping_count * sizeof(vorbis_mapping));

    AV_DEBUG(" There are %d mappings. \n", vc->mapping_count);

    for (i = 0; i < vc->mapping_count; ++i) {
        vorbis_mapping *mapping_setup = &vc->mappings[i];

        if (get_bits(gb, 16)) {
            av_log(vc->avccontext, AV_LOG_ERROR, "Other mappings than type 0 are not compliant with the Vorbis I specification. \n");
            return -1;
        }
        if (get_bits1(gb)) {
            mapping_setup->submaps = get_bits(gb, 4) + 1;
        } else {
            mapping_setup->submaps = 1;
        }

        if (get_bits1(gb)) {
            mapping_setup->coupling_steps = get_bits(gb, 8) + 1;
            mapping_setup->magnitude      = av_mallocz(mapping_setup->coupling_steps * sizeof(uint_fast8_t));
            mapping_setup->angle          = av_mallocz(mapping_setup->coupling_steps * sizeof(uint_fast8_t));
            for (j = 0; j < mapping_setup->coupling_steps; ++j) {
                GET_VALIDATED_INDEX(mapping_setup->magnitude[j], ilog(vc->audio_channels - 1), vc->audio_channels)
                GET_VALIDATED_INDEX(mapping_setup->angle[j],     ilog(vc->audio_channels - 1), vc->audio_channels)
            }
        } else {
            mapping_setup->coupling_steps = 0;
        }

        AV_DEBUG("   %d mapping coupling steps: %d \n", i, mapping_setup->coupling_steps);

        if (get_bits(gb, 2)) {
            av_log(vc->avccontext, AV_LOG_ERROR, "%d. mapping setup data invalid. \n", i);
            return -1; // following spec.
        }

        if (mapping_setup->submaps>1) {
            mapping_setup->mux = av_mallocz(vc->audio_channels * sizeof(uint_fast8_t));
            for (j = 0; j < vc->audio_channels; ++j)
                mapping_setup->mux[j] = get_bits(gb, 4);
        }

        for (j = 0; j < mapping_setup->submaps; ++j) {
            skip_bits(gb, 8); // FIXME check?
            GET_VALIDATED_INDEX(mapping_setup->submap_floor[j],   8, vc->floor_count)
            GET_VALIDATED_INDEX(mapping_setup->submap_residue[j], 8, vc->residue_count)

            AV_DEBUG("   %d mapping %d submap : floor %d, residue %d \n", i, j, mapping_setup->submap_floor[j], mapping_setup->submap_residue[j]);
        }
    }
    return 0;
}

// Process modes part

static void create_map(vorbis_context *vc, uint_fast8_t floor_number)
{
    vorbis_floor *floors = vc->floors;
    vorbis_floor0 *vf;
    int idx;
    int_fast8_t blockflag;
    int_fast32_t *map;
    int_fast32_t n; //TODO: could theoretically be smaller?

    for (blockflag = 0; blockflag < 2; ++blockflag) {
        n = vc->blocksize[blockflag] / 2;
        floors[floor_number].data.t0.map[blockflag] =
            av_malloc((n+1) * sizeof(int_fast32_t)); // n + sentinel

        map =  floors[floor_number].data.t0.map[blockflag];
        vf  = &floors[floor_number].data.t0;

        for (idx = 0; idx < n; ++idx) {
            map[idx] = floor(BARK((vf->rate * idx) / (2.0f * n)) *
                             ((vf->bark_map_size) /
                              BARK(vf->rate / 2.0f)));
            if (vf->bark_map_size-1 < map[idx])
                map[idx] = vf->bark_map_size - 1;
        }
        map[n] = -1;
        vf->map_size[blockflag] = n;
    }

#   ifdef V_DEBUG
    for (idx = 0; idx <= n; ++idx) {
        AV_DEBUG("floor0 map: map at pos %d is %d\n",
                 idx, map[idx]);
    }
#   endif
}

static int vorbis_parse_setup_hdr_modes(vorbis_context *vc)
{
    GetBitContext *gb = &vc->gb;
    uint_fast8_t i;

    vc->mode_count = get_bits(gb, 6) + 1;
    vc->modes      = av_mallocz(vc->mode_count * sizeof(vorbis_mode));

    AV_DEBUG(" There are %d modes.\n", vc->mode_count);

    for (i = 0; i < vc->mode_count; ++i) {
        vorbis_mode *mode_setup = &vc->modes[i];

        mode_setup->blockflag     = get_bits1(gb);
        mode_setup->windowtype    = get_bits(gb, 16); //FIXME check
        mode_setup->transformtype = get_bits(gb, 16); //FIXME check
        GET_VALIDATED_INDEX(mode_setup->mapping, 8, vc->mapping_count);

        AV_DEBUG(" %d mode: blockflag %d, windowtype %d, transformtype %d, mapping %d \n", i, mode_setup->blockflag, mode_setup->windowtype, mode_setup->transformtype, mode_setup->mapping);
    }
    return 0;
}

// Process the whole setup header using the functions above

static int vorbis_parse_setup_hdr(vorbis_context *vc)
{
    GetBitContext *gb = &vc->gb;

    if ((get_bits(gb, 8) != 'v') || (get_bits(gb, 8) != 'o') ||
        (get_bits(gb, 8) != 'r') || (get_bits(gb, 8) != 'b') ||
        (get_bits(gb, 8) != 'i') || (get_bits(gb, 8) != 's')) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (no vorbis signature). \n");
        return -1;
    }

    if (vorbis_parse_setup_hdr_codebooks(vc)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (codebooks). \n");
        return -2;
    }
    if (vorbis_parse_setup_hdr_tdtransforms(vc)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (time domain transforms). \n");
        return -3;
    }
    if (vorbis_parse_setup_hdr_floors(vc)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (floors). \n");
        return -4;
    }
    if (vorbis_parse_setup_hdr_residues(vc)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (residues). \n");
        return -5;
    }
    if (vorbis_parse_setup_hdr_mappings(vc)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (mappings). \n");
        return -6;
    }
    if (vorbis_parse_setup_hdr_modes(vc)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (modes). \n");
        return -7;
    }
    if (!get_bits1(gb)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (framing flag). \n");
        return -8; // framing flag bit unset error
    }

    return 0;
}

// Process the identification header

static int vorbis_parse_id_hdr(vorbis_context *vc)
{
    GetBitContext *gb = &vc->gb;
    uint_fast8_t bl0, bl1;

    if ((get_bits(gb, 8) != 'v') || (get_bits(gb, 8) != 'o') ||
        (get_bits(gb, 8) != 'r') || (get_bits(gb, 8) != 'b') ||
        (get_bits(gb, 8) != 'i') || (get_bits(gb, 8) != 's')) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis id header packet corrupt (no vorbis signature). \n");
        return -1;
    }

    vc->version        = get_bits_long(gb, 32);    //FIXME check 0
    vc->audio_channels = get_bits(gb, 8);
    if (vc->audio_channels <= 0) {
        av_log(vc->avccontext, AV_LOG_ERROR, "Invalid number of channels\n");
        return -1;
    }
    vc->audio_samplerate = get_bits_long(gb, 32);
    if (vc->audio_samplerate <= 0) {
        av_log(vc->avccontext, AV_LOG_ERROR, "Invalid samplerate\n");
        return -1;
    }
    vc->bitrate_maximum = get_bits_long(gb, 32);
    vc->bitrate_nominal = get_bits_long(gb, 32);
    vc->bitrate_minimum = get_bits_long(gb, 32);
    bl0 = get_bits(gb, 4);
    bl1 = get_bits(gb, 4);
    vc->blocksize[0] = (1 << bl0);
    vc->blocksize[1] = (1 << bl1);
    if (bl0 > 13 || bl0 < 6 || bl1 > 13 || bl1 < 6 || bl1 < bl0) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis id header packet corrupt (illegal blocksize). \n");
        return -3;
    }
    // output format int16
    if (vc->blocksize[1] / 2 * vc->audio_channels * 2 > AVCODEC_MAX_AUDIO_FRAME_SIZE) {
        av_log(vc->avccontext, AV_LOG_ERROR, "Vorbis channel count makes "
               "output packets too large.\n");
        return -4;
    }
    vc->win[0] = ff_vorbis_vwin[bl0 - 6];
    vc->win[1] = ff_vorbis_vwin[bl1 - 6];

    if ((get_bits1(gb)) == 0) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis id header packet corrupt (framing flag not set). \n");
        return -2;
    }

    vc->channel_residues =  av_malloc((vc->blocksize[1]  / 2) * vc->audio_channels * sizeof(float));
    vc->channel_floors   =  av_malloc((vc->blocksize[1]  / 2) * vc->audio_channels * sizeof(float));
    vc->saved            =  av_mallocz((vc->blocksize[1] / 4) * vc->audio_channels * sizeof(float));
    vc->previous_window  = 0;

    ff_mdct_init(&vc->mdct[0], bl0, 1, vc->exp_bias ? -(1 << 15) : -1.0);
    ff_mdct_init(&vc->mdct[1], bl1, 1, vc->exp_bias ? -(1 << 15) : -1.0);

    AV_DEBUG(" vorbis version %d \n audio_channels %d \n audio_samplerate %d \n bitrate_max %d \n bitrate_nom %d \n bitrate_min %d \n blk_0 %d blk_1 %d \n ",
            vc->version, vc->audio_channels, vc->audio_samplerate, vc->bitrate_maximum, vc->bitrate_nominal, vc->bitrate_minimum, vc->blocksize[0], vc->blocksize[1]);

/*
    BLK = vc->blocksize[0];
    for (i = 0; i < BLK / 2; ++i) {
        vc->win[0][i] = sin(0.5*3.14159265358*(sin(((float)i + 0.5) / (float)BLK*3.14159265358))*(sin(((float)i + 0.5) / (float)BLK*3.14159265358)));
    }
*/

    return 0;
}

// Process the extradata using the functions above (identification header, setup header)

static av_cold int vorbis_decode_init(AVCodecContext *avccontext)
{
    vorbis_context *vc = avccontext->priv_data ;
    uint8_t *headers   = avccontext->extradata;
    int headers_len    = avccontext->extradata_size;
    uint8_t *header_start[3];
    int header_len[3];
    GetBitContext *gb = &(vc->gb);
    int hdr_type;

    vc->avccontext = avccontext;
    dsputil_init(&vc->dsp, avccontext);

    if (vc->dsp.float_to_int16_interleave == ff_float_to_int16_interleave_c) {
        vc->add_bias = 385;
        vc->exp_bias = 0;
    } else {
        vc->add_bias = 0;
        vc->exp_bias = 15 << 23;
    }

    if (!headers_len) {
        av_log(avccontext, AV_LOG_ERROR, "Extradata missing.\n");
        return -1;
    }

    if (ff_split_xiph_headers(headers, headers_len, 30, header_start, header_len) < 0) {
        av_log(avccontext, AV_LOG_ERROR, "Extradata corrupt.\n");
        return -1;
    }

    init_get_bits(gb, header_start[0], header_len[0]*8);
    hdr_type = get_bits(gb, 8);
    if (hdr_type != 1) {
        av_log(avccontext, AV_LOG_ERROR, "First header is not the id header.\n");
        return -1;
    }
    if (vorbis_parse_id_hdr(vc)) {
        av_log(avccontext, AV_LOG_ERROR, "Id header corrupt.\n");
        vorbis_free(vc);
        return -1;
    }

    init_get_bits(gb, header_start[2], header_len[2]*8);
    hdr_type = get_bits(gb, 8);
    if (hdr_type != 5) {
        av_log(avccontext, AV_LOG_ERROR, "Third header is not the setup header.\n");
        vorbis_free(vc);
        return -1;
    }
    if (vorbis_parse_setup_hdr(vc)) {
        av_log(avccontext, AV_LOG_ERROR, "Setup header corrupt.\n");
        vorbis_free(vc);
        return -1;
    }

    if (vc->audio_channels > 8)
        avccontext->channel_layout = 0;
    else
        avccontext->channel_layout = ff_vorbis_channel_layouts[vc->audio_channels - 1];

    avccontext->channels    = vc->audio_channels;
    avccontext->sample_rate = vc->audio_samplerate;
    avccontext->frame_size  = FFMIN(vc->blocksize[0], vc->blocksize[1]) >> 2;
    avccontext->sample_fmt  = SAMPLE_FMT_S16;

    return 0 ;
}

// Decode audiopackets -------------------------------------------------

// Read and decode floor

static int vorbis_floor0_decode(vorbis_context *vc,
                                vorbis_floor_data *vfu, float *vec)
{
    vorbis_floor0 *vf = &vfu->t0;
    float *lsp = vf->lsp;
    uint_fast32_t amplitude;
    uint_fast32_t book_idx;
    uint_fast8_t blockflag = vc->modes[vc->mode_number].blockflag;

    amplitude = get_bits(&vc->gb, vf->amplitude_bits);
    if (amplitude > 0) {
        float last = 0;
        uint_fast16_t lsp_len = 0;
        uint_fast16_t idx;
        vorbis_codebook codebook;

        book_idx = get_bits(&vc->gb, ilog(vf->num_books));
        if (book_idx >= vf->num_books) {
            av_log(vc->avccontext, AV_LOG_ERROR,
                    "floor0 dec: booknumber too high!\n");
            book_idx =  0;
            //FIXME: look above
        }
        AV_DEBUG("floor0 dec: booknumber: %u\n", book_idx);
        codebook = vc->codebooks[vf->book_list[book_idx]];
        /* Invalid codebook! */
        if (!codebook.codevectors)
            return -1;

        while (lsp_len<vf->order) {
            int vec_off;

            AV_DEBUG("floor0 dec: book dimension: %d\n", codebook.dimensions);
            AV_DEBUG("floor0 dec: maximum depth: %d\n", codebook.maxdepth);
            /* read temp vector */
            vec_off = get_vlc2(&vc->gb, codebook.vlc.table,
                               codebook.nb_bits, codebook.maxdepth)
                      * codebook.dimensions;
            AV_DEBUG("floor0 dec: vector offset: %d\n", vec_off);
            /* copy each vector component and add last to it */
            for (idx = 0; idx < codebook.dimensions; ++idx)
                lsp[lsp_len+idx] = codebook.codevectors[vec_off+idx] + last;
            last = lsp[lsp_len+idx-1]; /* set last to last vector component */

            lsp_len += codebook.dimensions;
        }
#ifdef V_DEBUG
        /* DEBUG: output lsp coeffs */
        {
            int idx;
            for (idx = 0; idx < lsp_len; ++idx)
                AV_DEBUG("floor0 dec: coeff at %d is %f\n", idx, lsp[idx]);
        }
#endif

        /* synthesize floor output vector */
        {
            int i;
            int order = vf->order;
            float wstep = M_PI / vf->bark_map_size;

            for (i = 0; i < order; i++)
                lsp[i] = 2.0f * cos(lsp[i]);

            AV_DEBUG("floor0 synth: map_size = %d; m = %d; wstep = %f\n",
                     vf->map_size, order, wstep);

            i = 0;
            while (i < vf->map_size[blockflag]) {
                int j, iter_cond = vf->map[blockflag][i];
                float p = 0.5f;
                float q = 0.5f;
                float two_cos_w = 2.0f * cos(wstep * iter_cond); // needed all times

                /* similar part for the q and p products */
                for (j = 0; j + 1 < order; j += 2) {
                    q *= lsp[j]     - two_cos_w;
                    p *= lsp[j + 1] - two_cos_w;
                }
                if (j == order) { // even order
                    p *= p * (2.0f - two_cos_w);
                    q *= q * (2.0f + two_cos_w);
                } else { // odd order
                    q *= two_cos_w-lsp[j]; // one more time for q

                    /* final step and square */
                    p *= p * (4.f - two_cos_w * two_cos_w);
                    q *= q;
                }

                /* calculate linear floor value */
                {
                    q = exp((((amplitude*vf->amplitude_offset) /
                              (((1 << vf->amplitude_bits) - 1) * sqrt(p + q)))
                             - vf->amplitude_offset) * .11512925f);
                }

                /* fill vector */
                do {
                    vec[i] = q; ++i;
                } while (vf->map[blockflag][i] == iter_cond);
            }
        }
    } else {
        /* this channel is unused */
        return 1;
    }

    AV_DEBUG(" Floor0 decoded\n");

    return 0;
}

static int vorbis_floor1_decode(vorbis_context *vc,
                                vorbis_floor_data *vfu, float *vec)
{
    vorbis_floor1 *vf = &vfu->t1;
    GetBitContext *gb = &vc->gb;
    uint_fast16_t range_v[4] = { 256, 128, 86, 64 };
    uint_fast16_t range = range_v[vf->multiplier-1];
    uint_fast16_t floor1_Y[vf->x_list_dim];
    uint_fast16_t floor1_Y_final[vf->x_list_dim];
    int floor1_flag[vf->x_list_dim];
    uint_fast8_t class_;
    uint_fast8_t cdim;
    uint_fast8_t cbits;
    uint_fast8_t csub;
    uint_fast8_t cval;
    int_fast16_t book;
    uint_fast16_t offset;
    uint_fast16_t i,j;
    /*u*/int_fast16_t adx, ady, off, predicted; // WTF ? dy/adx =  (unsigned)dy/adx ?
    int_fast16_t dy, err;


    if (!get_bits1(gb)) // silence
        return 1;

// Read values (or differences) for the floor's points

    floor1_Y[0] = get_bits(gb, ilog(range - 1));
    floor1_Y[1] = get_bits(gb, ilog(range - 1));

    AV_DEBUG("floor 0 Y %d floor 1 Y %d \n", floor1_Y[0], floor1_Y[1]);

    offset = 2;
    for (i = 0; i < vf->partitions; ++i) {
        class_ = vf->partition_class[i];
        cdim   = vf->class_dimensions[class_];
        cbits  = vf->class_subclasses[class_];
        csub = (1 << cbits) - 1;
        cval = 0;

        AV_DEBUG("Cbits %d \n", cbits);

        if (cbits) // this reads all subclasses for this partition's class
            cval = get_vlc2(gb, vc->codebooks[vf->class_masterbook[class_]].vlc.table,
                            vc->codebooks[vf->class_masterbook[class_]].nb_bits, 3);

        for (j = 0; j < cdim; ++j) {
            book = vf->subclass_books[class_][cval & csub];

            AV_DEBUG("book %d Cbits %d cval %d  bits:%d \n", book, cbits, cval, get_bits_count(gb));

            cval = cval >> cbits;
            if (book > -1) {
                floor1_Y[offset+j] = get_vlc2(gb, vc->codebooks[book].vlc.table,
                vc->codebooks[book].nb_bits, 3);
            } else {
                floor1_Y[offset+j] = 0;
            }

            AV_DEBUG(" floor(%d) = %d \n", vf->list[offset+j].x, floor1_Y[offset+j]);
        }
        offset+=cdim;
    }

// Amplitude calculation from the differences

    floor1_flag[0] = 1;
    floor1_flag[1] = 1;
    floor1_Y_final[0] = floor1_Y[0];
    floor1_Y_final[1] = floor1_Y[1];

    for (i = 2; i < vf->x_list_dim; ++i) {
        uint_fast16_t val, highroom, lowroom, room;
        uint_fast16_t high_neigh_offs;
        uint_fast16_t low_neigh_offs;

        low_neigh_offs  = vf->list[i].low;
        high_neigh_offs = vf->list[i].high;
        dy  = floor1_Y_final[high_neigh_offs] - floor1_Y_final[low_neigh_offs];  // render_point begin
        adx = vf->list[high_neigh_offs].x - vf->list[low_neigh_offs].x;
        ady = FFABS(dy);
        err = ady * (vf->list[i].x - vf->list[low_neigh_offs].x);
        off = (int16_t)err / (int16_t)adx;
        if (dy < 0) {
            predicted = floor1_Y_final[low_neigh_offs] - off;
        } else {
            predicted = floor1_Y_final[low_neigh_offs] + off;
        } // render_point end

        val = floor1_Y[i];
        highroom = range-predicted;
        lowroom  = predicted;
        if (highroom < lowroom) {
            room = highroom * 2;
        } else {
            room = lowroom * 2;   // SPEC mispelling
        }
        if (val) {
            floor1_flag[low_neigh_offs]  = 1;
            floor1_flag[high_neigh_offs] = 1;
            floor1_flag[i]               = 1;
            if (val >= room) {
                if (highroom > lowroom) {
                    floor1_Y_final[i] = val - lowroom + predicted;
                } else {
                    floor1_Y_final[i] = predicted - val + highroom - 1;
                }
            } else {
                if (val & 1) {
                    floor1_Y_final[i] = predicted - (val + 1) / 2;
                } else {
                    floor1_Y_final[i] = predicted + val / 2;
                }
            }
        } else {
            floor1_flag[i]    = 0;
            floor1_Y_final[i] = predicted;
        }

        AV_DEBUG(" Decoded floor(%d) = %d / val %d \n", vf->list[i].x, floor1_Y_final[i], val);
    }

// Curve synth - connect the calculated dots and convert from dB scale FIXME optimize ?

    ff_vorbis_floor1_render_list(vf->list, vf->x_list_dim, floor1_Y_final, floor1_flag, vf->multiplier, vec, vf->list[1].x);

    AV_DEBUG(" Floor decoded\n");

    return 0;
}

// Read and decode residue

static av_always_inline int vorbis_residue_decode_internal(vorbis_context *vc,
                                                           vorbis_residue *vr,
                                                           uint_fast8_t ch,
                                                           uint_fast8_t *do_not_decode,
                                                           float *vec,
                                                           uint_fast16_t vlen,
                                                           int vr_type)
{
    GetBitContext *gb = &vc->gb;
    uint_fast8_t c_p_c = vc->codebooks[vr->classbook].dimensions;
    uint_fast16_t n_to_read = vr->end-vr->begin;
    uint_fast16_t ptns_to_read = n_to_read/vr->partition_size;
    uint_fast8_t classifs[ptns_to_read*vc->audio_channels];
    uint_fast8_t pass;
    uint_fast8_t ch_used;
    uint_fast8_t i,j,l;
    uint_fast16_t k;

    if (vr_type == 2) {
        for (j = 1; j < ch; ++j)
            do_not_decode[0] &= do_not_decode[j];  // FIXME - clobbering input
        if (do_not_decode[0])
            return 0;
        ch_used = 1;
    } else {
        ch_used = ch;
    }

    AV_DEBUG(" residue type 0/1/2 decode begin, ch: %d  cpc %d  \n", ch, c_p_c);

    for (pass = 0; pass <= vr->maxpass; ++pass) { // FIXME OPTIMIZE?
        uint_fast16_t voffset;
        uint_fast16_t partition_count;
        uint_fast16_t j_times_ptns_to_read;

        voffset = vr->begin;
        for (partition_count = 0; partition_count < ptns_to_read;) {  // SPEC        error
            if (!pass) {
                uint_fast32_t inverse_class = ff_inverse[vr->classifications];
                for (j_times_ptns_to_read = 0, j = 0; j < ch_used; ++j) {
                    if (!do_not_decode[j]) {
                        uint_fast32_t temp = get_vlc2(gb, vc->codebooks[vr->classbook].vlc.table,
                        vc->codebooks[vr->classbook].nb_bits, 3);

                        AV_DEBUG("Classword: %d \n", temp);

                        assert(vr->classifications > 1 && temp <= 65536); //needed for inverse[]
                        for (i = 0; i < c_p_c; ++i) {
                            uint_fast32_t temp2;

                            temp2 = (((uint_fast64_t)temp) * inverse_class) >> 32;
                            if (partition_count + c_p_c - 1 - i < ptns_to_read)
                                classifs[j_times_ptns_to_read + partition_count + c_p_c - 1 - i] = temp - temp2 * vr->classifications;
                            temp = temp2;
                        }
                    }
                    j_times_ptns_to_read += ptns_to_read;
                }
            }
            for (i = 0; (i < c_p_c) && (partition_count < ptns_to_read); ++i) {
                for (j_times_ptns_to_read = 0, j = 0; j < ch_used; ++j) {
                    uint_fast16_t voffs;

                    if (!do_not_decode[j]) {
                        uint_fast8_t vqclass = classifs[j_times_ptns_to_read+partition_count];
                        int_fast16_t vqbook = vr->books[vqclass][pass];

                        if (vqbook >= 0 && vc->codebooks[vqbook].codevectors) {
                            uint_fast16_t coffs;
                            unsigned dim =  vc->codebooks[vqbook].dimensions; // not uint_fast8_t: 64bit is slower here on amd64
                            uint_fast16_t step = dim == 1 ? vr->partition_size
                                                          : FASTDIV(vr->partition_size, dim);
                            vorbis_codebook codebook = vc->codebooks[vqbook];

                            if (vr_type == 0) {

                                voffs = voffset+j*vlen;
                                for (k = 0; k < step; ++k) {
                                    coffs = get_vlc2(gb, codebook.vlc.table, codebook.nb_bits, 3) * dim;
                                    for (l = 0; l < dim; ++l)
                                        vec[voffs + k + l * step] += codebook.codevectors[coffs + l];  // FPMATH
                                }
                            } else if (vr_type == 1) {
                                voffs = voffset + j * vlen;
                                for (k = 0; k < step; ++k) {
                                    coffs = get_vlc2(gb, codebook.vlc.table, codebook.nb_bits, 3) * dim;
                                    for (l = 0; l < dim; ++l, ++voffs) {
                                        vec[voffs]+=codebook.codevectors[coffs+l];  // FPMATH

                                        AV_DEBUG(" pass %d offs: %d curr: %f change: %f cv offs.: %d  \n", pass, voffs, vec[voffs], codebook.codevectors[coffs+l], coffs);
                                    }
                                }
                            } else if (vr_type == 2 && ch == 2 && (voffset & 1) == 0 && (dim & 1) == 0) { // most frequent case optimized
                                voffs = voffset >> 1;

                                if (dim == 2) {
                                    for (k = 0; k < step; ++k) {
                                        coffs = get_vlc2(gb, codebook.vlc.table, codebook.nb_bits, 3) * 2;
                                        vec[voffs + k       ] += codebook.codevectors[coffs    ];  // FPMATH
                                        vec[voffs + k + vlen] += codebook.codevectors[coffs + 1];  // FPMATH
                                    }
                                } else if (dim == 4) {
                                    for (k = 0; k < step; ++k, voffs += 2) {
                                        coffs = get_vlc2(gb, codebook.vlc.table, codebook.nb_bits, 3) * 4;
                                        vec[voffs           ] += codebook.codevectors[coffs    ];  // FPMATH
                                        vec[voffs + 1       ] += codebook.codevectors[coffs + 2];  // FPMATH
                                        vec[voffs + vlen    ] += codebook.codevectors[coffs + 1];  // FPMATH
                                        vec[voffs + vlen + 1] += codebook.codevectors[coffs + 3];  // FPMATH
                                    }
                                } else
                                for (k = 0; k < step; ++k) {
                                    coffs = get_vlc2(gb, codebook.vlc.table, codebook.nb_bits, 3) * dim;
                                    for (l = 0; l < dim; l += 2, voffs++) {
                                        vec[voffs       ] += codebook.codevectors[coffs + l    ];  // FPMATH
                                        vec[voffs + vlen] += codebook.codevectors[coffs + l + 1];  // FPMATH

                                        AV_DEBUG(" pass %d offs: %d curr: %f change: %f cv offs.: %d+%d  \n", pass, voffset / ch + (voffs % ch) * vlen, vec[voffset / ch + (voffs % ch) * vlen], codebook.codevectors[coffs + l], coffs, l);
                                    }
                                }

                            } else if (vr_type == 2) {
                                voffs = voffset;

                                for (k = 0; k < step; ++k) {
                                    coffs = get_vlc2(gb, codebook.vlc.table, codebook.nb_bits, 3) * dim;
                                    for (l = 0; l < dim; ++l, ++voffs) {
                                        vec[voffs / ch + (voffs % ch) * vlen] += codebook.codevectors[coffs + l];  // FPMATH FIXME use if and counter instead of / and %

                                        AV_DEBUG(" pass %d offs: %d curr: %f change: %f cv offs.: %d+%d  \n", pass, voffset / ch + (voffs % ch) * vlen, vec[voffset / ch + (voffs % ch) * vlen], codebook.codevectors[coffs + l], coffs, l);
                                    }
                                }
                            }
                        }
                    }
                    j_times_ptns_to_read += ptns_to_read;
                }
                ++partition_count;
                voffset += vr->partition_size;
            }
        }
    }
    return 0;
}

static inline int vorbis_residue_decode(vorbis_context *vc, vorbis_residue *vr,
                                        uint_fast8_t ch,
                                        uint_fast8_t *do_not_decode,
                                        float *vec, uint_fast16_t vlen)
{
    if (vr->type == 2)
        return vorbis_residue_decode_internal(vc, vr, ch, do_not_decode, vec, vlen, 2);
    else if (vr->type == 1)
        return vorbis_residue_decode_internal(vc, vr, ch, do_not_decode, vec, vlen, 1);
    else if (vr->type == 0)
        return vorbis_residue_decode_internal(vc, vr, ch, do_not_decode, vec, vlen, 0);
    else {
        av_log(vc->avccontext, AV_LOG_ERROR, " Invalid residue type while residue decode?! \n");
        return -1;
    }
}

void vorbis_inverse_coupling(float *mag, float *ang, int blocksize)
{
    int i;
    for (i = 0;  i < blocksize;  i++) {
        if (mag[i] > 0.0) {
            if (ang[i] > 0.0) {
                ang[i] = mag[i] - ang[i];
            } else {
                float temp = ang[i];
                ang[i]     = mag[i];
                mag[i]    += temp;
            }
        } else {
            if (ang[i] > 0.0) {
                ang[i] += mag[i];
            } else {
                float temp = ang[i];
                ang[i]     = mag[i];
                mag[i]    -= temp;
            }
        }
    }
}

static void copy_normalize(float *dst, float *src, int len, int exp_bias,
                           float add_bias)
{
    int i;
    if (exp_bias) {
        memcpy(dst, src, len * sizeof(float));
    } else {
        for (i = 0; i < len; i++)
            dst[i] = src[i] + add_bias;
    }
}

// Decode the audio packet using the functions above

static int vorbis_parse_audio_packet(vorbis_context *vc)
{
    GetBitContext *gb = &vc->gb;

    uint_fast8_t previous_window = vc->previous_window;
    uint_fast8_t mode_number;
    uint_fast8_t blockflag;
    uint_fast16_t blocksize;
    int_fast32_t i,j;
    uint_fast8_t no_residue[vc->audio_channels];
    uint_fast8_t do_not_decode[vc->audio_channels];
    vorbis_mapping *mapping;
    float *ch_res_ptr   = vc->channel_residues;
    float *ch_floor_ptr = vc->channel_floors;
    uint_fast8_t res_chan[vc->audio_channels];
    uint_fast8_t res_num = 0;
    int_fast16_t retlen  = 0;
    float fadd_bias = vc->add_bias;

    if (get_bits1(gb)) {
        av_log(vc->avccontext, AV_LOG_ERROR, "Not a Vorbis I audio packet.\n");
        return -1; // packet type not audio
    }

    if (vc->mode_count == 1) {
        mode_number = 0;
    } else {
        GET_VALIDATED_INDEX(mode_number, ilog(vc->mode_count-1), vc->mode_count)
    }
    vc->mode_number = mode_number;
    mapping = &vc->mappings[vc->modes[mode_number].mapping];

    AV_DEBUG(" Mode number: %d , mapping: %d , blocktype %d \n", mode_number, vc->modes[mode_number].mapping, vc->modes[mode_number].blockflag);

    blockflag = vc->modes[mode_number].blockflag;
    blocksize = vc->blocksize[blockflag];
    if (blockflag)
        skip_bits(gb, 2); // previous_window, next_window

    memset(ch_res_ptr,   0, sizeof(float) * vc->audio_channels * blocksize / 2); //FIXME can this be removed ?
    memset(ch_floor_ptr, 0, sizeof(float) * vc->audio_channels * blocksize / 2); //FIXME can this be removed ?

// Decode floor

    for (i = 0; i < vc->audio_channels; ++i) {
        vorbis_floor *floor;
        int ret;
        if (mapping->submaps > 1) {
            floor = &vc->floors[mapping->submap_floor[mapping->mux[i]]];
        } else {
            floor = &vc->floors[mapping->submap_floor[0]];
        }

        ret = floor->decode(vc, &floor->data, ch_floor_ptr);

        if (ret < 0) {
            av_log(vc->avccontext, AV_LOG_ERROR, "Invalid codebook in vorbis_floor_decode.\n");
            return -1;
        }
        no_residue[i] = ret;
        ch_floor_ptr += blocksize / 2;
    }

// Nonzero vector propagate

    for (i = mapping->coupling_steps - 1; i >= 0; --i) {
        if (!(no_residue[mapping->magnitude[i]] & no_residue[mapping->angle[i]])) {
            no_residue[mapping->magnitude[i]] = 0;
            no_residue[mapping->angle[i]]     = 0;
        }
    }

// Decode residue

    for (i = 0; i < mapping->submaps; ++i) {
        vorbis_residue *residue;
        uint_fast8_t ch = 0;

        for (j = 0; j < vc->audio_channels; ++j) {
            if ((mapping->submaps == 1) || (i == mapping->mux[j])) {
                res_chan[j] = res_num;
                if (no_residue[j]) {
                    do_not_decode[ch] = 1;
                } else {
                    do_not_decode[ch] = 0;
                }
                ++ch;
                ++res_num;
            }
        }
        residue = &vc->residues[mapping->submap_residue[i]];
        vorbis_residue_decode(vc, residue, ch, do_not_decode, ch_res_ptr, blocksize/2);

        ch_res_ptr += ch * blocksize / 2;
    }

// Inverse coupling

    for (i = mapping->coupling_steps - 1; i >= 0; --i) { //warning: i has to be signed
        float *mag, *ang;

        mag = vc->channel_residues+res_chan[mapping->magnitude[i]] * blocksize / 2;
        ang = vc->channel_residues+res_chan[mapping->angle[i]]     * blocksize / 2;
        vc->dsp.vorbis_inverse_coupling(mag, ang, blocksize / 2);
    }

// Dotproduct, MDCT

    for (j = vc->audio_channels-1;j >= 0; j--) {
        ch_floor_ptr = vc->channel_floors   + j           * blocksize / 2;
        ch_res_ptr   = vc->channel_residues + res_chan[j] * blocksize / 2;
        vc->dsp.vector_fmul(ch_floor_ptr, ch_res_ptr, blocksize / 2);
        ff_imdct_half(&vc->mdct[blockflag], ch_res_ptr, ch_floor_ptr);
    }

// Overlap/add, save data for next overlapping  FPMATH

    retlen = (blocksize + vc->blocksize[previous_window]) / 4;
    for (j = 0; j < vc->audio_channels; j++) {
        uint_fast16_t bs0 = vc->blocksize[0];
        uint_fast16_t bs1 = vc->blocksize[1];
        float *residue    = vc->channel_residues + res_chan[j] * blocksize / 2;
        float *saved      = vc->saved + j * bs1 / 4;
        float *ret        = vc->channel_floors + j * retlen;
        float *buf        = residue;
        const float *win  = vc->win[blockflag & previous_window];

        if (blockflag == previous_window) {
            vc->dsp.vector_fmul_window(ret, saved, buf, win, fadd_bias, blocksize / 4);
        } else if (blockflag > previous_window) {
            vc->dsp.vector_fmul_window(ret, saved, buf, win, fadd_bias, bs0 / 4);
            copy_normalize(ret+bs0/2, buf+bs0/4, (bs1-bs0)/4, vc->exp_bias, fadd_bias);
        } else {
            copy_normalize(ret, saved, (bs1 - bs0) / 4, vc->exp_bias, fadd_bias);
            vc->dsp.vector_fmul_window(ret + (bs1 - bs0) / 4, saved + (bs1 - bs0) / 4, buf, win, fadd_bias, bs0 / 4);
        }
        memcpy(saved, buf + blocksize / 4, blocksize / 4 * sizeof(float));
    }

    vc->previous_window = blockflag;
    return retlen;
}

// Return the decoded audio packet through the standard api

static int vorbis_decode_frame(AVCodecContext *avccontext,
                               void *data, int *data_size,
                               AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    vorbis_context *vc = avccontext->priv_data ;
    GetBitContext *gb = &(vc->gb);
    const float *channel_ptrs[vc->audio_channels];
    int i;

    int_fast16_t len;

    if (!buf_size)
        return 0;

    AV_DEBUG("packet length %d \n", buf_size);

    init_get_bits(gb, buf, buf_size*8);

    len = vorbis_parse_audio_packet(vc);

    if (len <= 0) {
        *data_size = 0;
        return buf_size;
    }

    if (!vc->first_frame) {
        vc->first_frame = 1;
        *data_size = 0;
        return buf_size ;
    }

    AV_DEBUG("parsed %d bytes %d bits, returned %d samples (*ch*bits) \n", get_bits_count(gb)/8, get_bits_count(gb)%8, len);

    if (vc->audio_channels > 8) {
        for (i = 0; i < vc->audio_channels; i++)
            channel_ptrs[i] = vc->channel_floors + i * len;
    } else {
        for (i = 0; i < vc->audio_channels; i++)
            channel_ptrs[i] = vc->channel_floors +
                              len * ff_vorbis_channel_layout_offsets[vc->audio_channels - 1][i];
    }

    vc->dsp.float_to_int16_interleave(data, channel_ptrs, len, vc->audio_channels);
    *data_size = len * 2 * vc->audio_channels;

    return buf_size ;
}

// Close decoder

static av_cold int vorbis_decode_close(AVCodecContext *avccontext)
{
    vorbis_context *vc = avccontext->priv_data;

    vorbis_free(vc);

    return 0 ;
}

AVCodec vorbis_decoder = {
    "vorbis",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_VORBIS,
    sizeof(vorbis_context),
    vorbis_decode_init,
    NULL,
    vorbis_decode_close,
    vorbis_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("Vorbis"),
    .channel_layouts = ff_vorbis_channel_layouts,
};

