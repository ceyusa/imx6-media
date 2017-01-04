#!/bin/sh
#
# A simple RTP server with retransmission
#  sends the output of videotestsrc as h264 encoded RTP on port 5000, RTCP is sent on
#  port 5001. The destination is 127.0.0.1.
#  the video receiver RTCP reports are received on port 5005
#
#  .-------.    .-------.    .-------.      .----------.     .-------.
#  |videots|    |h264enc|    |h264pay|      | rtpbin   |     |udpsink|  RTP
#  |      src->sink    src->sink    src->send_rtp send_rtp->sink     | port=5000
#  '-------'    '-------'    '-------'      |          |     '-------'
#                                           |          |
#                                           |          |     .-------.
#                                           |          |     |udpsink|  RTCP
#                                           |    send_rtcp->sink     | port=5001
#                            .-------.      |          |     '-------' sync=false
#                 RTCP       |udpsrc |      |          |               async=false
#               port=5005    |     src->recv_rtcp      |
#                            '-------'      '----------'
#
# ideally we should transport the properties on the RTP udpsink pads to the
# receiver in order to transmit the SPS and PPS earlier.

# change this to send the RTP data and RTCP to another host
DEST=192.168.2.221

# H264 encode from the source
VELEM="v4l2src io-mode=dmabuf"
VCAPS="video/x-raw,width=1280,height=720,framerate=10/1"
VSOURCE="$VELEM ! $VCAPS"
VENC="v4l2video2h264enc output-io-mode=dmabuf-import ! video/x-h264,stream-format=byte-stream,aligment=au,profile=constrained-baseline ! rtph264pay config-interval=2"

VRTPSINK="udpsink port=5000 host=$DEST name=vrtpsink"
VRTCPSINK="udpsink port=5001 host=$DEST sync=false async=false name=vrtcpsink"
VRTCPSRC="udpsrc port=5005 name=vrtpsrc"

gst-launch-1.0 -v rtpbin name=rtpbin rtp-profile=avpf  \
    $VSOURCE ! $VENC ! rtpbin.send_rtp_sink_0          \
        rtpbin.send_rtp_src_0 ! $VRTPSINK              \
        rtpbin.send_rtcp_src_0 ! $VRTCPSINK            \
      $VRTCPSRC ! rtpbin.recv_rtcp_sink_0

