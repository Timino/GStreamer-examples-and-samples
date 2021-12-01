#include "pch.h" //! Needs if you are Microsoft Visual Studio IDE user
#include <gst/gst.h>

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData
{
	GstElement *pipeline;
	GstElement *source;
	GstElement *videoConvert;
	GstElement *audioConvert;
	GstElement *videoSink;
	GstElement *audioSink;
	gboolean playing;      /* Are we in the PLAYING state? */
	gboolean terminate;    /* Should we terminate execution? */
	gint64 duration;       /* How long does this media last, in nanoseconds */
} CustomData;

/* Handler for the pad-added signal */
static void pad_added_handler(GstElement *src, GstPad *pad, CustomData *data);

/* Forward definition of the message processing function */
static void handle_message(CustomData *data, GstMessage *msg);

int main(int argc, char *argv[])
{
	CustomData data;
	GstBus *bus;
	GstMessage *msg;
	GstStateChangeReturn ret;
	gboolean terminate = FALSE;

	data.playing = FALSE;
	data.terminate = FALSE;
	data.duration = GST_CLOCK_TIME_NONE;

	/* Initialize GStreamer */
	gst_init(&argc, &argv);

	/* Create the elements */
	data.source = gst_element_factory_make("uridecodebin", "source");
	data.audioConvert = gst_element_factory_make("audioconvert", "audioConvert");
	data.audioSink = gst_element_factory_make("autoaudiosink", "audioSink");
	data.videoConvert = gst_element_factory_make("videoconvert", "videoConvert");
	data.videoSink = gst_element_factory_make("autovideosink", "videoSink");

	/* Create the empty pipeline */
	data.pipeline = gst_pipeline_new("test-pipeline");

	if (!data.pipeline || !data.source || !data.audioConvert || !data.audioSink || !data.videoConvert || !data.videoSink)
	{
		g_printerr("Not all elements could be created.\n");
		return -1;
	}

	/* Build the pipeline. Note that we are NOT linking the source at this point. We will do it later. */
	gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.audioConvert, data.audioSink,
		data.videoConvert, data.videoSink, NULL);

	if (!gst_element_link(data.audioConvert, data.audioSink)) //! conveyor sleeve AUDIO
	{
		g_printerr("Elements could not be linked.\n");
		gst_object_unref(data.pipeline);
		return -1;
	}

	if (!gst_element_link(data.videoConvert, data.videoSink)) //! conveyor sleeve VIDEO
	{
		g_printerr("Elements could not be linked.\n");
		gst_object_unref(data.pipeline);
		return -1;
	}

	/* Set the URI to play */
	g_object_set(data.source, "uri", "file:///D:/workspace/ocean.avi", NULL);

	/* Connect to the pad-added signal */
	g_signal_connect(data.source, "pad-added", G_CALLBACK(pad_added_handler), &data);	//! Here, we make connection. When first stream data will reach out pipeline, it will pe parsed, and dinamically linked to
																						//! propper pad in the "pad_added_handler" @function

	/* Start playing */
	ret = gst_element_set_state(data.pipeline, GST_STATE_PLAYING);

	if (ret == GST_STATE_CHANGE_FAILURE)
	{
		g_printerr("Unable to set the pipeline to the playing state.\n");
		gst_object_unref(data.pipeline);
		return -1;
	}

	/* Listen to the bus */
	bus = gst_element_get_bus(data.pipeline);

	do
	{
		msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
			(GstMessageType)(GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

		/* Parse message */
		if (msg != NULL)
		{
			handle_message(&data, msg);
		}
		else
		{
			/* We got no message, this means the timeout expired */
			if (data.playing)
			{
				gint64 current = -1;

				/* Query the current position of the stream */
				if (!gst_element_query_position(data.pipeline, GST_FORMAT_TIME, &current))
				{
					g_printerr("Could not query current position.\n");
				}

				/* If we didn't know it yet, query the stream duration */
				if (!GST_CLOCK_TIME_IS_VALID(data.duration))
				{
					if (!gst_element_query_duration(data.pipeline, GST_FORMAT_TIME, &data.duration))
					{
						g_printerr("Could not query current duration.\n");
					}
				}

				/* Print current position and total duration */
				g_print("Position %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "\r",
					GST_TIME_ARGS(current), GST_TIME_ARGS(data.duration));
			}
		}
	} while (!data.terminate);

	/* Free resources */
	gst_object_unref(bus);
	gst_element_set_state(data.pipeline, GST_STATE_NULL);
	gst_object_unref(data.pipeline);

	return 0;
}

