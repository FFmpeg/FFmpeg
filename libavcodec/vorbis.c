/**
 * @file vorbis.c
 * Vorbis I decoder
 * @author Denes Balatoni  ( dbalatoni programozo hu )

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
 *
 */

#undef V_DEBUG

#include <math.h>

#define ALT_BITSTREAM_READER_LE
#include "avcodec.h"
#include "bitstream.h"
#include "dsputil.h"

#include "vorbis.h"

#define V_NB_BITS 11
#define V_MAX_VLCS (1<<16)

#ifndef V_DEBUG
#define AV_DEBUG(...)
#endif



/* Helper functions */

/**
 *  reads 0-32 bits when using the ALT_BITSTREAM_READER_LE bitstream reader
 */
unsigned int get_bits_long_le(GetBitContext *s, int n){
    if(n<=17) return get_bits(s, n);
    else{
        int ret= get_bits(s, 16);
        return ret | (get_bits(s, n-16) << 16);
    }
}

static unsigned int ilog(unsigned int i) { // unfortunatelly av_log2 uses different rounding
    unsigned int ret=0;
    while (i!=0) {
        ++ret;
        i>>=1;
    }
    return ret;
}

static unsigned int nth_root(unsigned int x, unsigned int n) {   // x^(1/n)
    unsigned int ret=0, i, j;

    do {
        ++ret;
        for(i=0,j=ret;i<n-1;i++) j*=ret;
    } while (j<=x);

    return (ret-1);
}

static float vorbisfloat2float(uint_fast32_t val) {
    double mant=val&0x1fffff;
    long exp=(val&0x7fe00000L)>>21;
    if (val&0x80000000) mant=-mant;
    return(ldexp(mant, exp-20-768));
}


// Generate vlc codes from vorbis huffman code lengths

