/*
 * ID3v1 header parser
 * Copyright (c) 2003 Fabrice Bellard
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

#include "id3v1.h"
#include "libavcodec/avcodec.h"
#include "libavutil/dict.h"

/* See Genre List at http://id3.org/id3v2.3.0 */
const char * const ff_id3v1_genre_str[ID3v1_GENRE_MAX + 1] = {
      [0] = "Blues",
      [1] = "Classic Rock",
      [2] = "Country",
      [3] = "Dance",
      [4] = "Disco",
      [5] = "Funk",
      [6] = "Grunge",
      [7] = "Hip-Hop",
      [8] = "Jazz",
      [9] = "Metal",
     [10] = "New Age",
     [11] = "Oldies",
     [12] = "Other",
     [13] = "Pop",
     [14] = "R&B",
     [15] = "Rap",
     [16] = "Reggae",
     [17] = "Rock",
     [18] = "Techno",
     [19] = "Industrial",
     [20] = "Alternative",
     [21] = "Ska",
     [22] = "Death Metal",
     [23] = "Pranks",
     [24] = "Soundtrack",
     [25] = "Euro-Techno",
     [26] = "Ambient",
     [27] = "Trip-Hop",
     [28] = "Vocal",
     [29] = "Jazz+Funk",
     [30] = "Fusion",
     [31] = "Trance",
     [32] = "Classical",
     [33] = "Instrumental",
     [34] = "Acid",
     [35] = "House",
     [36] = "Game",
     [37] = "Sound Clip",
     [38] = "Gospel",
     [39] = "Noise",
     [40] = "AlternRock",
     [41] = "Bass",
     [42] = "Soul",
     [43] = "Punk",
     [44] = "Space",
     [45] = "Meditative",
     [46] = "Instrumental Pop",
     [47] = "Instrumental Rock",
     [48] = "Ethnic",
     [49] = "Gothic",
     [50] = "Darkwave",
     [51] = "Techno-Industrial",
     [52] = "Electronic",
     [53] = "Pop-Folk",
     [54] = "Eurodance",
     [55] = "Dream",
     [56] = "Southern Rock",
     [57] = "Comedy",
     [58] = "Cult",
     [59] = "Gangsta",
     [60] = "Top 40",
     [61] = "Christian Rap",
     [62] = "Pop/Funk",
     [63] = "Jungle",
     [64] = "Native American",
     [65] = "Cabaret",
     [66] = "New Wave",
     [67] = "Psychedelic",
     [68] = "Rave",
     [69] = "Showtunes",
     [70] = "Trailer",
     [71] = "Lo-Fi",
     [72] = "Tribal",
     [73] = "Acid Punk",
     [74] = "Acid Jazz",
     [75] = "Polka",
     [76] = "Retro",
     [77] = "Musical",
     [78] = "Rock & Roll",
     [79] = "Hard Rock",
     [80] = "Folk",
     [81] = "Folk-Rock",
     [82] = "National Folk",
     [83] = "Swing",
     [84] = "Fast Fusion",
     [85] = "Bebop",
     [86] = "Latin",
     [87] = "Revival",
     [88] = "Celtic",
     [89] = "Bluegrass",
     [90] = "Avantgarde",
     [91] = "Gothic Rock",
     [92] = "Progressive Rock",
     [93] = "Psychedelic Rock",
     [94] = "Symphonic Rock",
     [95] = "Slow Rock",
     [96] = "Big Band",
     [97] = "Chorus",
     [98] = "Easy Listening",
     [99] = "Acoustic",
    [100] = "Humour",
    [101] = "Speech",
    [102] = "Chanson",
    [103] = "Opera",
    [104] = "Chamber Music",
    [105] = "Sonata",
    [106] = "Symphony",
    [107] = "Booty Bass",
    [108] = "Primus",
    [109] = "Porn Groove",
    [110] = "Satire",
    [111] = "Slow Jam",
    [112] = "Club",
    [113] = "Tango",
    [114] = "Samba",
    [115] = "Folklore",
    [116] = "Ballad",
    [117] = "Power Ballad",
    [118] = "Rhythmic Soul",
    [119] = "Freestyle",
    [120] = "Duet",
    [121] = "Punk Rock",
    [122] = "Drum Solo",
    [123] = "A Cappella",
    [124] = "Euro-House",
    [125] = "Dance Hall",
    [126] = "Goa",
    [127] = "Drum & Bass",
    [128] = "Club-House",
    [129] = "Hardcore Techno",
    [130] = "Terror",
    [131] = "Indie",
    [132] = "BritPop",
    [133] = "Negerpunk",
    [134] = "Polsk Punk",
    [135] = "Beat",
    [136] = "Christian Gangsta Rap",
    [137] = "Heavy Metal",
    [138] = "Black Metal",
    [139] = "Crossover",
    [140] = "Contemporary Christian",
    [141] = "Christian Rock",
    [142] = "Merengue",
    [143] = "Salsa",
    [144] = "Thrash Metal",
    [145] = "Anime",
    [146] = "Jpop",
    [147] = "Synthpop",
    [148] = "Abstract",
    [149] = "Art Rock",
    [150] = "Baroque",
    [151] = "Bhangra",
    [152] = "Big Beat",
    [153] = "Breakbeat",
    [154] = "Chillout",
    [155] = "Downtempo",
    [156] = "Dub",
    [157] = "EBM",
    [158] = "Eclectic",
    [159] = "Electro",
    [160] = "Electroclash",
    [161] = "Emo",
    [162] = "Experimental",
    [163] = "Garage",
    [164] = "Global",
    [165] = "IDM",
    [166] = "Illbient",
    [167] = "Industro-Goth",
    [168] = "Jam Band",
    [169] = "Krautrock",
    [170] = "Leftfield",
    [171] = "Lounge",
    [172] = "Math Rock",
    [173] = "New Romantic",
    [174] = "Nu-Breakz",
    [175] = "Post-Punk",
    [176] = "Post-Rock",
    [177] = "Psytrance",
    [178] = "Shoegaze",
    [179] = "Space Rock",
    [180] = "Trop Rock",
    [181] = "World Music",
    [182] = "Neoclassical",
    [183] = "Audiobook",
    [184] = "Audio Theatre",
    [185] = "Neue Deutsche Welle",
    [186] = "Podcast",
    [187] = "Indie Rock",
    [188] = "G-Funk",
    [189] = "Dubstep",
    [190] = "Garage Rock",
    [191] = "Psybient"
};

