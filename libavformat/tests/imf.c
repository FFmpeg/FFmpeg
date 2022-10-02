/*
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

/*
 *
 * Copyright (c) Sandflow Consulting LLC
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * Tests for IMF CPL and ASSETMAP processing
 *
 * @author Valentin Noel
 * @author Pierre-Anthony Lemieux
 * @file
 * @ingroup lavu_imf
 */

#include "libavformat/imf_cpl.c"
#include "libavformat/imfdec.c"
#include "libavformat/mxf.h"

#include <stdio.h>

const char *cpl_doc =
    "<CompositionPlaylist xmlns=\"http://www.smpte-ra.org/schemas/2067-3/2016\""
    " xmlns:cc=\"http://www.smpte-ra.org/schemas/2067-2/2016\""
    " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
    "<Id>urn:uuid:8713c020-2489-45f5-a9f7-87be539e20b5</Id>"
    "<IssueDate>2021-07-13T17:06:22Z</IssueDate>"
    "<Creator language=\"en\">FFMPEG</Creator>"
    "<ContentTitle>FFMPEG sample content</ContentTitle>"
    "<EssenceDescriptorList>"
    "  <EssenceDescriptor>"
    "    <Id>urn:uuid:8e097bb0-cff7-4969-a692-bad47bfb528f</Id>"
    "  </EssenceDescriptor>"
    "</EssenceDescriptorList>"
    "<CompositionTimecode>"
    "<TimecodeDropFrame>false</TimecodeDropFrame>"
    "<TimecodeRate>24</TimecodeRate>"
    "<TimecodeStartAddress>02:10:01.23</TimecodeStartAddress>"
    "</CompositionTimecode>"
    "<EditRate>24000 1001</EditRate>"
    "<SegmentList>"
    "<Segment>"
    "<Id>urn:uuid:81fed4e5-9722-400a-b9d1-7f2bd21df4b6</Id>"
    "<SequenceList>"
    "<MarkerSequence>"
    "<Id>urn:uuid:16327185-9205-47ef-a17b-ee28df251db7</Id>"
    "<TrackId>urn:uuid:461f5424-8f6e-48a9-a385-5eda46fda381</TrackId>"
    "<ResourceList>"
    "<Resource xsi:type=\"MarkerResourceType\">"
    "<Id>urn:uuid:ea3d0f23-55d6-4e03-86ec-cfe0666f0e6a</Id>"
    "<IntrinsicDuration>24</IntrinsicDuration>"
    "<Marker>"
    "<Label>LFOA</Label>"
    "<Offset>5</Offset>"
    "</Marker>"
    "</Resource>"
    "</ResourceList>"
    "</MarkerSequence>"
    "<cc:MainImageSequence>"
    "<Id>urn:uuid:6ae100b0-92d1-41be-9321-85e0933dfc42</Id>"
    "<TrackId>urn:uuid:e8ef9653-565c-479c-8039-82d4547973c5</TrackId>"
    "<ResourceList>"
    "<Resource xsi:type=\"TrackFileResourceType\">"
    "<Id>urn:uuid:7d418acb-07a3-4e57-984c-b8ea2f7de4ec</Id>"
    "<IntrinsicDuration>24</IntrinsicDuration>"
    "<SourceEncoding>urn:uuid:f00e49a8-0dec-4e6c-95e7-078df988b751</SourceEncoding>"
    "<TrackFileId>urn:uuid:6f768ca4-c89e-4dac-9056-a29425d40ba1</TrackFileId>"
    "</Resource>"
    "</ResourceList>"
    "</cc:MainImageSequence>"
    "<cc:MainAudioSequence>"
    "<Id>urn:uuid:754dae53-c25f-4f3c-97e4-2bfe5463f83b</Id>"
    "<TrackId>urn:uuid:68e3fae5-d94b-44d2-92a6-b94877fbcdb5</TrackId>"
    "<ResourceList>"
    "<Resource xsi:type=\"TrackFileResourceType\">"
    "<Id>urn:uuid:61ce2a70-10a2-4521-850b-4218755ff3c9</Id>"
    "<IntrinsicDuration>24</IntrinsicDuration>"
    "<SourceEncoding>urn:uuid:f00e49a8-0dec-4e6c-95e7-078df988b751</SourceEncoding>"
    "<TrackFileId>urn:uuid:381dadd2-061e-46cc-a63a-e3d58ce7f488</TrackFileId>"
    "</Resource>"
    "</ResourceList>"
    "</cc:MainAudioSequence>"
    "<cc:MainAudioSequence>"
    "<Id>urn:uuid:d29b3884-6633-4dad-9c67-7154af342bc6</Id>"
    "<TrackId>urn:uuid:6978c106-95bc-424b-a17c-628206a5892d</TrackId>"
    "<ResourceList>"
    "<Resource xsi:type=\"TrackFileResourceType\">"
    "<Id>urn:uuid:001ea472-f5da-436c-86de-acaa68c1a7e4</Id>"
    "<IntrinsicDuration>24</IntrinsicDuration>"
    "<SourceEncoding>urn:uuid:f00e49a8-0dec-4e6c-95e7-078df988b751</SourceEncoding>"
    "<TrackFileId>urn:uuid:381dadd2-061e-46cc-a63a-e3d58ce7f488</TrackFileId>"
    "</Resource>"
    "</ResourceList>"
    "</cc:MainAudioSequence>"
    "<cc:SubtitlesSequence>"
    "<Id>urn:uuid:02af22bf-f776-488a-b001-eb6e16953119</Id>"
    "<TrackId>urn:uuid:19ff6da1-be79-4235-8d04-42201ad06e65</TrackId>"
    "<ResourceList>"
    "<Resource xsi:type=\"TrackFileResourceType\">"
    "<Id>urn:uuid:dfa84292-0609-4097-824c-8e2e15e2ce4d</Id>"
    "<IntrinsicDuration>24</IntrinsicDuration>"
    "<SourceEncoding>urn:uuid:f00e49a8-0dec-4e6c-95e7-078df988b751</SourceEncoding>"
    "<TrackFileId>urn:uuid:bd6272b6-511e-47c1-93bc-d56ebd314a70</TrackFileId>"
    "</Resource>"
    "</ResourceList>"
    "</cc:SubtitlesSequence>"
    "</SequenceList>"
    "</Segment>"
    "<Segment>"
    "<Id>urn:uuid:a94be493-cd55-4bf7-b496-ea87bfe38632</Id>"
    "<SequenceList>"
    "<MarkerSequence>"
    "<Id>urn:uuid:20c6020b-1fc0-4080-bcf7-89f09f95bea8</Id>"
    "<TrackId>urn:uuid:461f5424-8f6e-48a9-a385-5eda46fda381</TrackId>"
    "<ResourceList>"
    "<Resource xsi:type=\"MarkerResourceType\">"
    "<Id>urn:uuid:d1f93845-d3e5-4c3b-aa67-8d96c45cfe9c</Id>"
    "<IntrinsicDuration>36</IntrinsicDuration>"
    "<Marker>"
    "<Label>FFOA</Label>"
    "<Offset>20</Offset>"
    "</Marker>"
    "<Marker>"
    "<Label>LFOC</Label>"
    "<Offset>24</Offset>"
    "</Marker>"
    "</Resource>"
    "</ResourceList>"
    "</MarkerSequence>"
    "<cc:MainImageSequence>"
    "<Id>urn:uuid:9b509f42-e4e8-4f78-8c2a-12ddd79ef3c5</Id>"
    "<TrackId>urn:uuid:e8ef9653-565c-479c-8039-82d4547973c5</TrackId>"
    "<ResourceList>"
    "<Resource xsi:type=\"TrackFileResourceType\">"
    "<Id>urn:uuid:a733d812-a3d7-45e9-ba50-13b856d5d35a</Id>"
    "<IntrinsicDuration>36</IntrinsicDuration>"
    "<SourceEncoding>urn:uuid:f00e49a8-0dec-4e6c-95e7-078df988b751</SourceEncoding>"
    "<TrackFileId>urn:uuid:f3b263b3-096b-4360-a952-b1a9623cd0ca</TrackFileId>"
    "</Resource>"
    "</ResourceList>"
    "</cc:MainImageSequence>"
    "<cc:MainAudioSequence>"
    "<Id>urn:uuid:19a282e6-beac-4d99-a008-afa61378eb6c</Id>"
    "<TrackId>urn:uuid:68e3fae5-d94b-44d2-92a6-b94877fbcdb5</TrackId>"
    "<ResourceList>"
    "<Resource xsi:type=\"TrackFileResourceType\">"
    "<Id>urn:uuid:53de5ff9-f5f7-47c5-a2d8-117c36cce517</Id>"
    "<IntrinsicDuration>36</IntrinsicDuration>"
    "<SourceEncoding>urn:uuid:f00e49a8-0dec-4e6c-95e7-078df988b751</SourceEncoding>"
    "<TrackFileId>urn:uuid:2484d613-bb7d-4bcc-8b0f-2e65938f0535</TrackFileId>"
    "</Resource>"
    "</ResourceList>"
    "</cc:MainAudioSequence>"
    "<cc:MainAudioSequence>"
    "<Id>urn:uuid:94b0ef77-0621-4086-95a2-85432fa97d40</Id>"
    "<TrackId>urn:uuid:6978c106-95bc-424b-a17c-628206a5892d</TrackId>"
    "<ResourceList>"
    "<Resource xsi:type=\"TrackFileResourceType\">"
    "<Id>urn:uuid:2ce499f2-59bc-4053-87bc-80f4b7e7b73e</Id>"
    "<IntrinsicDuration>36</IntrinsicDuration>"
    "<SourceEncoding>urn:uuid:f00e49a8-0dec-4e6c-95e7-078df988b751</SourceEncoding>"
    "<TrackFileId>urn:uuid:2484d613-bb7d-4bcc-8b0f-2e65938f0535</TrackFileId>"
    "</Resource>"
    "</ResourceList>"
    "</cc:MainAudioSequence>"
    "<cc:SubtitlesSequence>"
    "<Id>urn:uuid:9ac3b905-c599-4da8-8f0f-fc07e619899d</Id>"
    "<TrackId>urn:uuid:19ff6da1-be79-4235-8d04-42201ad06e65</TrackId>"
    "<ResourceList>"
    "<Resource xsi:type=\"TrackFileResourceType\">"
    "<Id>urn:uuid:0239017b-2ad9-4235-b46d-c4c1126e29fc</Id>"
    "<IntrinsicDuration>36</IntrinsicDuration>"
    "<SourceEncoding>urn:uuid:f00e49a8-0dec-4e6c-95e7-078df988b751</SourceEncoding>"
    "<TrackFileId>urn:uuid:bd6272b6-511e-47c1-93bc-d56ebd314a70</TrackFileId>"
    "</Resource>"
    "</ResourceList>"
    "</cc:SubtitlesSequence>"
    "</SequenceList>"
    "</Segment>"
    "</SegmentList>"
    "</CompositionPlaylist>";

