/*
 *  This file is part of GNOME Twitch - 'Enjoy Twitch on your GNU/Linux desktop'
 *  Copyright © 2017 Vincent Szolnoky <vinszent@vinszent.com>
 *
 *  GNOME Twitch is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GNOME Twitch is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNOME Twitch. If not, see <http://www.gnu.org/licenses/>.
 */

#include "gt-player-backend-gstreamer-opengl.h"
#include "gnome-twitch/gt-player-backend.h"
#include <gst/gst.h>

#define TAG "GtPlayerBackendGstreamerOpenGL"
#include "gnome-twitch/gt-log.h"

typedef struct
{
    GstElement* playbin;
    GstElement* upload;
    GstElement* video_sink;
    GstElement* video_bin;
    GstBus* bus;
    GstPad* pad;
    GstPad* ghost_pad;

    GtkWidget* widget;

    gchar* uri;

    gdouble volume;
    gboolean playing;
    gdouble buffer_fill;
} GtPlayerBackendGstreamerOpenGLPrivate;

static void gt_player_backend_iface_init(GtPlayerBackendInterface* iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED(GtPlayerBackendGstreamerOpenGL, gt_player_backend_gstreamer_opengl, PEAS_TYPE_EXTENSION_BASE, 0,
                               G_IMPLEMENT_INTERFACE_DYNAMIC(GT_TYPE_PLAYER_BACKEND, gt_player_backend_iface_init)
                               G_ADD_PRIVATE_DYNAMIC(GtPlayerBackendGstreamerOpenGL))

enum
{
    PROP_0,
    PROP_VOLUME,
    PROP_PLAYING,
    PROP_URI,
    PROP_BUFFER_FILL,
    NUM_PROPS
};

static GParamSpec* props[NUM_PROPS];

static gboolean
gst_message_cb(GstBus* bus, GstMessage* msg, gpointer udata)
{
    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(udata);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    switch (GST_MESSAGE_TYPE(msg))
    {
        case GST_MESSAGE_BUFFERING:
        {
            gint perc;

            gst_message_parse_buffering(msg, &perc);

            perc < 100 ? gst_element_set_state(priv->playbin, GST_STATE_PAUSED)
                : gst_element_set_state(priv->playbin, GST_STATE_PLAYING);
            g_object_set(self, "buffer-fill", (gdouble) perc/100.0, NULL);
            break;
        }
        default:
            break;
    }

    return G_SOURCE_CONTINUE;
}

static void
play(GtPlayerBackendGstreamerOpenGL* self)
{
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    gst_element_set_state(priv->playbin, GST_STATE_PLAYING);
}

static void
stop(GtPlayerBackendGstreamerOpenGL* self)
{
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    gst_element_set_state(priv->playbin, GST_STATE_NULL);
}

static GtkWidget*
get_widget(GtPlayerBackend* backend)
{
    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(backend);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    return priv->widget;
}

static void
finalise(GObject* obj)
{
    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(obj);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    MESSAGE("Finalise");

    G_OBJECT_CLASS(gt_player_backend_gstreamer_opengl_parent_class)->finalize(obj);

    stop(self);

    gst_object_unref(priv->playbin);
//    gst_object_unref(priv->ghost_pad);
//    gst_object_unref(priv->pad);
//    gst_object_unref(priv->video_bin);
//    gst_object_unref(priv->video_sink);
//    gst_object_unref(priv->upload);

    g_clear_object(&priv->widget);

    g_free(priv->uri);
}

static void
get_property(GObject* obj,
             guint prop,
             GValue* val,
             GParamSpec* pspec)
{
    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(obj);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    switch (prop)
    {
        case PROP_VOLUME:
            g_value_set_double(val, priv->volume);
            break;
        case PROP_PLAYING:
            g_value_set_boolean(val, priv->playing);
            break;
        case PROP_URI:
            g_value_set_string(val, priv->uri);
            break;
        case PROP_BUFFER_FILL:
            g_value_set_double(val, priv->buffer_fill);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop, pspec);
    }
}

