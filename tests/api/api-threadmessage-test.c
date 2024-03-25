/*
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

/**
 * Thread message API test
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/threadmessage.h"
#include "libavutil/thread.h" // not public

struct sender_data {
    int id;
    pthread_t tid;
    int workload;
    AVThreadMessageQueue *queue;
};

/* same as sender_data but shuffled for testing purpose */
struct receiver_data {
    pthread_t tid;
    int workload;
    int id;
    AVThreadMessageQueue *queue;
};

struct message {
    AVFrame *frame;
    // we add some junk in the message to make sure the message size is >
    // sizeof(void*)
    int magic;
};

#define MAGIC 0xdeadc0de

static void free_frame(void *arg)
{
    struct message *msg = arg;
    av_assert0(msg->magic == MAGIC);
    av_frame_free(&msg->frame);
}

static void *sender_thread(void *arg)
{
    int i, ret = 0;
    struct sender_data *wd = arg;

    av_log(NULL, AV_LOG_INFO, "sender #%d: workload=%d\n", wd->id, wd->workload);
    for (i = 0; i < wd->workload; i++) {
        if (rand() % wd->workload < wd->workload / 10) {
            av_log(NULL, AV_LOG_INFO, "sender #%d: flushing the queue\n", wd->id);
            av_thread_message_flush(wd->queue);
        } else {
            char *val;
            AVDictionary *meta = NULL;
            struct message msg = {
                .magic = MAGIC,
                .frame = av_frame_alloc(),
            };

            if (!msg.frame) {
                ret = AVERROR(ENOMEM);
                break;
            }

            /* we add some metadata to identify the frames */
            val = av_asprintf("frame %d/%d from sender %d",
                              i + 1, wd->workload, wd->id);
            if (!val) {
                av_frame_free(&msg.frame);
                ret = AVERROR(ENOMEM);
                break;
            }
            ret = av_dict_set(&meta, "sig", val, AV_DICT_DONT_STRDUP_VAL);
            if (ret < 0) {
                av_frame_free(&msg.frame);
                break;
            }
            msg.frame->metadata = meta;

            /* allocate a real frame in order to simulate "real" work */
            msg.frame->format = AV_PIX_FMT_RGBA;
            msg.frame->width  = 320;
            msg.frame->height = 240;
            ret = av_frame_get_buffer(msg.frame, 0);
            if (ret < 0) {
                av_frame_free(&msg.frame);
                break;
            }

            /* push the frame in the common queue */
            av_log(NULL, AV_LOG_INFO, "sender #%d: sending my work (%d/%d frame:%p)\n",
                   wd->id, i + 1, wd->workload, msg.frame);
            ret = av_thread_message_queue_send(wd->queue, &msg, 0);
            if (ret < 0) {
                av_frame_free(&msg.frame);
                break;
            }
        }
    }
    av_log(NULL, AV_LOG_INFO, "sender #%d: my work is done here (%s)\n",
           wd->id, av_err2str(ret));
    av_thread_message_queue_set_err_recv(wd->queue, ret < 0 ? ret : AVERROR_EOF);
    return NULL;
}

static void *receiver_thread(void *arg)
{
    int i, ret = 0;
    struct receiver_data *rd = arg;

    for (i = 0; i < rd->workload; i++) {
        if (rand() % rd->workload < rd->workload / 10) {
            av_log(NULL, AV_LOG_INFO, "receiver #%d: flushing the queue, "
                   "discarding %d message(s)\n", rd->id,
                   av_thread_message_queue_nb_elems(rd->queue));
            av_thread_message_flush(rd->queue);
        } else {
            struct message msg;
            AVDictionary *meta;
            AVDictionaryEntry *e;

            ret = av_thread_message_queue_recv(rd->queue, &msg, 0);
            if (ret < 0)
                break;
            av_assert0(msg.magic == MAGIC);
            meta = msg.frame->metadata;
            e = av_dict_get(meta, "sig", NULL, 0);
            av_log(NULL, AV_LOG_INFO, "got \"%s\" (%p)\n", e->value, msg.frame);
            av_frame_free(&msg.frame);
        }
    }

    av_log(NULL, AV_LOG_INFO, "consumed enough (%d), stop\n", i);
    av_thread_message_queue_set_err_send(rd->queue, ret < 0 ? ret : AVERROR_EOF);

    return NULL;
}

