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
#include <string.h>

#include "rb-shell.h"
#include "rb-media-player-source.h"
#include "rb-media-player-prefs.h"
#include "rb-debug.h"

typedef struct {
	RBMediaPlayerPrefs *prefs;
	
	GKeyFile **key_file;
	
	GMutex *syncing;
	GMutex *waiting;
	
} RBMediaPlayerSourcePrivate;

/* macro to create rb_media_player_source_get_type and set rb_media_player_source_parent_class */
G_DEFINE_TYPE (RBMediaPlayerSource, rb_media_player_source, RB_TYPE_REMOVABLE_MEDIA_SOURCE);

#define MEDIA_PLAYER_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSourcePrivate))

static void rb_media_player_source_class_init (RBMediaPlayerSourceClass *klass);
static GObject *rb_media_player_source_constructor (GType type, 
					    guint n_construct_properties,
					    GObjectConstructParam *construct_properties);
static void rb_media_player_source_init (RBMediaPlayerSource *source);
static void rb_media_player_source_dispose (GObject *object);

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
	klass->impl_get_free_space = NULL;
	klass->impl_add_entries = NULL;
	klass->impl_trash_entries = NULL;
	klass->impl_add_playlist = NULL;
	klass->impl_trash_playlist = NULL;
	klass->impl_get_serial = NULL;
	klass->impl_get_name = NULL;
	klass->impl_show_properties = NULL;
	
	g_object_class_install_property (object_class,
					 PROP_KEY_FILE,
					 g_param_spec_pointer ("key-file",
							       "key-file",
							       "Pointer to the GKeyfile",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	
	g_type_class_add_private (klass, sizeof (RBMediaPlayerSourcePrivate));
}

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
	
	if (priv->syncing != NULL) {
		g_mutex_free (priv->syncing);
		priv->syncing = NULL;
	}
	
	if (priv->waiting != NULL) {
		g_mutex_free (priv->waiting);
		priv->waiting = NULL;
	}
	
	G_OBJECT_CLASS (rb_media_player_source_parent_class)->dispose (object);
}