static int vorbis_len2vlc(vorbis_context *vc, uint_fast8_t *bits, uint_fast32_t *codes, uint_fast32_t num) {
    uint_fast32_t exit_at_level[33]={404,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    uint_fast8_t i,j;
    uint_fast32_t code,p;

#ifdef V_DEBUG
    GetBitContext gb;
#endif

    for(p=0;(bits[p]==0) && (p<num);++p);
    if (p==num) {
//        av_log(vc->avccontext, AV_LOG_INFO, "An empty codebook. Heh?! \n");
        return 0;
    }

    codes[p]=0;
    for(i=0;i<bits[p];++i) {
        exit_at_level[i+1]=1<<i;
    }

#ifdef V_DEBUG
    av_log(vc->avccontext, AV_LOG_INFO, " %d. of %d code len %d code %d - ", p, num, bits[p], codes[p]);
    init_get_bits(&gb, (uint_fast8_t *)&codes[p], bits[p]);
    for(i=0;i<bits[p];++i) {
        av_log(vc->avccontext, AV_LOG_INFO, "%s", get_bits1(&gb) ? "1" : "0");
    }
    av_log(vc->avccontext, AV_LOG_INFO, "\n");
#endif

    ++p;

    for(;p<num;++p) {
        if (bits[p]==0) continue;
        // find corresponding exit(node which the tree can grow further from)
        for(i=bits[p];i>0;--i) {
            if (exit_at_level[i]) break;
        }
        if (!i) return 1; // overspecified tree
        code=exit_at_level[i];
        exit_at_level[i]=0;
        // construct code (append 0s to end) and introduce new exits
        for(j=i+1;j<=bits[p];++j) {
            exit_at_level[j]=code+(1<<(j-1));
        }
        codes[p]=code;

#ifdef V_DEBUG
        av_log(vc->avccontext, AV_LOG_INFO, " %d. code len %d code %d - ", p, bits[p], codes[p]);
        init_get_bits(&gb, (uint_fast8_t *)&codes[p], bits[p]);
        for(i=0;i<bits[p];++i) {
            av_log(vc->avccontext, AV_LOG_INFO, "%s", get_bits1(&gb) ? "1" : "0");
        }
        av_log(vc->avccontext, AV_LOG_INFO, "\n");
#endif

    }

    //FIXME no exits should be left (underspecified tree - ie. unused valid vlcs - not allowed by SPEC)

    return 0;
}

// Free all allocated memory -----------------------------------------

static void vorbis_free(vorbis_context *vc) {
    int_fast16_t i;

    av_freep(&vc->channel_residues);
    av_freep(&vc->channel_floors);
    av_freep(&vc->saved);
    av_freep(&vc->ret);

    av_freep(&vc->residues);
    av_freep(&vc->modes);

    ff_mdct_end(&vc->mdct0);
    ff_mdct_end(&vc->mdct1);

    for(i=0;i<vc->codebook_count;++i) {
        av_free(vc->codebooks[i].codevectors);
        free_vlc(&vc->codebooks[i].vlc);
    }
    av_freep(&vc->codebooks);

    for(i=0;i<vc->floor_count;++i) {
        av_free(vc->floors[i].x_list);
        av_free(vc->floors[i].x_list_order);
        av_free(vc->floors[i].low_neighbour);
        av_free(vc->floors[i].high_neighbour);
    }
    av_freep(&vc->floors);

    for(i=0;i<vc->mapping_count;++i) {
        av_free(vc->mappings[i].magnitude);
        av_free(vc->mappings[i].angle);
        av_free(vc->mappings[i].mux);
    }
    av_freep(&vc->mappings);
}

// Parse setup header -------------------------------------------------

// Process codebooks part

static int vorbis_parse_setup_hdr_codebooks(vorbis_context *vc) {
    uint_fast16_t cb;
    uint_fast8_t *tmp_vlc_bits;
    uint_fast32_t *tmp_vlc_codes;
    GetBitContext *gb=&vc->gb;

    vc->codebook_count=get_bits(gb,8)+1;

    AV_DEBUG(" Codebooks: %d \n", vc->codebook_count);

    vc->codebooks=(vorbis_codebook *)av_mallocz(vc->codebook_count * sizeof(vorbis_codebook));
    tmp_vlc_bits=(uint_fast8_t *)av_mallocz(V_MAX_VLCS * sizeof(uint_fast8_t));
    tmp_vlc_codes=(uint_fast32_t *)av_mallocz(V_MAX_VLCS * sizeof(uint_fast32_t));

    for(cb=0;cb<vc->codebook_count;++cb) {
        vorbis_codebook *codebook_setup=&vc->codebooks[cb];
        uint_fast8_t ordered;
        uint_fast32_t t, used_entries=0;
        uint_fast32_t entries;

        AV_DEBUG(" %d. Codebook \n", cb);

        if (get_bits(gb, 24)!=0x564342) {
            av_log(vc->avccontext, AV_LOG_ERROR, " %d. Codebook setup data corrupt. \n", cb);
            goto error;
        }

        codebook_setup->dimensions=get_bits(gb, 16);
        if (codebook_setup->dimensions>16) {
            av_log(vc->avccontext, AV_LOG_ERROR, " %d. Codebook's dimension is too large (%d). \n", cb, codebook_setup->dimensions);
            goto error;
        }
        entries=get_bits(gb, 24);
        if (entries>V_MAX_VLCS) {
            av_log(vc->avccontext, AV_LOG_ERROR, " %d. Codebook has too many entries (%d). \n", cb, entries);
            goto error;
        }

        ordered=get_bits1(gb);

        AV_DEBUG(" codebook_dimensions %d, codebook_entries %d \n", codebook_setup->dimensions, entries);

        if (!ordered) {
            uint_fast16_t ce;
            uint_fast8_t flag;
            uint_fast8_t sparse=get_bits1(gb);

            AV_DEBUG(" not ordered \n");

            if (sparse) {
                AV_DEBUG(" sparse \n");

                used_entries=0;
                for(ce=0;ce<entries;++ce) {
                    flag=get_bits1(gb);
                    if (flag) {
                        tmp_vlc_bits[ce]=get_bits(gb, 5)+1;
                        ++used_entries;
                    }
                    else tmp_vlc_bits[ce]=0;
                }
            } else {
                AV_DEBUG(" not sparse \n");

                used_entries=entries;
                for(ce=0;ce<entries;++ce) {
                    tmp_vlc_bits[ce]=get_bits(gb, 5)+1;
                }
            }
        } else {
            uint_fast16_t current_entry=0;
            uint_fast8_t current_length=get_bits(gb, 5)+1;

            AV_DEBUG(" ordered, current length: %d \n", current_length);  //FIXME

            used_entries=entries;
            for(;current_entry<used_entries;++current_length) {
                uint_fast16_t i, number;

                AV_DEBUG(" number bits: %d ", ilog(entries - current_entry));

                number=get_bits(gb, ilog(entries - current_entry));

                AV_DEBUG(" number: %d \n", number);

                for(i=current_entry;i<number+current_entry;++i) {
                    if (i<used_entries) tmp_vlc_bits[i]=current_length;
                }

                current_entry+=number;
            }
            if (current_entry>used_entries) {
                av_log(vc->avccontext, AV_LOG_ERROR, " More codelengths than codes in codebook. \n");
                goto error;
            }
        }

        codebook_setup->lookup_type=get_bits(gb, 4);

        AV_DEBUG(" lookup type: %d : %s \n", codebook_setup->lookup_type, codebook_setup->lookup_type ? "vq" : "no lookup" );

// If the codebook is used for (inverse) VQ, calculate codevectors.

        if (codebook_setup->lookup_type==1) {
            uint_fast16_t i, j, k;
            uint_fast16_t codebook_lookup_values=nth_root(entries, codebook_setup->dimensions);
            uint_fast16_t codebook_multiplicands[codebook_lookup_values];

            float codebook_minimum_value=vorbisfloat2float(get_bits_long_le(gb, 32));
            float codebook_delta_value=vorbisfloat2float(get_bits_long_le(gb, 32));
            uint_fast8_t codebook_value_bits=get_bits(gb, 4)+1;
            uint_fast8_t codebook_sequence_p=get_bits1(gb);

            AV_DEBUG(" We expect %d numbers for building the codevectors. \n", codebook_lookup_values);
            AV_DEBUG("  delta %f minmum %f \n", codebook_delta_value, codebook_minimum_value);

            for(i=0;i<codebook_lookup_values;++i) {
                codebook_multiplicands[i]=get_bits(gb, codebook_value_bits);

                AV_DEBUG(" multiplicands*delta+minmum : %e \n", (float)codebook_multiplicands[i]*codebook_delta_value+codebook_minimum_value);
                AV_DEBUG(" multiplicand %d \n", codebook_multiplicands[i]);
            }

// Weed out unused vlcs and build codevector vector
            codebook_setup->codevectors=(float *)av_mallocz(used_entries*codebook_setup->dimensions * sizeof(float));
            for(j=0, i=0;i<entries;++i) {
                uint_fast8_t dim=codebook_setup->dimensions;

                if (tmp_vlc_bits[i]) {
                    float last=0.0;
                    uint_fast32_t lookup_offset=i;

#ifdef V_DEBUG
                    av_log(vc->avccontext, AV_LOG_INFO, "Lookup offset %d ,", i);
#endif

                    for(k=0;k<dim;++k) {
                        uint_fast32_t multiplicand_offset = lookup_offset % codebook_lookup_values;
                        codebook_setup->codevectors[j*dim+k]=codebook_multiplicands[multiplicand_offset]*codebook_delta_value+codebook_minimum_value+last;
                        if (codebook_sequence_p) {
                            last=codebook_setup->codevectors[j*dim+k];
                        }
                        lookup_offset/=codebook_lookup_values;
                    }
                    tmp_vlc_bits[j]=tmp_vlc_bits[i];

#ifdef V_DEBUG
                    av_log(vc->avccontext, AV_LOG_INFO, "real lookup offset %d, vector: ", j);
                    for(k=0;k<dim;++k) {
                        av_log(vc->avccontext, AV_LOG_INFO, " %f ", codebook_setup->codevectors[j*dim+k]);
                    }
                    av_log(vc->avccontext, AV_LOG_INFO, "\n");
#endif

                    ++j;
                }
            }
            if (j!=used_entries) {
                av_log(vc->avccontext, AV_LOG_ERROR, "Bug in codevector vector building code. \n");
                goto error;
            }
            entries=used_entries;
        }
        else if (codebook_setup->lookup_type>=2) {
            av_log(vc->avccontext, AV_LOG_ERROR, "Codebook lookup type not supported. \n");
            goto error;
        }

// Initialize VLC table
        if (vorbis_len2vlc(vc, tmp_vlc_bits, tmp_vlc_codes, entries)) {
            av_log(vc->avccontext, AV_LOG_ERROR, " Invalid code lengths while generating vlcs. \n");
            goto error;
        }
        codebook_setup->maxdepth=0;
        for(t=0;t<entries;++t)
            if (tmp_vlc_bits[t]>=codebook_setup->maxdepth) codebook_setup->maxdepth=tmp_vlc_bits[t];

        codebook_setup->maxdepth=(codebook_setup->maxdepth+V_NB_BITS-1)/V_NB_BITS;

        if (init_vlc(&codebook_setup->vlc, V_NB_BITS, entries, tmp_vlc_bits, sizeof(*tmp_vlc_bits), sizeof(*tmp_vlc_bits), tmp_vlc_codes, sizeof(*tmp_vlc_codes), sizeof(*tmp_vlc_codes), INIT_VLC_LE)) {
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
    return 1;
}

// Process time domain transforms part (unused in Vorbis I)

static int vorbis_parse_setup_hdr_tdtransforms(vorbis_context *vc) {
    GetBitContext *gb=&vc->gb;
    uint_fast8_t i;
    uint_fast8_t vorbis_time_count=get_bits(gb, 6)+1;

    for(i=0;i<vorbis_time_count;++i) {
        uint_fast16_t vorbis_tdtransform=get_bits(gb, 16);

        AV_DEBUG(" Vorbis time domain transform %d: %d \n", vorbis_time_count, vorbis_tdtransform);

        if (vorbis_tdtransform) {
            av_log(vc->avccontext, AV_LOG_ERROR, "Vorbis time domain transform data nonzero. \n");
            return 1;
        }
    }
    return 0;
}

// Process floors part - only floor type 1 is supported

static int vorbis_parse_setup_hdr_floors(vorbis_context *vc) {
    GetBitContext *gb=&vc->gb;
    uint_fast16_t i,j,k;

    vc->floor_count=get_bits(gb, 6)+1;

    vc->floors=(vorbis_floor *)av_mallocz(vc->floor_count * sizeof(vorbis_floor));

    for (i=0;i<vc->floor_count;++i) {
        vorbis_floor *floor_setup=&vc->floors[i];

        floor_setup->floor_type=get_bits(gb, 16);

        AV_DEBUG(" %d. floor type %d \n", i, floor_setup->floor_type);

        if (floor_setup->floor_type==1) {
            uint_fast8_t maximum_class=0;
            uint_fast8_t rangebits;
            uint_fast16_t floor1_values=2;

            floor_setup->partitions=get_bits(gb, 5);

            AV_DEBUG(" %d.floor: %d partitions \n", i, floor_setup->partitions);

            for(j=0;j<floor_setup->partitions;++j) {
                floor_setup->partition_class[j]=get_bits(gb, 4);
                if (floor_setup->partition_class[j]>maximum_class) maximum_class=floor_setup->partition_class[j];

                AV_DEBUG(" %d. floor %d partition class %d \n", i, j, floor_setup->partition_class[j]);

            }

            AV_DEBUG(" maximum class %d \n", maximum_class);

            floor_setup->maximum_class=maximum_class;

            for(j=0;j<=maximum_class;++j) {
                floor_setup->class_dimensions[j]=get_bits(gb, 3)+1;
                floor_setup->class_subclasses[j]=get_bits(gb, 2);

                AV_DEBUG(" %d floor %d class dim: %d subclasses %d \n", i, j, floor_setup->class_dimensions[j], floor_setup->class_subclasses[j]);

                if (floor_setup->class_subclasses[j]) {
                    floor_setup->class_masterbook[j]=get_bits(gb, 8);

                    AV_DEBUG("   masterbook: %d \n", floor_setup->class_masterbook[j]);
                }

                for(k=0;k<(1<<floor_setup->class_subclasses[j]);++k) {
                    floor_setup->subclass_books[j][k]=get_bits(gb, 8)-1;

                    AV_DEBUG("    book %d. : %d \n", k, floor_setup->subclass_books[j][k]);
                }
            }

            floor_setup->multiplier=get_bits(gb, 2)+1;
            floor_setup->x_list_dim=2;

            for(j=0;j<floor_setup->partitions;++j) {
                floor_setup->x_list_dim+=floor_setup->class_dimensions[floor_setup->partition_class[j]];
            }

            floor_setup->x_list=(uint_fast16_t *)av_mallocz(floor_setup->x_list_dim * sizeof(uint_fast16_t));
            floor_setup->x_list_order=(uint_fast16_t *)av_mallocz(floor_setup->x_list_dim * sizeof(uint_fast16_t));
            floor_setup->low_neighbour=(uint_fast16_t *)av_mallocz(floor_setup->x_list_dim * sizeof(uint_fast16_t));
            floor_setup->high_neighbour=(uint_fast16_t *)av_mallocz(floor_setup->x_list_dim * sizeof(uint_fast16_t));


            rangebits=get_bits(gb, 4);
            floor_setup->x_list[0] = 0;
            floor_setup->x_list[1] = (1<<rangebits);

            for(j=0;j<floor_setup->partitions;++j) {
                for(k=0;k<floor_setup->class_dimensions[floor_setup->partition_class[j]];++k,++floor1_values) {
                    floor_setup->x_list[floor1_values]=get_bits(gb, rangebits);

                    AV_DEBUG(" %d. floor1 Y coord. %d \n", floor1_values, floor_setup->x_list[floor1_values]);
                }
            }

// Precalculate order of x coordinates - needed for decode

            for(k=0;k<floor_setup->x_list_dim;++k) {
                floor_setup->x_list_order[k]=k;
            }

            for(k=0;k<floor_setup->x_list_dim-1;++k) {   // FIXME optimize sorting ?
                for(j=k+1;j<floor_setup->x_list_dim;++j) {
                    if(floor_setup->x_list[floor_setup->x_list_order[k]]>floor_setup->x_list[floor_setup->x_list_order[j]]) {
                        uint_fast16_t tmp=floor_setup->x_list_order[k];
                        floor_setup->x_list_order[k]=floor_setup->x_list_order[j];
                        floor_setup->x_list_order[j]=tmp;
                    }
                }
            }

// Precalculate low and high neighbours

            for(k=2;k<floor_setup->x_list_dim;++k) {
                floor_setup->low_neighbour[k]=0;
                floor_setup->high_neighbour[k]=1;  // correct according to SPEC requirements

                for (j=0;j<k;++j) {
                    if ((floor_setup->x_list[j]<floor_setup->x_list[k]) &&
                      (floor_setup->x_list[j]>floor_setup->x_list[floor_setup->low_neighbour[k]])) {
                        floor_setup->low_neighbour[k]=j;
                    }
                    if ((floor_setup->x_list[j]>floor_setup->x_list[k]) &&
                      (floor_setup->x_list[j]<floor_setup->x_list[floor_setup->high_neighbour[k]])) {
                        floor_setup->high_neighbour[k]=j;
                    }
                }
            }
        }
        else {
            av_log(vc->avccontext, AV_LOG_ERROR, "Only floor type 1 supported. \n");
            return 1;
        }
    }
    return 0;
}

// Process residues part

static int vorbis_parse_setup_hdr_residues(vorbis_context *vc){
    GetBitContext *gb=&vc->gb;
    uint_fast8_t i, j, k;

    vc->residue_count=get_bits(gb, 6)+1;
    vc->residues=(vorbis_residue *)av_mallocz(vc->residue_count * sizeof(vorbis_residue));

    AV_DEBUG(" There are %d residues. \n", vc->residue_count);

    for(i=0;i<vc->residue_count;++i) {
        vorbis_residue *res_setup=&vc->residues[i];
        uint_fast8_t cascade[64];
        uint_fast8_t high_bits;
        uint_fast8_t low_bits;

        res_setup->type=get_bits(gb, 16);

        AV_DEBUG(" %d. residue type %d \n", i, res_setup->type);

        res_setup->begin=get_bits(gb, 24);
        res_setup->end=get_bits(gb, 24);
        res_setup->partition_size=get_bits(gb, 24)+1;
        res_setup->classifications=get_bits(gb, 6)+1;
        res_setup->classbook=get_bits(gb, 8);

        AV_DEBUG("    begin %d end %d part.size %d classif.s %d classbook %d \n", res_setup->begin, res_setup->end, res_setup->partition_size,
          res_setup->classifications, res_setup->classbook);

        for(j=0;j<res_setup->classifications;++j) {
            high_bits=0;
            low_bits=get_bits(gb, 3);
            if (get_bits1(gb)) {
                high_bits=get_bits(gb, 5);
            }
            cascade[j]=(high_bits<<3)+low_bits;

            AV_DEBUG("     %d class casscade depth: %d \n", j, ilog(cascade[j]));
        }

        res_setup->maxpass=0;
        for(j=0;j<res_setup->classifications;++j) {
            for(k=0;k<8;++k) {
                if (cascade[j]&(1<<k)) {
                        res_setup->books[j][k]=get_bits(gb, 8);

                    AV_DEBUG("     %d class casscade depth %d book: %d \n", j, k, res_setup->books[j][k]);

                    if (k>res_setup->maxpass) {
                        res_setup->maxpass=k;
                    }
                } else {
                    res_setup->books[j][k]=-1;
                }
            }
        }
    }
    return 0;
}

// Process mappings part

static int vorbis_parse_setup_hdr_mappings(vorbis_context *vc) {
    GetBitContext *gb=&vc->gb;
    uint_fast8_t i, j;

    vc->mapping_count=get_bits(gb, 6)+1;
    vc->mappings=(vorbis_mapping *)av_mallocz(vc->mapping_count * sizeof(vorbis_mapping));

    AV_DEBUG(" There are %d mappings. \n", vc->mapping_count);

    for(i=0;i<vc->mapping_count;++i) {
        vorbis_mapping *mapping_setup=&vc->mappings[i];

        if (get_bits(gb, 16)) {
            av_log(vc->avccontext, AV_LOG_ERROR, "Other mappings than type 0 are not compliant with the Vorbis I specification. \n");
            return 1;
        }
        if (get_bits1(gb)) {
            mapping_setup->submaps=get_bits(gb, 4)+1;
        } else {
            mapping_setup->submaps=1;
        }

        if (get_bits1(gb)) {
            mapping_setup->coupling_steps=get_bits(gb, 8)+1;
            mapping_setup->magnitude=(uint_fast8_t *)av_mallocz(mapping_setup->coupling_steps * sizeof(uint_fast8_t));
            mapping_setup->angle=(uint_fast8_t *)av_mallocz(mapping_setup->coupling_steps * sizeof(uint_fast8_t));
            for(j=0;j<mapping_setup->coupling_steps;++j) {
                mapping_setup->magnitude[j]=get_bits(gb, ilog(vc->audio_channels-1));
                mapping_setup->angle[j]=get_bits(gb, ilog(vc->audio_channels-1));
                // FIXME: sanity checks
            }
        } else {
            mapping_setup->coupling_steps=0;
        }

        AV_DEBUG("   %d mapping coupling steps: %d \n", i, mapping_setup->coupling_steps);

        if(get_bits(gb, 2)) {
            av_log(vc->avccontext, AV_LOG_ERROR, "%d. mapping setup data invalid. \n", i);
            return 1; // following spec.
        }

        if (mapping_setup->submaps>1) {
            mapping_setup->mux=(uint_fast8_t *)av_mallocz(vc->audio_channels * sizeof(uint_fast8_t));
            for(j=0;j<vc->audio_channels;++j) {
                mapping_setup->mux[j]=get_bits(gb, 4);
            }
        }

        for(j=0;j<mapping_setup->submaps;++j) {
            get_bits(gb, 8); // FIXME check?
            mapping_setup->submap_floor[j]=get_bits(gb, 8);
            mapping_setup->submap_residue[j]=get_bits(gb, 8);

            AV_DEBUG("   %d mapping %d submap : floor %d, residue %d \n", i, j, mapping_setup->submap_floor[j], mapping_setup->submap_residue[j]);
        }
    }
    return 0;
}

// Process modes part

static int vorbis_parse_setup_hdr_modes(vorbis_context *vc) {
    GetBitContext *gb=&vc->gb;
    uint_fast8_t i;

    vc->mode_count=get_bits(gb, 6)+1;
    vc->modes=(vorbis_mode *)av_mallocz(vc->mode_count * sizeof(vorbis_mode));

    AV_DEBUG(" There are %d modes.\n", vc->mode_count);

    for(i=0;i<vc->mode_count;++i) {
        vorbis_mode *mode_setup=&vc->modes[i];

        mode_setup->blockflag=get_bits(gb, 1);
        mode_setup->windowtype=get_bits(gb, 16); //FIXME check
        mode_setup->transformtype=get_bits(gb, 16); //FIXME check
        mode_setup->mapping=get_bits(gb, 8); //FIXME check

        AV_DEBUG(" %d mode: blockflag %d, windowtype %d, transformtype %d, mapping %d \n", i, mode_setup->blockflag, mode_setup->windowtype, mode_setup->transformtype, mode_setup->mapping);
    }
    return 0;
}

// Process the whole setup header using the functions above

static int vorbis_parse_setup_hdr(vorbis_context *vc) {
    GetBitContext *gb=&vc->gb;

    if ((get_bits(gb, 8)!='v') || (get_bits(gb, 8)!='o') ||
    (get_bits(gb, 8)!='r') || (get_bits(gb, 8)!='b') ||
    (get_bits(gb, 8)!='i') || (get_bits(gb, 8)!='s')) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (no vorbis signature). \n");
        return 1;
    }

    if (vorbis_parse_setup_hdr_codebooks(vc)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (codebooks). \n");
        return 2;
    }
    if (vorbis_parse_setup_hdr_tdtransforms(vc)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (time domain transforms). \n");
        return 3;
    }
    if (vorbis_parse_setup_hdr_floors(vc)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (floors). \n");
        return 4;
    }
    if (vorbis_parse_setup_hdr_residues(vc)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (residues). \n");
        return 5;
    }
    if (vorbis_parse_setup_hdr_mappings(vc)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (mappings). \n");
        return 6;
    }
    if (vorbis_parse_setup_hdr_modes(vc)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (modes). \n");
        return 7;
    }
    if (!get_bits1(gb)) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis setup header packet corrupt (framing flag). \n");
        return 8; // framing flag bit unset error
    }

    return 0;
}

// Process the identification header

static int vorbis_parse_id_hdr(vorbis_context *vc){
    GetBitContext *gb=&vc->gb;
    uint_fast8_t bl0, bl1;
    const float *vwin[8]={ vwin64, vwin128, vwin256, vwin512, vwin1024, vwin2048, vwin4096, vwin8192 };

    if ((get_bits(gb, 8)!='v') || (get_bits(gb, 8)!='o') ||
    (get_bits(gb, 8)!='r') || (get_bits(gb, 8)!='b') ||
    (get_bits(gb, 8)!='i') || (get_bits(gb, 8)!='s')) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis id header packet corrupt (no vorbis signature). \n");
        return 1;
    }

    vc->version=get_bits_long_le(gb, 32);    //FIXME check 0
    vc->audio_channels=get_bits(gb, 8);   //FIXME check >0
    vc->audio_samplerate=get_bits_long_le(gb, 32);   //FIXME check >0
    vc->bitrate_maximum=get_bits_long_le(gb, 32);
    vc->bitrate_nominal=get_bits_long_le(gb, 32);
    vc->bitrate_minimum=get_bits_long_le(gb, 32);
    bl0=get_bits(gb, 4);
    bl1=get_bits(gb, 4);
    vc->blocksize_0=(1<<bl0);
    vc->blocksize_1=(1<<bl1);
    if (bl0>13 || bl0<6 || bl1>13 || bl1<6) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis id header packet corrupt (illegal blocksize). \n");
        return 3;
    }
    vc->swin=vwin[bl0-6];
    vc->lwin=vwin[bl1-6];

    if ((get_bits1(gb)) == 0) {
        av_log(vc->avccontext, AV_LOG_ERROR, " Vorbis id header packet corrupt (framing flag not set). \n");
        return 2;
    }

    vc->channel_residues=(float *)av_malloc((vc->blocksize_1/2)*vc->audio_channels * sizeof(float));
    vc->channel_floors=(float *)av_malloc((vc->blocksize_1/2)*vc->audio_channels * sizeof(float));
    vc->saved=(float *)av_malloc((vc->blocksize_1/2)*vc->audio_channels * sizeof(float));
    vc->ret=(float *)av_malloc((vc->blocksize_1/2)*vc->audio_channels * sizeof(float));
    vc->saved_start=0;

    ff_mdct_init(&vc->mdct0, bl0, 1);
    ff_mdct_init(&vc->mdct1, bl1, 1);

    AV_DEBUG(" vorbis version %d \n audio_channels %d \n audio_samplerate %d \n bitrate_max %d \n bitrate_nom %d \n bitrate_min %d \n blk_0 %d blk_1 %d \n ",
            vc->version, vc->audio_channels, vc->audio_samplerate, vc->bitrate_maximum, vc->bitrate_nominal, vc->bitrate_minimum, vc->blocksize_0, vc->blocksize_1);

