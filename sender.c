/*
 * gst-rtpbin-stream-sender-loop.c
 *
 *  quick'n'dirty test application for creating/destroying video stream on i.MX6
 *  in loop/multiple iterations
 */
#include <gst/gst.h>
#include <stdlib.h>
#include <unistd.h>

static struct options_st
{
  gchar *sender_host;
  gint video_rtp_port;
  gint width_video;
  gint height_video;
  gboolean no_dma_buf;
} options = {
  "127.0.0.1",
  9997,
  320,
  240,
  FALSE
};

typedef struct {
  GMainLoop *loop;
  GstElement *pipeline;
  gboolean halt;
} App;

static guint stats_id = 0;

/* this function is called every second and dumps the RTP manager stats */
static gboolean
print_stats (GstElement * rtpbin)
{
  GObject *session;
  GstStructure *stats;
  gchar *str;

  /* get session 0 */
  g_signal_emit_by_name (rtpbin, "get-internal-session", 0, &session);

  /* get the source stats */
  g_object_get (session, "stats", &stats, NULL);

  /* simply dump the stats structure */
  str = gst_structure_to_string (stats);
  g_print ("session stats: %s\n", str);

  gst_structure_free (stats);
  g_free (str);

  g_object_unref (session);

  return TRUE;
}

static gboolean
stop (gpointer data)
{
  App *app;

  app = data;
  g_print ("Stopping\n");
  g_main_loop_quit (app->loop);

  return FALSE;
}

static gboolean
parse_cmdline (int *argc, char **argv, struct options_st * options)
{
  GOptionEntry entries[] = {
    {"sender-host", 0, 0, G_OPTION_ARG_STRING, &options->sender_host,
        "IP address of the sender host (used to send feedback RTCP to)",
          "127.0.0.1"},

    {"rtp-port", 0, 0, G_OPTION_ARG_INT, &options->video_rtp_port,
        "UDP port for RTP video.  Should be even (RTCP will take the next following it)",
          "9997"},

    {"video-width", 0, 0, G_OPTION_ARG_INT, &options->width_video,
        "width of the video to send", "320"},

    {"video-height", 0, 0, G_OPTION_ARG_INT, &options->height_video,
        "height of the video to send", "240"},

    {"no-dma-buf", 0, 0, G_OPTION_ARG_NONE, &options->no_dma_buf,
        "Disable usage of DMABUF", NULL},

    {NULL}
  };

  gboolean ret = TRUE;
  GError *gerror = NULL;
  GOptionContext *context;

  context = g_option_context_new ("- Send video streaming over RTP");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());

  ret = g_option_context_parse (context, argc, &argv, &gerror);
  if (gerror) {
    g_printerr ("Error initialising: %s\n", gerror->message);
    g_error_free (gerror);
  }
  g_option_context_free (context);

  return ret;
}