const char *cpl_bad_doc = "<Composition></Composition>";

const char *asset_map_doc =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>"
    "<am:AssetMap xmlns:am=\"http://www.smpte-ra.org/schemas/429-9/2007/AM\">"
    "<am:Id>urn:uuid:68d9f591-8191-46b5-38b4-affb87a14132</am:Id>"
    "<am:AnnotationText>IMF_TEST_ASSET_MAP</am:AnnotationText>"
    "<am:Creator>Some tool</am:Creator>"
    "<am:VolumeCount>1</am:VolumeCount>"
    "<am:IssueDate>2021-06-07T12:00:00+00:00</am:IssueDate>"
    "<am:Issuer>FFmpeg</am:Issuer>"
    "<am:AssetList>"
    "<am:Asset>"
    "<am:Id>urn:uuid:b5d674b8-c6ce-4bce-3bdf-be045dfdb2d0</am:Id>"
    "<am:ChunkList>"
    "<am:Chunk>"
    "<am:Path>IMF_TEST_ASSET_MAP_video.mxf</am:Path>"
    "<am:VolumeIndex>1</am:VolumeIndex>"
    "<am:Offset>0</am:Offset>"
    "<am:Length>1234567</am:Length>"
    "</am:Chunk>"
    "</am:ChunkList>"
    "</am:Asset>"
    "<am:Asset>"
    "<am:Id>urn:uuid:ec3467ec-ab2a-4f49-c8cb-89caa3761f4a</am:Id>"
    "<am:ChunkList>"
    "<am:Chunk>"
    "<am:Path>IMF_TEST_ASSET_MAP_video_1.mxf</am:Path>"
    "<am:VolumeIndex>1</am:VolumeIndex>"
    "<am:Offset>0</am:Offset>"
    "<am:Length>234567</am:Length>"
    "</am:Chunk>"
    "</am:ChunkList>"
    "</am:Asset>"
    "<am:Asset>"
    "<am:Id>urn:uuid:5cf5b5a7-8bb3-4f08-eaa6-3533d4b77fa6</am:Id>"
    "<am:ChunkList>"
    "<am:Chunk>"
    "<am:Path>IMF_TEST_ASSET_MAP_audio.mxf</am:Path>"
    "<am:VolumeIndex>1</am:VolumeIndex>"
    "<am:Offset>0</am:Offset>"
    "<am:Length>34567</am:Length>"
    "</am:Chunk>"
    "</am:ChunkList>"
    "</am:Asset>"
    "<am:Asset>"
    "<am:Id>urn:uuid:559777d6-ec29-4375-f90d-300b0bf73686</am:Id>"
    "<am:ChunkList>"
    "<am:Chunk>"
    "<am:Path>CPL_IMF_TEST_ASSET_MAP.xml</am:Path>"
    "<am:VolumeIndex>1</am:VolumeIndex>"
    "<am:Offset>0</am:Offset>"
    "<am:Length>12345</am:Length>"
    "</am:Chunk>"
    "</am:ChunkList>"
    "</am:Asset>"
    "<am:Asset>"
    "<am:Id>urn:uuid:dd04528d-9b80-452a-7a13-805b08278b3d</am:Id>"
    "<am:PackingList>true</am:PackingList>"
    "<am:ChunkList>"
    "<am:Chunk>"
    "<am:Path>PKL_IMF_TEST_ASSET_MAP.xml</am:Path>"
    "<am:VolumeIndex>1</am:VolumeIndex>"
    "<am:Offset>0</am:Offset>"
    "<am:Length>2345</am:Length>"
    "</am:Chunk>"
    "</am:ChunkList>"
    "</am:Asset>"
    "</am:AssetList>"
    "</am:AssetMap>";