/*
    BLK=vc->blocksize_0;
    for(i=0;i<BLK/2;++i) {
        vc->swin[i]=sin(0.5*3.14159265358*(sin(((float)i+0.5)/(float)BLK*3.14159265358))*(sin(((float)i+0.5)/(float)BLK*3.14159265358)));
    }
*/

    return 0;
}

// Process the extradata using the functions above (identification header, setup header)

static int vorbis_decode_init(AVCodecContext *avccontext) {
    vorbis_context *vc = avccontext->priv_data ;
    uint8_t *headers = avccontext->extradata;
    int headers_len=avccontext->extradata_size;
    uint8_t *header_start[3];
    int header_len[3];
    GetBitContext *gb = &(vc->gb);
    int i, j, hdr_type;

    vc->avccontext = avccontext;

    if (!headers_len) {
        av_log(avccontext, AV_LOG_ERROR, "Extradata corrupt.\n");
        return -1;
    }

    if(headers[0] == 0 && headers[1] == 30) {
        for(i = 0; i < 3; i++){
            header_len[i] = *headers++ << 8;
            header_len[i] += *headers++;
            header_start[i] = headers;
            headers += header_len[i];
        }
    } else if(headers[0] == 2) {
        for(j=1,i=0;i<2;++i, ++j) {
            header_len[i]=0;
            while(j<headers_len && headers[j]==0xff) {
                header_len[i]+=0xff;
                ++j;
            }
            if (j>=headers_len) {
                av_log(avccontext, AV_LOG_ERROR, "Extradata corrupt.\n");
                return -1;
            }
            header_len[i]+=headers[j];
        }
        header_len[2]=headers_len-header_len[0]-header_len[1]-j;
        headers+=j;
        header_start[0] = headers;
        header_start[1] = header_start[0] + header_len[0];
        header_start[2] = header_start[1] + header_len[1];
    } else {
        av_log(avccontext, AV_LOG_ERROR, "Extradata corrupt.\n");
        return -1;
    }

    init_get_bits(gb, header_start[0], header_len[0]*8);
    hdr_type=get_bits(gb, 8);
    if (hdr_type!=1) {
        av_log(avccontext, AV_LOG_ERROR, "First header is not the id header.\n");
        return -1;
    }
    if (vorbis_parse_id_hdr(vc)) {
        av_log(avccontext, AV_LOG_ERROR, "Id header corrupt.\n");
        vorbis_free(vc);
        return -1;
    }

    init_get_bits(gb, header_start[2], header_len[2]*8);
    hdr_type=get_bits(gb, 8);
    if (hdr_type!=5) {
        av_log(avccontext, AV_LOG_ERROR, "Third header is not the setup header.\n");
        return -1;
    }
    if (vorbis_parse_setup_hdr(vc)) {
        av_log(avccontext, AV_LOG_ERROR, "Setup header corrupt.\n");
        vorbis_free(vc);
        return -1;
    }

    avccontext->channels = vc->audio_channels;
    avccontext->sample_rate = vc->audio_samplerate;

    return 0 ;
}

