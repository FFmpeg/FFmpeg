/*
 * Generate the AMR streams used by the DTX FATE tests.
 *
 * The storage format is a header plus one table-of-contents byte and the
 * payload of each frame (RFC 4867). These files are built from that layout
 * here, the same way audiogen builds its wave files, so the tests do not
 * need media from the fate-suite.
 *
 * Copyright (c) 2026 Jun Zhao
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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Narrowband speech is one repeated 4.75 kbit/s frame: pitch lag 40 on the
   first subframe (coded index 61) and gain index 3. Comfort-noise energy is
   the 6-bit index carried by a SID_UPDATE. A silent period mutes once more
   than 50 frames have passed with no SID update. */

enum {
    NB_FRAME_SAMPLES = 160,
    WB_FRAME_SAMPLES = 320,
};

static void die(const char *path)
{
    fprintf(stderr, "amrgen: %s: %s\n", path, strerror(errno));
    exit(1);
}

static void put_msb(uint8_t *data, int *pos, int n, unsigned value)
{
    int i;

    for (i = n - 1; i >= 0; i--) {
        int bit = (*pos)++;

        if (value & (1u << i))
            data[bit >> 3] |= 1u << (7 - (bit & 7));
    }
}

static void set_lsb_field(uint8_t *data, const uint8_t *bits, int n,
                          unsigned value)
{
    int i;

    for (i = 0; i < n; i++) {
        if (value & (1u << (n - 1 - i)))
            data[bits[i] >> 3] |= 1u << (bits[i] & 7);
    }
}

static void write_nb_speech(FILE *f)
{
    /* Bit positions are those consumed by order_MODE_4k75[], MSB first. */
    static const uint8_t lag0[]  = { 23, 22, 21, 20, 19, 18, 43, 42 };
    static const uint8_t gain0[] = { 54, 55, 40, 41, 24, 25, 26, 27 };
    static const uint8_t gain2[] = { 44, 45, 46, 47, 36, 37, 38, 39 };
    uint8_t frame[13] = { 0x04 };

    set_lsb_field(frame + 1, lag0, 8, 61);
    set_lsb_field(frame + 1, gain0, 8, 3);
    set_lsb_field(frame + 1, gain2, 8, 3);
    if (fwrite(frame, 1, sizeof(frame), f) != sizeof(frame))
        die("write");
}

/* energy is the 6-bit index. update selects SID_UPDATE over SID_FIRST. */
static void write_nb_sid(FILE *f, int energy, int update)
{
    uint8_t frame[6] = { 0x44 };
    int pos = 0;

    put_msb(frame + 1, &pos, 3, 0);
    put_msb(frame + 1, &pos, 8, 0);
    put_msb(frame + 1, &pos, 9, 0);
    put_msb(frame + 1, &pos, 9, 0);
    put_msb(frame + 1, &pos, 6, energy);
    put_msb(frame + 1, &pos, 1, update);
    put_msb(frame + 1, &pos, 3, 0);
    if (fwrite(frame, 1, sizeof(frame), f) != sizeof(frame))
        die("write");
}

static void write_nodata(FILE *f, int q)
{
    uint8_t toc = q ? 0x7c : 0x78;

    if (fwrite(&toc, 1, 1, f) != 1)
        die("write");
}

/* Frame types 9..14 are reserved and decoded as NO_DATA. */
static void write_reserved(FILE *f)
{
    uint8_t toc = 0x4c;

    if (fwrite(&toc, 1, 1, f) != 1)
        die("write");
}

static void write_wb_sid(FILE *f, int energy)
{
    uint8_t frame[6] = { 0x4c };
    int pos = 0;
    int i;

    for (i = 0; i < 3; i++)
        put_msb(frame + 1, &pos, 6, 0);
    put_msb(frame + 1, &pos, 5, 0);
    put_msb(frame + 1, &pos, 5, 0);
    put_msb(frame + 1, &pos, 6, energy);
    put_msb(frame + 1, &pos, 1, 0); /* dither */
    put_msb(frame + 1, &pos, 1, 1); /* SID_UPDATE */
    if (fwrite(frame, 1, sizeof(frame), f) != sizeof(frame))
        die("write");
}

static FILE *open_out(const char *path, const char *header)
{
    FILE *f = fopen(path, "wb");

    if (!f)
        die(path);
    if (fwrite(header, 1, strlen(header), f) != strlen(header))
        die(path);
    return f;
}

/* 8 speech, SID, 4 NO_DATA, SID, 4 NO_DATA, 8 speech.
   Frame 13 is the second SID. Frame 18 is the first speech after it. */
