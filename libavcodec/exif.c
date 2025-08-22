/*
 * EXIF metadata parser
 * Copyright (c) 2013 Thilo Borgmann <thilo.borgmann _at_ mail.de>
 * Copyright (c) 2024-2025 Leo Izen <leo.izen@gmail.com>
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

/**
 * @file
 * EXIF metadata parser
 * @author Thilo Borgmann <thilo.borgmann _at_ mail.de>
 * @author Leo Izen <leo.izen@gmail.com>
 */

#include <inttypes.h>

#include "libavutil/avconfig.h"
#include "libavutil/bprint.h"
#include "libavutil/display.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "bytestream.h"
#include "exif_internal.h"
#include "tiff_common.h"

#define EXIF_II_LONG           0x49492a00
#define EXIF_MM_LONG           0x4d4d002a

#define BASE_TAG_SIZE          12
#define IFD_EXTRA_SIZE         6

#define EXIF_TAG_NAME_LENGTH   32
#define MAKERNOTE_TAG          0x927c
#define ORIENTATION_TAG        0x112
#define EXIFIFD_TAG            0x8769
#define IMAGE_WIDTH_TAG        0x100
#define IMAGE_LENGTH_TAG       0x101
#define PIXEL_X_TAG            0xa002
#define PIXEL_Y_TAG            0xa003

struct exif_tag {
    const char name[EXIF_TAG_NAME_LENGTH];
    uint16_t id;
};

static const struct exif_tag tag_list[] = { // JEITA CP-3451 EXIF specification:
    {"GPSVersionID",               0x00}, // <- Table 12 GPS Attribute Information
    {"GPSLatitudeRef",             0x01},
    {"GPSLatitude",                0x02},
    {"GPSLongitudeRef",            0x03},
    {"GPSLongitude",               0x04},
    {"GPSAltitudeRef",             0x05},
    {"GPSAltitude",                0x06},
    {"GPSTimeStamp",               0x07},
    {"GPSSatellites",              0x08},
    {"GPSStatus",                  0x09},
    {"GPSMeasureMode",             0x0A},
    {"GPSDOP",                     0x0B},
    {"GPSSpeedRef",                0x0C},
    {"GPSSpeed",                   0x0D},
    {"GPSTrackRef",                0x0E},
    {"GPSTrack",                   0x0F},
    {"GPSImgDirectionRef",         0x10},
    {"GPSImgDirection",            0x11},
    {"GPSMapDatum",                0x12},
    {"GPSDestLatitudeRef",         0x13},
    {"GPSDestLatitude",            0x14},
    {"GPSDestLongitudeRef",        0x15},
    {"GPSDestLongitude",           0x16},
    {"GPSDestBearingRef",          0x17},
    {"GPSDestBearing",             0x18},
    {"GPSDestDistanceRef",         0x19},
    {"GPSDestDistance",            0x1A},
    {"GPSProcessingMethod",        0x1B},
    {"GPSAreaInformation",         0x1C},
    {"GPSDateStamp",               0x1D},
    {"GPSDifferential",            0x1E},
    {"ImageWidth",                 0x100}, // <- Table 3 TIFF Rev. 6.0 Attribute Information Used in Exif
    {"ImageLength",                0x101},
    {"BitsPerSample",              0x102},
    {"Compression",                0x103},
    {"PhotometricInterpretation",  0x106},
    {"Orientation",                0x112},
    {"SamplesPerPixel",            0x115},
    {"PlanarConfiguration",        0x11C},
    {"YCbCrSubSampling",           0x212},
    {"YCbCrPositioning",           0x213},
    {"XResolution",                0x11A},
    {"YResolution",                0x11B},
    {"ResolutionUnit",             0x128},
    {"StripOffsets",               0x111},
    {"RowsPerStrip",               0x116},
    {"StripByteCounts",            0x117},
    {"JPEGInterchangeFormat",      0x201},
    {"JPEGInterchangeFormatLength",0x202},
    {"TransferFunction",           0x12D},
    {"WhitePoint",                 0x13E},
    {"PrimaryChromaticities",      0x13F},
    {"YCbCrCoefficients",          0x211},
    {"ReferenceBlackWhite",        0x214},
    {"DateTime",                   0x132},
    {"ImageDescription",           0x10E},
    {"Make",                       0x10F},
    {"Model",                      0x110},
    {"Software",                   0x131},
    {"Artist",                     0x13B},
    {"Copyright",                  0x8298},
    {"ExifVersion",                0x9000}, // <- Table 4 Exif IFD Attribute Information (1)
    {"FlashpixVersion",            0xA000},
    {"ColorSpace",                 0xA001},
    {"ComponentsConfiguration",    0x9101},
    {"CompressedBitsPerPixel",     0x9102},
    {"PixelXDimension",            0xA002},
    {"PixelYDimension",            0xA003},
    {"MakerNote",                  0x927C},
    {"UserComment",                0x9286},
    {"RelatedSoundFile",           0xA004},
    {"DateTimeOriginal",           0x9003},
    {"DateTimeDigitized",          0x9004},
    {"SubSecTime",                 0x9290},
    {"SubSecTimeOriginal",         0x9291},
    {"SubSecTimeDigitized",        0x9292},
    {"ImageUniqueID",              0xA420},
    {"ExposureTime",               0x829A}, // <- Table 5 Exif IFD Attribute Information (2)
    {"FNumber",                    0x829D},
    {"ExposureProgram",            0x8822},
    {"SpectralSensitivity",        0x8824},
    {"ISOSpeedRatings",            0x8827},
    {"OECF",                       0x8828},
    {"ShutterSpeedValue",          0x9201},
    {"ApertureValue",              0x9202},
    {"BrightnessValue",            0x9203},
    {"ExposureBiasValue",          0x9204},
    {"MaxApertureValue",           0x9205},
    {"SubjectDistance",            0x9206},
    {"MeteringMode",               0x9207},
    {"LightSource",                0x9208},
    {"Flash",                      0x9209},
    {"FocalLength",                0x920A},
    {"SubjectArea",                0x9214},
    {"FlashEnergy",                0xA20B},
    {"SpatialFrequencyResponse",   0xA20C},
    {"FocalPlaneXResolution",      0xA20E},
    {"FocalPlaneYResolution",      0xA20F},
    {"FocalPlaneResolutionUnit",   0xA210},
    {"SubjectLocation",            0xA214},
    {"ExposureIndex",              0xA215},
    {"SensingMethod",              0xA217},
    {"FileSource",                 0xA300},
    {"SceneType",                  0xA301},
    {"CFAPattern",                 0xA302},
    {"CustomRendered",             0xA401},
    {"ExposureMode",               0xA402},
    {"WhiteBalance",               0xA403},
    {"DigitalZoomRatio",           0xA404},
    {"FocalLengthIn35mmFilm",      0xA405},
    {"SceneCaptureType",           0xA406},
    {"GainControl",                0xA407},
    {"Contrast",                   0xA408},
    {"Saturation",                 0xA409},
    {"Sharpness",                  0xA40A},
    {"DeviceSettingDescription",   0xA40B},
    {"SubjectDistanceRange",       0xA40C},

    /* InteropIFD tags */
    {"RelatedImageFileFormat",     0x1000},
    {"RelatedImageWidth",          0x1001},
    {"RelatedImageLength",         0x1002},

    /* private EXIF tags */
    {"PrintImageMatching",         0xC4A5}, // <- undocumented meaning

    /* IFD tags */
    {"ExifIFD",                    0x8769}, // <- An IFD pointing to standard Exif metadata
    {"GPSInfo",                    0x8825}, // <- An IFD pointing to GPS Exif Metadata
    {"InteropIFD",                 0xA005}, // <- Table 13 Interoperability IFD Attribute Information
    {"GlobalParametersIFD",        0x0190},
    {"ProfileIFD",                 0xc6f5},
};

