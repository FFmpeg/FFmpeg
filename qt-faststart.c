/*
 * qt-faststart.c, v0.1
 * by Mike Melanson (melanson@pcisys.net)
 * This file is placed in the public domain. Use the program however you
 * see fit.
 *
 * This utility rearranges a Quicktime file such that the moov atom
 * is in front of the data, thus facilitating network streaming.
 *
 * Compile this program using:
 *  cc qt-faststart.c -o qt-faststart
 * Invoke the program with:
 *  qt-faststart <infile.mov> <outfile.mov>
 *
 * Notes: Quicktime files can come in many configurations of top-level
 * atoms. This utility stipulates that the very last atom in the file needs
 * to be a moov atom. When given such a file, this utility will rearrange
 * the top-level atoms by shifting the moov atom from the back of the file
 * to the front, and patch the chunk offsets along the way. This utility
 * presently only operates on uncompressed moov atoms.
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#define BE_16(x) ((((uint8_t*)(x))[0] << 8) | ((uint8_t*)(x))[1])
#define BE_32(x) ((((uint8_t*)(x))[0] << 24) | \
                  (((uint8_t*)(x))[1] << 16) | \
                  (((uint8_t*)(x))[2] << 8) | \
                   ((uint8_t*)(x))[3])
#define BE_64(x) (((uint64_t)(((uint8_t*)(x))[0]) << 56) | \
                  ((uint64_t)(((uint8_t*)(x))[1]) << 48) | \
                  ((uint64_t)(((uint8_t*)(x))[2]) << 40) | \
                  ((uint64_t)(((uint8_t*)(x))[3]) << 32) | \
                  ((uint64_t)(((uint8_t*)(x))[4]) << 24) | \
                  ((uint64_t)(((uint8_t*)(x))[5]) << 16) | \
                  ((uint64_t)(((uint8_t*)(x))[6]) << 8) | \
                  ((uint64_t)((uint8_t*)(x))[7]))

#define BE_FOURCC( ch0, ch1, ch2, ch3 )             \
        ( (uint32_t)(unsigned char)(ch3) |          \
        ( (uint32_t)(unsigned char)(ch2) << 8 ) |   \
        ( (uint32_t)(unsigned char)(ch1) << 16 ) |  \
        ( (uint32_t)(unsigned char)(ch0) << 24 ) )

#define QT_ATOM BE_FOURCC
/* top level atoms */
#define FREE_ATOM QT_ATOM('f', 'r', 'e', 'e')
#define JUNK_ATOM QT_ATOM('j', 'u', 'n', 'k')
#define MDAT_ATOM QT_ATOM('m', 'd', 'a', 't')
#define MOOV_ATOM QT_ATOM('m', 'o', 'o', 'v')
#define PNOT_ATOM QT_ATOM('p', 'n', 'o', 't')
#define SKIP_ATOM QT_ATOM('s', 'k', 'i', 'p')
#define WIDE_ATOM QT_ATOM('w', 'i', 'd', 'e')
#define PICT_ATOM QT_ATOM('P', 'I', 'C', 'T')
#define FTYP_ATOM QT_ATOM('f', 't', 'y', 'p')

#define CMOV_ATOM QT_ATOM('c', 'm', 'o', 'v')
#define STCO_ATOM QT_ATOM('s', 't', 'c', 'o')
#define CO64_ATOM QT_ATOM('c', 'o', '6', '4')

#define ATOM_PREAMBLE_SIZE 8
#define COPY_BUFFER_SIZE 1024

