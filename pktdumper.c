#include <avformat.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PKTFILESUFF "_%08Ld_%02d_%010Ld_%06d_%c.bin"

static int usage(int ret)
{
    fprintf(stderr, "dump (up to maxpkts) AVPackets as they are demuxed by libavformat.\n");
    fprintf(stderr, "each packet is dumped in its own file named like `basename file.ext`_$PKTNUM_$STREAMINDEX_$STAMP_$SIZE_$FLAGS.bin\n");
    fprintf(stderr, "pktdumper [-nw] file [maxpkts]\n");
    fprintf(stderr, "-n\twrite No file at all, only demux.\n");
    fprintf(stderr, "-w\tWait at end of processing instead of quitting.\n");
    return ret;
}

int main(int argc, char **argv)
{
    char fntemplate[PATH_MAX];
    char pktfilename[PATH_MAX];
    AVFormatContext *fctx;
    AVPacket pkt;
    int64_t pktnum = 0;
    int64_t maxpkts = 0;
    int dontquit = 0;
    int nowrite = 0;
    int err;

    if ((argc > 1) && !strncmp(argv[1], "-", 1)) {
        if (strchr(argv[1], 'w'))
            dontquit = 1;
        if (strchr(argv[1], 'n'))
            nowrite = 1;
        argv++;
        argc--;
    }
    if (argc < 2)
        return usage(1);
    if (argc > 2)
        maxpkts = atoi(argv[2]);
    strncpy(fntemplate, argv[1], PATH_MAX-1);
    if (strrchr(argv[1], '/'))
        strncpy(fntemplate, strrchr(argv[1], '/')+1, PATH_MAX-1);
    if (strrchr(fntemplate, '.'))
        *strrchr(fntemplate, '.') = '\0';
    if (strchr(fntemplate, '%')) {
        fprintf(stderr, "can't use filenames containing '%%'\n");
        return usage(1);
    }
    if (strlen(fntemplate) + sizeof(PKTFILESUFF) >= PATH_MAX-1) {
        fprintf(stderr, "filename too long\n");
        return usage(1);
    }
    strcat(fntemplate, PKTFILESUFF);
    printf("FNTEMPLATE: '%s'\n", fntemplate);

    // register all file formats
    av_register_all();

    err = av_open_input_file(&fctx, argv[1], NULL, 0, NULL);
    if (err < 0) {
        fprintf(stderr, "av_open_input_file: error %d\n", err);
        return 1;
    }

    err = av_find_stream_info(fctx);
    if (err < 0) {
        fprintf(stderr, "av_find_stream_info: error %d\n", err);
        return 1;
    }

    av_init_packet(&pkt);

    while ((err = av_read_frame(fctx, &pkt)) >= 0) {
        int fd;
        snprintf(pktfilename, PATH_MAX-1, fntemplate, pktnum, pkt.stream_index, pkt.pts, pkt.size, (pkt.flags & PKT_FLAG_KEY)?'K':'_');
        printf(PKTFILESUFF"\n", pktnum, pkt.stream_index, pkt.pts, pkt.size, (pkt.flags & PKT_FLAG_KEY)?'K':'_');
        //printf("open(\"%s\")\n", pktfilename);
        if (!nowrite) {
            fd = open(pktfilename, O_WRONLY|O_CREAT, 0644);
            write(fd, pkt.data, pkt.size);
            close(fd);
        }
        pktnum++;
        if (maxpkts && (pktnum >= maxpkts))
            break;
    }

    while (dontquit)
        sleep(60);

    return 0;
}