/* same as type_sizes but with string == 1 */
static const size_t exif_sizes[] = {
    [0] = 0,
    [AV_TIFF_BYTE] = 1,
    [AV_TIFF_STRING] = 1,
    [AV_TIFF_SHORT] = 2,
    [AV_TIFF_LONG] = 4,
    [AV_TIFF_RATIONAL] = 8,
    [AV_TIFF_SBYTE] = 1,
    [AV_TIFF_UNDEFINED] = 1,
    [AV_TIFF_SSHORT] = 2,
    [AV_TIFF_SLONG] = 4,
    [AV_TIFF_SRATIONAL] = 8,
    [AV_TIFF_FLOAT] = 4,
    [AV_TIFF_DOUBLE] = 8,
    [AV_TIFF_IFD] = 4,
};

const char *av_exif_get_tag_name(uint16_t id)
{
    for (size_t i = 0; i < FF_ARRAY_ELEMS(tag_list); i++) {
        if (tag_list[i].id == id)
            return tag_list[i].name;
    }

    return NULL;
}

int32_t av_exif_get_tag_id(const char *name)
{
    if (!name)
        return -1;

    for (size_t i = 0; i < FF_ARRAY_ELEMS(tag_list); i++) {
        if (!strcmp(tag_list[i].name, name))
            return tag_list[i].id;
    }

    return -1;
}

static inline void tput16(PutByteContext *pb, const int le, const uint16_t value)
{
    le ? bytestream2_put_le16(pb, value) : bytestream2_put_be16(pb, value);
}

static inline void tput32(PutByteContext *pb, const int le, const uint32_t value)
{
    le ? bytestream2_put_le32(pb, value) : bytestream2_put_be32(pb, value);
}

static inline void tput64(PutByteContext *pb, const int le, const uint64_t value)
{
    le ? bytestream2_put_le64(pb, value) : bytestream2_put_be64(pb, value);
}

static int exif_read_values(void *logctx, GetByteContext *gb, int le, AVExifEntry *entry)
{
    switch (entry->type) {
        case AV_TIFF_SHORT:
        case AV_TIFF_LONG:
            entry->value.uint = av_calloc(entry->count, sizeof(*entry->value.uint));
            break;
        case AV_TIFF_SSHORT:
        case AV_TIFF_SLONG:
            entry->value.sint = av_calloc(entry->count, sizeof(*entry->value.sint));
            break;
        case AV_TIFF_DOUBLE:
        case AV_TIFF_FLOAT:
            entry->value.dbl = av_calloc(entry->count, sizeof(*entry->value.dbl));
            break;
        case AV_TIFF_RATIONAL:
        case AV_TIFF_SRATIONAL:
            entry->value.rat = av_calloc(entry->count, sizeof(*entry->value.rat));
            break;
        case AV_TIFF_UNDEFINED:
        case AV_TIFF_BYTE:
            entry->value.ubytes = av_mallocz(entry->count);
            break;
        case AV_TIFF_SBYTE:
            entry->value.sbytes = av_mallocz(entry->count);
            break;
        case AV_TIFF_STRING:
            entry->value.str = av_mallocz(entry->count + 1);
            break;
        case AV_TIFF_IFD:
            av_log(logctx, AV_LOG_WARNING, "Bad IFD type for non-IFD tag\n");
            return AVERROR_INVALIDDATA;
    }
    if (!entry->value.ptr)
        return AVERROR(ENOMEM);
    switch (entry->type) {
        case AV_TIFF_SHORT:
            for (size_t i = 0; i < entry->count; i++)
                entry->value.uint[i] = ff_tget_short(gb, le);
            break;
        case AV_TIFF_LONG:
            for (size_t i = 0; i < entry->count; i++)
                entry->value.uint[i] = ff_tget_long(gb, le);
            break;
        case AV_TIFF_SSHORT:
            for (size_t i = 0; i < entry->count; i++)
                entry->value.sint[i] = (int16_t) ff_tget_short(gb, le);
            break;
        case AV_TIFF_SLONG:
            for (size_t i = 0; i < entry->count; i++)
                entry->value.sint[i] = (int32_t) ff_tget_long(gb, le);
            break;
        case AV_TIFF_DOUBLE:
            for (size_t i = 0; i < entry->count; i++)
                entry->value.dbl[i] = ff_tget_double(gb, le);
            break;
        case AV_TIFF_FLOAT:
            for (size_t i = 0; i < entry->count; i++) {
                av_alias32 alias = { .u32 = ff_tget_long(gb, le) };
                entry->value.dbl[i] = alias.f32;
            }
            break;
        case AV_TIFF_RATIONAL:
        case AV_TIFF_SRATIONAL:
            for (size_t i = 0; i < entry->count; i++) {
                int32_t num = ff_tget_long(gb, le);
                int32_t den = ff_tget_long(gb, le);
                entry->value.rat[i] = av_make_q(num, den);
            }
            break;
        case AV_TIFF_UNDEFINED:
        case AV_TIFF_BYTE:
            /* these three fields are aliased to entry->value.ptr via a union */
            /* and entry->value.ptr will always be nonzero here */
            av_assert0(entry->value.ubytes);
            bytestream2_get_buffer(gb, entry->value.ubytes, entry->count);
            break;
        case AV_TIFF_SBYTE:
            av_assert0(entry->value.sbytes);
            bytestream2_get_buffer(gb, entry->value.sbytes, entry->count);
            break;
        case AV_TIFF_STRING:
            av_assert0(entry->value.str);
            bytestream2_get_buffer(gb, entry->value.str, entry->count);
            break;
    }

    return 0;
}