int main(int argc, char *argv[])
{
    FILE *infile;
    FILE *outfile;
    unsigned char atom_bytes[ATOM_PREAMBLE_SIZE];
    uint32_t atom_type = 0;
    uint64_t atom_size;
    uint64_t last_offset;
    unsigned char *moov_atom;
    uint64_t moov_atom_size;
    uint64_t i, j;
    uint32_t offset_count;
    uint64_t current_offset;
    unsigned char copy_buffer[COPY_BUFFER_SIZE];
    int bytes_to_copy;

    if (argc != 3) {
        printf ("Usage: qt-faststart <infile.mov> <outfile.mov>\n");
        return 0;
    }

    infile = fopen(argv[1], "rb");
    if (!infile) {
        perror(argv[1]);
        return 1;
    }

    /* traverse through the atoms in the file to make sure that 'moov' is
     * at the end */
    while (!feof(infile)) {
        if (fread(atom_bytes, ATOM_PREAMBLE_SIZE, 1, infile) != 1) {
            break;
        }
        atom_size = BE_32(&atom_bytes[0]);
        atom_type = BE_32(&atom_bytes[4]);

        if ((atom_type != FREE_ATOM) &&
            (atom_type != JUNK_ATOM) &&
            (atom_type != MDAT_ATOM) &&
            (atom_type != MOOV_ATOM) &&
            (atom_type != PNOT_ATOM) &&
            (atom_type != SKIP_ATOM) &&
            (atom_type != WIDE_ATOM) &&
            (atom_type != PICT_ATOM) &&
            (atom_type != FTYP_ATOM)) {
            printf ("encountered non-QT top-level atom (is this a Quicktime file?)\n");
            break;
        }

        /* 64-bit special case */
        if (atom_size == 1) {
            if (fread(atom_bytes, ATOM_PREAMBLE_SIZE, 1, infile) != 1) {
                break;
            }
            atom_size = BE_64(&atom_bytes[0]);
            fseek(infile, atom_size - ATOM_PREAMBLE_SIZE * 2, SEEK_CUR);
        } else {
            fseek(infile, atom_size - ATOM_PREAMBLE_SIZE, SEEK_CUR);
        }
    }

    if (atom_type != MOOV_ATOM) {
        printf ("last atom in file was not a moov atom\n");
        fclose(infile);
        return 0;
    }

    /* moov atom was, in fact, the last atom in the chunk; load the whole
     * moov atom */
    fseek(infile, -atom_size, SEEK_END);
    last_offset = (uint64_t)ftell(infile);
    moov_atom_size = atom_size;
    moov_atom = malloc(moov_atom_size);
    if (!moov_atom) {
        printf ("could not allocate 0x%llX byte for moov atom\n",
            atom_size);
        fclose(infile);
        return 1;
    }
    if (fread(moov_atom, atom_size, 1, infile) != 1) {
        perror(argv[1]);
        free(moov_atom);
        fclose(infile);
        return 1;
    }

    /* this utility does not support compressed atoms yet, so disqualify
     * files with compressed QT atoms */
    if (BE_32(&moov_atom[12]) == CMOV_ATOM) {
        printf ("this utility does not support compressed moov atoms yet\n");
        free(moov_atom);
        fclose(infile);
        return 1;
    }

    /* close; will be re-opened later */
    fclose(infile);

    /* crawl through the moov chunk in search of stco or co64 atoms */
    for (i = 4; i < moov_atom_size - 4; i++) {
        atom_type = BE_32(&moov_atom[i]);
        if (atom_type == STCO_ATOM) {
            printf (" patching stco atom...\n");
            atom_size = BE_32(&moov_atom[i - 4]);
            if (i + atom_size - 4 > moov_atom_size) {
                printf (" bad atom size\n");
                free(moov_atom);
                return 1;
            }
            offset_count = BE_32(&moov_atom[i + 8]);
            for (j = 0; j < offset_count; j++) {
                current_offset = BE_32(&moov_atom[i + 12 + j * 4]);
                current_offset += moov_atom_size;
                moov_atom[i + 12 + j * 4 + 0] = (current_offset >> 24) & 0xFF;
                moov_atom[i + 12 + j * 4 + 1] = (current_offset >> 16) & 0xFF;
                moov_atom[i + 12 + j * 4 + 2] = (current_offset >>  8) & 0xFF;
                moov_atom[i + 12 + j * 4 + 3] = (current_offset >>  0) & 0xFF;
            }
            i += atom_size - 4;
        } else if (atom_type == CO64_ATOM) {
            printf (" patching co64 atom...\n");
            atom_size = BE_32(&moov_atom[i - 4]);
            if (i + atom_size - 4 > moov_atom_size) {
                printf (" bad atom size\n");
                free(moov_atom);
                return 1;
            }
            offset_count = BE_32(&moov_atom[i + 8]);
            for (j = 0; j < offset_count; j++) {
                current_offset = BE_64(&moov_atom[i + 12 + j * 8]);
                current_offset += moov_atom_size;
                moov_atom[i + 12 + j * 8 + 0] = (current_offset >> 56) & 0xFF;
                moov_atom[i + 12 + j * 8 + 1] = (current_offset >> 48) & 0xFF;
                moov_atom[i + 12 + j * 8 + 2] = (current_offset >> 40) & 0xFF;
                moov_atom[i + 12 + j * 8 + 3] = (current_offset >> 32) & 0xFF;
                moov_atom[i + 12 + j * 8 + 4] = (current_offset >> 24) & 0xFF;
                moov_atom[i + 12 + j * 8 + 5] = (current_offset >> 16) & 0xFF;
                moov_atom[i + 12 + j * 8 + 6] = (current_offset >>  8) & 0xFF;
                moov_atom[i + 12 + j * 8 + 7] = (current_offset >>  0) & 0xFF;
            }
            i += atom_size - 4;
        }
    }

    /* re-open the input file and open the output file */
    infile = fopen(argv[1], "rb");
    if (!infile) {
        perror(argv[1]);
        free(moov_atom);
        return 1;
    }
    outfile = fopen(argv[2], "wb");
    if (!outfile) {
        perror(argv[2]);
        fclose(outfile);
        free(moov_atom);
        return 1;
    }

    /* dump the new moov atom */
    printf (" writing moov atom...\n");
    if (fwrite(moov_atom, moov_atom_size, 1, outfile) != 1) {
        perror(argv[2]);
        goto error_out;
    }

    /* copy the remainder of the infile, from offset 0 -> last_offset - 1 */
    printf (" copying rest of file...\n");
    while (last_offset) {
        if (last_offset > COPY_BUFFER_SIZE)
            bytes_to_copy = COPY_BUFFER_SIZE;
        else
            bytes_to_copy = last_offset;

        if (fread(copy_buffer, bytes_to_copy, 1, infile) != 1) {
            perror(argv[1]);
            goto error_out;
        }
        if (fwrite(copy_buffer, bytes_to_copy, 1, outfile) != 1) {
            perror(argv[2]);
            goto error_out;
        }

        last_offset -= bytes_to_copy;
    }

    fclose(infile);
    fclose(outfile);
    free(moov_atom);

    return 0;

error_out:
    fclose(infile);
    fclose(outfile);
    free(moov_atom);
    return 1;
}