/* This function will be called by the pad-added signal */
static void pad_added_handler(GstElement *src, GstPad *new_pad, CustomData *data)
{
	GstPad *sink_pad_audio = gst_element_get_static_pad(data->audioConvert, "sink");
	GstPad *sink_pad_video = gst_element_get_static_pad(data->videoConvert, "sink");
	GstPadLinkReturn ret;
	GstCaps *new_pad_caps = NULL;
	GstStructure *new_pad_struct = NULL;
	const gchar *new_pad_type = NULL;

	g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

	/* If our converter is already linked, we have nothing to do here */
	if (gst_pad_is_linked(sink_pad_audio) && gst_pad_is_linked(sink_pad_video))
	{
		g_print("  We are already linked. Ignoring.\n");
		goto exit;
	}

	/* Check the new pad's type */
	new_pad_caps = gst_pad_query_caps(new_pad, NULL);
	new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
	new_pad_type = gst_structure_get_name(new_pad_struct);

	if (!g_str_has_prefix(new_pad_type, "audio/x-raw") && gst_pad_is_linked(sink_pad_audio))
	{
		goto exit;
	}
	else
	{
		/* Attempt the link */
		ret = gst_pad_link(new_pad, sink_pad_audio);

		if (GST_PAD_LINK_FAILED(ret))
		{
			g_print("  Type is '%s' but link failed.\n", new_pad_type);
		}
		else
		{
			g_print("  Link succeeded (type '%s').\n", new_pad_type);
		}
	}

	if (!g_str_has_prefix(new_pad_type, "video/x-raw") && gst_pad_is_linked(sink_pad_video))
	{
		goto exit;
	}
	else
	{
		/* Attempt the link */
		ret = gst_pad_link(new_pad, sink_pad_video);

		if (GST_PAD_LINK_FAILED(ret))
		{
			g_print("  Type is '%s' but link failed.\n", new_pad_type);
		}
		else
		{
			g_print("  Link succeeded (type '%s').\n", new_pad_type);
		}
	}

exit:
	/* Unreference the new pad's caps, if we got them */
	if (new_pad_caps != NULL)
	{
		gst_caps_unref(new_pad_caps);
	}

	/* Unreference the sink pad */
	gst_object_unref(sink_pad_audio);
	gst_object_unref(sink_pad_video);
}

void handle_message(CustomData * data, GstMessage * msg)
{
	GError *err;
	gchar *debug_info;

	switch (GST_MESSAGE_TYPE(msg))
	{
	case GST_MESSAGE_ERROR:
		gst_message_parse_error(msg, &err, &debug_info);
		g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
		g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
		g_clear_error(&err);
		g_free(debug_info);
		data->terminate = TRUE;
		break;
	case GST_MESSAGE_EOS:
		g_print("End-Of-Stream reached.\n");
		data->terminate = TRUE;
		break;
	case GST_MESSAGE_DURATION:
		/* The duration has changed, mark the current one as invalid */
		data->duration = GST_CLOCK_TIME_NONE;
		break;
	case GST_MESSAGE_STATE_CHANGED:
	{
		GstState old_state, new_state, pending_state;
		gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
		if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data->pipeline))
		{
			g_print("Pipeline state changed from %s to %s:\n",
				gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));

			/* Remember whether we are in the PLAYING state or not */
			data->playing = (new_state == GST_STATE_PLAYING);
		}
	} break;
	default:
		/* We should not reach here */
		g_printerr("Unexpected message received.\n");
		break;
	}
	gst_message_unref(msg);
}