static void exif_write_values(PutByteContext *pb, int le, const AVExifEntry *entry)
{
    switch (entry->type) {
        case AV_TIFF_SHORT:
            for (size_t i = 0; i < entry->count; i++)
                tput16(pb, le, entry->value.uint[i]);
            break;
        case AV_TIFF_LONG:
            for (size_t i = 0; i < entry->count; i++)
                tput32(pb, le, entry->value.uint[i]);
            break;
        case AV_TIFF_SSHORT:
            for (size_t i = 0; i < entry->count; i++)
                tput16(pb, le, entry->value.sint[i]);
            break;
        case AV_TIFF_SLONG:
            for (size_t i = 0; i < entry->count; i++)
                tput32(pb, le, entry->value.sint[i]);
            break;
        case AV_TIFF_DOUBLE:
            for (size_t i = 0; i < entry->count; i++) {
                const av_alias64 a = { .f64 = entry->value.dbl[i] };
                tput64(pb, le, a.u64);
            }
            break;
        case AV_TIFF_FLOAT:
            for (size_t i = 0; i < entry->count; i++) {
                const av_alias32 a = { .f32 = entry->value.dbl[i] };
                tput32(pb, le, a.u32);
            }
            break;
        case AV_TIFF_RATIONAL:
        case AV_TIFF_SRATIONAL:
            for (size_t i = 0; i < entry->count; i++) {
                tput32(pb, le, entry->value.rat[i].num);
                tput32(pb, le, entry->value.rat[i].den);
            }
            break;
        case AV_TIFF_UNDEFINED:
        case AV_TIFF_BYTE:
            bytestream2_put_buffer(pb, entry->value.ubytes, entry->count);
            break;
        case AV_TIFF_SBYTE:
            bytestream2_put_buffer(pb, entry->value.sbytes, entry->count);
            break;
        case AV_TIFF_STRING:
            bytestream2_put_buffer(pb, entry->value.str, entry->count);
            break;
    }
}

static const uint8_t aoc_header[] = { 'A', 'O', 'C', 0, };
static const uint8_t casio_header[] = { 'Q', 'V', 'C', 0, 0, 0, };
static const uint8_t foveon_header[] = { 'F', 'O', 'V', 'E', 'O', 'N', 0, 0, };
static const uint8_t fuji_header[] = { 'F', 'U', 'J', 'I', };
static const uint8_t nikon_header[] = { 'N', 'i', 'k', 'o', 'n', 0, };
static const uint8_t olympus1_header[] = { 'O', 'L', 'Y', 'M', 'P', 0, };
static const uint8_t olympus2_header[] = { 'O', 'L', 'Y', 'M', 'P', 'U', 'S', 0, 'I', 'I', };
static const uint8_t panasonic_header[] = { 'P', 'a', 'n', 'a', 's', 'o', 'n', 'i', 'c', 0, 0, 0, };
static const uint8_t sigma_header[] = { 'S', 'I', 'G', 'M', 'A', 0, 0, 0, };
static const uint8_t sony_header[] = { 'S', 'O', 'N', 'Y', ' ', 'D', 'S', 'C', ' ', 0, 0, 0, };

struct exif_makernote_data {
    const uint8_t *header;
    size_t header_size;
    int result;
};

#define MAKERNOTE_STRUCT(h, r) { \
    .header = (h),               \
    .header_size = sizeof((h)),  \
    .result = (r),               \
}

static const struct exif_makernote_data makernote_data[] = {
    MAKERNOTE_STRUCT(aoc_header, 6),
    MAKERNOTE_STRUCT(casio_header, -1),
    MAKERNOTE_STRUCT(foveon_header, 10),
    MAKERNOTE_STRUCT(fuji_header, -1),
    MAKERNOTE_STRUCT(olympus1_header, 8),
    MAKERNOTE_STRUCT(olympus2_header, -1),
    MAKERNOTE_STRUCT(panasonic_header, 12),
    MAKERNOTE_STRUCT(sigma_header, 10),
    MAKERNOTE_STRUCT(sony_header, 12),
};

/*
 * derived from Exiv2 MakerNote's article
 * https://exiv2.org/makernote.html or archived at
 * https://web.archive.org/web/20250311155857/https://exiv2.org/makernote.html
 */
static int exif_get_makernote_offset(GetByteContext *gb)
{
    if (bytestream2_get_bytes_left(gb) < BASE_TAG_SIZE)
        return -1;

    for (int i = 0; i < FF_ARRAY_ELEMS(makernote_data); i++) {
        if (!memcmp(gb->buffer, makernote_data[i].header, makernote_data[i].header_size))
            return makernote_data[i].result;
    }

    if (!memcmp(gb->buffer, nikon_header, sizeof(nikon_header))) {
        if (bytestream2_get_bytes_left(gb) < 14)
            return -1;
        else if (AV_RB32(gb->buffer + 10) == EXIF_MM_LONG || AV_RB32(gb->buffer + 10) == EXIF_II_LONG)
            return -1;
        return 8;
    }

    return 0;
}

static int exif_parse_ifd_list(void *logctx, GetByteContext *gb, int le,
                               int depth, AVExifMetadata *ifd);

