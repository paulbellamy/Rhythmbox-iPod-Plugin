/*
 *  arch-tag: Implementation of the Media Player Source object
 *
 *  Copyright (C) 2009 Paul Bellamy  <paul.a.bellamy@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <glib.h>

#include "rb-shell.h"
#include "rb-media-player-source.h"
#include "rb-media-player-prefs.h"
#include "rb-debug.h"

typedef struct {
	RBMediaPlayerPrefs *prefs;
	
	GKeyFile **key_file;
	
} RBMediaPlayerSourcePrivate;

/* macro to create rb_media_player_source_get_type and set rb_media_player_source_parent_class */
G_DEFINE_TYPE (RBMediaPlayerSource, rb_media_player_source, RB_TYPE_REMOVABLE_MEDIA_SOURCE);

#define MEDIA_PLAYER_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSourcePrivate))

static GObject *rb_media_player_source_constructor (GType type, 
					    guint n_construct_properties,
					    GObjectConstructParam *construct_properties);
static void rb_media_player_source_dispose (GObject *object);
static void rb_media_player_source_class_init (RBMediaPlayerSourceClass *klass);
static void rb_media_player_source_init (RBMediaPlayerSource *self);

static void connect_signal_handlers (GObject *source);
static void disconnect_signal_handlers (GObject *source);

static void rb_media_player_source_set_property (GObject *object,
					 guint prop_id,
					 const GValue *value,
					 GParamSpec *pspec);
static void rb_media_player_source_get_property (GObject *object,
					 guint prop_id,
					 GValue *value,
					 GParamSpec *pspec);

static void auto_sync_cb_with_changes (RhythmDB *db,
			   	       RhythmDBEntry *entry,
			   	       GSList *changes,
			   	       RBMediaPlayerSource *source);
static void auto_sync_cb (RhythmDB *db,
			  RhythmDBEntry *entry,
			  RBMediaPlayerSource *source);

enum
{
	PROP_0,
	PROP_KEY_FILE
};

static GObject *
rb_media_player_source_constructor (GType type, 
				    guint n_construct_properties,
				    GObjectConstructParam *construct_properties)
{
	GObject *source;
	
	source = G_OBJECT_CLASS(rb_media_player_source_parent_class)
				->constructor (type, n_construct_properties, construct_properties);
	
	return G_OBJECT (source);
}

static void
rb_media_player_source_dispose (GObject *object)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);
	
	disconnect_signal_handlers (object);
	
	if (priv->prefs) {
		g_object_unref (G_OBJECT (priv->prefs));
		priv->prefs = NULL;
	}
	
	G_OBJECT_CLASS (rb_media_player_source_parent_class)->dispose (object);
}

static void
rb_media_player_source_class_init (RBMediaPlayerSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructor = rb_media_player_source_constructor;
	object_class->dispose = rb_media_player_source_dispose;
	
	object_class->set_property = rb_media_player_source_set_property;
	object_class->get_property = rb_media_player_source_get_property;
	
	/* pure virtual methods: mandates implementation in children. */
	klass->impl_get_entries = NULL;
	klass->impl_get_podcasts = NULL;
	klass->impl_get_capacity = NULL;
	klass->impl_add_entries = NULL;
	klass->impl_trash_entries = NULL;
	klass->impl_get_serial = NULL;
	klass->impl_get_name = NULL;
	klass->impl_show_properties = NULL;
	
	g_object_class_install_property (object_class,
					 PROP_KEY_FILE,
					 g_param_spec_pointer ("key-file",
							       "key-file",
							       "Pointer to the GKeyfile",
							       G_PARAM_READWRITE));
	
	g_type_class_add_private (klass, sizeof (RBMediaPlayerSourcePrivate));
}

static void
rb_media_player_source_init (RBMediaPlayerSource *self)
{
	/* initialize the object */
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (self);
	GKeyFile **key_file;
	g_object_get(self, "key-file", &key_file, NULL);
	priv->prefs = rb_media_player_prefs_new ( key_file,
						  rb_media_player_source_get_serial (RB_MEDIA_PLAYER_SOURCE (self) ) );
	
	connect_signal_handlers (G_OBJECT (self));
}

static void
rb_media_player_source_set_property (GObject *object,
			     guint prop_id,
			     const GValue *value,
			     GParamSpec *pspec)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_KEY_FILE:
		priv->key_file = g_value_get_pointer (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_media_player_source_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_KEY_FILE:
		g_value_set_pointer (value, priv->key_file);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GHashTable *
rb_media_player_source_get_entries (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_entries (source);
}

GHashTable *
rb_media_player_source_get_podcasts (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_podcasts (source);
}

guint64
rb_media_player_source_get_capacity (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_capacity (source);
}

void
rb_media_player_source_add_entries	(RBMediaPlayerSource *source,
					 GList *entries)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_add_entries (source, entries);
}

