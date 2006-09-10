/*
 * RTSP definitions
 * copyright (c) 2002 Fabrice Bellard
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
DEF(200, RTSP_STATUS_OK, "OK")
DEF(405, RTSP_STATUS_METHOD, "Method Not Allowed")
DEF(453, RTSP_STATUS_BANDWIDTH, "Not Enough Bandwidth")
DEF(454, RTSP_STATUS_SESSION, "Session Not Found")
DEF(455, RTSP_STATUS_STATE, "Method Not Valid in This State")
DEF(459, RTSP_STATUS_AGGREGATE, "Aggregate operation not allowed")
DEF(460, RTSP_STATUS_ONLY_AGGREGATE, "Only aggregate operation allowed")
DEF(461, RTSP_STATUS_TRANSPORT, "Unsupported transport")
DEF(500, RTSP_STATUS_INTERNAL, "Internal Server Error")
DEF(503, RTSP_STATUS_SERVICE, "Service Unavailable")
DEF(505, RTSP_STATUS_VERSION, "RTSP Version not supported")