static int exif_decode_tag(void *logctx, GetByteContext *gb, int le,
                           int depth, AVExifEntry *entry)
{
    int ret = 0, makernote_offset = -1, tell, is_ifd, count;
    enum AVTiffDataType type;
    uint32_t payload;

    /* safety check to prevent infinite recursion on malicious IFDs */
    if (depth > 3)
        return AVERROR_INVALIDDATA;

    tell = bytestream2_tell(gb);

    entry->id = ff_tget_short(gb, le);
    type = ff_tget_short(gb, le);
    count = ff_tget_long(gb, le);
    payload = ff_tget_long(gb, le);

    av_log(logctx, AV_LOG_DEBUG, "TIFF Tag: id: 0x%04x, type: %d, count: %u, offset: %d, "
                                 "payload: %" PRIu32 "\n", entry->id, type, count, tell, payload);

    /* AV_TIFF_IFD is the largest, numerically */
    if (type > AV_TIFF_IFD)
        return AVERROR_INVALIDDATA;

    is_ifd = type == AV_TIFF_IFD || ff_tis_ifd(entry->id) || entry->id == MAKERNOTE_TAG;

    if (is_ifd) {
        if (!payload)
            goto end;
        bytestream2_seek(gb, payload, SEEK_SET);
    }

    if (entry->id == MAKERNOTE_TAG) {
        makernote_offset = exif_get_makernote_offset(gb);
        if (makernote_offset < 0)
            is_ifd = 0;
    }

    if (is_ifd) {
        entry->type = AV_TIFF_IFD;
        entry->count = 1;
        entry->ifd_offset = makernote_offset > 0 ? makernote_offset : 0;
        if (entry->ifd_offset) {
            entry->ifd_lead = av_malloc(entry->ifd_offset);
            if (!entry->ifd_lead)
                return AVERROR(ENOMEM);
            bytestream2_get_buffer(gb, entry->ifd_lead, entry->ifd_offset);
        }
        ret = exif_parse_ifd_list(logctx, gb, le, depth + 1, &entry->value.ifd);
        if (ret < 0 && entry->id == MAKERNOTE_TAG) {
            /*
             * we guessed that MakerNote was an IFD
             * but we were probably incorrect at this
             * point so we try again as a binary blob
             */
            av_exif_free(&entry->value.ifd);
            av_log(logctx, AV_LOG_DEBUG, "unrecognized MakerNote IFD, retrying as blob\n");
            is_ifd = 0;
        }
    }

    /* inverted condition instead of else so we can fall through from above */
    if (!is_ifd) {
        entry->type = type == AV_TIFF_IFD ? AV_TIFF_UNDEFINED : type;
        entry->count = count;
        bytestream2_seek(gb, count * exif_sizes[type] > 4 ? payload : tell + 8, SEEK_SET);
        ret = exif_read_values(logctx, gb, le, entry);
    }

end:
    bytestream2_seek(gb, tell + BASE_TAG_SIZE, SEEK_SET);

    return ret;
}

static int exif_parse_ifd_list(void *logctx, GetByteContext *gb, int le,
                               int depth, AVExifMetadata *ifd)
{
    uint32_t entries;
    size_t required_size;
    void *temp;

    av_log(logctx, AV_LOG_DEBUG, "parsing IFD list at offset: %d\n", bytestream2_tell(gb));

    if (bytestream2_get_bytes_left(gb) < 2) {
        av_log(logctx, AV_LOG_ERROR, "not enough bytes remaining in EXIF buffer: 2 required\n");
        return AVERROR_INVALIDDATA;
    }

    entries = ff_tget_short(gb, le);
    if (bytestream2_get_bytes_left(gb) < entries * BASE_TAG_SIZE) {
        av_log(logctx, AV_LOG_ERROR, "not enough bytes remaining in EXIF buffer. entries: %" PRIu32 "\n", entries);
        return AVERROR_INVALIDDATA;
    }
    if (entries > 4096) {
        /* that is a lot of entries, probably an error */
        av_log(logctx, AV_LOG_ERROR, "too many entries: %" PRIu32 "\n", entries);
        return AVERROR_INVALIDDATA;
    }

    ifd->count = entries;
    av_log(logctx, AV_LOG_DEBUG, "entry count for IFD: %u\n", ifd->count);

    /* empty IFD is technically legal but equivalent to no metadata present */
    if (!ifd->count)
        goto end;

    if (av_size_mult(ifd->count, sizeof(*ifd->entries), &required_size) < 0)
        return AVERROR(ENOMEM);
    temp = av_fast_realloc(ifd->entries, &ifd->size, required_size);
    if (!temp) {
        av_freep(&ifd->entries);
        return AVERROR(ENOMEM);
    }
    ifd->entries = temp;

    /* entries have pointers in them which can cause issues if */
    /* they are freed or realloc'd when garbage */
    memset(ifd->entries, 0, required_size);

    for (uint32_t i = 0; i < entries; i++) {
        int ret = exif_decode_tag(logctx, gb, le, depth, &ifd->entries[i]);
        if (ret < 0)
            return ret;
    }

end:
    /*
     * at the end of an IFD is an pointer to the next IFD
     * or zero if there are no more IFDs, which is usually the case
     */
    return ff_tget_long(gb, le);
}

/*
 * note that this function does not free the entry pointer itself
 * because it's probably part of a larger array that should be freed
 * all at once
 */
static void exif_free_entry(AVExifEntry *entry)
{
    if (!entry)
        return;
    if (entry->type == AV_TIFF_IFD)
        av_exif_free(&entry->value.ifd);
    else
        av_freep(&entry->value.ptr);
    av_freep(&entry->ifd_lead);
}

void av_exif_free(AVExifMetadata *ifd)
{
    if (!ifd)
        return;
    if (!ifd->entries) {
        ifd->count = 0;
        ifd->size = 0;
        return;
    }
    for (size_t i = 0; i < ifd->count; i++) {
        AVExifEntry *entry = &ifd->entries[i];
        exif_free_entry(entry);
    }
    av_freep(&ifd->entries);
    ifd->count = 0;
    ifd->size = 0;
}

static size_t exif_get_ifd_size(const AVExifMetadata *ifd)
{
    /* 6 == 4 + 2; 2-byte entry-count at the beginning */
    /* plus 4-byte next-IFD pointer at the end */
    size_t total_size = IFD_EXTRA_SIZE;
    for (size_t i = 0; i < ifd->count; i++) {
        const AVExifEntry *entry = &ifd->entries[i];
        if (entry->type == AV_TIFF_IFD) {
            total_size += BASE_TAG_SIZE + exif_get_ifd_size(&entry->value.ifd) + entry->ifd_offset;
        } else {
            size_t payload_size = entry->count * exif_sizes[entry->type];
            total_size += BASE_TAG_SIZE + (payload_size > 4 ? payload_size : 0);
        }
    }
    return total_size;
}