static gboolean
handle_messages (GstBus * bus, GstMessage * message, gpointer data)
{
  App *app;
  GError *err;
  gchar *debug;

  app = data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error (message, &err, &debug);
      g_printerr ("Error received from element %s: %s\n",
          GST_OBJECT_NAME (message->src), err->message);
      g_printerr ("Debugging information: %s\n", debug ? debug : "none");
      g_clear_error (&err);
      g_free (debug);
      g_main_loop_quit (app->loop);
      app->halt = TRUE;
      break;
    case GST_MESSAGE_EOS:
      g_print ("EOS\n");
      g_main_loop_quit (app->loop);
      break;
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state, pending_state;

      gst_message_parse_state_changed (message, &old_state, &new_state,
           &pending_state);
      if (GST_MESSAGE_SRC (message) == GST_OBJECT (app->pipeline)) {
        g_print ("%s\n", gst_element_state_get_name (new_state));
        if (new_state == GST_STATE_PLAYING) {
          g_timeout_add_seconds (4, stop, data);
        }
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}

/* build a pipeline equivalent to:
 *
 * gst-launch-1.0 -v rtpbin \
 *    v4l2src io-mode=dmabuf ! video/x-raw, width=320, height=240, format=I420 ! \
 *               v4l2video2h264enc output-io-mode=dmabuf-import ! \
 *               video/x-h264, stream-format=byte-stream, aligment=au, profile=constrained-baseline ! \
 *               h264parse ! rtph264pay ! rtpbin.send_rtp_sink_0               \
 *           rtpbin.send_rtp_src_0 ! udpsink port=9997 host=127.0.0.1          \
 *           rtpbin.send_rtcp_src_0 ! udpsink port=9998 host=127.0.0.1 sync=false async=false \
 *        udpsrc port=10002 ! rtpbin.recv_rtcp_sink_0
 */
static GstElement *
create_pipeline (struct options_st * options)
{
  GstElement *pipeline, *rtpbin, *source, *srccapsfilter, *payloader, *encoder,
    *capsfilter, *parser, *rtpsink, *rtcpsink, *rtcpsrc;
  GstCaps *caps;
  GstPad *sinkpad, *srcpad;

  pipeline = gst_pipeline_new (NULL);

  /* video source */
  source = gst_element_factory_make ("v4l2src", NULL);
  if (!options->no_dma_buf)
    g_object_set (source, "io-mode", 4, NULL);

  srccapsfilter = gst_element_factory_make ("capsfilter", NULL);
  caps = gst_caps_new_simple ("video/x-raw",
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "format", G_TYPE_STRING, "I420",
      "width", G_TYPE_INT, options->width_video,
      "height", G_TYPE_INT, options->height_video,
      NULL);
  g_object_set (srccapsfilter, "caps", caps, NULL);

  encoder = gst_element_factory_make ("v4l2video2h264enc", NULL);
  if (!options->no_dma_buf)
    g_object_set (encoder, "output-io-mode", 5, NULL);

  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  caps = gst_caps_new_simple ("video/x-h264",
      "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au",
      "profile", G_TYPE_STRING, "constrained-baseline",
      NULL);
  g_object_set (capsfilter, "caps", caps, NULL);

  parser = gst_element_factory_make ("h264parse", NULL);
  payloader = gst_element_factory_make ("rtph264pay", NULL);

  gst_bin_add_many (GST_BIN (pipeline), source, srccapsfilter, encoder,
      capsfilter, parser, payloader, NULL);

  if (!gst_element_link_many (source, srccapsfilter, encoder, capsfilter,
           parser, payloader, NULL)) {
    g_error ("Failed to link source, srccapsfilter, encoder, capsfilter, "
        "parser and payloader");
  }

  /* RTP */
  rtpbin = gst_element_factory_make ("rtpbin", NULL);
  gst_bin_add (GST_BIN (pipeline), rtpbin);

  rtpsink = gst_element_factory_make ("udpsink", NULL);
  g_object_set (rtpsink, "port", options->video_rtp_port,
      "host", options->sender_host, NULL);

  rtcpsink = gst_element_factory_make ("udpsink", NULL);
  g_object_set (rtpsink, "port", options->video_rtp_port + 1,
      "host", options->sender_host, "async", FALSE, "sync", FALSE, NULL);

  rtcpsrc = gst_element_factory_make ("udpsrc", NULL);
  g_object_set (rtcpsrc, "port", options->video_rtp_port + 5, NULL);

  gst_bin_add_many (GST_BIN (pipeline), rtpsink, rtcpsink, rtcpsrc, NULL);

  /* now link all to the rtpbin, start by getting an RTP sinkpad for
   * session 0 */
  sinkpad = gst_element_get_request_pad (rtpbin, "send_rtp_sink_0");
  srcpad = gst_element_get_static_pad (payloader, "src");
  if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link payloader to rtpbin");
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* get the RTP srcpad that was created when we requested the sinkpad
   * above and link it to the rtpsink sinkpad*/
  srcpad = gst_element_get_static_pad (rtpbin, "send_rtp_src_0");
  sinkpad = gst_element_get_static_pad (rtpsink, "sink");
  if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link rtpbin to rtpsink");
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* get an RTCP srcpad for sending RTCP to the receiver */
  srcpad = gst_element_get_request_pad (rtpbin, "send_rtcp_src_0");
  sinkpad = gst_element_get_static_pad (rtcpsink, "sink");
  if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link rtpbin to rtcpsink");
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* we also want to receive RTCP, request an RTCP sinkpad for session 0 and
   * link it to the srcpad of the udpsrc for RTCP */
  srcpad = gst_element_get_static_pad (rtcpsrc, "src");
  sinkpad = gst_element_get_request_pad (rtpbin, "recv_rtcp_sink_0");
  if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link rtcpsrc to rtpbin");
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* print stats every second */
  stats_id = g_timeout_add_seconds (1, (GSourceFunc) print_stats, rtpbin);

  return pipeline;
}

int
main (int argc, char *argv[])
{
  GstBus *bus;
  App app;
  int cnt;

  g_set_prgname ("sender");
  if (!parse_cmdline (&argc, argv, &options))
    return 1;

  if (argc != 1) {
    g_printerr ("Extra unknown arguments present.  See --help\n");
    exit (-1);
  }

  app.halt = FALSE;
  for (cnt = 0; cnt < 10; cnt++) {
    g_print ("Build pipeline (iteration %d)\n", cnt);

    /* we need to run a GLib main loop to get the messages */
    app.loop = g_main_loop_new (NULL, FALSE);

    /* build pipeline */
    app.pipeline = create_pipeline (&options);
    bus = gst_element_get_bus (app.pipeline);
    gst_bus_add_watch (bus, handle_messages, &app);
    gst_object_unref (bus);

    gst_element_set_state (app.pipeline, GST_STATE_PLAYING);

    g_main_loop_run (app.loop);

    gst_element_set_state (app.pipeline, GST_STATE_NULL);

    if (stats_id != 0)
      g_source_remove (stats_id);
    gst_object_unref (app.pipeline);
    g_main_loop_unref (app.loop);

    if (app.halt)
      break;
  }

  return 0;
}