static int get_workload(int minv, int maxv)
{
    return maxv == minv ? maxv : rand() % (maxv - minv) + minv;
}

int main(int ac, char **av)
{
    int i, ret = 0;
    int max_queue_size;
    int nb_senders, sender_min_load, sender_max_load;
    int nb_receivers, receiver_min_load, receiver_max_load;
    struct sender_data *senders;
    struct receiver_data *receivers;
    AVThreadMessageQueue *queue = NULL;

    if (ac != 8) {
        av_log(NULL, AV_LOG_ERROR, "%s <max_queue_size> "
               "<nb_senders> <sender_min_send> <sender_max_send> "
               "<nb_receivers> <receiver_min_recv> <receiver_max_recv>\n", av[0]);
        return 1;
    }

    max_queue_size    = atoi(av[1]);
    nb_senders        = atoi(av[2]);
    sender_min_load   = atoi(av[3]);
    sender_max_load   = atoi(av[4]);
    nb_receivers      = atoi(av[5]);
    receiver_min_load = atoi(av[6]);
    receiver_max_load = atoi(av[7]);

    if (max_queue_size <= 0 ||
        nb_senders <= 0 || sender_min_load <= 0 || sender_max_load <= 0 ||
        nb_receivers <= 0 || receiver_min_load <= 0 || receiver_max_load <= 0) {
        av_log(NULL, AV_LOG_ERROR, "negative values not allowed\n");
        return 1;
    }

    av_log(NULL, AV_LOG_INFO, "qsize:%d / %d senders sending [%d-%d] / "
           "%d receivers receiving [%d-%d]\n", max_queue_size,
           nb_senders, sender_min_load, sender_max_load,
           nb_receivers, receiver_min_load, receiver_max_load);

    senders   = av_calloc(nb_senders,   sizeof(*senders));
    receivers = av_calloc(nb_receivers, sizeof(*receivers));
    if (!senders || !receivers) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = av_thread_message_queue_alloc(&queue, max_queue_size, sizeof(struct message));
    if (ret < 0)
        goto end;

    av_thread_message_queue_set_free_func(queue, free_frame);

#define SPAWN_THREADS(type) do {                                                \
    for (i = 0; i < nb_##type##s; i++) {                                        \
        struct type##_data *td = &type##s[i];                                   \
                                                                                \
        td->id = i;                                                             \
        td->queue = queue;                                                      \
        td->workload = get_workload(type##_min_load, type##_max_load);          \
                                                                                \
        ret = pthread_create(&td->tid, NULL, type##_thread, td);                \
        if (ret) {                                                              \
            const int err = AVERROR(ret);                                       \
            av_log(NULL, AV_LOG_ERROR, "Unable to start " AV_STRINGIFY(type)    \
                   " thread: %s\n", av_err2str(err));                           \
            goto end;                                                           \
        }                                                                       \
    }                                                                           \
} while (0)

#define WAIT_THREADS(type) do {                                                 \
    for (i = 0; i < nb_##type##s; i++) {                                        \
        struct type##_data *td = &type##s[i];                                   \
                                                                                \
        ret = pthread_join(td->tid, NULL);                                      \
        if (ret) {                                                              \
            const int err = AVERROR(ret);                                       \
            av_log(NULL, AV_LOG_ERROR, "Unable to join " AV_STRINGIFY(type)     \
                   " thread: %s\n", av_err2str(err));                           \
            goto end;                                                           \
        }                                                                       \
    }                                                                           \
} while (0)

    SPAWN_THREADS(receiver);
    SPAWN_THREADS(sender);

    WAIT_THREADS(sender);
    WAIT_THREADS(receiver);

end:
    av_thread_message_queue_free(&queue);
    av_freep(&senders);
    av_freep(&receivers);

    if (ret < 0 && ret != AVERROR_EOF) {
        av_log(NULL, AV_LOG_ERROR, "Error: %s\n", av_err2str(ret));
        return 1;
    }
    return 0;
}