static int exif_write_ifd(void *logctx, PutByteContext *pb, int le, int depth, const AVExifMetadata *ifd)
{
    int offset, ret, tell, tell2;
    tell = bytestream2_tell_p(pb);
    tput16(pb, le, ifd->count);
    offset = tell + IFD_EXTRA_SIZE + BASE_TAG_SIZE * (uint32_t) ifd->count;
    av_log(logctx, AV_LOG_DEBUG, "writing IFD with %u entries and initial offset %d\n", ifd->count, offset);
    for (size_t i = 0; i < ifd->count; i++) {
        const AVExifEntry *entry = &ifd->entries[i];
        av_log(logctx, AV_LOG_DEBUG, "writing TIFF entry: id: 0x%04" PRIx16 ", type: %d, count: %"
                                      PRIu32 ", offset: %d, offset value: %d\n",
                                      entry->id, entry->type, entry->count,
                                      bytestream2_tell_p(pb), offset);
        tput16(pb, le, entry->id);
        if (entry->id == MAKERNOTE_TAG && entry->type == AV_TIFF_IFD) {
            size_t ifd_size = exif_get_ifd_size(&entry->value.ifd);
            tput16(pb, le, AV_TIFF_UNDEFINED);
            tput32(pb, le, ifd_size);
        } else {
            tput16(pb, le, entry->type);
            tput32(pb, le, entry->count);
        }
        if (entry->type == AV_TIFF_IFD) {
            tput32(pb, le, offset);
            tell2 = bytestream2_tell_p(pb);
            bytestream2_seek_p(pb, offset, SEEK_SET);
            if (entry->ifd_offset)
                bytestream2_put_buffer(pb, entry->ifd_lead, entry->ifd_offset);
            ret = exif_write_ifd(logctx, pb, le, depth + 1, &entry->value.ifd);
            if (ret < 0)
                return ret;
            offset += ret + entry->ifd_offset;
            bytestream2_seek_p(pb, tell2, SEEK_SET);
        } else {
            size_t payload_size = entry->count * exif_sizes[entry->type];
            if (payload_size > 4) {
                tput32(pb, le, offset);
                tell2 = bytestream2_tell_p(pb);
                bytestream2_seek_p(pb, offset, SEEK_SET);
                exif_write_values(pb, le, entry);
                offset += payload_size;
                bytestream2_seek_p(pb, tell2, SEEK_SET);
            } else {
                /* zero uninitialized excess payload values */
                AV_WN32(pb->buffer, 0);
                exif_write_values(pb, le, entry);
                bytestream2_seek_p(pb, 4 - payload_size, SEEK_CUR);
            }
        }
    }

    /*
     * we write 0 if this is the top-level exif IFD
     * indicating that there are no more IFD pointers
     */
    tput32(pb, le, depth ? offset : 0);
    return offset - tell;
}

int av_exif_write(void *logctx, const AVExifMetadata *ifd, AVBufferRef **buffer, enum AVExifHeaderMode header_mode)
{
    AVBufferRef *buf = NULL;
    size_t size, headsize = 8;
    PutByteContext pb;
    int ret, off = 0;

    int le = 1;

    if (*buffer)
        return AVERROR(EINVAL);

    size = exif_get_ifd_size(ifd);
    switch (header_mode) {
        case AV_EXIF_EXIF00:
            off = 6;
            break;
        case AV_EXIF_T_OFF:
            off = 4;
            break;
        case AV_EXIF_ASSUME_BE:
            le = 0;
            headsize = 0;
            break;
        case AV_EXIF_ASSUME_LE:
            le = 1;
            headsize = 0;
            break;
    }
    buf = av_buffer_alloc(size + off + headsize);
    if (!buf)
        return AVERROR(ENOMEM);

    if (header_mode == AV_EXIF_EXIF00) {
        AV_WL32(buf->data, MKTAG('E','x','i','f'));
        AV_WN16(buf->data + 4, 0);
    } else if (header_mode == AV_EXIF_T_OFF) {
        AV_WN32(buf->data, 0);
    }

    bytestream2_init_writer(&pb, buf->data + off, buf->size - off);

    if (header_mode != AV_EXIF_ASSUME_BE && header_mode != AV_EXIF_ASSUME_LE) {
        /* these constants are be32 in both cases */
        /* le == 1 always in this case */
        bytestream2_put_be32(&pb, EXIF_II_LONG);
        tput32(&pb, le, 8);
    }

    ret = exif_write_ifd(logctx, &pb, le, 0, ifd);
    if (ret < 0) {
        av_buffer_unref(&buf);
        av_log(logctx, AV_LOG_ERROR, "error writing EXIF data: %s\n", av_err2str(ret));
        return ret;
    }

    *buffer = buf;

    return 0;
}

int av_exif_parse_buffer(void *logctx, const uint8_t *buf, size_t size,
                         AVExifMetadata *ifd, enum AVExifHeaderMode header_mode)
{
    int ret, le;
    GetByteContext gbytes;
    if (size > INT_MAX)
        return AVERROR(EINVAL);
    size_t off = 0;
    switch (header_mode) {
        case AV_EXIF_EXIF00:
            if (size < 6)
                return AVERROR_INVALIDDATA;
            off = 6;
            /* fallthrough */
        case AV_EXIF_T_OFF:
            if (size < 4)
                return AVERROR_INVALIDDATA;
            if (!off)
                off = AV_RB32(buf) + 4;
            /* fallthrough */
        case AV_EXIF_TIFF_HEADER: {
            int ifd_offset;
            if (size <= off)
                return AVERROR_INVALIDDATA;
            bytestream2_init(&gbytes, buf + off, size - off);
            // read TIFF header
            ret = ff_tdecode_header(&gbytes, &le, &ifd_offset);
            if (ret < 0) {
                av_log(logctx, AV_LOG_ERROR, "invalid TIFF header in EXIF data: %s\n", av_err2str(ret));
                return ret;
            }
            bytestream2_seek(&gbytes, ifd_offset, SEEK_SET);
            break;
        }
        case AV_EXIF_ASSUME_LE:
            le = 1;
            bytestream2_init(&gbytes, buf, size);
            break;
        case AV_EXIF_ASSUME_BE:
            le = 0;
            bytestream2_init(&gbytes, buf, size);
            break;
        default:
            return AVERROR(EINVAL);
    }

    /*
     * parse IFD0 here. If the return value is positive that tells us
     * there is subimage metadata, but we don't parse that IFD here
     */
    ret = exif_parse_ifd_list(logctx, &gbytes, le, 0, ifd);
    if (ret < 0) {
        av_exif_free(ifd);
        av_log(logctx, AV_LOG_ERROR, "error decoding EXIF data: %s\n", av_err2str(ret));
        return ret;
    }

    return bytestream2_tell(&gbytes);
}

#define COLUMN_SEP(i, c) ((i) ? ((i) % (c) ? ", " : "\n") : "")

