/*
 *  Copyright (C) 2006 Jonathan Matthew <jonathan@kaolin.hn.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

/*
 * Client for out-of-process metadata reader communicating via D-BUS.
 *
 * How this works:
 * - spawn rb-metadata process, with pipes
 * - child process sets up its dbus server or whatever
 * - if successful, child writes dbus server address to stdout; otherwise, dies.
 * - parent opens dbus connection
 *
 * For each request, the parent checks if the dbus connection is still alive,
 * and pings the child to see if it's still responding.  If the child has
 * exited or is not responding, the parent starts a new metadata helper as
 * described above.  
 *
 * The child process exits after a certain period of inactivity (30s
 * currently), so the ping message serves two purposes - it checks that the
 * child is still capable of handling messages, and it ensures the child
 * doesn't time out between when we check the child is still running and when
 * we actually send it the request.
 */

#include <config.h>

#include "rb-metadata.h"
#include "rb-metadata-dbus.h"
#include "rb-debug.h"
#include "rb-util.h"

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <glib/gi18n.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>

static void rb_metadata_class_init (RBMetaDataClass *klass);
static void rb_metadata_init (RBMetaData *md);
static void rb_metadata_finalize (GObject *object);

static gboolean tried_env_address = FALSE;
static DBusConnection *dbus_connection = NULL;
static GPid metadata_child = 0;
static GMainContext *main_context = NULL;

struct RBMetaDataPrivate
{
	char *uri;
	char *mimetype;
	GHashTable *metadata;
};

G_DEFINE_TYPE (RBMetaData, rb_metadata, G_TYPE_OBJECT)

static void
rb_metadata_class_init (RBMetaDataClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = rb_metadata_finalize;

	g_type_class_add_private (object_class, sizeof (RBMetaDataPrivate));

	main_context = g_main_context_new ();	/* maybe not needed? */
}

static void
rb_metadata_init (RBMetaData *md)
{
	md->priv = G_TYPE_INSTANCE_GET_PRIVATE (md, RB_TYPE_METADATA, RBMetaDataPrivate);
}

static void
rb_metadata_finalize (GObject *object)
{
	RBMetaData *md;

	md = RB_METADATA (object);
	g_free (md->priv->uri);
	g_free (md->priv->mimetype);
	if (md->priv->metadata)
		g_hash_table_destroy (md->priv->metadata);

	G_OBJECT_CLASS (rb_metadata_parent_class)->finalize (object);
}

RBMetaData *
rb_metadata_new (void)
{
	return RB_METADATA (g_object_new (RB_TYPE_METADATA, NULL));
}

static void
kill_metadata_service (void)
{
	if (dbus_connection) {
		if (dbus_connection_get_is_connected (dbus_connection)) {
			rb_debug ("closing dbus connection");
			dbus_connection_disconnect (dbus_connection);
		} else {
			rb_debug ("dbus connection already closed");
		}
		dbus_connection_unref (dbus_connection);
		dbus_connection = NULL;
	}

	if (metadata_child) {
		rb_debug ("killing child process");
		kill (metadata_child, SIGINT);
		g_spawn_close_pid (metadata_child);
		metadata_child = 0;
	}
}

static gboolean
ping_metadata_service (GError **error)
{
	DBusMessage *message, *response;
	DBusError dbus_error = {0,};

	if (!dbus_connection_get_is_connected (dbus_connection))
		return FALSE;

	message = dbus_message_new_method_call (RB_METADATA_DBUS_NAME,
						RB_METADATA_DBUS_OBJECT_PATH,
						RB_METADATA_DBUS_INTERFACE,
						"ping");
	if (!message) {
		return FALSE;
	}
	response = dbus_connection_send_with_reply_and_block (dbus_connection, 
							      message, 
							      RB_METADATA_DBUS_TIMEOUT, 
							      &dbus_error);
	dbus_message_unref (message);
	if (dbus_error_is_set (&dbus_error)) {
		/* ignore 'no reply': just means the service is dead */
		if (strcmp (dbus_error.name, DBUS_ERROR_NO_REPLY)) {
			dbus_set_g_error (error, &dbus_error);
		}
		dbus_error_free (&dbus_error);
		return FALSE;
	}
	dbus_message_unref (response);
	return TRUE;
}