static void
rb_media_player_source_init (RBMediaPlayerSource *source)
{
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

/* Must be called in the 'instance_init' method of any children */
void
rb_media_player_source_load		(RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	
	priv->prefs = rb_media_player_prefs_new ( priv->key_file,
						  G_OBJECT (source) );
	
	g_assert (priv->syncing == NULL);
	priv->syncing = g_mutex_new ();
	
	g_assert (priv->waiting == NULL);
	priv->waiting = g_mutex_new ();
	
	connect_signal_handlers (G_OBJECT (source));
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

guint64
rb_media_player_source_get_free_space (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_free_space (source);
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


void
rb_media_player_source_add_playlist	(RBMediaPlayerSource *source,
					 gchar *name,
					 GList *entries)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	if (klass->impl_add_playlist != NULL)
		return klass->impl_add_playlist (source, name, entries);
}

void
rb_media_player_source_trash_playlist	(RBMediaPlayerSource *source,
					 gchar *name)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	if (klass->impl_trash_playlist != NULL)
		return klass->impl_trash_playlist (source, name);
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

	/* Something changed in the database, sync needs to rebuild update list */
	rb_media_player_prefs_set_boolean (priv->prefs, SYNC_UPDATED, FALSE);
	
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

static void
impl_sync_playlists (RBMediaPlayerSource *source)
{
	/* FIXME: WTF!? How should this work!!! */
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	
	// Make sure the hash tables are created/updated
	if (!rb_media_player_prefs_get_boolean (priv->prefs, SYNC_UPDATED))
		rb_media_player_prefs_update_sync (priv->prefs);
	
	/*
	GList *iter, *entries, *lib_playlists;
	GHashTable *to_add;
	GHashTableIter hash_iter;
	gpointer key, value;
	RBShell *shell;
	GtkTreeModel *query_model;
	
	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	lib_playlists = rb_playlist_manager_get_playlists ( (RBPlaylistManager *) rb_shell_get_playlist_manager (shell) );
	
	to_add = rb_media_player_prefs_get_hash_table (priv->prefs, SYNC_PLAYLISTS_LIST);
	
	for (iter = playlists_on_device;
	     iter != NULL;
	     iter = iter->next)
	{
		// For each playlist on the device
			// Check if it should be there
			if (g_hash_table_lookup (to_add, iter->data)) {
				// If it should be, remove it from the list to add
				g_hash_table_remove (to_add, iter->data);
			} else {
				// If it shouldn't be there, remove it
				rb_media_player_source_trash_playlist (source, iter->data);
			}
	}
	
	// For each playlist remaining in the list to add
	g_hash_table_iter_init (&hash_iter, to_add);
	while (g_hash_table_iter_next (&hash_iter, &key, &value)) {
		// Get it's entries
		query_model = GTK_TREE_MODEL (rb_playlist_source_get_query_model (list_iter->data));
				
		// Add the entries to the hash_table
		gtk_tree_model_foreach (query_model,
					(GtkTreeModelForeachFunc) rb_media_player_prefs_tree_view_insert,
					&insertion_data);
		
		// Add it
		rb_media_player_source_add_playlist (source, key, entries);
	}
	
	g_object_unref (shell);
	*/
}

static guint
sync_idle_cb_cleanup (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	
	// Set needs Update
	rb_media_player_prefs_set_boolean (priv->prefs, SYNC_UPDATED, FALSE);
	
	// Unlock the mutex
	g_mutex_unlock (priv->syncing);
	
	return FALSE;
}

static guint
sync_idle_cb_playlists (RBMediaPlayerSource *source)
{
	// Transfer the playlists
	impl_sync_playlists (source);
	
	g_idle_add ((GSourceFunc)sync_idle_cb_cleanup,
		    source);
	return FALSE;
}

static guint
sync_idle_cb_add_entries (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	
	// Transfer needed tracks and podcasts from itinerary to device
	rb_media_player_source_add_entries ( source, rb_media_player_prefs_get_list (priv->prefs, SYNC_TO_ADD) );
	
	// Done with this list, clear it.
	rb_media_player_prefs_set_list (priv->prefs, SYNC_TO_ADD, NULL);
	
	g_idle_add ((GSourceFunc)sync_idle_cb_playlists,
		    source);
	return FALSE;
}

static guint
sync_idle_cb_trash_entries (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	
	// Remove tracks and podcasts on device, but not in itinerary
	rb_media_player_source_trash_entries ( source, rb_media_player_prefs_get_list (priv->prefs, SYNC_TO_REMOVE) );
	
	// Done with this list, clear it.
	rb_media_player_prefs_set_list (priv->prefs, SYNC_TO_REMOVE, NULL);
	
	g_idle_add ((GSourceFunc)sync_idle_cb_add_entries,
		    source);
	return FALSE;
}

static guint
sync_idle_cb_update_sync (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	
	if (!rb_media_player_prefs_get_boolean (priv->prefs, SYNC_UPDATED)) {
		rb_media_player_prefs_update_sync (priv->prefs);
	}
	
	g_idle_add ((GSourceFunc)sync_idle_cb_trash_entries,
		    source);
	return FALSE;
}

static guint
sync_idle_cb_check_space (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	
	// Check we have enough space, on the iPod.
	if (rb_media_player_prefs_get_uint64 (priv->prefs, SYNC_SPACE_NEEDED) > rb_media_player_source_get_capacity (source)) {
		//Not enough Space on Device throw up an error
		rb_debug("Not enough Free Space on Device.\n");
		g_mutex_unlock (priv->syncing);
		return FALSE;
	}
	
	g_idle_add ((GSourceFunc)sync_idle_cb_update_sync,
		    source);
	return FALSE;
}

static guint
sync_idle_cb_start (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	
	if (!g_mutex_trylock (priv->syncing)) {
		// If we are already syncing
		if (!g_mutex_trylock (priv->waiting)) {
			// If we have another one waiting
			return FALSE;
		} else {
			// Wait...
			g_mutex_lock (priv->syncing);
			g_mutex_unlock (priv->waiting);
		}
	}
	
	g_idle_add ((GSourceFunc)sync_idle_cb_check_space,
		    source);
	return FALSE;
}

void
rb_media_player_source_sync (RBMediaPlayerSource *source)
{
	g_idle_add ((GSourceFunc)sync_idle_cb_start,
		    source);
}

gchar *
rb_media_player_source_track_uuid  (RhythmDBEntry *entry)
{
	/* This function is for hashing the two databases for syncing. */
	GString *str = g_string_new ("");

	g_string_printf (str, "%s%s%s%s%"G_GUINT64_FORMAT"%lu%lu%lu",
			 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE),
			 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST),
			 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE),
			 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM),
			 rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE),
			 rhythmdb_entry_get_ulong  (entry, RHYTHMDB_PROP_DURATION),
			 rhythmdb_entry_get_ulong  (entry, RHYTHMDB_PROP_TRACK_NUMBER),
			 rhythmdb_entry_get_ulong  (entry, RHYTHMDB_PROP_DISC_NUMBER));
	
	gchar * result = g_compute_checksum_for_string ( G_CHECKSUM_MD5, str->str, str->len );
	
	g_string_free ( str, TRUE );
	
	return result;
}

