/*
 * EXIF metadata parser
 * Copyright (c) 2013 Thilo Borgmann <thilo.borgmann _at_ mail.de>
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
 */

#ifndef AVCODEC_EXIF_H
#define AVCODEC_EXIF_H

#include "avcodec.h"
#include "bytestream.h"
#include "tiff.h"

#define EXIF_MAX_IFD_RECURSION 2
#define EXIF_TAG_NAME_LENGTH   32

struct exif_tag {
    char      name[EXIF_TAG_NAME_LENGTH];
    uint16_t  id;
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
    {"SubjectDistanceRange",       0xA40C}
//    {"InteroperabilityIndex",      0x1}, // <- Table 13 Interoperability IFD Attribute Information
//    {"",                           0x0}
};

/** Recursively decodes all IFD's and
 *  adds included TAGS into the metadata dictionary. */
int ff_exif_decode_ifd(AVCodecContext *avctx, GetByteContext *gbytes, int le,
                       int depth, AVDictionary **metadata);

#endif /* AVCODEC_EXIF_H */