// Decode audiopackets -------------------------------------------------

// Read and decode floor (type 1 only)

static uint_fast8_t vorbis_floor1_decode(vorbis_context *vc, vorbis_floor *vf, float *vec) {
    GetBitContext *gb=&vc->gb;
    uint_fast16_t range_v[4]={ 256, 128, 86, 64 };
    uint_fast16_t range=range_v[vf->multiplier-1];
    uint_fast16_t floor1_Y[vf->x_list_dim];
    uint_fast16_t floor1_Y_final[vf->x_list_dim];
    uint_fast8_t floor1_flag[vf->x_list_dim];
    uint_fast8_t class_;
    uint_fast8_t cdim;
    uint_fast8_t cbits;
    uint_fast8_t csub;
    uint_fast8_t cval;
    int_fast16_t book;
    uint_fast16_t offset;
    uint_fast16_t i,j;
    uint_fast16_t *floor_x_sort=vf->x_list_order;
    /*u*/int_fast16_t adx, ady, off, predicted; // WTF ? dy/adx= (unsigned)dy/adx ?
    int_fast16_t dy, err;
    uint_fast16_t lx,hx, ly, hy=0;


    if (!get_bits1(gb)) return 1; // silence

// Read values (or differences) for the floor's points

    floor1_Y[0]=get_bits(gb, ilog(range-1));
    floor1_Y[1]=get_bits(gb, ilog(range-1));

    AV_DEBUG("floor 0 Y %d floor 1 Y %d \n", floor1_Y[0], floor1_Y[1]);

    offset=2;
    for(i=0;i<vf->partitions;++i) {
        class_=vf->partition_class[i];
        cdim=vf->class_dimensions[class_];
        cbits=vf->class_subclasses[class_];
        csub=(1<<cbits)-1;
        cval=0;

        AV_DEBUG("Cbits %d \n", cbits);

        if (cbits) { // this reads all subclasses for this partition's class
            cval=get_vlc2(gb, vc->codebooks[vf->class_masterbook[class_]].vlc.table,
            V_NB_BITS, vc->codebooks[vf->class_masterbook[class_]].maxdepth);
        }

        for(j=0;j<cdim;++j) {
            book=vf->subclass_books[class_][cval & csub];

            AV_DEBUG("book %d Cbits %d cval %d  bits:%d \n", book, cbits, cval, get_bits_count(gb));

            cval=cval>>cbits;
            if (book>0) {
                floor1_Y[offset+j]=get_vlc2(gb, vc->codebooks[book].vlc.table,
                V_NB_BITS, vc->codebooks[book].maxdepth);
            } else {
                floor1_Y[offset+j]=0;
            }

            AV_DEBUG(" floor(%d) = %d \n", vf->x_list[offset+j], floor1_Y[offset+j]);
        }
        offset+=cdim;
    }

// Amplitude calculation from the differences

    floor1_flag[0]=1;
    floor1_flag[1]=1;
    floor1_Y_final[0]=floor1_Y[0];
    floor1_Y_final[1]=floor1_Y[1];

    for(i=2;i<vf->x_list_dim;++i) {
        uint_fast16_t val, highroom, lowroom, room;
        uint_fast16_t high_neigh_offs;
        uint_fast16_t low_neigh_offs;

        low_neigh_offs=vf->low_neighbour[i];
        high_neigh_offs=vf->high_neighbour[i];
        dy=floor1_Y_final[high_neigh_offs]-floor1_Y_final[low_neigh_offs];  // render_point begin
        adx=vf->x_list[high_neigh_offs]-vf->x_list[low_neigh_offs];
        ady= ABS(dy);
        err=ady*(vf->x_list[i]-vf->x_list[low_neigh_offs]);
        off=err/adx;
        if (dy<0) {
            predicted=floor1_Y_final[low_neigh_offs]-off;
        } else {
            predicted=floor1_Y_final[low_neigh_offs]+off;
        } // render_point end

        val=floor1_Y[i];
        highroom=range-predicted;
        lowroom=predicted;
        if (highroom < lowroom) {
            room=highroom*2;
        } else {
            room=lowroom*2;   // SPEC mispelling
        }
        if (val) {
            floor1_flag[low_neigh_offs]=1;
            floor1_flag[high_neigh_offs]=1;
            floor1_flag[i]=1;
            if (val>=room) {
                if (highroom > lowroom) {
                    floor1_Y_final[i]=val-lowroom+predicted;
                } else {
                    floor1_Y_final[i]=predicted-val+highroom-1;
                }
            } else {
                if (val & 1) {
                    floor1_Y_final[i]=predicted-(val+1)/2;
                } else {
                    floor1_Y_final[i]=predicted+val/2;
                }
            }
        } else {
            floor1_flag[i]=0;
            floor1_Y_final[i]=predicted;
        }

        AV_DEBUG(" Decoded floor(%d) = %d / val %d \n", vf->x_list[i], floor1_Y_final[i], val);
    }

// Curve synth - connect the calculated dots and convert from dB scale FIXME optimize ?

    hx=0;
    lx=0;
    ly=floor1_Y_final[0]*vf->multiplier;  // conforms to SPEC

    vec[0]=floor1_inverse_db_table[ly];

    for(i=1;i<vf->x_list_dim;++i) {
        AV_DEBUG(" Looking at post %d \n", i);

        if (floor1_flag[floor_x_sort[i]]) {   // SPEC mispelled
            int_fast16_t x, y, dy, base, sy; // if uncommented: dy = -32 adx = 2  base = 2blablabla ?????

            hy=floor1_Y_final[floor_x_sort[i]]*vf->multiplier;
            hx=vf->x_list[floor_x_sort[i]];

            dy=hy-ly;
            adx=hx-lx;
            ady= (dy<0) ? -dy:dy;//ABS(dy);
            base=dy/adx;

            AV_DEBUG(" dy %d  adx %d base %d = %d \n", dy, adx, base, dy/adx);

            x=lx;
            y=ly;
            err=0;
            if (dy<0) {
                sy=base-1;
            } else {
                sy=base+1;
            }
            ady=ady-(base<0 ? -base : base)*adx;
            vec[x]=floor1_inverse_db_table[y];

            AV_DEBUG(" vec[ %d ] = %d \n", x, y);

            for(x=lx+1;(x<hx) && (x<vf->x_list[1]);++x) {
                err+=ady;
                if (err>=adx) {
                    err-=adx;
                    y+=sy;
                } else {
                    y+=base;
                }
                vec[x]=floor1_inverse_db_table[y];

                AV_DEBUG(" vec[ %d ] = %d \n", x, y);
            }

/*            for(j=1;j<hx-lx+1;++j) {  // iterating render_point
                dy=hy-ly;
                adx=hx-lx;
                ady= dy<0 ? -dy : dy;
                err=ady*j;
                off=err/adx;
                if (dy<0) {
                    predicted=ly-off;
                } else {
                    predicted=ly+off;
                }
                if (lx+j < vf->x_list[1]) {
                    vec[lx+j]=floor1_inverse_db_table[predicted];
                }
            }*/

            lx=hx;
            ly=hy;
        }
    }

    if (hx<vf->x_list[1]) {
        for(i=hx;i<vf->x_list[1];++i) {
            vec[i]=floor1_inverse_db_table[hy];
        }
    }

    AV_DEBUG(" Floor decoded\n");

    return 0;
}

