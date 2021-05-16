/*
 * cws2fws by Alex Beregszaszi
 * This file is placed in the public domain.
 * Use the program however you see fit.
 *
 * This utility converts compressed Macromedia Flash files to uncompressed ones.
 */

#include "config.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_IO_H
#include <io.h>
#endif
#include <zlib.h>

#ifdef DEBUG
#define dbgprintf printf
#else
#define dbgprintf(...) do { if (0) printf(__VA_ARGS__); } while (0)
#endif

int main(int argc, char *argv[])
{
    int fd_in, fd_out, comp_len, uncomp_len, i, last_out;
    char buf_in[1024], buf_out[65536];
    z_stream zstream;
    struct stat statbuf;
    int ret = 1;

    if (argc < 3) {
        printf("Usage: %s <infile.swf> <outfile.swf>\n", argv[0]);
        return 1;
    }

    fd_in = open(argv[1], O_RDONLY);
    if (fd_in < 0) {
        perror("Error opening input file");
        return 1;
    }

    fd_out = open(argv[2], O_WRONLY | O_CREAT, 00644);
    if (fd_out < 0) {
        perror("Error opening output file");
        close(fd_in);
        return 1;
    }

    if (read(fd_in, &buf_in, 8) != 8) {
        printf("Header error\n");
        goto out;
    }

    if (buf_in[0] != 'C' || buf_in[1] != 'W' || buf_in[2] != 'S') {
        printf("Not a compressed flash file\n");
        goto out;
    }

    if (fstat(fd_in, &statbuf) < 0) {
        perror("fstat failed");
        return 1;
    }
    comp_len   = statbuf.st_size;
    uncomp_len = buf_in[4] | (buf_in[5] << 8) | (buf_in[6] << 16) | (buf_in[7] << 24);

    printf("Compressed size: %d Uncompressed size: %d\n",
           comp_len - 4, uncomp_len - 4);

    // write out modified header
    buf_in[0] = 'F';
    if (write(fd_out, &buf_in, 8) < 8) {
        perror("Error writing output file");
        goto out;
    }

    zstream.zalloc = NULL;
    zstream.zfree  = NULL;
    zstream.opaque = NULL;
    if (inflateInit(&zstream) != Z_OK) {
        fprintf(stderr, "inflateInit failed\n");
        return 1;
    }

    for (i = 0; i < comp_len - 8;) {
        int ret, len = read(fd_in, &buf_in, 1024);

        if (len == -1) {
            printf("read failure\n");
            inflateEnd(&zstream);
            goto out;
        }

        dbgprintf("read %d bytes\n", len);

        last_out = zstream.total_out;

        zstream.next_in   = &buf_in[0];
        zstream.avail_in  = len;
        zstream.next_out  = &buf_out[0];
        zstream.avail_out = 65536;

        ret = inflate(&zstream, Z_SYNC_FLUSH);
        if (ret != Z_STREAM_END && ret != Z_OK) {
            printf("Error while decompressing: %d\n", ret);
            inflateEnd(&zstream);
            goto out;
        }

        dbgprintf("a_in: %d t_in: %lu a_out: %d t_out: %lu -- %lu out\n",
                  zstream.avail_in, zstream.total_in, zstream.avail_out,
                  zstream.total_out, zstream.total_out - last_out);

        if (write(fd_out, &buf_out, zstream.total_out - last_out) <
            zstream.total_out - last_out) {
            perror("Error writing output file");
            inflateEnd(&zstream);
            goto out;
        }

        i += len;

        if (ret == Z_STREAM_END || ret == Z_BUF_ERROR)
            break;
    }

    if (zstream.total_out != uncomp_len - 8) {
        printf("Size mismatch (%lu != %d), updating header...\n",
               zstream.total_out, uncomp_len - 8);

        buf_in[0] =  (zstream.total_out + 8)        & 0xff;
        buf_in[1] = ((zstream.total_out + 8) >>  8) & 0xff;
        buf_in[2] = ((zstream.total_out + 8) >> 16) & 0xff;
        buf_in[3] = ((zstream.total_out + 8) >> 24) & 0xff;

        if (   lseek(fd_out, 4, SEEK_SET) < 0
            || write(fd_out, &buf_in, 4) < 4) {
            perror("Error writing output file");
            inflateEnd(&zstream);
            goto out;
        }
    }

    ret = 0;
    inflateEnd(&zstream);
out:
    close(fd_in);
    close(fd_out);
    return ret;
}
