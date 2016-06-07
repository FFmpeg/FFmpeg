
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

  if (!packetentry->pkt)
    av_log(avctx, AV_LOG_ERROR, "queuing null packet !!\n");

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

  av_log(avctx, AV_LOG_DEBUG, "queued packet in %x, entry=%x, pkt=%x size= %d\n", queue, packetentry,packetentry->pkt, queue->size);

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

  av_log(avctx, AV_LOG_DEBUG, "dequeued packet in %x, entry=%x, remaining %d\n", queue, packetentry, queue->size);
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
    if (!packetentry->pkt)
      av_log(avctx, AV_LOG_ERROR, "packet entry %x packet is null and shouldn't be !\n", packetentry);

    packet_pts = packetentry->pkt->pts * av_q2d(avctx->time_base);
    if (packet_pts < smallest_pts)
    {
      foundentry = packetentry;
      smallest_pts = packet_pts;
    }
    packetentry = packetentry->next;
  }

   av_log(avctx, AV_LOG_DEBUG, "peeking packet in %x, entry=%x, pts=%f, remaining %d\n", queue, foundentry, (double)foundentry->pkt->pts * av_q2d(avctx->time_base), queue->size);

  // now remove the rentry from queue
  if (foundentry->prev)
    foundentry->prev->next = foundentry->next;
  else
    queue->head = foundentry->next;

  if (foundentry->next)
    foundentry->next->prev = foundentry->prev;
  else
    queue->tail = foundentry->prev;

  queue->size--;

  pkt = foundentry->pkt;
  free(foundentry);

  return pkt;
}

void ffaml_queue_clear(AVCodecContext *avctx, PacketQueue *queue)
{
  AVPacket *pkt;
  while ((pkt = ffaml_dequeue_packet(avctx, queue)))
  {
    av_packet_free(&pkt);
  }

}
