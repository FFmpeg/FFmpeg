/*
 * UDP prototype streaming system
 * Copyright (c) 2000 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

typedef struct {
    int udp_socket;
    int max_payload_size; /* in bytes */
} UDPContext;

#define UDP_TX_BUF_SIZE 32768

/* put it in UDP context */
static struct sockaddr_in dest_addr;

/* return non zero if error */
static int udp_open(URLContext *h, const char *uri, int flags)
{
    int local_port = 0;
    struct sockaddr_in my_addr;
    const char *p, *q;
    char hostname[1024];
    int port, udp_socket, tmp;
    struct hostent *hp;
    UDPContext *s;

    h->is_streamed = 1;

    if (!(flags & URL_WRONLY))
        return -EIO;
    
    /* fill the dest addr */
    p = uri;
    if (!strstart(p, "udp:", &p))
        return -1;
    q = strchr(p, ':');
    if (!q)
        return -1;
    memcpy(hostname, p, q - p);
    hostname[q - p] = '\0';
    port = strtol(q+1, NULL, 10);
    if (port <= 0)
        return -1;

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if ((inet_aton(hostname, &dest_addr.sin_addr)) == 0) {
        hp = gethostbyname(hostname);
        if (!hp)
            return -1;
        memcpy ((char *) &dest_addr.sin_addr, hp->h_addr, hp->h_length);
    }
    
    udp_socket = socket(PF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0)
        return -1;

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(local_port);
    my_addr.sin_addr.s_addr = htonl (INADDR_ANY);

    /* the bind is needed to give a port to the socket now */
    if (bind(udp_socket,(struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) 
        goto fail;

    /* limit the tx buf size to limit latency */

    tmp = UDP_TX_BUF_SIZE;
    if (setsockopt(udp_socket, SOL_SOCKET, SO_SNDBUF, &tmp, sizeof(tmp)) < 0) {
        perror("setsockopt sndbuf");
        goto fail;
    }

    s = av_malloc(sizeof(UDPContext));
    if (!s)
        return -ENOMEM;
    h->priv_data = s;
    s->udp_socket = udp_socket;
    h->packet_size = 1500;
    return 0;
 fail:
    return -EIO;
}

int udp_close(URLContext *h)
{
    UDPContext *s = h->priv_data;
    close(s->udp_socket);
    return 0;
}

int udp_write(URLContext *h, UINT8 *buf, int size)
{
    UDPContext *s = h->priv_data;
    int ret, len, size1;

    /* primitive way to avoid big packets */
    size1 = size;
    while (size > 0) {
        len = size;
        if (len > h->packet_size)
            len = h->packet_size;
        
        ret = sendto (s->udp_socket, buf, len, 0, 
                      (struct sockaddr *) &dest_addr,
                      sizeof (dest_addr));
        if (ret < 0)
            perror("sendto");
        buf += len;
        size -= len;
    }
    return size1 - size;
}

URLProtocol udp_protocol = {
    "udp",
    udp_open,
    NULL, /* read */
    udp_write,
    NULL, /* seek */
    udp_close,
};