void
rb_media_player_source_trash_entries	(RBMediaPlayerSource *source,
					 GList *entries)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_trash_entries (source, entries);
}

gchar *
rb_media_player_source_get_serial (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_serial (source);
}

gchar *
rb_media_player_source_get_name (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_name (source);
}

static void
connect_signal_handlers (GObject *source)
{
	RBShell *shell;
	RhythmDB *db;
	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	
	g_signal_connect_object (db,
			  	 "entry-added",
			  	 G_CALLBACK (auto_sync_cb),
			  	 G_OBJECT (source),
			  	 0);
	g_signal_connect_object (db,
			  	 "entry-deleted",
			  	 G_CALLBACK (auto_sync_cb),
			  	 G_OBJECT(source),
			  	 0);
	g_signal_connect_object (db,
			  	 "entry-changed",
			  	 G_CALLBACK (auto_sync_cb_with_changes),
				 G_OBJECT(source),
			  	 0);
	
        g_object_unref (G_OBJECT (db));
	g_object_unref (G_OBJECT (shell));
}

static void
disconnect_signal_handlers (GObject *source)
{
	RBShell *shell;
	RhythmDB *db;
	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	
	g_signal_handlers_disconnect_by_func (db,
					     G_CALLBACK (auto_sync_cb),
					     G_OBJECT(source));
	
	g_object_unref (G_OBJECT (db));
	g_object_unref (G_OBJECT (shell));
}

static void
auto_sync_cb_with_changes (RhythmDB *db,
			   RhythmDBEntry *entry,
			   GSList *changes,
			   RBMediaPlayerSource *source)
{
	auto_sync_cb (db, entry, source);
}

static void 
auto_sync_cb (RhythmDB *db,
	      RhythmDBEntry *entry,
	      RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);

	if (rb_media_player_prefs_get_boolean (priv->prefs, SYNC_AUTO))
		rb_media_player_source_sync (source);
}

void
rb_media_player_source_show_properties (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);
	
	return klass->impl_show_properties (source, priv->prefs);
}


void
rb_media_player_source_sync (RBMediaPlayerSource *source)
{
	/* FIXME: this is a pretty ugly skeleton function.
	 * 
	 */
	 RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	
	// Check we have enough space, on the iPod.
	if (rb_media_player_prefs_get_uint64 (priv->prefs, SYNC_SPACE_NEEDED) > rb_media_player_source_get_capacity (source)) {
		//Not enough Space on Device throw up an error
		rb_debug("Not enough Free Space on Device.\n");
		return;
	}
	
	rb_media_player_prefs_update_sync (priv->prefs);
	
	/*
	// DEBUGGING - Print the lists
	GList *iter;
	g_print("To Add:\n");
	for (iter = rb_media_player_prefs_get_list (priv->prefs, SYNC_TO_ADD);
	     iter;
	     iter = iter->next)
	{
		g_print("%15s - %15s - %15s\n",
			rhythmdb_entry_get_string (iter->data, RHYTHMDB_PROP_TITLE),
			rhythmdb_entry_get_string (iter->data, RHYTHMDB_PROP_ARTIST),
			rhythmdb_entry_get_string (iter->data, RHYTHMDB_PROP_ALBUM));
	}
	
	g_print("To Remove:\n");
	for (iter = rb_media_player_prefs_get_list (priv->prefs, SYNC_TO_REMOVE);
	     iter;
	     iter = iter->next)
	{
		g_print("%15s - %15s - %15s\n",if (priv->prefs) {
		g_object_unref (G_OBJECT (priv->prefs));
		priv->prefs = NULL;
	}
			rhythmdb_entry_get_string (iter->data, RHYTHMDB_PROP_TITLE),
			rhythmdb_entry_get_string (iter->data, RHYTHMDB_PROP_ARTIST),
			rhythmdb_entry_get_string (iter->data, RHYTHMDB_PROP_ALBUM));
	}
	//*/
	//*
	// Remove tracks and podcasts on device, but not in itinerary
	rb_media_player_source_trash_entries ( source, rb_media_player_prefs_get_list (priv->prefs, SYNC_TO_REMOVE) );
	
	// Done with this list, clear it.
	rb_media_player_prefs_set_list(priv->prefs, SYNC_TO_REMOVE, NULL);
	
	// Transfer needed tracks and podcasts from itinerary to device
	rb_media_player_source_add_entries ( source, rb_media_player_prefs_get_list (priv->prefs, SYNC_TO_ADD) );
	
	// Done with this list, clear it.
	rb_media_player_prefs_set_list(priv->prefs, SYNC_TO_ADD, NULL);
	//*/
}


