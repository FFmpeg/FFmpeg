FFmpeg for SMPTE ST 2110
========================

[FFmpeg](README_FFMPEG.md) is a collection of libraries and tools to
process multimedia content such as audio, video, subtitles and related
metadata. This fork intents to support SMPTE ST 2110 for the broadcast
industry where the SDI-to-IP transition is in progress and where fully
virtualized solutions are being evaluated.

The primary goal is to decode a large format uncompressed video signal
(SMPTE ST 2110-20) along with raw audio (SMPTE ST 2110-30); the stream
synchronization being based on PTP (SMPTE ST 2110-10). After the AV
content is reconstructed, FFmpeg re-encodes the signal for storage or
streaming. The project also aims at evaluating the limitation in terms
of bandwidth, especially when additional streams are provided.

## Supported standards

* RFC 4175 for YCbCr-4:2:2, 8-bit and 10-bit
* AES67

## Install

### Transcoding dependencies

* `x264`
* `faac`
* `yasm`

### Build

```
./configure \
	--extra-libs=-ldl \
	--enable-version3 --enable-gpl --enable-nonfree \
	--enable-small \
	--enable-postproc --enable-avresample \
	--enable-libfdk-aac --enable-libx264 \
	--disable-ffplay --disable-ffprobe --disable-ffserver \
	--disable-stripping --disable-debug
make
./ffmpeg -version
```

## Simple A/V test

The following diagram shows a basic setup composed by 3 elements
connected on a LAN:

```
+-----------------------+            +-------------------+              +-----------+
| Source:               |            | Transcoder:       |              | Monitor:  |
| generate rtp streams  |-- video -->| depacketize,      |   h264       |           |
| conformed to RFC 4175 |            | reconstruct,      |-- mpeg-ts -->| playback  |
| and AES67             |-- audio -->| encode and stream |              |           |
+-----------------------+            +-------------------+              +-----------+
```

Since the transcoder is likely to run on a dedicated headless host,
there is no graphic to monitor the A/V content directly. So the
transcoded video is streamed to an extenal host.

### Source

If no source available, [gstreamer](https://gstreamer.freedesktop.org/)
can generate adequate streams. The following command:
* generates a video raw signal, 4:2:2 8-bit
* assembles a udp payload of type 102
* streams to multicast @ 239.0.0.0:5005
* generates an audio raw signal, 24-bit linear, 2 channels
* assembles a udp payload of type 103
* streams to multicast @ 239.0.0.0:5007

```
gst-launch-1.0 rtpbin name=rtpbin \
	videotestsrc horizontal-speed=2 !  \
	video/x-raw,width=1920,height=1080,framerate=30/1,format=UYVP ! rtpvrawpay pt=102 ! queue ! \
	rtpbin.send_rtp_sink_0 rtpbin.send_rtp_src_0 ! queue ! \
	udpsink host=239.0.0.0 port=5005 render-delay=0 rtpbin.send_rtcp_src_0 ! \
	udpsink host=239.0.0.0 port=5005 sync=false async=false \
	audiotestsrc ! audioresample ! audioconvert ! \
	rtpL24pay ! application/x-rtp, pt=103, payload=103, clock-rate=48000, channels=2 ! \
	rtpbin.send_rtp_sink_1 rtpbin.send_rtp_src_1 ! \
	udpsink host=239.0.0.0 port=5007 render-delay=0 rtpbin.send_rtcp_src_1 ! \
	udpsink host=239.0.0.0 port=5007 sync=false async=false
```

### Transcoder

Copy RTP session description to new test.sdp file:

```
# session
v=0
o=- 123456 11 IN IP4 X.X.X.X
s=A simple SMPTE ST 2110 session
i=basic streams for audio video
a=recvonly

# timing: unbounded and permanent session
t=0 0

# audio description first for better results
m=audio 5007 RTP/AVP 103
c=IN IP4 239.0.0.0/4
a=rtpmap:103 L24/48000/2

# video description
m=video 5005 RTP/AVP 102
c=IN IP4 239.0.0.0/4
a=rtpmap:102 raw/90000
a=fmtp:102 sampling=YCbCr-4:2:2; width=1920; height=1080; depth=10; colorimetry=BT.709
```

Run ffmpeg transcoder:

```
./ffmpeg -strict experimental -buffer_size 671088640 \
	-protocol_whitelist 'file,udp,rtp' \
	-i test.sdp -fifo_size 1000000000 -smpte2110_timestamp 1 \
	-c:a libfdk_aac -c:v libx264 -pass 1 \
	-f mpegts udp://<monitor IP>:<monitor port>
```

### Monitor

```
vlc udp://<monitor IP>:<monitor port>
```

or

```
ffplay udp://<monitor IP>:<monitor port>
```

### For 8-bit pixel format

Replace ``format=UYVP`` with ``format=UYVV`` in the gstreamer command
line and ``depth=10`` with ``depth=8`` in the SDP file.

### Transcoding performance

If your host struggles while transcoding the stream, you can still:
* enable multi-threading
* downsize the image right after the decoding
* decrease the quality of the h264 output

```
./ffmpeg -strict experimental \
	-buffer_size 671088640 \
	-threads 4 \
	-protocol_whitelist 'file,udp,rtp' \
	-i test.sdp -fifo_size 1000000000 \
	-vf scale=640:480 \
	-c:v libx264 -preset ultrafast -pass 1 \
	-c:a libfdk_aac -ac 2 \
	-f mpegts udp://<monitor IP>:<monitor port>
```

### Network performance

On the NIC side, one can increase the number of entries in the rx ring
buffer. Get the maximum supported value and set it using ``ethtool``:

```
ethtool -g <iface name>
ethtool -G <iface name> rx <max rx ring entries>
```

In the kernel, the receive socket buffer size is dynamically adjusted
and the maximum value (in bytes) can be adjusted depending on the available
memory:

```
echo 33554432 > /proc/sys/net/core/rmem_max
```

Do the same for the max number of packets in the receive queue:

```
echo 10000 > /proc/sys/net/core/netdev_max_backlog
```

## FFmpeg Documentation

Online documentation is available in the main [website](https://ffmpeg.org)
and in the [wiki](https://trac.ffmpeg.org).

## License

FFmpeg codebase and this derived work are mainly LGPL-licensed with
optional components licensed under GPL. Please refer to the LICENSE file
for detailed information.

## Contributing

Patches should be submitted using Github pull requests.