static int test_cpl_parsing(void)
{
    xmlDocPtr doc;
    FFIMFCPL *cpl;
    char tc_buf[AV_TIMECODE_STR_SIZE];
    int ret;

    doc = xmlReadMemory(cpl_doc, strlen(cpl_doc), NULL, NULL, 0);
    if (doc == NULL) {
        printf("XML parsing failed.\n");
        return 1;
    }

    ret = ff_imf_parse_cpl_from_xml_dom(doc, &cpl);
    xmlFreeDoc(doc);
    if (ret) {
        printf("CPL parsing failed.\n");
        return 1;
    }

    printf("%s\n", cpl->content_title_utf8);
    printf(AV_PRI_URN_UUID "\n", AV_UUID_ARG(cpl->id_uuid));
    printf("%i %i\n", cpl->edit_rate.num, cpl->edit_rate.den);
    printf("%s\n", av_timecode_make_string(cpl->tc, tc_buf, 0));

    printf("Marker resource count: %" PRIu32 "\n", cpl->main_markers_track->resource_count);
    for (uint32_t i = 0; i < cpl->main_markers_track->resource_count; i++) {
        printf("Marker resource %" PRIu32 "\n", i);
        for (uint32_t j = 0; j < cpl->main_markers_track->resources[i].marker_count; j++) {
            printf("  Marker %" PRIu32 "\n", j);
            printf("    Label %s\n", cpl->main_markers_track->resources[i].markers[j].label_utf8);
            printf("    Offset %" PRIu32 "\n", cpl->main_markers_track->resources[i].markers[j].offset);
        }
    }

    printf("Main image resource count: %" PRIu32 "\n", cpl->main_image_2d_track->resource_count);
    for (uint32_t i = 0; i < cpl->main_image_2d_track->resource_count; i++) {
        printf("Track file resource %" PRIu32 "\n", i);
        printf("  " AV_PRI_URN_UUID "\n", AV_UUID_ARG(cpl->main_image_2d_track->resources[i].track_file_uuid));
    }

    printf("Main audio track count: %" PRIu32 "\n", cpl->main_audio_track_count);
    for (uint32_t i = 0; i < cpl->main_audio_track_count; i++) {
        printf("  Main audio virtual track %" PRIu32 "\n", i);
        printf("  Main audio resource count: %" PRIu32 "\n", cpl->main_audio_tracks[i].resource_count);
        for (uint32_t j = 0; j < cpl->main_audio_tracks[i].resource_count; j++) {
            printf("  Track file resource %" PRIu32 "\n", j);
            printf("    " AV_PRI_URN_UUID "\n", AV_UUID_ARG(cpl->main_audio_tracks[i].resources[j].track_file_uuid));
        }
    }

    ff_imf_cpl_free(cpl);

    return 0;
}

