
#include "amlqueue.h"

void ffaml_init_queue(PacketQueue *queue)
{
  queue->head = NULL;
  queue->tail = NULL;
  queue->size = 0;
}

int ffaml_queue_packet(AVCodecContext *avctx, PacketQueue *queue, AVPacket *avpkt)
{
  int ret = 0;

  // create our packet entry for the queue
  PacketEntry *packetentry = malloc(sizeof(*packetentry));
  if (!packetentry) {
      ret = AVERROR(ENOMEM);
      goto done;
  }

  packetentry->pkt = av_packet_clone(avpkt);
  packetentry->next = NULL;
  packetentry->prev = NULL;

  // add the packet to start of the queue
  if (queue->head)
  {
    packetentry->next = queue->head;
    queue->head->prev = packetentry;
  }
  if (!queue->tail)
    queue->tail = packetentry;

  queue->head = packetentry;
  queue->size++;

  return 0;

  done:
    return ret;
}

AVPacket *ffaml_dequeue_packet(AVCodecContext *avctx, PacketQueue *queue)
{
  PacketEntry *packetentry;
  AVPacket *pkt;
  if (!queue->tail)
    return NULL;

  packetentry = queue->tail;
  queue->tail = packetentry->prev;
  if (!queue->tail)
    queue->head = NULL;

  if (queue->tail)
    queue->tail->next = NULL;

  queue->size--;

  pkt = packetentry->pkt;
  free(packetentry);

  return pkt;
}

AVPacket *ffaml_queue_peek_pts_packet(AVCodecContext *avctx, PacketQueue *queue)
{
  double smallest_pts = 10000000000000.0;
  PacketEntry *packetentry = queue->head;
  PacketEntry *foundentry = NULL;
  AVPacket *pkt = NULL;
  double packet_pts = 0;

  // find the smallest pts entry
  while (packetentry)
  {
    packet_pts = packetentry->pkt->pts * av_q2d(avctx->time_base);
    if (packet_pts < smallest_pts)
    {
      foundentry = packetentry;
      smallest_pts = packet_pts;
    }
    packetentry = packetentry->next;
  }

  // now remove the rentry from queue
  if (foundentry->prev)
    foundentry->prev->next = foundentry->next;
  if (foundentry->next)
    foundentry->next->prev = foundentry->prev;

  queue->size--;

  pkt = foundentry->pkt;
  free(foundentry);

  return pkt;
}