static int exif_ifd_to_dict(void *logctx, const char *prefix, const AVExifMetadata *ifd, AVDictionary **metadata)
{
    AVBPrint bp;
    int ret = 0;
    char *key = NULL;
    char *value = NULL;

    if (!prefix)
        prefix = "";

    for (uint16_t i = 0; i < ifd->count; i++) {
        const AVExifEntry *entry = &ifd->entries[i];
        const char *name = av_exif_get_tag_name(entry->id);
        av_bprint_init(&bp, entry->count * 10, AV_BPRINT_SIZE_UNLIMITED);
        if (*prefix)
            av_bprintf(&bp, "%s/", prefix);
        if (name)
            av_bprintf(&bp, "%s", name);
        else
            av_bprintf(&bp, "0x%04X", entry->id);
        ret = av_bprint_finalize(&bp, &key);
        if (ret < 0)
            goto end;
        av_bprint_init(&bp, entry->count * 10, AV_BPRINT_SIZE_UNLIMITED);
        switch (entry->type) {
            case AV_TIFF_IFD:
                ret = exif_ifd_to_dict(logctx, key, &entry->value.ifd, metadata);
                if (ret < 0)
                    goto end;
                break;
            case AV_TIFF_SHORT:
            case AV_TIFF_LONG:
                for (uint32_t j = 0; j < entry->count; j++)
                    av_bprintf(&bp, "%s%7" PRIu32, COLUMN_SEP(j, 8), (uint32_t)entry->value.uint[j]);
                break;
            case AV_TIFF_SSHORT:
            case AV_TIFF_SLONG:
                for (uint32_t j = 0; j < entry->count; j++)
                    av_bprintf(&bp, "%s%7" PRId32, COLUMN_SEP(j, 8), (int32_t)entry->value.sint[j]);
                break;
            case AV_TIFF_RATIONAL:
            case AV_TIFF_SRATIONAL:
                for (uint32_t j = 0; j < entry->count; j++)
                    av_bprintf(&bp, "%s%7i:%-7i", COLUMN_SEP(j, 4), entry->value.rat[j].num, entry->value.rat[j].den);
                break;
            case AV_TIFF_DOUBLE:
            case AV_TIFF_FLOAT:
                for (uint32_t j = 0; j < entry->count; j++)
                    av_bprintf(&bp, "%s%.15g", COLUMN_SEP(j, 4), entry->value.dbl[j]);
                break;
            case AV_TIFF_STRING:
                av_bprintf(&bp, "%s", entry->value.str);
                break;
            case AV_TIFF_UNDEFINED:
            case AV_TIFF_BYTE:
                for (uint32_t j = 0; j < entry->count; j++)
                    av_bprintf(&bp, "%s%3i", COLUMN_SEP(j, 16), entry->value.ubytes[j]);
                break;
            case AV_TIFF_SBYTE:
                for (uint32_t j = 0; j < entry->count; j++)
                    av_bprintf(&bp, "%s%3i", COLUMN_SEP(j, 16), entry->value.sbytes[j]);
                break;
        }
        if (entry->type != AV_TIFF_IFD) {
            if (!av_bprint_is_complete(&bp)) {
                av_bprint_finalize(&bp, NULL);
                ret = AVERROR(ENOMEM);
                goto end;
            }
            ret = av_bprint_finalize(&bp, &value);
            if (ret < 0)
                goto end;
            ret = av_dict_set(metadata, key, value, AV_DICT_DONT_STRDUP_KEY | AV_DICT_DONT_STRDUP_VAL);
            key = NULL;
            value = NULL;
            if (ret < 0)
                goto end;
        } else {
            av_freep(&key);
        }
    }

end:
    av_freep(&key);
    av_freep(&value);
    return ret;
}

int av_exif_ifd_to_dict(void *logctx, const AVExifMetadata *ifd, AVDictionary **metadata)
{
    return exif_ifd_to_dict(logctx, "", ifd, metadata);
}

#if LIBAVCODEC_VERSION_MAJOR < 63
int avpriv_exif_decode_ifd(void *logctx, const uint8_t *buf, int size,
                           int le, int depth, AVDictionary **metadata)
{
    AVExifMetadata ifd = { 0 };
    GetByteContext gb;
    int ret;
    bytestream2_init(&gb, buf, size);
    ret = exif_parse_ifd_list(logctx, &gb, le, depth, &ifd);
    if (ret < 0)
        return ret;
    ret = av_exif_ifd_to_dict(logctx, &ifd, metadata);
    av_exif_free(&ifd);
    return ret;
}
#endif

#define EXIF_COPY(fname, srcname) do { \
    size_t sz; \
    if (av_size_mult(src->count, sizeof(*(fname)), &sz) < 0) { \
        ret = AVERROR(ENOMEM); \
        goto end; \
    } \
    (fname) = av_memdup((srcname), sz); \
    if (!(fname)) { \
        ret = AVERROR(ENOMEM); \
        goto end; \
    } \
} while (0)

static int exif_clone_entry(AVExifEntry *dst, const AVExifEntry *src)
{
    int ret = 0;

    dst->count = src->count;
    dst->id = src->id;
    dst->type = src->type;

    dst->ifd_offset = src->ifd_offset;
    if (src->ifd_lead) {
        dst->ifd_lead = av_memdup(src->ifd_lead, src->ifd_offset);
        if (!dst->ifd_lead) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
    } else {
        dst->ifd_lead = NULL;
    }

    switch(src->type) {
        case AV_TIFF_IFD: {
            AVExifMetadata *cloned = av_exif_clone_ifd(&src->value.ifd);
            if (!cloned) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
            dst->value.ifd = *cloned;
            av_freep(&cloned);
            break;
        }
        case AV_TIFF_SHORT:
        case AV_TIFF_LONG:
            EXIF_COPY(dst->value.uint, src->value.uint);
            break;
        case AV_TIFF_SLONG:
        case AV_TIFF_SSHORT:
            EXIF_COPY(dst->value.sint, src->value.sint);
            break;
        case AV_TIFF_RATIONAL:
        case AV_TIFF_SRATIONAL:
            EXIF_COPY(dst->value.rat, src->value.rat);
            break;
        case AV_TIFF_DOUBLE:
        case AV_TIFF_FLOAT:
            EXIF_COPY(dst->value.dbl, src->value.dbl);
            break;
        case AV_TIFF_BYTE:
        case AV_TIFF_UNDEFINED:
            EXIF_COPY(dst->value.ubytes, src->value.ubytes);
            break;
        case AV_TIFF_SBYTE:
            EXIF_COPY(dst->value.sbytes, src->value.sbytes);
            break;
        case AV_TIFF_STRING:
            dst->value.str = av_memdup(src->value.str, src->count+1);
            if (!dst->value.str) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
            break;
    }

    return 0;

end:
    av_freep(&dst->ifd_lead);
    if (src->type == AV_TIFF_IFD)
        av_exif_free(&dst->value.ifd);
    else
        av_freep(&dst->value.ptr);
    memset(dst, 0, sizeof(*dst));

    return ret;
}