static int test_bad_cpl_parsing(FFIMFCPL **cpl)
{
    xmlDocPtr doc;
    int ret;

    doc = xmlReadMemory(cpl_bad_doc, strlen(cpl_bad_doc), NULL, NULL, 0);
    if (doc == NULL) {
        printf("XML parsing failed.\n");
        return 1;
    }

    ret = ff_imf_parse_cpl_from_xml_dom(doc, cpl);
    xmlFreeDoc(doc);
    if (ret) {
        printf("CPL parsing failed.\n");
        return ret;
    }

    return 0;
}

static int check_asset_locator_attributes(IMFAssetLocator *asset, IMFAssetLocator *expected_asset)
{

    printf("\tCompare " AV_PRI_URN_UUID " to " AV_PRI_URN_UUID ".\n",
           AV_UUID_ARG(asset->uuid),
           AV_UUID_ARG(expected_asset->uuid));

    for (uint32_t i = 0; i < 16; ++i) {
        if (asset->uuid[i] != expected_asset->uuid[i]) {
            printf("Invalid asset locator UUID: found " AV_PRI_URN_UUID " instead of " AV_PRI_URN_UUID " expected.\n",
                   AV_UUID_ARG(asset->uuid),
                   AV_UUID_ARG(expected_asset->uuid));
            return 1;
        }
    }

    printf("\tCompare %s to %s.\n", asset->absolute_uri, expected_asset->absolute_uri);
    if (strcmp(asset->absolute_uri, expected_asset->absolute_uri) != 0) {
        printf("Invalid asset locator URI: found %s instead of %s expected.\n",
               asset->absolute_uri,
               expected_asset->absolute_uri);
        return 1;
    }

    return 0;
}

