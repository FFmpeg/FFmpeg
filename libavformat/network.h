/*
 * Copyright (c) 2007 The FFmpeg Project.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef NETWORK_H
#define NETWORK_H

#ifdef __MINGW32__
#include <winsock2.h>
#include <ws2tcpip.h>

#define ff_neterrno() WSAGetLastError()
#define FF_NETERROR(err) WSA##err
#define WSAEAGAIN WSAEWOULDBLOCK
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define ff_neterrno() errno
#define FF_NETERROR(err) err
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

int ff_socket_nonblock(int socket, int enable);

#if !defined(HAVE_INET_ATON)
/* in os_support.c */
int inet_aton (const char * str, struct in_addr * add);
#endif

#endif