// Read and decode residue

static int vorbis_residue_decode(vorbis_context *vc, vorbis_residue *vr, uint_fast8_t ch, uint_fast8_t *do_not_decode, float *vec, uint_fast16_t vlen) {
    GetBitContext *gb=&vc->gb;
    uint_fast8_t c_p_c=vc->codebooks[vr->classbook].dimensions;
    uint_fast16_t n_to_read=vr->end-vr->begin;
    uint_fast16_t ptns_to_read=n_to_read/vr->partition_size;
    uint_fast8_t classifs[ptns_to_read*vc->audio_channels];
    uint_fast8_t pass;
    uint_fast8_t ch_used;
    uint_fast8_t i,j,l;
    uint_fast16_t k;

    if (vr->type==2) {
        for(j=1;j<ch;++j) {
                do_not_decode[0]&=do_not_decode[j];  // FIXME - clobbering input
        }
        if (do_not_decode[0]) return 0;
        ch_used=1;
    } else {
        ch_used=ch;
    }

    AV_DEBUG(" residue type 0/1/2 decode begin, ch: %d  cpc %d  \n", ch, c_p_c);

    for(pass=0;pass<=vr->maxpass;++pass) { // FIXME OPTIMIZE?
        uint_fast16_t voffset;
        uint_fast16_t partition_count;
        uint_fast16_t j_times_ptns_to_read;

        voffset=vr->begin;
        for(partition_count=0;partition_count<ptns_to_read;) {  // SPEC        error
            if (!pass) {
                for(j_times_ptns_to_read=0, j=0;j<ch_used;++j) {
                    if (!do_not_decode[j]) {
                        uint_fast32_t temp=get_vlc2(gb, vc->codebooks[vr->classbook].vlc.table,
                        V_NB_BITS, vc->codebooks[vr->classbook].maxdepth);

                        AV_DEBUG("Classword: %d \n", temp);

                        for(i=0;i<c_p_c;++i) {
                            uint_fast32_t temp2;

                            temp2=temp/vr->classifications;
                            classifs[j_times_ptns_to_read+partition_count+c_p_c-1-i]=temp-temp2*vr->classifications;
                            temp=temp2;
                        }
                    }
                    j_times_ptns_to_read+=ptns_to_read;
                }
            }
            for(i=0;(i<c_p_c) && (partition_count<ptns_to_read);++i) {
                for(j_times_ptns_to_read=0, j=0;j<ch_used;++j) {
                    uint_fast16_t voffs;

                    if (!do_not_decode[j]) {
                        uint_fast8_t vqclass=classifs[j_times_ptns_to_read+partition_count];
                        int_fast16_t vqbook=vr->books[vqclass][pass];

                        if (vqbook>=0) {
                            uint_fast16_t coffs;

                            if (vr->type==0) {
                                uint_fast16_t step=vr->partition_size/vc->codebooks[vqbook].dimensions;

                                voffs=voffset+j*vlen;
                                for(k=0;k<step;++k) {
                                    coffs=get_vlc2(gb, vc->codebooks[vqbook].vlc.table,
                                    V_NB_BITS, vc->codebooks[vr->classbook].maxdepth) * vc->codebooks[vqbook].dimensions;
                                    for(l=0;l<vc->codebooks[vqbook].dimensions;++l) {
                                        vec[voffs+k+l*step]+=vc->codebooks[vqbook].codevectors[coffs+l];  // FPMATH
                                    }
                                }
                            }
                            else if (vr->type==1) {
                                voffs=voffset+j*vlen;
                                for(k=0;k<vr->partition_size/vc->codebooks[vqbook].dimensions;++k) {
                                    coffs=get_vlc2(gb, vc->codebooks[vqbook].vlc.table,
                                    V_NB_BITS, vc->codebooks[vr->classbook].maxdepth) * vc->codebooks[vqbook].dimensions;
                                    for(l=0;l<vc->codebooks[vqbook].dimensions;++l, ++voffs) {
                                        vec[voffs]+=vc->codebooks[vqbook].codevectors[coffs+l];  // FPMATH

                                        AV_DEBUG(" pass %d offs: %d curr: %f change: %f cv offs.: %d  \n", pass, voffs, vec[voffs], vc->codebooks[vqbook].codevectors[coffs+l], coffs);
                                    }
                                }
                            }
                            else if (vr->type==2 && ch==2) { // most frequent case optimized
                                voffs=voffset;

                                for(k=0;k<vr->partition_size/vc->codebooks[vqbook].dimensions;++k) {
                                    coffs=get_vlc2(gb, vc->codebooks[vqbook].vlc.table,
                                    V_NB_BITS, vc->codebooks[vr->classbook].maxdepth) * vc->codebooks[vqbook].dimensions;
                                    for(l=0;l<vc->codebooks[vqbook].dimensions;++l, ++voffs) {
                                        vec[(voffs>>1)+((voffs&1) ? vlen : 0)]+=vc->codebooks[vqbook].codevectors[coffs+l];  // FPMATH

                                        AV_DEBUG(" pass %d offs: %d curr: %f change: %f cv offs.: %d+%d  \n", pass, voffset/ch+(voffs%ch)*vlen, vec[voffset/ch+(voffs%ch)*vlen], vc->codebooks[vqbook].codevectors[coffs+l], coffs, l);
                                    }
                                }

                            }
                            else if (vr->type==2) {
                                voffs=voffset;

                                for(k=0;k<vr->partition_size/vc->codebooks[vqbook].dimensions;++k) {
                                    coffs=get_vlc2(gb, vc->codebooks[vqbook].vlc.table,
                                    V_NB_BITS, vc->codebooks[vr->classbook].maxdepth) * vc->codebooks[vqbook].dimensions;
                                    for(l=0;l<vc->codebooks[vqbook].dimensions;++l, ++voffs) {
                                        vec[voffs/ch+(voffs%ch)*vlen]+=vc->codebooks[vqbook].codevectors[coffs+l];  // FPMATH FIXME use if and counter instead of / and %

                                        AV_DEBUG(" pass %d offs: %d curr: %f change: %f cv offs.: %d+%d  \n", pass, voffset/ch+(voffs%ch)*vlen, vec[voffset/ch+(voffs%ch)*vlen], vc->codebooks[vqbook].codevectors[coffs+l], coffs, l);
                                    }
                                }
                            } else {
                                av_log(vc->avccontext, AV_LOG_ERROR, " Invalid residue type while residue decode?! \n");
                                return 1;
                            }
                        }
                    }
                    j_times_ptns_to_read+=ptns_to_read;
                }
                ++partition_count;
                voffset+=vr->partition_size;
            }
        }
    }
    return 0;
}

