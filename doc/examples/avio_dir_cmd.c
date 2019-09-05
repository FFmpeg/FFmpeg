/*
 * Copyright (c) 2014 Lukasz Marek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>

static const char *type_string(int type)
{
    switch (type) {
    case AVIO_ENTRY_DIRECTORY:
        return "<DIR>";
    case AVIO_ENTRY_FILE:
        return "<FILE>";
    case AVIO_ENTRY_BLOCK_DEVICE:
        return "<BLOCK DEVICE>";
    case AVIO_ENTRY_CHARACTER_DEVICE:
        return "<CHARACTER DEVICE>";
    case AVIO_ENTRY_NAMED_PIPE:
        return "<PIPE>";
    case AVIO_ENTRY_SYMBOLIC_LINK:
        return "<LINK>";
    case AVIO_ENTRY_SOCKET:
        return "<SOCKET>";
    case AVIO_ENTRY_SERVER:
        return "<SERVER>";
    case AVIO_ENTRY_SHARE:
        return "<SHARE>";
    case AVIO_ENTRY_WORKGROUP:
        return "<WORKGROUP>";
    case AVIO_ENTRY_UNKNOWN:
    default:
        break;
    }
    return "<UNKNOWN>";
}

static int list_op(const char *input_dir)
{
    AVIODirEntry *entry = NULL;
    AVIODirContext *ctx = NULL;
    int cnt, ret;
    char filemode[4], uid_and_gid[20];

    if ((ret = avio_open_dir(&ctx, input_dir, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open directory: %s.\n", av_err2str(ret));
        goto fail;
    }

    cnt = 0;
    for (;;) {
        if ((ret = avio_read_dir(ctx, &entry)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot list directory: %s.\n", av_err2str(ret));
            goto fail;
        }
        if (!entry)
            break;
        if (entry->filemode == -1) {
            snprintf(filemode, 4, "???");
        } else {
            snprintf(filemode, 4, "%3"PRIo64, entry->filemode);
        }
        snprintf(uid_and_gid, 20, "%"PRId64"(%"PRId64")", entry->user_id, entry->group_id);
        if (cnt == 0)
            av_log(NULL, AV_LOG_INFO, "%-9s %12s %30s %10s %s %16s %16s %16s\n",
                   "TYPE", "SIZE", "NAME", "UID(GID)", "UGO", "MODIFIED",
                   "ACCESSED", "STATUS_CHANGED");
        av_log(NULL, AV_LOG_INFO, "%-9s %12"PRId64" %30s %10s %s %16"PRId64" %16"PRId64" %16"PRId64"\n",
               type_string(entry->type),
               entry->size,
               entry->name,
               uid_and_gid,
               filemode,
               entry->modification_timestamp,
               entry->access_timestamp,
               entry->status_change_timestamp);
        avio_free_directory_entry(&entry);
        cnt++;
    };

  fail:
    avio_close_dir(&ctx);
    return ret;
}

static int del_op(const char *url)
{
    int ret = avpriv_io_delete(url);
    if (ret < 0)
        av_log(NULL, AV_LOG_ERROR, "Cannot delete '%s': %s.\n", url, av_err2str(ret));
    return ret;
}

static int move_op(const char *src, const char *dst)
{
    int ret = avpriv_io_move(src, dst);
    if (ret < 0)
        av_log(NULL, AV_LOG_ERROR, "Cannot move '%s' into '%s': %s.\n", src, dst, av_err2str(ret));
    return ret;
}


static void usage(const char *program_name)
{
    fprintf(stderr, "usage: %s OPERATION entry1 [entry2]\n"
            "API example program to show how to manipulate resources "
            "accessed through AVIOContext.\n"
            "OPERATIONS:\n"
            "list      list content of the directory\n"
            "move      rename content in directory\n"
            "del       delete content in directory\n",
            program_name);
}

int main(int argc, char *argv[])
{
    const char *op = NULL;
    int ret;

    av_log_set_level(AV_LOG_DEBUG);

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    avformat_network_init();

    op = argv[1];
    if (strcmp(op, "list") == 0) {
        if (argc < 3) {
            av_log(NULL, AV_LOG_INFO, "Missing argument for list operation.\n");
            ret = AVERROR(EINVAL);
        } else {
            ret = list_op(argv[2]);
        }
    } else if (strcmp(op, "del") == 0) {
        if (argc < 3) {
            av_log(NULL, AV_LOG_INFO, "Missing argument for del operation.\n");
            ret = AVERROR(EINVAL);
        } else {
            ret = del_op(argv[2]);
        }
    } else if (strcmp(op, "move") == 0) {
        if (argc < 4) {
            av_log(NULL, AV_LOG_INFO, "Missing argument for move operation.\n");
            ret = AVERROR(EINVAL);
        } else {
            ret = move_op(argv[2], argv[3]);
        }
    } else {
        av_log(NULL, AV_LOG_INFO, "Invalid operation %s\n", op);
        ret = AVERROR(EINVAL);
    }

    avformat_network_deinit();

    return ret < 0 ? 1 : 0;
}