static IMFAssetLocator ASSET_MAP_EXPECTED_LOCATORS[5] = {
    {.uuid = {0xb5, 0xd6, 0x74, 0xb8, 0xc6, 0xce, 0x4b, 0xce, 0x3b, 0xdf, 0xbe, 0x04, 0x5d, 0xfd, 0xb2, 0xd0},
     .absolute_uri = (char *)"IMF_TEST_ASSET_MAP_video.mxf"},
    {.uuid = {0xec, 0x34, 0x67, 0xec, 0xab, 0x2a, 0x4f, 0x49, 0xc8, 0xcb, 0x89, 0xca, 0xa3, 0x76, 0x1f, 0x4a},
     .absolute_uri = (char *)"IMF_TEST_ASSET_MAP_video_1.mxf"},
    {.uuid = {0x5c, 0xf5, 0xb5, 0xa7, 0x8b, 0xb3, 0x4f, 0x08, 0xea, 0xa6, 0x35, 0x33, 0xd4, 0xb7, 0x7f, 0xa6},
     .absolute_uri = (char *)"IMF_TEST_ASSET_MAP_audio.mxf"},
    {.uuid = {0x55, 0x97, 0x77, 0xd6, 0xec, 0x29, 0x43, 0x75, 0xf9, 0x0d, 0x30, 0x0b, 0x0b, 0xf7, 0x36, 0x86},
     .absolute_uri = (char *)"CPL_IMF_TEST_ASSET_MAP.xml"},
    {.uuid = {0xdd, 0x04, 0x52, 0x8d, 0x9b, 0x80, 0x45, 0x2a, 0x7a, 0x13, 0x80, 0x5b, 0x08, 0x27, 0x8b, 0x3d},
     .absolute_uri = (char *)"PKL_IMF_TEST_ASSET_MAP.xml"},
};

static int test_asset_map_parsing(void)
{
    IMFAssetLocatorMap asset_locator_map;
    xmlDoc *doc;
    int ret;

    doc = xmlReadMemory(asset_map_doc, strlen(asset_map_doc), NULL, NULL, 0);
    if (doc == NULL) {
        printf("Asset map XML parsing failed.\n");
        return 1;
    }

    printf("Allocate asset map\n");
    imf_asset_locator_map_init(&asset_locator_map);

    printf("Parse asset map XML document\n");
    ret = parse_imf_asset_map_from_xml_dom(NULL, doc, &asset_locator_map, doc->name);
    if (ret) {
        printf("Asset map parsing failed.\n");
        goto cleanup;
    }

    printf("Compare assets count: %d to 5\n", asset_locator_map.asset_count);
    if (asset_locator_map.asset_count != 5) {
        printf("Asset map parsing failed: found %d assets instead of 5 expected.\n",
               asset_locator_map.asset_count);
        ret = 1;
        goto cleanup;
    }

    for (uint32_t i = 0; i < asset_locator_map.asset_count; ++i) {
        printf("For asset: %d:\n", i);
        ret = check_asset_locator_attributes(&(asset_locator_map.assets[i]),
                                             &(ASSET_MAP_EXPECTED_LOCATORS[i]));
        if (ret > 0)
            goto cleanup;
    }

cleanup:
    imf_asset_locator_map_deinit(&asset_locator_map);
    xmlFreeDoc(doc);
    return ret;
}