static void gen_sid_update(const char *path)
{
    FILE *f = open_out(path, "#!AMR\n");
    int i;

    for (i = 0; i < 8; i++)
        write_nb_speech(f);
    write_nb_sid(f, 16, 1);
    for (i = 0; i < 4; i++)
        write_nodata(f, 1);
    write_nb_sid(f, 16, 1);
    for (i = 0; i < 4; i++)
        write_nodata(f, 1);
    for (i = 0; i < 8; i++)
        write_nb_speech(f);
    if (fclose(f))
        die(path);
}

/* 8 speech, quiet SID, 4 NO_DATA, loud SID, 4 NO_DATA, 4 speech.
   Frame 18 is the first speech frame after the loud update. */
static void gen_state_resume(const char *path)
{
    FILE *f = open_out(path, "#!AMR\n");
    int i;

    for (i = 0; i < 8; i++)
        write_nb_speech(f);
    write_nb_sid(f, 8, 1);
    for (i = 0; i < 4; i++)
        write_nodata(f, 1);
    write_nb_sid(f, 32, 1);
    for (i = 0; i < 4; i++)
        write_nodata(f, 1);
    for (i = 0; i < 4; i++)
        write_nb_speech(f);
    if (fclose(f))
        die(path);
}

/* Speech, then one NO_DATA or reserved frame, repeated.
   q1_at_first selects the Q bit of the first NO_DATA frame only.
   Frame 8 is that NO_DATA, frame 17 is a Q=0 NO_DATA, frame 26 is reserved. */
static void gen_nodata(const char *path, int q1_at_first)
{
    FILE *f = open_out(path, "#!AMR\n");
    int i;

    for (i = 0; i < 8; i++)
        write_nb_speech(f);
    write_nodata(f, q1_at_first);
    for (i = 0; i < 8; i++)
        write_nb_speech(f);
    write_nodata(f, 0);
    for (i = 0; i < 8; i++)
        write_nb_speech(f);
    write_reserved(f);
    for (i = 0; i < 8; i++)
        write_nb_speech(f);
    if (fclose(f))
        die(path);
}

/* 8 speech, one lost frame, SID_UPDATE, 4 NO_DATA, 4 speech.
   Frame 10 is the first comfort-noise frame after that SID. */
static void gen_sid_after_nodata(const char *path)
{
    FILE *f = open_out(path, "#!AMR\n");
    int i;

    for (i = 0; i < 8; i++)
        write_nb_speech(f);
    write_nodata(f, 1);
    write_nb_sid(f, 20, 1);
    for (i = 0; i < 4; i++)
        write_nodata(f, 1);
    for (i = 0; i < 4; i++)
        write_nb_speech(f);
    if (fclose(f))
        die(path);
}

/* 11 speech, one SID, 51 NO_DATA (past the 50-frame mute threshold), 2 speech.
   Eleven speech frames (44 subframes) take fixed_gain_smooth() past its
   40-subframe hangover before silence begins. Frame 63 is the first speech
   frame after the muted period. 65 frames * 160 samples = 10400. */
static void gen_mute_resume(const char *path)
{
    FILE *f = open_out(path, "#!AMR\n");
    int i;

    for (i = 0; i < 11; i++)
        write_nb_speech(f);
    write_nb_sid(f, 15, 1);
    for (i = 0; i < 51; i++)
        write_nodata(f, 1);
    for (i = 0; i < 2; i++)
        write_nb_speech(f);
    if (fclose(f))
        die(path);
}

/* One SID_UPDATE and 10 NO_DATA frames. 11 * 320 = 3520 samples. */
static void gen_wb(const char *path)
{
    FILE *f = open_out(path, "#!AMR-WB\n");
    int i;

    write_wb_sid(f, 20);
    for (i = 0; i < 10; i++)
        write_nodata(f, 1);
    if (fclose(f))
        die(path);
}

int main(int argc, char **argv)
{
    const char *base;

    if (argc != 2) {
        fprintf(stderr, "usage: amrgen <output.amr>\n");
        return 1;
    }

    base = strrchr(argv[1], '/');
    base = base ? base + 1 : argv[1];

    if (!strcmp(base, "amrnb-sid-update.amr"))
        gen_sid_update(argv[1]);
    else if (!strcmp(base, "amrnb-state-resume-update.amr"))
        gen_state_resume(argv[1]);
    else if (!strcmp(base, "amrnb-nodata-cases.amr"))
        gen_nodata(argv[1], 1);
    else if (!strcmp(base, "amrnb-nodata-qswap-q0.amr"))
        gen_nodata(argv[1], 0);
    else if (!strcmp(base, "amrnb-sid-after-nodata.amr"))
        gen_sid_after_nodata(argv[1]);
    else if (!strcmp(base, "amrnb-dtx-mute-resume.amr"))
        gen_mute_resume(argv[1]);
    else if (!strcmp(base, "amrwb-dtx.amr"))
        gen_wb(argv[1]);
    else {
        fprintf(stderr, "amrgen: unknown stream '%s'\n", base);
        return 1;
    }
    return 0;
}
