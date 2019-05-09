/*
 * qt-faststart.c, v0.2
 * by Mike Melanson (melanson@pcisys.net)
 * This file is placed in the public domain. Use the program however you
 * see fit.
 *
 * This utility rearranges a Quicktime file such that the moov atom
 * is in front of the data, thus facilitating network streaming.
 *
 * To compile this program, start from the base directory from which you
 * are building FFmpeg and type:
 *  make tools/qt-faststart
 * The qt-faststart program will be built in the tools/ directory. If you
 * do not build the program in this manner, correct results are not
 * guaranteed, particularly on 64-bit platforms.
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
#include <string.h>
#include <limits.h>

#ifdef __MINGW32__
#undef fseeko
#define fseeko(x, y, z) fseeko64(x, y, z)
#undef ftello
#define ftello(x)       ftello64(x)
#elif defined(_WIN32)
#undef fseeko
#define fseeko(x, y, z) _fseeki64(x, y, z)
#undef ftello
#define ftello(x)       _ftelli64(x)
#endif

#define MIN(a,b) ((a) > (b) ? (b) : (a))

#define BE_32(x) (((uint32_t)(((uint8_t*)(x))[0]) << 24) |  \
                             (((uint8_t*)(x))[1]  << 16) |  \
                             (((uint8_t*)(x))[2]  <<  8) |  \
                              ((uint8_t*)(x))[3])

#define BE_64(x) (((uint64_t)(((uint8_t*)(x))[0]) << 56) |  \
                  ((uint64_t)(((uint8_t*)(x))[1]) << 48) |  \
                  ((uint64_t)(((uint8_t*)(x))[2]) << 40) |  \
                  ((uint64_t)(((uint8_t*)(x))[3]) << 32) |  \
                  ((uint64_t)(((uint8_t*)(x))[4]) << 24) |  \
                  ((uint64_t)(((uint8_t*)(x))[5]) << 16) |  \
                  ((uint64_t)(((uint8_t*)(x))[6]) <<  8) |  \
                  ((uint64_t)( (uint8_t*)(x))[7]))

#define AV_WB32(p, val)    {                    \
    ((uint8_t*)(p))[0] = ((val) >> 24) & 0xff;  \
    ((uint8_t*)(p))[1] = ((val) >> 16) & 0xff;  \
    ((uint8_t*)(p))[2] = ((val) >> 8) & 0xff;   \
    ((uint8_t*)(p))[3] = (val) & 0xff;          \
    }

#define AV_WB64(p, val)    {                    \
    AV_WB32(p, (val) >> 32)                     \
    AV_WB32(p + 4, val)                         \
    }

#define BE_FOURCC(ch0, ch1, ch2, ch3)           \
    ( (uint32_t)(unsigned char)(ch3)        |   \
     ((uint32_t)(unsigned char)(ch2) <<  8) |   \
     ((uint32_t)(unsigned char)(ch1) << 16) |   \
     ((uint32_t)(unsigned char)(ch0) << 24) )

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
#define UUID_ATOM QT_ATOM('u', 'u', 'i', 'd')

#define CMOV_ATOM QT_ATOM('c', 'm', 'o', 'v')
#define TRAK_ATOM QT_ATOM('t', 'r', 'a', 'k')
#define MDIA_ATOM QT_ATOM('m', 'd', 'i', 'a')
#define MINF_ATOM QT_ATOM('m', 'i', 'n', 'f')
#define STBL_ATOM QT_ATOM('s', 't', 'b', 'l')
#define STCO_ATOM QT_ATOM('s', 't', 'c', 'o')
#define CO64_ATOM QT_ATOM('c', 'o', '6', '4')

#define ATOM_PREAMBLE_SIZE    8
#define COPY_BUFFER_SIZE   33554432
#define MAX_FTYP_ATOM_SIZE 1048576

typedef struct {
    uint32_t type;
    uint32_t header_size;
    uint64_t size;
    unsigned char *data;
} atom_t;

typedef struct {
    uint64_t moov_atom_size;
    uint64_t stco_offset_count;
    uint64_t stco_data_size;
    int stco_overflow;
    uint32_t depth;
} update_chunk_offsets_context_t;

typedef struct {
    unsigned char *dest;
    uint64_t original_moov_size;
    uint64_t new_moov_size;
} upgrade_stco_context_t;

typedef int (*parse_atoms_callback_t)(void *context, atom_t *atom);

static int parse_atoms(
    unsigned char *buf,
    uint64_t size,
    parse_atoms_callback_t callback,
    void *context)
{
    unsigned char *pos = buf;
    unsigned char *end = pos + size;
    atom_t atom;
    int ret;

    while (end - pos >= ATOM_PREAMBLE_SIZE) {
        atom.size = BE_32(pos);
        atom.type = BE_32(pos + 4);
        pos += ATOM_PREAMBLE_SIZE;
        atom.header_size = ATOM_PREAMBLE_SIZE;

        switch (atom.size) {
        case 1:
            if (end - pos < 8) {
                fprintf(stderr, "not enough room for 64 bit atom size\n");
                return -1;
            }

            atom.size = BE_64(pos);
            pos += 8;
            atom.header_size = ATOM_PREAMBLE_SIZE + 8;
            break;

        case 0:
            atom.size = ATOM_PREAMBLE_SIZE + end - pos;
            break;
        }

        if (atom.size < atom.header_size) {
            fprintf(stderr, "atom size %"PRIu64" too small\n", atom.size);
            return -1;
        }

        atom.size -= atom.header_size;

        if (atom.size > end - pos) {
            fprintf(stderr, "atom size %"PRIu64" too big\n", atom.size);
            return -1;
        }

        atom.data = pos;
        ret = callback(context, &atom);
        if (ret < 0) {
            return ret;
        }

        pos += atom.size;
    }

    return 0;
}

static int update_stco_offsets(update_chunk_offsets_context_t *context, atom_t *atom)
{
    uint32_t current_offset;
    uint32_t offset_count;
    unsigned char *pos;
    unsigned char *end;

    printf(" patching stco atom...\n");
    if (atom->size < 8) {
        fprintf(stderr, "stco atom size %"PRIu64" too small\n", atom->size);
        return -1;
    }

    offset_count = BE_32(atom->data + 4);
    if (offset_count > (atom->size - 8) / 4) {
        fprintf(stderr, "stco offset count %"PRIu32" too big\n", offset_count);
        return -1;
    }

    context->stco_offset_count += offset_count;
    context->stco_data_size += atom->size - 8;

    for (pos = atom->data + 8, end = pos + offset_count * 4;
        pos < end;
        pos += 4) {
        current_offset = BE_32(pos);
        if (current_offset > UINT_MAX - context->moov_atom_size) {
            context->stco_overflow = 1;
        }
        current_offset += context->moov_atom_size;
        AV_WB32(pos, current_offset);
    }

    return 0;
}

static int update_co64_offsets(update_chunk_offsets_context_t *context, atom_t *atom)
{
    uint64_t current_offset;
    uint32_t offset_count;
    unsigned char *pos;
    unsigned char *end;

    printf(" patching co64 atom...\n");
    if (atom->size < 8) {
        fprintf(stderr, "co64 atom size %"PRIu64" too small\n", atom->size);
        return -1;
    }

    offset_count = BE_32(atom->data + 4);
    if (offset_count > (atom->size - 8) / 8) {
        fprintf(stderr, "co64 offset count %"PRIu32" too big\n", offset_count);
        return -1;
    }

    for (pos = atom->data + 8, end = pos + offset_count * 8;
        pos < end;
        pos += 8) {
        current_offset = BE_64(pos);
        current_offset += context->moov_atom_size;
        AV_WB64(pos, current_offset);
    }

    return 0;
}

static int update_chunk_offsets_callback(void *ctx, atom_t *atom)
{
    update_chunk_offsets_context_t *context = ctx;
    int ret;

    switch (atom->type) {
    case STCO_ATOM:
        return update_stco_offsets(context, atom);

    case CO64_ATOM:
        return update_co64_offsets(context, atom);

    case MOOV_ATOM:
    case TRAK_ATOM:
    case MDIA_ATOM:
    case MINF_ATOM:
    case STBL_ATOM:
        context->depth++;
        if (context->depth > 10) {
            fprintf(stderr, "atoms too deeply nested\n");
            return -1;
        }

        ret = parse_atoms(
            atom->data,
            atom->size,
            update_chunk_offsets_callback,
            context);
        context->depth--;
        return ret;
    }

    return 0;
}

static void set_atom_size(unsigned char *header, uint32_t header_size, uint64_t size)
{
    switch (header_size) {
    case 8:
        AV_WB32(header, size);
        break;

    case 16:
        AV_WB64(header + 8, size);
        break;
    }
}

static void upgrade_stco_atom(upgrade_stco_context_t *context, atom_t *atom)
{
    unsigned char *pos;
    unsigned char *end;
    uint64_t new_offset;
    uint32_t offset_count;
    uint32_t original_offset;

    /* Note: not performing validations since they were performed on the first pass */

    offset_count = BE_32(atom->data + 4);

    /* write the header */
    memcpy(context->dest, atom->data - atom->header_size, atom->header_size + 8);
    AV_WB32(context->dest + 4, CO64_ATOM);
    set_atom_size(context->dest, atom->header_size, atom->header_size + 8 + offset_count * 8);
    context->dest += atom->header_size + 8;

    /* write the data */
    for (pos = atom->data + 8, end = pos + offset_count * 4;
        pos < end;
        pos += 4) {
        original_offset = BE_32(pos) - context->original_moov_size;
        new_offset = (uint64_t)original_offset + context->new_moov_size;
        AV_WB64(context->dest, new_offset);
        context->dest += 8;
    }
}