// Decode the audio packet using the functions above

static int vorbis_parse_audio_packet(vorbis_context *vc) {
    GetBitContext *gb=&vc->gb;

    uint_fast8_t previous_window=0,next_window=0;
    uint_fast8_t mode_number;
    uint_fast16_t blocksize;
    int_fast32_t i,j;
    uint_fast8_t no_residue[vc->audio_channels];
    uint_fast8_t do_not_decode[vc->audio_channels];
    vorbis_mapping *mapping;
    float *ch_res_ptr=vc->channel_residues;
    float *ch_floor_ptr=vc->channel_floors;
    uint_fast8_t res_chan[vc->audio_channels];
    uint_fast8_t res_num=0;
    int_fast16_t retlen=0;
    uint_fast16_t saved_start=0;

    if (get_bits1(gb)) {
        av_log(vc->avccontext, AV_LOG_ERROR, "Not a Vorbis I audio packet.\n");
        return -1; // packet type not audio
    }

    if (vc->mode_count==1) {
        mode_number=0;
    } else {
        mode_number=get_bits(gb, ilog(vc->mode_count-1));
    }
    mapping=&vc->mappings[vc->modes[mode_number].mapping];

    AV_DEBUG(" Mode number: %d , mapping: %d , blocktype %d \n", mode_number, vc->modes[mode_number].mapping, vc->modes[mode_number].blockflag);

    if (vc->modes[mode_number].blockflag) {
        previous_window=get_bits1(gb);
        next_window=get_bits1(gb);
    }

    blocksize=vc->modes[mode_number].blockflag ? vc->blocksize_1 : vc->blocksize_0;
    memset(ch_res_ptr, 0, sizeof(float)*vc->audio_channels*blocksize/2); //FIXME can this be removed ?
    memset(ch_floor_ptr, 0, sizeof(float)*vc->audio_channels*blocksize/2); //FIXME can this be removed ?

// Decode floor(1)

    for(i=0;i<vc->audio_channels;++i) {
        vorbis_floor *floor;
        if (mapping->submaps>1) {
            floor=&vc->floors[mapping->submap_floor[mapping->mux[i]]];
        } else {
            floor=&vc->floors[mapping->submap_floor[0]];
        }

        no_residue[i]=vorbis_floor1_decode(vc, floor, ch_floor_ptr);
        ch_floor_ptr+=blocksize/2;
    }

// Nonzero vector propagate

    for(i=mapping->coupling_steps-1;i>=0;--i) {
        if (!(no_residue[mapping->magnitude[i]] & no_residue[mapping->angle[i]])) {
            no_residue[mapping->magnitude[i]]=0;
            no_residue[mapping->angle[i]]=0;
        }
    }

// Decode residue

    for(i=0;i<mapping->submaps;++i) {
        vorbis_residue *residue;
        uint_fast8_t ch=0;

        for(j=0;j<vc->audio_channels;++j) {
            if ((mapping->submaps==1) || (i=mapping->mux[j])) {
                res_chan[j]=res_num;
                if (no_residue[j]) {
                    do_not_decode[ch]=1;
                } else {
                    do_not_decode[ch]=0;
                }
                ++ch;
                ++res_num;
            }
        }
        residue=&vc->residues[mapping->submap_residue[i]];
        vorbis_residue_decode(vc, residue, ch, do_not_decode, ch_res_ptr, blocksize/2);

        ch_res_ptr+=ch*blocksize/2;
    }

// Inverse coupling

    for(i=mapping->coupling_steps-1;i>=0;--i) { //warning: i has to be signed
        float *mag, *ang;

        mag=vc->channel_residues+res_chan[mapping->magnitude[i]]*blocksize/2;
        ang=vc->channel_residues+res_chan[mapping->angle[i]]*blocksize/2;
        for(j=0;j<blocksize/2;++j) {
            float temp;
            if (mag[j]>0.0) {
                if (ang[j]>0.0) {
                    ang[j]=mag[j]-ang[j];
                } else {
                    temp=ang[j];
                    ang[j]=mag[j];
                    mag[j]+=temp;
                }
            } else {
                if (ang[j]>0.0) {
                    ang[j]+=mag[j];
                } else {
                    temp=ang[j];
                    ang[j]=mag[j];
                    mag[j]-=temp;
                }
            }
        }
    }

// Dotproduct

    for(j=0, ch_floor_ptr=vc->channel_floors;j<vc->audio_channels;++j,ch_floor_ptr+=blocksize/2) {
        ch_res_ptr=vc->channel_residues+res_chan[j]*blocksize/2;

        for(i=0;i<blocksize/2;++i) {
            ch_floor_ptr[i]*=ch_res_ptr[i]; //FPMATH
        }
    }

// MDCT, overlap/add, save data for next overlapping  FPMATH

    for(j=0;j<vc->audio_channels;++j) {
        uint_fast8_t step=vc->audio_channels;
        uint_fast16_t k;
        float *saved=vc->saved+j*vc->blocksize_1/2;
        float *ret=vc->ret;
        const float *lwin=vc->lwin;
        const float *swin=vc->swin;
        float buf[blocksize];
        float buf_tmp[blocksize];

        ch_floor_ptr=vc->channel_floors+j*blocksize/2;

        saved_start=vc->saved_start;

        ff_imdct_calc(vc->modes[mode_number].blockflag ? &vc->mdct1 : &vc->mdct0, buf, ch_floor_ptr, buf_tmp);

        if (vc->modes[mode_number].blockflag) {
            // -- overlap/add
            if (previous_window) {
                for(k=j, i=0;i<vc->blocksize_1/2;++i, k+=step) {
                    ret[k]=saved[i]+buf[i]*lwin[i];
                }
                retlen=vc->blocksize_1/2;
            } else {
                for(k=j, i=0;i<vc->blocksize_0/2;++i, k+=step) {
                    ret[k]=saved[i]+buf[(vc->blocksize_1-vc->blocksize_0)/4+i]*swin[i];
                }
                for(i=0;i<(vc->blocksize_1-vc->blocksize_0)/4;++i, k+=step) {
                    ret[k]=buf[vc->blocksize_0/2+(vc->blocksize_1-vc->blocksize_0)/4+i];
                }
                retlen=vc->blocksize_0/2+(vc->blocksize_1-vc->blocksize_0)/4;
            }
            // -- save
            if (next_window) {
                for(i=0;i<vc->blocksize_1/2;++i) {
                    saved[i]=buf[vc->blocksize_1/2+i]*lwin[vc->blocksize_1/2-1-i];
                }
                saved_start=0;
            } else {
                saved_start=(vc->blocksize_1-vc->blocksize_0)/4;
                for(i=0;i<saved_start;++i) {
                    saved[i]=buf[vc->blocksize_1/2+i];
                }
                for(i=0;i<vc->blocksize_0/2;++i) {
                    saved[saved_start+i]=buf[vc->blocksize_1/2+saved_start+i]*swin[vc->blocksize_0/2-1-i];
                }
            }
        } else {
            // --overlap/add
            for(k=j, i=0;i<saved_start;++i, k+=step) {
                ret[k]=saved[i];
            }
            for(i=0;i<vc->blocksize_0/2;++i, k+=step) {
                ret[k]=saved[saved_start+i]+buf[i]*swin[i];
            }
            retlen=saved_start+vc->blocksize_0/2;
            // -- save
            for(i=0;i<vc->blocksize_0/2;++i) {
                saved[i]=buf[vc->blocksize_0/2+i]*swin[vc->blocksize_0/2-1-i];
            }
            saved_start=0;
        }
    }
    vc->saved_start=saved_start;

    return retlen*vc->audio_channels;
}

