/*
 * test-1.c
 *
 */

#include <gst/gst.h>

struct _app
{
  GstElement *src;
  GstElement *pipeline;
  guint count;
};

static void
handoff_cb (GstElement * object, GstBuffer * buffer, GstPad * pad,
    gpointer data)
{
  struct _app *app = data;

  app->count++;
  if (app->count == 500) {
    g_printerr ("\n");
    if (!gst_element_send_event (app->pipeline, gst_event_new_eos()))
      g_warning ("failed to send EOS event");
  } else {
    g_printerr ("\r%d", app->count);
  }
}

static void
setup (struct _app * app)
{
  GstElement *capsfilter, *encoder, *sink;
  GstCaps *caps;

  app->count = 0;

  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  caps = gst_caps_new_simple ("video/x-raw",
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "format", G_TYPE_STRING, "I420",
      "width", G_TYPE_INT, 1280,
      "height", G_TYPE_INT, 720,
      NULL);
  g_object_set (capsfilter, "caps", caps, NULL);

  encoder = gst_element_factory_make ("v4l2video2h264enc", NULL);
  g_object_set (encoder, "output-io-mode", 5, NULL); /* dmabuf-import */

  sink = gst_element_factory_make ("fakesink", NULL);
  g_object_set (sink, "sync", FALSE, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", (GCallback) handoff_cb, app);

  app->pipeline = gst_pipeline_new (NULL);
  gst_bin_add_many (GST_BIN (app->pipeline), app->src, capsfilter, encoder,
      sink, NULL);

  if (!gst_element_link_many (app->src, capsfilter, encoder, sink, NULL))
    g_error ("Failed to link source ! encoder ! sink");

  gst_element_set_state (app->pipeline, GST_STATE_PLAYING);
}

static void
wait (struct _app * app)
{
  GstBus *bus;
  GstMessage *msg;

  bus = gst_element_get_bus (app->pipeline);
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS);
  gst_message_unref (msg);
  gst_object_unref (bus);
}

static void
teardown (struct _app * app)
{
  gst_element_set_state (app->pipeline, GST_STATE_NULL);
  gst_object_unref (app->pipeline);
}

static gboolean
run (gpointer data)
{
  struct _app *app = data;
  gint i;

  for (i = 0; i < 10; i++) {
    g_print ("test %d\n", i);
    setup (app);
    wait (app);
    teardown (app);
  }

  return FALSE;
}

gint
main (gint argc, gchar **argv)
{
  struct _app app  = { 0, };

  gst_init (&argc, &argv);

  app.src = g_object_ref_sink (gst_element_factory_make ("v4l2src", NULL));
  g_object_set (app.src, "io-mode", 4, NULL); /* dmabuf */

  run (&app);

  return 0;
}