static gboolean
start_metadata_service (GError **error)
{
	/*
	 * Normally, we find the metadata helper in the libexec dir,
	 * but when --enable-uninstalled-build is specified, we look
	 * in the directory it's built in.
	 */
	char *argv[] = { 
#ifdef METADATA_UNINSTALLED_DIR
		METADATA_UNINSTALLED_DIR "/rhythmbox-metadata",
#else
		LIBEXEC_DIR "/rhythmbox-metadata", 
#endif
		"unix:tmpdir=/tmp", NULL 
	};
	DBusError dbus_error = {0,};
	GIOChannel *stdout_channel;
	GIOStatus status;
	gchar *dbus_address = NULL;

	if (dbus_connection) {
		if (ping_metadata_service (error))
			return TRUE;

		/* Metadata service is broken.  Kill it, and if we haven't run
		 * into any errors yet, we can try to restart it.
		 */
		kill_metadata_service ();
		
		if (*error)
			return FALSE;
	}

	if (!tried_env_address) {
		const char *addr = g_getenv ("RB_DBUS_METADATA_ADDRESS");
		tried_env_address = TRUE;
		if (addr) {
			rb_debug ("trying metadata service address %s (from environment)", addr);
			dbus_address = g_strdup (addr);
			metadata_child = 0;
		}
	} 

	if (dbus_address == NULL) {
		gint metadata_stdout;

		if (!g_spawn_async_with_pipes (NULL,
					       argv,
					       NULL,
					       0,
					       NULL, NULL,
					       &metadata_child,
					       NULL,
					       &metadata_stdout,
					       NULL,
					       error)) {
			return FALSE;
		}

		/* hmm, probably shouldn't do this */
		signal (SIGPIPE, SIG_IGN);
		
		stdout_channel = g_io_channel_unix_new (metadata_stdout);
		status = g_io_channel_read_line (stdout_channel, &dbus_address, NULL, NULL, error);
		g_io_channel_unref (stdout_channel);
		if (status != G_IO_STATUS_NORMAL) {
			kill_metadata_service ();
			return FALSE;
		}

		g_strchomp (dbus_address);
		rb_debug ("Got metadata helper D-BUS address %s", dbus_address);
	}

	dbus_connection = dbus_connection_open_private (dbus_address, &dbus_error);
	g_free (dbus_address);
	if (!dbus_connection) {
		kill_metadata_service ();

		dbus_set_g_error (error, &dbus_error);
		dbus_error_free (&dbus_error);
		return FALSE;
	}
	dbus_connection_set_exit_on_disconnect (dbus_connection, FALSE);

	dbus_connection_setup_with_g_main (dbus_connection, g_main_context_default ());
	
	rb_debug ("Metadata process %d started", metadata_child);
	return TRUE;
}

static void
handle_dbus_error (RBMetaData *md, DBusError *dbus_error, GError **error)
{
	/*
	 * If the error is 'no reply within the specified time',
	 * then we assume that either the metadata process died, or
	 * it's stuck in a loop and needs to be killed.
	 */
	if (strcmp (dbus_error->name, DBUS_ERROR_NO_REPLY) == 0) {
		kill_metadata_service ();

		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_INTERNAL,
			     _("Internal GStreamer problem; file a bug"));
	} else {
		dbus_set_g_error (error, dbus_error);
		dbus_error_free (dbus_error);
	}
}

static void
read_error_from_message (RBMetaData *md, DBusMessageIter *iter, GError **error)
{
	guint32 error_code;
	gchar *error_message;

	if (!rb_metadata_dbus_get_uint32 (iter, &error_code) ||
	    !rb_metadata_dbus_get_string (iter, &error_message)) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_INTERNAL,
			     _("D-BUS communication error"));
		return;
	}

	g_set_error (error, RB_METADATA_ERROR,
		     error_code,
		     "%s", error_message);
	g_free (error_message);
}

void
rb_metadata_load (RBMetaData *md,
		  const char *uri,
		  GError **error)
{
	DBusMessage *message;
	DBusMessage *response;
	DBusMessageIter iter;
	DBusError dbus_error = {0,};
	gboolean ok;

	g_free (md->priv->mimetype);
	md->priv->mimetype = NULL;

	g_free (md->priv->uri);
	md->priv->uri = g_strdup (uri);
	if (uri == NULL)
		return;

	if (md->priv->metadata)
		g_hash_table_destroy (md->priv->metadata);
	md->priv->metadata = g_hash_table_new (g_direct_hash, g_direct_equal);

	
	if (!start_metadata_service (error))
		return;

	message = dbus_message_new_method_call (RB_METADATA_DBUS_NAME,
						RB_METADATA_DBUS_OBJECT_PATH,
						RB_METADATA_DBUS_INTERFACE,
						"load");
	if (!message) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_INTERNAL,
			     _("D-BUS communication error"));
		return;
	}
	if (!dbus_message_append_args (message, DBUS_TYPE_STRING, &uri, DBUS_TYPE_INVALID)) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_INTERNAL,
			     _("D-BUS communication error"));
		return;
	}

	rb_debug ("sending metadata load request");
	response = dbus_connection_send_with_reply_and_block (dbus_connection, 
							      message, 
							      RB_METADATA_DBUS_TIMEOUT, 
							      &dbus_error);

	dbus_message_unref (message);
	if (!response) {
		handle_dbus_error (md, &dbus_error, error);
		return;
	}

	if (!dbus_message_iter_init (response, &iter)) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_INTERNAL,
			     _("D-BUS communication error"));
		rb_debug ("couldn't read response message");
		dbus_message_unref (response);
		return;
	}

	if (!rb_metadata_dbus_get_boolean (&iter, &ok)) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_INTERNAL,
			     _("D-BUS communication error"));
		rb_debug ("couldn't get success flag from response message");
		dbus_message_unref (response);
		return;
	}

	if (ok) {
		/* get mime type */
		if (!rb_metadata_dbus_get_string (&iter, &md->priv->mimetype)) {
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_INTERNAL,
				     _("D-BUS communication error"));
		} else {
			/* get metadata */
			rb_debug ("got mimetype: %s", md->priv->mimetype);
			rb_metadata_dbus_read_from_message (md, md->priv->metadata, &iter);
		}
	} else {
		read_error_from_message (md, &iter, error);
	}

	dbus_message_unref (response);
}