// Return the decoded audio packet through the standard api

static int vorbis_decode_frame(AVCodecContext *avccontext,
                        void *data, int *data_size,
                        uint8_t *buf, int buf_size)
{
    vorbis_context *vc = avccontext->priv_data ;
    GetBitContext *gb = &(vc->gb);

    int_fast16_t i, len;

    if(!buf_size){
        return 0;
    }

    AV_DEBUG("packet length %d \n", buf_size);

    init_get_bits(gb, buf, buf_size*8);

    len=vorbis_parse_audio_packet(vc);

    if (len<=0) {
        *data_size=0;
        return buf_size;
    }

    if (!vc->first_frame) {
        vc->first_frame=1;
        *data_size=0;
        return buf_size ;
    }

    AV_DEBUG("parsed %d bytes %d bits, returned %d samples (*ch*bits) \n", get_bits_count(gb)/8, get_bits_count(gb)%8, len);

    for(i=0;i<len;++i) {
        int_fast32_t tmp;

        tmp=vc->ret[i]*32768;
        if (tmp>32767) tmp=32767;
        if (tmp<-32768) tmp=-32768;
        ((int16_t*)data)[i]=tmp;
    }
    *data_size=len*2;

    return buf_size ;
}

// Close decoder

static int vorbis_decode_close(AVCodecContext *avccontext) {
    vorbis_context *vc = avccontext->priv_data;

    vorbis_free(vc);

    return 0 ;
}

AVCodec vorbis_decoder = {
    "vorbis",
    CODEC_TYPE_AUDIO,
    CODEC_ID_VORBIS,
    sizeof(vorbis_context),
    vorbis_decode_init,
    NULL,
    vorbis_decode_close,
    vorbis_decode_frame,
};