static int upgrade_stco_callback(void *ctx, atom_t *atom)
{
    upgrade_stco_context_t *context = ctx;
    unsigned char *start_pos;
    uint64_t copy_size;

    switch (atom->type) {
    case STCO_ATOM:
        upgrade_stco_atom(context, atom);
        break;

    case MOOV_ATOM:
    case TRAK_ATOM:
    case MDIA_ATOM:
    case MINF_ATOM:
    case STBL_ATOM:
        /* write the atom header */
        memcpy(context->dest, atom->data - atom->header_size, atom->header_size);
        start_pos = context->dest;
        context->dest += atom->header_size;

        /* parse internal atoms*/
        if (parse_atoms(
            atom->data,
            atom->size,
            upgrade_stco_callback,
            context) < 0) {
            return -1;
        }

        /* update the atom size */
        set_atom_size(start_pos, atom->header_size, context->dest - start_pos);
        break;

    default:
        copy_size = atom->header_size + atom->size;
        memcpy(context->dest, atom->data - atom->header_size, copy_size);
        context->dest += copy_size;
        break;
    }

    return 0;
}

static int update_moov_atom(
    unsigned char **moov_atom,
    uint64_t *moov_atom_size)
{
    update_chunk_offsets_context_t update_context = { 0 };
    upgrade_stco_context_t upgrade_context;
    unsigned char *new_moov_atom;

    update_context.moov_atom_size = *moov_atom_size;

    if (parse_atoms(
        *moov_atom,
        *moov_atom_size,
        update_chunk_offsets_callback,
        &update_context) < 0) {
        return -1;
    }

    if (!update_context.stco_overflow) {
        return 0;
    }

    printf(" upgrading stco atoms to co64...\n");
    upgrade_context.new_moov_size = *moov_atom_size +
        update_context.stco_offset_count * 8 -
        update_context.stco_data_size;

    new_moov_atom = malloc(upgrade_context.new_moov_size);
    if (new_moov_atom == NULL) {
        fprintf(stderr, "could not allocate %"PRIu64" bytes for updated moov atom\n",
            upgrade_context.new_moov_size);
        return -1;
    }

    upgrade_context.original_moov_size = *moov_atom_size;
    upgrade_context.dest = new_moov_atom;

    if (parse_atoms(
        *moov_atom,
        *moov_atom_size,
        upgrade_stco_callback,
        &upgrade_context) < 0) {
        free(new_moov_atom);
        return -1;
    }

    free(*moov_atom);
    *moov_atom = new_moov_atom;
    *moov_atom_size = upgrade_context.new_moov_size;

    if (upgrade_context.dest != *moov_atom + *moov_atom_size) {
        fprintf(stderr, "unexpected - wrong number of moov bytes written\n");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    FILE *infile  = NULL;
    FILE *outfile = NULL;
    unsigned char atom_bytes[ATOM_PREAMBLE_SIZE];
    uint32_t atom_type   = 0;
    uint64_t atom_size   = 0;
    uint64_t atom_offset = 0;
    int64_t last_offset;
    unsigned char *moov_atom = NULL;
    unsigned char *ftyp_atom = NULL;
    uint64_t moov_atom_size;
    uint64_t ftyp_atom_size = 0;
    int64_t start_offset = 0;
    unsigned char *copy_buffer = NULL;
    int bytes_to_copy;
    uint64_t free_size = 0;
    uint64_t moov_size = 0;

    if (argc != 3) {
        printf("Usage: qt-faststart <infile.mov> <outfile.mov>\n"
               "Note: alternatively you can use -movflags +faststart in ffmpeg\n");
        return 0;
    }

    if (!strcmp(argv[1], argv[2])) {
        fprintf(stderr, "input and output files need to be different\n");
        return 1;
    }

    infile = fopen(argv[1], "rb");
    if (!infile) {
        perror(argv[1]);
        goto error_out;
    }

    /* traverse through the atoms in the file to make sure that 'moov' is
     * at the end */
    while (!feof(infile)) {
        if (fread(atom_bytes, ATOM_PREAMBLE_SIZE, 1, infile) != 1) {
            break;
        }
        atom_size = BE_32(&atom_bytes[0]);
        atom_type = BE_32(&atom_bytes[4]);

        /* keep ftyp atom */
        if (atom_type == FTYP_ATOM) {
            if (atom_size > MAX_FTYP_ATOM_SIZE) {
                fprintf(stderr, "ftyp atom size %"PRIu64" too big\n",
                       atom_size);
                goto error_out;
            }
            ftyp_atom_size = atom_size;
            free(ftyp_atom);
            ftyp_atom = malloc(ftyp_atom_size);
            if (!ftyp_atom) {
                fprintf(stderr, "could not allocate %"PRIu64" bytes for ftyp atom\n",
                       atom_size);
                goto error_out;
            }
            if (fseeko(infile, -ATOM_PREAMBLE_SIZE, SEEK_CUR) ||
                fread(ftyp_atom, atom_size, 1, infile) != 1 ||
                (start_offset = ftello(infile)) < 0) {
                perror(argv[1]);
                goto error_out;
            }
        } else {
            int ret;
            /* 64-bit special case */
            if (atom_size == 1) {
                if (fread(atom_bytes, ATOM_PREAMBLE_SIZE, 1, infile) != 1) {
                    break;
                }
                atom_size = BE_64(&atom_bytes[0]);
                ret = fseeko(infile, atom_size - ATOM_PREAMBLE_SIZE * 2, SEEK_CUR);
            } else {
                ret = fseeko(infile, atom_size - ATOM_PREAMBLE_SIZE, SEEK_CUR);
            }
            if (ret) {
                perror(argv[1]);
                goto error_out;
            }
        }
        printf("%c%c%c%c %10"PRIu64" %"PRIu64"\n",
               (atom_type >> 24) & 255,
               (atom_type >> 16) & 255,
               (atom_type >>  8) & 255,
               (atom_type >>  0) & 255,
               atom_offset,
               atom_size);
        if ((atom_type != FREE_ATOM) &&
            (atom_type != JUNK_ATOM) &&
            (atom_type != MDAT_ATOM) &&
            (atom_type != MOOV_ATOM) &&
            (atom_type != PNOT_ATOM) &&
            (atom_type != SKIP_ATOM) &&
            (atom_type != WIDE_ATOM) &&
            (atom_type != PICT_ATOM) &&
            (atom_type != UUID_ATOM) &&
            (atom_type != FTYP_ATOM)) {
            fprintf(stderr, "encountered non-QT top-level atom (is this a QuickTime file?)\n");
            break;
        }
        atom_offset += atom_size;

        /* The atom header is 8 (or 16 bytes), if the atom size (which
         * includes these 8 or 16 bytes) is less than that, we won't be
         * able to continue scanning sensibly after this atom, so break. */
        if (atom_size < 8)
            break;

        if (atom_type == MOOV_ATOM)
            moov_size = atom_size;

        if (moov_size && atom_type == FREE_ATOM) {
            free_size += atom_size;
            atom_type = MOOV_ATOM;
            atom_size = moov_size;
        }
    }

    if (atom_type != MOOV_ATOM) {
        printf("last atom in file was not a moov atom\n");
        free(ftyp_atom);
        fclose(infile);
        return 0;
    }

    if (atom_size < 16) {
        fprintf(stderr, "bad moov atom size\n");
        goto error_out;
    }

    /* moov atom was, in fact, the last atom in the chunk; load the whole
     * moov atom */
    if (fseeko(infile, -(atom_size + free_size), SEEK_END)) {
        perror(argv[1]);
        goto error_out;
    }
    last_offset    = ftello(infile);
    if (last_offset < 0) {
        perror(argv[1]);
        goto error_out;
    }
    moov_atom_size = atom_size;
    moov_atom      = malloc(moov_atom_size);
    if (!moov_atom) {
        fprintf(stderr, "could not allocate %"PRIu64" bytes for moov atom\n", atom_size);
        goto error_out;
    }
    if (fread(moov_atom, atom_size, 1, infile) != 1) {
        perror(argv[1]);
        goto error_out;
    }

    /* this utility does not support compressed atoms yet, so disqualify
     * files with compressed QT atoms */
    if (BE_32(&moov_atom[12]) == CMOV_ATOM) {
        fprintf(stderr, "this utility does not support compressed moov atoms yet\n");
        goto error_out;
    }

    /* close; will be re-opened later */
    fclose(infile);
    infile = NULL;

    if (update_moov_atom(&moov_atom, &moov_atom_size) < 0) {
        goto error_out;
    }

    /* re-open the input file and open the output file */
    infile = fopen(argv[1], "rb");
    if (!infile) {
        perror(argv[1]);
        goto error_out;
    }

    if (start_offset > 0) { /* seek after ftyp atom */
        if (fseeko(infile, start_offset, SEEK_SET)) {
            perror(argv[1]);
            goto error_out;
        }

        last_offset -= start_offset;
    }

    outfile = fopen(argv[2], "wb");
    if (!outfile) {
        perror(argv[2]);
        goto error_out;
    }

    /* dump the same ftyp atom */
    if (ftyp_atom_size > 0) {
        printf(" writing ftyp atom...\n");
        if (fwrite(ftyp_atom, ftyp_atom_size, 1, outfile) != 1) {
            perror(argv[2]);
            goto error_out;
        }
    }

    /* dump the new moov atom */
    printf(" writing moov atom...\n");
    if (fwrite(moov_atom, moov_atom_size, 1, outfile) != 1) {
        perror(argv[2]);
        goto error_out;
    }

    /* copy the remainder of the infile, from offset 0 -> last_offset - 1 */
    bytes_to_copy = MIN(COPY_BUFFER_SIZE, last_offset);
    copy_buffer = malloc(bytes_to_copy);
    if (!copy_buffer) {
        fprintf(stderr, "could not allocate %d bytes for copy_buffer\n", bytes_to_copy);
        goto error_out;
    }
    printf(" copying rest of file...\n");
    while (last_offset) {
        bytes_to_copy = MIN(bytes_to_copy, last_offset);

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
    free(ftyp_atom);
    free(copy_buffer);

    return 0;

error_out:
    if (infile)
        fclose(infile);
    if (outfile)
        fclose(outfile);
    free(moov_atom);
    free(ftyp_atom);
    free(copy_buffer);
    return 1;
}