static void
set_property(GObject* obj,
             guint prop,
             const GValue* val,
             GParamSpec* pspec)
{
    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(obj);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    switch (prop)
    {
        case PROP_VOLUME:
            priv->volume = g_value_get_double(val);
            break;
        case PROP_PLAYING:
            priv->playing = g_value_get_boolean(val);
            priv->playing ? play(self) : stop(self);
            break;
        case PROP_URI:
            g_free(priv->uri);
            priv->uri = g_value_dup_string(val);
            break;
        case PROP_BUFFER_FILL:
            priv->buffer_fill = g_value_get_double(val);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop, pspec);
    }
}

static void
gt_player_backend_iface_init(GtPlayerBackendInterface* iface)
{
    iface->get_widget = get_widget;
}

static void
gt_player_backend_gstreamer_opengl_class_finalize(GtPlayerBackendGstreamerOpenGLClass* klass)
{
    if (gst_is_initialized())
        gst_deinit();
}

static void
gt_player_backend_gstreamer_opengl_class_init(GtPlayerBackendGstreamerOpenGLClass* klass)
{
    GObjectClass* obj_class = G_OBJECT_CLASS(klass);

    obj_class->finalize = finalise;
    obj_class->get_property = get_property;
    obj_class->set_property = set_property;

    props[PROP_VOLUME] = g_param_spec_double("volume",
                                             "Volume",
                                             "Volume of player",
                                             0.0, 1.0, 0.3,
                                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    props[PROP_PLAYING] = g_param_spec_boolean("playing",
                                               "Playing",
                                               "Whether playing",
                                               FALSE,
                                               G_PARAM_READABLE | G_PARAM_CONSTRUCT);
    props[PROP_URI] = g_param_spec_string("uri",
                                          "Uri",
                                          "Current uri",
                                          "",
                                          G_PARAM_READWRITE);
    props[PROP_BUFFER_FILL] = g_param_spec_double("buffer-fill",
                                                     "Buffer Fill",
                                                     "Current buffer fill",
                                                     0, 1.0, 0,
                                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    g_object_class_override_property(obj_class, PROP_VOLUME, "volume");
    g_object_class_override_property(obj_class, PROP_PLAYING, "playing");
    g_object_class_override_property(obj_class, PROP_URI, "uri");
    g_object_class_override_property(obj_class, PROP_BUFFER_FILL, "buffer-fill");

    if (!gst_is_initialized())
        gst_init(NULL, NULL);
}

static void
gt_player_backend_gstreamer_opengl_init(GtPlayerBackendGstreamerOpenGL* self)
{
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    MESSAGE("{GtPlayerBackendGstreamerOpenGL} Init");

    priv->playbin = gst_element_factory_make("playbin", NULL);
    priv->video_sink = gst_element_factory_make("gtkglsink", NULL);
    priv->video_bin = gst_bin_new("video_bin");
    priv->upload = gst_element_factory_make("glupload", NULL);

    priv->bus = gst_element_get_bus(priv->playbin);

    gst_bus_add_watch(priv->bus, (GstBusFunc) gst_message_cb, self);
    gst_bin_add_many(GST_BIN(priv->video_bin), priv->upload, priv->video_sink, NULL);
    gst_element_link_many(priv->upload, priv->video_sink, NULL);

    priv->pad = gst_element_get_static_pad(priv->upload, "sink");
    priv->ghost_pad = gst_ghost_pad_new("sink", priv->pad);
    gst_pad_set_active(priv->ghost_pad, TRUE);
    gst_element_add_pad(priv->video_bin, priv->ghost_pad);

    g_object_get(priv->video_sink, "widget", &priv->widget, NULL);
    g_object_set(priv->playbin, "video-sink", priv->video_bin, NULL);

    g_object_bind_property(self, "volume",
                           priv->playbin, "volume",
                           G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
    g_object_bind_property(self, "uri",
                           priv->playbin, "uri",
                           G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
}

G_MODULE_EXPORT
void
peas_register_types(PeasObjectModule* module)
{
    gt_player_backend_gstreamer_opengl_register_type(G_TYPE_MODULE(module));

    peas_object_module_register_extension_type(module,
                                               GT_TYPE_PLAYER_BACKEND,
                                               GT_TYPE_PLAYER_BACKEND_GSTREAMER_OPENGL);
}