static void get_string(AVFormatContext *s, const char *key,
                       const uint8_t *buf, int buf_size)
{
    int i, c;
    char *q, str[512], *first_free_space = NULL;

    q = str;
    for(i = 0; i < buf_size; i++) {
        c = buf[i];
        if (c == '\0')
            break;
        if ((q - str) >= sizeof(str) - 1)
            break;
        if (c == ' ') {
            if (!first_free_space)
                first_free_space = q;
        } else {
            first_free_space = NULL;
        }
        *q++ = c;
    }
    *q = '\0';

    if (first_free_space)
        *first_free_space = '\0';

    if (*str)
        av_dict_set(&s->metadata, key, str, 0);
}

/**
 * Parse an ID3v1 tag
 *
 * @param buf ID3v1_TAG_SIZE long buffer containing the tag
 */
static int parse_tag(AVFormatContext *s, const uint8_t *buf)
{
    int genre;

    if (!(buf[0] == 'T' &&
          buf[1] == 'A' &&
          buf[2] == 'G'))
        return -1;
    get_string(s, "title",   buf +  3, 30);
    get_string(s, "artist",  buf + 33, 30);
    get_string(s, "album",   buf + 63, 30);
    get_string(s, "date",    buf + 93,  4);
    get_string(s, "comment", buf + 97, 30);
    if (buf[125] == 0 && buf[126] != 0) {
        av_dict_set_int(&s->metadata, "track", buf[126], 0);
    }
    genre = buf[127];
    if (genre <= ID3v1_GENRE_MAX)
        av_dict_set(&s->metadata, "genre", ff_id3v1_genre_str[genre], 0);
    return 0;
}

void ff_id3v1_read(AVFormatContext *s)
{
    int ret;
    uint8_t buf[ID3v1_TAG_SIZE];
    int64_t filesize, position = avio_tell(s->pb);

    if (s->pb->seekable & AVIO_SEEKABLE_NORMAL) {
        /* XXX: change that */
        filesize = avio_size(s->pb);
        if (filesize > 128) {
            avio_seek(s->pb, filesize - 128, SEEK_SET);
            ret = avio_read(s->pb, buf, ID3v1_TAG_SIZE);
            if (ret == ID3v1_TAG_SIZE) {
                parse_tag(s, buf);
            }
            avio_seek(s->pb, position, SEEK_SET);
        }
    }
}
