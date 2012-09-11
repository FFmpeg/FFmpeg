/*
 * originally by Andreas Öman (andoma)
 * some changes by Alexander Strange
 */

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


int
main(int argc, char **argv)
{
    int fd[2];
    int print_pixels = 0;
    int dump_blocks = 0;

    int width;
    int height;
    int to_skip = 0;

    if (argc < 6) {
        fprintf(stderr, "%s [YUV file 1] [YUV file 2] width height pixelcmp|blockdump (# to skip)\n", argv[0]);
        return 1;
    }

    width  = atoi(argv[3]);
    height = atoi(argv[4]);
    if (argc > 6)
        to_skip = atoi(argv[6]);

    uint8_t *Y[2], *C[2][2];
    int i, v, c, p;
    int lsiz = width * height;
    int csiz = width * height / 4;
    int x, y;
    int cwidth = width / 2;
    int fr = to_skip;
    int mb;
    char *mberrors;
    int mb_x, mb_y;
    uint8_t *a;
    uint8_t *b;
    int die = 0;

    print_pixels = strstr(argv[5], "pixelcmp") ? 1 : 0;
    dump_blocks  = strstr(argv[5], "blockdump") ? 1 : 0;

    for(i = 0; i < 2; i++) {
        Y[i] = malloc(lsiz);
        C[0][i] = malloc(csiz);
        C[1][i] = malloc(csiz);

        fd[i] = open(argv[1 + i], O_RDONLY);
        if(fd[i] == -1) {
            perror("open");
            exit(1);
        }
        fcntl(fd[i], F_NOCACHE, 1);

        if (to_skip)
            lseek(fd[i], to_skip * (lsiz + 2*csiz), SEEK_SET);
    }

    mb_x = width / 16;
    mb_y = height / 16;

    mberrors = malloc(mb_x * mb_y);

    while(!die) {
        memset(mberrors, 0, mb_x * mb_y);

        printf("Loading frame %d\n", ++fr);

        for(i = 0; i < 2; i++) {
            v = read(fd[i], Y[i], lsiz);
            if(v != lsiz) {
                fprintf(stderr, "Unable to read Y from file %d, exiting\n", i + 1);
                return 1;
            }
        }


        for(c = 0; c < lsiz; c++) {
            if(Y[0][c] != Y[1][c]) {
                x = c % width;
                y = c / width;

                mb = x / 16 + (y / 16) * mb_x;

                if(print_pixels)
                    printf("Luma diff 0x%02x != 0x%02x at pixel (%4d,%-4d) mb(%d,%d) #%d\n",
                           Y[0][c],
                           Y[1][c],
                           x, y,
                           x / 16,
                           y / 16,
                           mb);

                mberrors[mb] |= 1;
            }
        }

        /* Chroma planes */

        for(p = 0; p < 2; p++) {

            for(i = 0; i < 2; i++) {
                v = read(fd[i], C[p][i], csiz);
                if(v != csiz) {
                    fprintf(stderr, "Unable to read %c from file %d, exiting\n",
                            "UV"[p], i + 1);
                    return 1;
                }
            }

            for(c = 0; c < csiz; c++) {
                if(C[p][0][c] != C[p][1][c]) {
                    x = c % cwidth;
                    y = c / cwidth;

                    mb = x / 8 + (y / 8) * mb_x;

                    mberrors[mb] |= 2 << p;

                    if(print_pixels)

                        printf("c%c diff 0x%02x != 0x%02x at pixel (%4d,%-4d) "
                               "mb(%3d,%-3d) #%d\n",
                               p ? 'r' : 'b',
                               C[p][0][c],
                               C[p][1][c],

                               x, y,
                               x / 8,
                               y / 8,
                               x / 8 + y / 8 * cwidth / 8);
                }
            }
        }

        for(i = 0; i < mb_x * mb_y; i++) {
            x = i % mb_x;
            y = i / mb_x;

            if(mberrors[i]) {
                die = 1;

                printf("MB (%3d,%-3d) %4d %d %c%c%c damaged\n",
                       x, y, i, mberrors[i],
                       mberrors[i] & 1 ? 'Y' : ' ',
                       mberrors[i] & 2 ? 'U' : ' ',
                       mberrors[i] & 4 ? 'V' : ' ');

                if(dump_blocks) {
                    a = Y[0] + x * 16 + y * 16 * width;
                    b = Y[1] + x * 16 + y * 16 * width;

                    for(y = 0; y < 16; y++) {
                        printf("%c ", "TB"[y&1]);
                        for(x = 0; x < 16; x++)
                            printf("%02x%c", a[x + y * width],
                                   a[x + y * width] != b[x + y * width] ? '<' : ' ');

                        printf("| ");
                        for(x = 0; x < 16; x++)
                            printf("%02x%c", b[x + y * width],
                                   a[x + y * width] != b[x + y * width] ? '<' : ' ');

                        printf("\n");
                    }
                }
            }
        }
    }

    return 0;
}