const char *
rb_metadata_get_mime (RBMetaData *md)
{
	return md->priv->mimetype;
}

gboolean
rb_metadata_get (RBMetaData *md, RBMetaDataField field,
		 GValue *ret)
{
	GValue *val;
	if (!md->priv->metadata)
		return FALSE;

	if ((val = g_hash_table_lookup (md->priv->metadata,
					GINT_TO_POINTER (field)))) {
		g_value_init (ret, G_VALUE_TYPE (val));
		g_value_copy (val, ret);
		return TRUE;
	}
	return FALSE;
}

gboolean
rb_metadata_set (RBMetaData *md, RBMetaDataField field,
		 const GValue *val)
{
	GValue *newval;
	GType type;
	
	type = rb_metadata_get_field_type (field);
	g_return_val_if_fail (type == G_VALUE_TYPE (val), FALSE);

	newval = g_new0 (GValue, 1);
	g_value_init (newval, type);
	g_value_copy (val, newval);

	g_hash_table_insert (md->priv->metadata, GINT_TO_POINTER (field),
			     newval);
	return TRUE;
}

gboolean
rb_metadata_can_save (RBMetaData *md, const char *mimetype)
{
	GError *error = NULL;
	DBusMessage *message;
	DBusMessage *response;
	gboolean can_save = FALSE;
	DBusError dbus_error = {0,};
	DBusMessageIter iter;

	if (start_metadata_service (&error) == FALSE) {
		g_error_free (error);
		return FALSE;
	}

	message = dbus_message_new_method_call (RB_METADATA_DBUS_NAME,
						RB_METADATA_DBUS_OBJECT_PATH,
						RB_METADATA_DBUS_INTERFACE,
						"canSave");
	if (!message) {
		return FALSE;
	}
	if (!dbus_message_append_args (message, DBUS_TYPE_STRING, &mimetype, DBUS_TYPE_INVALID)) {
		return FALSE;
	}

	response = dbus_connection_send_with_reply_and_block (dbus_connection, 
							      message, 
							      RB_METADATA_DBUS_TIMEOUT, 
							      &dbus_error);
	dbus_message_unref (message);
	if (!response) {
		dbus_error_free (&dbus_error);
		return FALSE;
	}

	if (dbus_message_iter_init (response, &iter)) {
		rb_metadata_dbus_get_boolean (&iter, &can_save);
	}
	dbus_message_unref (response);
	return can_save;
}

void
rb_metadata_save (RBMetaData *md, GError **error)
{
	DBusMessage *message;
	DBusMessage *response;
	DBusError dbus_error = {0,};
	DBusMessageIter iter;

	if (start_metadata_service (error) == FALSE)
		return;

	message = dbus_message_new_method_call (RB_METADATA_DBUS_NAME,
						RB_METADATA_DBUS_OBJECT_PATH,
						RB_METADATA_DBUS_INTERFACE,
						"save");
	if (!message) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_INTERNAL,
			     _("D-BUS communication error"));
		return;
	}

	dbus_message_iter_init_append (message, &iter);
	if (!rb_metadata_dbus_add_to_message (md, &iter)) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_INTERNAL,
			     _("D-BUS communication error"));
		return;
	}

	response = dbus_connection_send_with_reply_and_block (dbus_connection, 
							      message, 
							      RB_METADATA_DBUS_TIMEOUT, 
							      &dbus_error);
	dbus_message_unref (message);
	if (!response) {
		handle_dbus_error (md, &dbus_error, error);
		return;
	}

	/* if there's any return data at all, it'll be an error */
	if (dbus_message_iter_init (response, &iter)) {
		read_error_from_message (md, &iter, error);
	}
}