typedef struct PathTypeTestStruct {
    const char *path;
    int is_url;
    int is_unix_absolute_path;
    int is_dos_absolute_path;
} PathTypeTestStruct;

static const PathTypeTestStruct PATH_TYPE_TEST_STRUCTS[11] = {
    {.path = "file://path/to/somewhere", .is_url = 1, .is_unix_absolute_path = 0, .is_dos_absolute_path = 0},
    {.path = "http://path/to/somewhere", .is_url = 1, .is_unix_absolute_path = 0, .is_dos_absolute_path = 0},
    {.path = "https://path/to/somewhere", .is_url = 1, .is_unix_absolute_path = 0, .is_dos_absolute_path = 0},
    {.path = "s3://path/to/somewhere", .is_url = 1, .is_unix_absolute_path = 0, .is_dos_absolute_path = 0},
    {.path = "ftp://path/to/somewhere", .is_url = 1, .is_unix_absolute_path = 0, .is_dos_absolute_path = 0},
    {.path = "/path/to/somewhere", .is_url = 0, .is_unix_absolute_path = 1, .is_dos_absolute_path = 0},
    {.path = "path/to/somewhere", .is_url = 0, .is_unix_absolute_path = 0, .is_dos_absolute_path = 0},
    {.path = "C:\\path\\to\\somewhere", .is_url = 0, .is_unix_absolute_path = 0, .is_dos_absolute_path = 1},
    {.path = "C:/path/to/somewhere", .is_url = 0, .is_unix_absolute_path = 0, .is_dos_absolute_path = 1},
    {.path = "\\\\path\\to\\somewhere", .is_url = 0, .is_unix_absolute_path = 0, .is_dos_absolute_path = 1},
    {.path = "path\\to\\somewhere", .is_url = 0, .is_unix_absolute_path = 0, .is_dos_absolute_path = 0},
};

static int test_path_type_functions(void)
{
    PathTypeTestStruct path_type;
    for (uint32_t i = 0; i < 11; ++i) {
        path_type = PATH_TYPE_TEST_STRUCTS[i];
        if (imf_uri_is_url(path_type.path) != path_type.is_url) {
            fprintf(stderr,
                    "URL comparison test failed for '%s', got %d instead of expected %d\n",
                    path_type.path,
                    path_type.is_url,
                    !path_type.is_url);
            goto fail;
        }

        if (imf_uri_is_unix_abs_path(path_type.path) != path_type.is_unix_absolute_path) {
            fprintf(stderr,
                    "Unix absolute path comparison test failed for '%s', got %d instead of expected %d\n",
                    path_type.path,
                    path_type.is_unix_absolute_path,
                    !path_type.is_unix_absolute_path);
            goto fail;
        }

        if (imf_uri_is_dos_abs_path(path_type.path) != path_type.is_dos_absolute_path) {
            fprintf(stderr,
                    "DOS absolute path comparison test failed for '%s', got %d instead of expected %d\n",
                    path_type.path,
                    path_type.is_dos_absolute_path,
                    !path_type.is_dos_absolute_path);
            goto fail;
        }
    }

    return 0;

fail:
    return 1;
}

int main(int argc, char *argv[])
{
    FFIMFCPL *cpl;
    int ret = 0;

    if (test_cpl_parsing() != 0)
        ret = 1;

    if (test_asset_map_parsing() != 0)
        ret = 1;

    if (test_path_type_functions() != 0)
        ret = 1;

    printf("#### The following should fail ####\n");
    if (test_bad_cpl_parsing(&cpl) == 0) {
        ret = 1;
    } else if (cpl) {
        printf("Improper cleanup after failed CPL parsing\n");
        ret = 1;
    }
    printf("#### End failing test ####\n");

    return ret;
}