static int exif_get_entry(void *logctx, AVExifMetadata *ifd, uint16_t id, int depth, AVExifEntry **value)
{
    int offset = 1;

    if (!ifd || ifd->count && !ifd->entries || !value)
        return AVERROR(EINVAL);

    for (size_t i = 0; i < ifd->count; i++) {
        if (ifd->entries[i].id == id) {
            *value = &ifd->entries[i];
            return i + offset;
        }
        if (ifd->entries[i].type == AV_TIFF_IFD) {
            if (depth < 3) {
                int ret = exif_get_entry(logctx, &ifd->entries[i].value.ifd, id, depth + 1, value);
                if (ret)
                    return ret < 0 ? ret : ret + offset;
            }
            offset += ifd->entries[i].value.ifd.count;
        }
    }

    return 0;
}

int av_exif_get_entry(void *logctx, AVExifMetadata *ifd, uint16_t id, int flags, AVExifEntry **value)
{
    return exif_get_entry(logctx, ifd, id, (flags & AV_EXIF_FLAG_RECURSIVE) ? 0 : INT_MAX, value);
}

int av_exif_set_entry(void *logctx, AVExifMetadata *ifd, uint16_t id, enum AVTiffDataType type,
    uint32_t count, const uint8_t *ifd_lead, uint32_t ifd_offset, const void *value)
{
    void *temp;
    int ret = 0;
    AVExifEntry *entry = NULL;
    AVExifEntry src = { 0 };

    if (!ifd || ifd->count && !ifd->entries
             || ifd_lead && !ifd_offset || !ifd_lead && ifd_offset
             || !value || ifd->count == 0xFFFFu)
        return AVERROR(EINVAL);

    ret = av_exif_get_entry(logctx, ifd, id, 0, &entry);
    if (ret < 0)
        return ret;

    if (entry) {
        exif_free_entry(entry);
    } else {
        size_t required_size;
        ret = av_size_mult(ifd->count + 1, sizeof(*ifd->entries), &required_size);
        if (ret < 0)
            return AVERROR(ENOMEM);
        temp = av_fast_realloc(ifd->entries, &ifd->size, required_size);
        if (!temp)
            return AVERROR(ENOMEM);
        ifd->entries = temp;
        entry = &ifd->entries[ifd->count++];
    }

    src.count = count;
    src.id = id;
    src.type = type;
    src.ifd_lead = (uint8_t *) ifd_lead;
    src.ifd_offset = ifd_offset;
    if (type == AV_TIFF_IFD)
        src.value.ifd = * (const AVExifMetadata *) value;
    else
        src.value.ptr = (void *) value;

    ret = exif_clone_entry(entry, &src);

    if (ret < 0)
        ifd->count--;

    return ret;
}

static int exif_remove_entry(void *logctx, AVExifMetadata *ifd, uint16_t id, int depth)
{
    int32_t index = -1;
    int ret = 0;

    if (!ifd || ifd->count && !ifd->entries)
        return AVERROR(EINVAL);

    for (size_t i = 0; i < ifd->count; i++) {
        if (ifd->entries[i].id == id) {
            index = i;
            break;
        }
        if (ifd->entries[i].type == AV_TIFF_IFD && depth < 3) {
            ret = exif_remove_entry(logctx, &ifd->entries[i].value.ifd, id, depth + 1);
            if (ret)
                return ret;
        }
    }

    if (index < 0)
        return 0;
    exif_free_entry(&ifd->entries[index]);

    if (index == --ifd->count) {
        if (!index)
            av_freep(&ifd->entries);
        return 1;
    }

    memmove(&ifd->entries[index], &ifd->entries[index + 1], (ifd->count - index) * sizeof(*ifd->entries));

    return 1 + (ifd->count - index);
}

int av_exif_remove_entry(void *logctx, AVExifMetadata *ifd, uint16_t id, int flags)
{
    return exif_remove_entry(logctx, ifd, id, (flags & AV_EXIF_FLAG_RECURSIVE) ? 0 : INT_MAX);
}

AVExifMetadata *av_exif_clone_ifd(const AVExifMetadata *ifd)
{
    AVExifMetadata *ret = av_mallocz(sizeof(*ret));
    if (!ret)
        return NULL;

    ret->count = ifd->count;
    if (ret->count) {
        size_t required_size;
        if (av_size_mult(ret->count, sizeof(*ret->entries), &required_size) < 0)
            goto fail;
        ret->entries = av_fast_realloc(NULL, &ret->size, required_size);
        if (!ret->entries)
            goto fail;
    }

    for (size_t i = 0; i < ret->count; i++) {
        const AVExifEntry *entry = &ifd->entries[i];
        AVExifEntry *ret_entry = &ret->entries[i];
        int status = exif_clone_entry(ret_entry, entry);
        if (status < 0)
            goto fail;
    }

    return ret;

fail:
    av_exif_free(ret);
    av_free(ret);
    return NULL;
}

static const int rotation_lut[2][4] = {
    {1, 8, 3, 6}, {4, 7, 2, 5},
};

int av_exif_matrix_to_orientation(const int32_t *matrix)
{
    double rotation = av_display_rotation_get(matrix);
    // determinant
    int vflip = ((int64_t)matrix[0] * (int64_t)matrix[4]
               - (int64_t)matrix[1] * (int64_t)matrix[3]) < 0;
    if (!isfinite(rotation))
        return 0;
    int rot = (int)(rotation + 0.5);
    rot = (((rot % 360) + 360) % 360) / 90;
    return rotation_lut[vflip][rot];
}

int av_exif_orientation_to_matrix(int32_t *matrix, int orientation)
{
    switch (orientation) {
        case 1:
            av_display_rotation_set(matrix, 0.0);
            break;
        case 2:
            av_display_rotation_set(matrix, 0.0);
            av_display_matrix_flip(matrix, 1, 0);
            break;
        case 3:
            av_display_rotation_set(matrix, 180.0);
            break;
        case 4:
            av_display_rotation_set(matrix, 180.0);
            av_display_matrix_flip(matrix, 1, 0);
            break;
        case 5:
            av_display_rotation_set(matrix, 90.0);
            av_display_matrix_flip(matrix, 1, 0);
            break;
        case 6:
            av_display_rotation_set(matrix, 90.0);
            break;
        case 7:
            av_display_rotation_set(matrix, -90.0);
            av_display_matrix_flip(matrix, 1, 0);
            break;
        case 8:
            av_display_rotation_set(matrix, -90.0);
            break;
        default:
            return AVERROR(EINVAL);
    }

    return 0;
}

