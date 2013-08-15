/*
 * TIFF data tables
 * Copyright (c) 2011 Thomas Kuehnel
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * TIFF data tables
 * @author Thomas Kuehnel
 * @see GeoTIFF specification at
 * http://www.remotesensing.org/geotiff/spec/geotiffhome.html
 */

#ifndef AVCODEC_TIFF_DATA_H
#define AVCODEC_TIFF_DATA_H

#include "tiff.h"

#define TIFF_CONF_KEY_ID_OFFSET 1024
extern const TiffGeoTagNameType ff_tiff_conf_name_type_map[3];

#define TIFF_GEOG_KEY_ID_OFFSET 2048
extern const TiffGeoTagNameType ff_tiff_geog_name_type_map[14];

#define TIFF_PROJ_KEY_ID_OFFSET 3072
extern const TiffGeoTagNameType ff_tiff_proj_name_type_map[24];

#define TIFF_VERT_KEY_ID_OFFSET 4096
extern const TiffGeoTagNameType ff_tiff_vert_name_type_map[4];

#define TIFF_GEO_KEY_UNDEFINED    0
#define TIFF_GEO_KEY_USER_DEFINED 32767

#define TIFF_GT_MODEL_TYPE_OFFSET 1
extern const char *const ff_tiff_gt_model_type_codes[3];

#define TIFF_GT_RASTER_TYPE_OFFSET 1
extern const char *const ff_tiff_gt_raster_type_codes[2];

#define TIFF_LINEAR_UNIT_OFFSET 9001
extern const char *const ff_tiff_linear_unit_codes[15];

#define TIFF_ANGULAR_UNIT_OFFSET 9101
extern const char *const ff_tiff_angular_unit_codes[8];

#define TIFF_GCS_TYPE_OFFSET 4201
extern const char *const ff_tiff_gcs_type_codes[133];

#define TIFF_GCSE_TYPE_OFFSET 4001
extern const char *const ff_tiff_gcse_type_codes[35];

#define TIFF_GEODETIC_DATUM_OFFSET 6201
extern const char *const ff_tiff_geodetic_datum_codes[120];

#define TIFF_GEODETIC_DATUM_E_OFFSET 6001
extern const char *const ff_tiff_geodetic_datum_e_codes[35];

#define TIFF_ELLIPSOID_OFFSET 7001
extern const char *const ff_tiff_ellipsoid_codes[35];

#define TIFF_PRIME_MERIDIAN_OFFSET 8901
extern const char *const ff_tiff_prime_meridian_codes[11];

extern const TiffGeoTagKeyName ff_tiff_proj_cs_type_codes[978];

extern const TiffGeoTagKeyName ff_tiff_projection_codes[298];

#define TIFF_COORD_TRANS_OFFSET 1
extern const char *const ff_tiff_coord_trans_codes[27];

#define TIFF_VERT_CS_OFFSET 5001
extern const char *const ff_tiff_vert_cs_codes[32];

#define TIFF_ORTHO_VERT_CS_OFFSET 5101
extern const char *const ff_tiff_ortho_vert_cs_codes[6];
#endif
