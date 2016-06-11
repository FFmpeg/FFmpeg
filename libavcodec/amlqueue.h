#ifndef _AMLQUEUE_H_
#define _AMLQUEUE_H_

#include "avcodec.h"

// types declaration
typedef struct PacketEntry {
  AVPacket *pkt;
  int pkt_id;
  struct PacketEntry *next;
  struct PacketEntry *prev;
} PacketEntry;

typedef struct PacketQueue {
  PacketEntry *head;
  PacketEntry *tail;
  int size;
} PacketQueue;

// fucntions prototypes
void ffaml_init_queue(PacketQueue *queue);
int ffaml_queue_packet(AVCodecContext *avctx, PacketQueue *queuex, AVPacket *avpkt);
AVPacket *ffaml_dequeue_packet(AVCodecContext *avctx,PacketQueue *queue);
AVPacket *ffaml_queue_peek_pts_packet(AVCodecContext *avctx, PacketQueue *queue);
void ffaml_queue_clear(AVCodecContext *avctx, PacketQueue *queue);

#define DEBUG   (0)

#endif /* _AMLQUEUE_H_ */