int ff_exif_sanitize_ifd(void *logctx, const AVFrame *frame, AVExifMetadata *ifd)
{
    int ret = 0;
    AVFrameSideData *sd_orient = NULL;
    AVExifEntry *or = NULL;
    AVExifEntry *iw = NULL;
    AVExifEntry *ih = NULL;
    AVExifEntry *pw = NULL;
    AVExifEntry *ph = NULL;
    uint64_t orientation = 1;
    uint64_t w = frame->width;
    uint64_t h = frame->height;
    int rewrite = 0;

    sd_orient = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX);

    if (sd_orient)
        orientation = av_exif_matrix_to_orientation((int32_t *) sd_orient->data);
    if (orientation != 1)
        av_log(logctx, AV_LOG_DEBUG, "matrix contains nontrivial EXIF orientation: %" PRIu64 "\n", orientation);

    for (size_t i = 0; i < ifd->count; i++) {
        AVExifEntry *entry = &ifd->entries[i];
        if (entry->id == ORIENTATION_TAG && entry->count > 0 && entry->type == AV_TIFF_SHORT) {
            or = entry;
            continue;
        }
        if (entry->id == IMAGE_WIDTH_TAG && entry->count > 0 && entry->type == AV_TIFF_LONG) {
            iw = entry;
            continue;
        }
        if (entry->id == IMAGE_LENGTH_TAG && entry->count > 0 && entry->type == AV_TIFF_LONG) {
            ih = entry;
            continue;
        }
        if (entry->id == EXIFIFD_TAG && entry->type == AV_TIFF_IFD) {
            AVExifMetadata *exif = &entry->value.ifd;
            for (size_t j = 0; j < exif->count; j++) {
                AVExifEntry *exifentry = &exif->entries[j];
                if (exifentry->id == PIXEL_X_TAG && exifentry->count > 0 && exifentry->type == AV_TIFF_SHORT) {
                    pw = exifentry;
                    continue;
                }
                if (exifentry->id == PIXEL_Y_TAG && exifentry->count > 0 && exifentry->type == AV_TIFF_SHORT) {
                    ph = exifentry;
                    continue;
                }
            }
        }
    }

    if (or && or->value.uint[0] != orientation) {
        rewrite = 1;
        or->value.uint[0] = orientation;
    }
    if (iw && iw->value.uint[0] != w) {
        rewrite = 1;
        iw->value.uint[0] = w;
    }
    if (ih && ih->value.uint[0] != h) {
        rewrite = 1;
        ih->value.uint[0] = h;
    }
    if (pw && pw->value.uint[0] != w) {
        rewrite = 1;
        pw->value.uint[0] = w;
    }
    if (ph && ph->value.uint[0] != h) {
        rewrite = 1;
        ph->value.uint[0] = h;
    }
    if (!or && orientation != 1) {
        rewrite = 1;
        ret = av_exif_set_entry(logctx, ifd, ORIENTATION_TAG, AV_TIFF_SHORT, 1, NULL, 0, &orientation);
        if (ret < 0)
            goto end;
    }
    if (!iw && w) {
        rewrite = 1;
        ret = av_exif_set_entry(logctx, ifd, IMAGE_WIDTH_TAG, AV_TIFF_LONG, 1, NULL, 0, &w);
        if (ret < 0)
            goto end;
    }
    if (!ih && h) {
        rewrite = 1;
        ret = av_exif_set_entry(logctx, ifd, IMAGE_LENGTH_TAG, AV_TIFF_LONG, 1, NULL, 0, &h);
        if (ret < 0)
            goto end;
    }
    if (!pw && w && w < 0xFFFFu || !ph && h && h < 0xFFFFu) {
        AVExifMetadata *exif;
        AVExifEntry *exif_entry;
        int exif_found = av_exif_get_entry(logctx, ifd, EXIFIFD_TAG, 0, &exif_entry);
        rewrite = 1;
        if (exif_found < 0)
            goto end;
        if (exif_found > 0) {
            exif = &exif_entry->value.ifd;
        } else {
            AVExifMetadata exif_new = { 0 };
            ret = av_exif_set_entry(logctx, ifd, EXIFIFD_TAG, AV_TIFF_IFD, 1, NULL, 0, &exif_new);
            if (ret < 0) {
                av_exif_free(&exif_new);
                goto end;
            }
            exif = &ifd->entries[ifd->count - 1].value.ifd;
        }
        if (!pw && w && w < 0xFFFFu) {
            ret = av_exif_set_entry(logctx, exif, PIXEL_X_TAG, AV_TIFF_SHORT, 1, NULL, 0, &w);
            if (ret < 0)
                goto end;
        }
        if (!ph && h && h < 0xFFFFu) {
            ret = av_exif_set_entry(logctx, exif, PIXEL_Y_TAG, AV_TIFF_SHORT, 1, NULL, 0, &h);
            if (ret < 0)
                goto end;
        }
    }

    return rewrite;

end:
    return ret;
}

int ff_exif_get_buffer(void *logctx, const AVFrame *frame, AVBufferRef **buffer_ptr, enum AVExifHeaderMode header_mode)
{
    AVFrameSideData *sd_exif = NULL;
    AVBufferRef *buffer = NULL;
    AVExifMetadata ifd = { 0 };
    int ret = 0;
    int rewrite = 0;

    if (!buffer_ptr || *buffer_ptr)
        return AVERROR(EINVAL);

    sd_exif = av_frame_get_side_data(frame, AV_FRAME_DATA_EXIF);
    if (!sd_exif)
        return 0;

    ret = av_exif_parse_buffer(logctx, sd_exif->data, sd_exif->size, &ifd, AV_EXIF_TIFF_HEADER);
    if (ret < 0)
        goto end;

    rewrite = ff_exif_sanitize_ifd(logctx, frame, &ifd);
    if (rewrite < 0) {
        ret = rewrite;
        goto end;
    }

    if (rewrite) {
        ret = av_exif_write(logctx, &ifd, &buffer, header_mode);
        if (ret < 0)
            goto end;

        *buffer_ptr = buffer;
    } else {
        *buffer_ptr = av_buffer_ref(sd_exif->buf);
        if (!*buffer_ptr) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
    }

    av_exif_free(&ifd);
    return rewrite;

end:
    av_exif_free(&ifd);
    return ret;
}
