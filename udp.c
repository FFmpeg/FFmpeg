/*
 * UDP prototype streaming system
 * Copyright (c) 2000 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "mpegenc.h"

#define UDP_TX_BUF_SIZE 32768
/* put it in UDP context */
static struct sockaddr_in dest_addr;

/* return non zero if error */
int udp_tx_open(UDPContext *s,
                const char *uri,
                int local_port)
{
    struct sockaddr_in my_addr;
    const char *p, *q;
    char hostname[1024];
    int port, udp_socket, tmp;
    struct hostent *hp;

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

    s->udp_socket = udp_socket;
    s->max_payload_size = 1024;
    return 0;
 fail:
    return -1;
}

void udp_tx_close(UDPContext *s)
{
    close(s->udp_socket);
}

extern int data_out_size;

void udp_write_data(void *opaque, UINT8 *buf, int size)
{
    UDPContext *s = opaque;
    int ret, len;

    /* primitive way to avoid big packets */
    data_out_size += size;
    while (size > 0) {
        len = size;
        if (len > s->max_payload_size)
            len = s->max_payload_size;
        
        ret = sendto (s->udp_socket, buf, len, 0, 
                      (struct sockaddr *) &dest_addr,
                      sizeof (dest_addr));
        if (ret < 0)
            perror("sendto");
        buf += len;
        size -= len;
    }
}

