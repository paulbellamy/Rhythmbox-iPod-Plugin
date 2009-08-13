/*
 *  arch-tag: Source file for RBMediaPlayerPrefs
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

//#include <string.h>
#include <glib.h>


#include "rb-media-player-prefs.h"
#include "rb-media-player-source.h"
#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-playlist-source.h"
#include "rb-playlist-manager.h"
#include "rb-podcast-manager.h"

typedef struct {
	GKeyFile *key_file;
	gchar *group;
	GMutex *updating;
	GMutex *waiting;
	
	/* Pointers to stuff for setting up the sync */
	RBShell *shell;
	RhythmDB *db;
	RBMediaPlayerSource *source;
	
	/* lazy-loading style for building sync lists. */
	gboolean sync_updated;
	
	
	/* loaded/saved to file */
	gboolean sync_auto;
	gboolean sync_music;
	gboolean sync_music_all;
	gboolean sync_podcasts;
	gboolean sync_podcasts_all;
	GHashTable * sync_playlists_list;
	GHashTable * sync_podcasts_list;
	
	/* generated on rb_media_player_prefs_sync_update */
	GHashTable * itinerary_hash;
	GHashTable * device_hash;
	GList *  sync_to_add;
	GList *  sync_to_remove;
	guint64	 sync_space_needed; // The space used after syncing
	
} RBMediaPlayerPrefsPrivate;

enum {
	PROP_0,
	
	PROP_SHELL,
	PROP_DB,
	PROP_SOURCE
};

G_DEFINE_TYPE (RBMediaPlayerPrefs, rb_media_player_prefs, G_TYPE_OBJECT)

#define MEDIA_PLAYER_PREFS_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MEDIA_PLAYER_PREFS, RBMediaPlayerPrefsPrivate))

static void rb_media_player_prefs_init (RBMediaPlayerPrefs *prefs);
static void rb_media_player_prefs_dispose (GObject *object);
static void rb_media_player_prefs_class_init (RBMediaPlayerPrefsClass *klass);

static gchar ** hash_table_to_string_list (GHashTable * hash_table);

static GHashTable * string_list_to_hash_table (const gchar ** string_list);

static void rb_media_player_prefs_hash_table_compare (gpointer key,
						      gpointer value,
						      gpointer user_data);
static void rb_media_player_prefs_hash_table_insert (gpointer key,
						     gpointer value,
						     gpointer user_data);

static guint64 rb_media_player_prefs_calculate_space_needed (RBMediaPlayerPrefs *prefs);

static void rb_media_player_prefs_set_property (GObject *object,
					 guint prop_id,
					 const GValue *value,
					 GParamSpec *pspec);
static void rb_media_player_prefs_get_property (GObject *object,
					 guint prop_id,
					 GValue *value,
					 GParamSpec *pspec);

static void hash_table_duplicate (GHashTable **hash1, GHashTable *hash2);
static void hash_table_insert_all (RBMediaPlayerPrefs *prefs,
				   GHashTable *hash,
				   RhythmDBEntryType entry_type);

static void hash_table_insert_some_playlists (RBMediaPlayerPrefs *prefs,
					      GHashTable *hash);
static void hash_table_insert_some_podcasts (RBMediaPlayerPrefs *prefs,
					     GHashTable *hash);

static GKeyFile * rb_media_player_prefs_load_file (RBMediaPlayerPrefs *prefs,
						   GError **error);

static void
rb_media_player_prefs_init (RBMediaPlayerPrefs *prefs)
{
}

static void
rb_media_player_prefs_dispose (GObject *object)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (object);
	
	priv->key_file = NULL;
	
	if (priv->group != NULL)
		g_free( priv->group );

	if (priv->sync_playlists_list != NULL)
		g_hash_table_unref ( priv->sync_playlists_list );
	
	if (priv->sync_podcasts_list != NULL)
		g_hash_table_unref ( priv->sync_podcasts_list );
	
	if (priv->sync_to_add != NULL)
		g_list_free( priv->sync_to_add );
	
	if (priv->sync_to_remove != NULL)
		g_list_free( priv->sync_to_remove );
		
	if (priv->itinerary_hash != NULL)
		g_hash_table_unref (priv->itinerary_hash);
		
	if (priv->device_hash != NULL)	
		g_hash_table_unref (priv->device_hash);
	
	if (priv->source != NULL)
		g_object_unref (priv->source);
	
	if (priv->shell != NULL)
		g_object_unref (priv->shell);
		
	if (priv->db != NULL)
		g_object_unref (priv->db);
	
	if (priv->updating != NULL)
		g_mutex_free (priv->updating);
	
	if (priv->waiting != NULL)
		g_mutex_free (priv->waiting);
		
	priv->source = NULL;
	
	G_OBJECT_CLASS (rb_media_player_prefs_parent_class)->dispose (object);
}

static GObject *
rb_media_player_prefs_constructor (GType type, 
				    guint n_construct_properties,
				    GObjectConstructParam *construct_properties)
{
	GObject *prefs;
	
	prefs = G_OBJECT_CLASS(rb_media_player_prefs_parent_class)
				->constructor (type, n_construct_properties, construct_properties);
	
	return G_OBJECT (prefs);
}

static void
rb_media_player_prefs_class_init (RBMediaPlayerPrefsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructor = rb_media_player_prefs_constructor;
	object_class->dispose = rb_media_player_prefs_dispose;
	
	object_class->set_property = rb_media_player_prefs_set_property;
	object_class->get_property = rb_media_player_prefs_get_property;
	
	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_pointer ("shell",
							       "shell",
							       "Pointer to the RBShell",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_pointer ("db",
							       "db",
							       "Pointer to the RhythmDB",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_pointer ("source",
							       "source",
							       "Pointer to the RBMediaPlayerSource",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBMediaPlayerPrefsPrivate));
}

static gchar **
hash_table_to_string_list (GHashTable * hash_table)
{
	gchar ** strv = g_new0 (gchar *, g_hash_table_size (hash_table) + 1 );
	gchar ** strv_iter;
	GHashTableIter hash_iter;
	gpointer key, value;
	
	strv_iter = strv;
	g_hash_table_iter_init (&hash_iter, hash_table);
	while (g_hash_table_iter_next (&hash_iter, &key, &value)) {
		*strv_iter = g_strdup((const gchar *)key);
		strv_iter++;
	}
	
	return strv;
}

static GHashTable *
string_list_to_hash_table (const gchar ** string_list)
{
	const gchar ** iter;
	GHashTable *hash_table = g_hash_table_new (g_str_hash, g_str_equal);
	
	if (string_list != NULL) {
		for (iter = (const gchar **) string_list;
		     *iter != NULL;
		     iter++)
		{
			g_hash_table_insert (hash_table, g_strdup (*iter), g_strdup (*iter));
		}
	}
	
	return hash_table;
}

typedef struct {
	GHashTable *other_hash_table;
	GList **list;
} HashTableComparisonData;

static void
rb_media_player_prefs_hash_table_compare (gpointer key,		// RhythmDBEntry *
				  gpointer value,	// Whatever
				  gpointer user_data)	// HashTableComparisonData *
{
	HashTableComparisonData *data = user_data;
/*	
	gpointer orig_key;
	
	if ( !g_hash_table_lookup_extended (data->other_hash_table, key, &orig_key, &value) ) {
		g_print("Not Found: %15s - %15s - %15s\n",
			rhythmdb_entry_get_string (orig_key, RHYTHMDB_PROP_TITLE),
			rhythmdb_entry_get_string (orig_key, RHYTHMDB_PROP_ARTIST),
			rhythmdb_entry_get_string (orig_key, RHYTHMDB_PROP_ALBUM));
		*(data->list) = g_list_append ( *(data->list), key );
	} else {
		g_print("Found: %15s - %15s - %15s\n",
			rhythmdb_entry_get_string (orig_key, RHYTHMDB_PROP_TITLE),
			rhythmdb_entry_get_string (orig_key, RHYTHMDB_PROP_ARTIST),
			rhythmdb_entry_get_string (orig_key, RHYTHMDB_PROP_ALBUM));
	}
*/	
	if ( !g_hash_table_lookup (data->other_hash_table, key) ) {
		*(data->list) = g_list_append ( *(data->list), key );
	}

}

typedef struct {
	RBMediaPlayerPrefs *prefs;
	GHashTable **hash_table;
	GtkTreeModel *query_model;
	RhythmDBEntryType *entry_type;
	const gchar *iter;
} HashTableInsertionData;

static void
rb_media_player_prefs_hash_table_insert ( gpointer key,		// gchar * track_uuid
					  gpointer value,	// RhythmDBEntry *entry
					  gpointer user_data )	// HashTableInsertionData *
{
	g_assert (key != NULL);
	g_assert (value != NULL);
	g_assert (user_data != NULL);
	
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (((HashTableInsertionData *)user_data)->prefs);
	HashTableInsertionData *data = user_data;
	GHashTable *hash_table = *(data->hash_table);
	
	if ( g_hash_table_lookup ( hash_table, key ) ) {
		rb_debug ("Hash table Collision: uuid = %s", (char *)key);
		return;
	}
	
	RhythmDBEntryType entry_type = rhythmdb_entry_get_entry_type(value);
	if (entry_type == RHYTHMDB_ENTRY_TYPE_SONG && !priv->sync_music)
		return;
	if (entry_type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST
	    || entry_type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED)
	{
		if (!priv->sync_podcasts)
			return;
		if (!rb_podcast_manager_entry_downloaded (value))
			return;
	}
		
	//g_print("entry_type->name: %s\n", entry_type->name); // DEBUGGING
		
	g_hash_table_insert ( hash_table,
			      key,
			      value );
}

static gboolean
rb_media_player_prefs_tree_view_insert (GtkTreeModel *query_model,
					GtkTreePath  *path,
					GtkTreeIter  *iter,
					HashTableInsertionData *insertion_data)
{
	RhythmDBEntry *entry;
	
	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (query_model), iter);
	
	// DEBUGGING
	//g_print ("Inserting: %15s - %15s - %15s\n",
	//	 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE),
	//	 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST),
	//	 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM));
	
	rb_media_player_prefs_hash_table_insert (rb_media_player_source_track_uuid (entry), entry, insertion_data);
	
	return FALSE;
}

static guint64
rb_media_player_prefs_calculate_space_needed (RBMediaPlayerPrefs *prefs)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	GList * list_iter;
	priv->sync_space_needed = rb_media_player_source_get_capacity( priv->source )
					 - rb_media_player_source_get_free_space (priv->source);
	
	for (list_iter = priv->sync_to_add; list_iter; list_iter = list_iter->next) {		
		priv->sync_space_needed += rhythmdb_entry_get_uint64 ( list_iter->data,
								       RHYTHMDB_PROP_FILE_SIZE );
	}
	
	for (list_iter = priv->sync_to_remove; list_iter; list_iter = list_iter->next) {		
		priv->sync_space_needed -= rhythmdb_entry_get_uint64 ( list_iter->data,
								       RHYTHMDB_PROP_FILE_SIZE );
	}
	
	// DEBUGGING
	//g_print("Space Needed: %s\n", g_format_size_for_display (priv->sync_space_needed));
	
	return priv->sync_space_needed;
}

static void
rb_media_player_prefs_set_property (GObject *object,
			     guint prop_id,
			     const GValue *value,
			     GParamSpec *pspec)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_SHELL:
		priv->shell = g_value_get_pointer (value);
		break;
	case PROP_DB:
		priv->db = g_value_get_pointer (value);
		break;
	case PROP_SOURCE:
		priv->source = g_value_get_pointer (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_media_player_prefs_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_SHELL:
		g_value_set_pointer (value, priv->shell);
		break;
	case PROP_DB:
		g_value_set_pointer (value, priv->db);
		break;
	case PROP_SOURCE:
		g_value_set_pointer (value, priv->source);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* Duplicates hash2 into hash1 */
static void
hash_table_duplicate (GHashTable **hash1, GHashTable *hash2)
{
	GHashTableIter iter;
	gpointer key, value;
	
	g_hash_table_iter_init (&iter, hash2);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		// DEBUGGING
		//g_print ("Duplicating: %15s - %15s - %15s\n",
		//	 rhythmdb_entry_get_string (key, RHYTHMDB_PROP_TITLE),
		//	 rhythmdb_entry_get_string (key, RHYTHMDB_PROP_ARTIST),
		//	 rhythmdb_entry_get_string (key, RHYTHMDB_PROP_ALBUM));
		
		g_hash_table_insert(*hash1, key, value);
	}
}

static guint
hash_table_insert_all_idle_cb (HashTableInsertionData *data)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (data->prefs);
	
	RhythmDB *db = priv->db;
	
	// Do Query
	data->query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty(db));
	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (data->query_model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, *(data->entry_type),
				RHYTHMDB_QUERY_END);
	
	gtk_tree_model_foreach (data->query_model,
				(GtkTreeModelForeachFunc) rb_media_player_prefs_tree_view_insert,
				data);
	
	g_free(data);
	
	// Done
	return FALSE;
}

static void
hash_table_insert_all (RBMediaPlayerPrefs *prefs,
		       GHashTable *hash,
		       RhythmDBEntryType entry_type)
{
	HashTableInsertionData *data = g_new0(HashTableInsertionData, 1);
	data->prefs = prefs;
	data->hash_table = &hash;
	data->query_model = NULL;
	data->entry_type = &entry_type;
	
	g_idle_add ((GSourceFunc)hash_table_insert_all_idle_cb,
		    data);
}

static void
hash_table_insert_some_playlists (RBMediaPlayerPrefs *prefs,
				  GHashTable *hash)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	const gchar **iter;
	gchar *name;
	GList *list_iter, *items;
	GtkTreeModel *query_model;
	HashTableInsertionData insertion_data = { prefs, &hash };


	items = rb_playlist_manager_get_playlists ( (RBPlaylistManager *) rb_shell_get_playlist_manager (priv->shell) );	
	for ( iter = (const gchar **) rb_media_player_prefs_get_string_list ( prefs, SYNC_PLAYLISTS_LIST );
	      *iter != NULL;
	      iter++ )
	{
		// get item with ( g_strcmp0(name, iter) == 0 )
		for (list_iter = items;
		     list_iter;
		     list_iter = list_iter->next )
		{
			// This gripes about something being uninstantiable
			g_object_get (G_OBJECT (list_iter->data), "name", &name, NULL);
			
			if ( g_strcmp0 ( name, *iter ) == 0 ) {
				// DEBUGGING
				//g_print("Found Playlist: %s\n", name);
				
				query_model = GTK_TREE_MODEL (rb_playlist_source_get_query_model (list_iter->data));
				
				// Add the entries to the hash_table
				gtk_tree_model_foreach (query_model,
							(GtkTreeModelForeachFunc) rb_media_player_prefs_tree_view_insert,
							&insertion_data);
				break;
			}
		}
	}
	g_list_free (items);
}

static guint
hash_table_insert_some_podcasts_idle_cb (HashTableInsertionData *data)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (data->prefs);
	
	data->query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty(priv->db));
	rhythmdb_do_full_query (priv->db, RHYTHMDB_QUERY_RESULTS (data->query_model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_ALBUM, data->iter,
				RHYTHMDB_QUERY_END);
	
	gtk_tree_model_foreach (data->query_model,
				(GtkTreeModelForeachFunc) rb_media_player_prefs_tree_view_insert,
				&data);
	
	g_free (data);
	
	return FALSE;
}

static void
hash_table_insert_some_podcasts (RBMediaPlayerPrefs *prefs,
				 GHashTable *hash)
{
	const gchar **iter;
	
	for (iter = (const gchar **) rb_media_player_prefs_get_string_list (prefs, SYNC_PODCASTS_LIST);
	     *iter != NULL;
	     iter++)
	{
		HashTableInsertionData *insertion_data = g_new0 (HashTableInsertionData, 1);
		insertion_data->prefs = prefs;
		insertion_data->hash_table = &hash;
		insertion_data->query_model = NULL;
		insertion_data->entry_type = NULL;
		insertion_data->iter = *iter;
		
		g_idle_add ((GSourceFunc)hash_table_insert_some_podcasts_idle_cb,
			    insertion_data);
	}
}

static gboolean
rb_media_player_prefs_update_sync_helper ( RBMediaPlayerPrefs *prefs )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	if (priv->itinerary_hash != NULL) {
		g_hash_table_unref (priv->itinerary_hash);
		priv->itinerary_hash = NULL;
	}
	
	if (priv->device_hash != NULL) {
		g_hash_table_unref (priv->device_hash);
		priv->device_hash = NULL;
	}
	
	priv->itinerary_hash = g_hash_table_new (g_str_hash, g_str_equal);
	priv->device_hash = g_hash_table_new (g_str_hash, g_str_equal);
	GList	*to_add = NULL; // Files to go onto the device
	GList	*to_remove = NULL; // Files to be removed from the device
	HashTableComparisonData comparison_data;
	
	/* Build itinerary_hash */
	if (priv->sync_music) {
		if (priv->sync_music_all) {
			// Syncing all songs
			hash_table_insert_all (prefs,
					       priv->itinerary_hash,
					       RHYTHMDB_ENTRY_TYPE_SONG);
		} else {
			// Only syncing some songs
			hash_table_insert_some_playlists (prefs,
							  priv->itinerary_hash);
		}
	}
	
	if (priv->sync_podcasts) {
		if (priv->sync_podcasts_all) {
			// Syncing all podcasts
			hash_table_insert_all (prefs,
					       priv->itinerary_hash,
					       RHYTHMDB_ENTRY_TYPE_PODCAST_POST);
		} else {
			// Only syncing some podcasts
			hash_table_insert_some_podcasts (prefs,
							 priv->itinerary_hash);
		}
	}
	
	/* Build device_hash */
	if (priv->sync_music) {
		hash_table_duplicate (&priv->device_hash, rb_media_player_source_get_entries (priv->source));
	}
	
	if (priv->sync_podcasts) {
		hash_table_duplicate (&priv->device_hash, rb_media_player_source_get_podcasts (priv->source));
	}
	
	/* Build Addition List */
	comparison_data.other_hash_table = priv->device_hash;
	comparison_data.list = &to_add;
	g_hash_table_foreach (priv->itinerary_hash,
			      rb_media_player_prefs_hash_table_compare, // function to add to add list if necessary
			      &comparison_data );
	rb_media_player_prefs_set_list(prefs, SYNC_TO_ADD, to_add);
	
	/* Build Removal List */
	comparison_data.other_hash_table = priv->itinerary_hash;
	comparison_data.list = &to_remove;
	g_hash_table_foreach (priv->device_hash,
			      rb_media_player_prefs_hash_table_compare, // function to add to remove list if necessary
			      &comparison_data );
	rb_media_player_prefs_set_list(prefs, SYNC_TO_REMOVE, to_remove);
	
	/* Calculate how much space we need */
	rb_media_player_prefs_calculate_space_needed (prefs);
	
	return TRUE;
}

void
rb_media_player_prefs_update_sync ( RBMediaPlayerPrefs *prefs )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	if (!g_mutex_trylock (priv->updating)) {
		// If we are already updating
		if (!g_mutex_trylock (priv->waiting)) {
			// If we have another one waiting...
			g_mutex_lock (priv->updating);
			g_mutex_unlock (priv->updating);
			return;
		} else {
			// Wait...
			g_mutex_lock (priv->updating);
			g_mutex_unlock (priv->waiting);
		}
	}
	
	rb_media_player_prefs_update_sync_helper (prefs);
	
	/* mark as updated */
	priv->sync_updated = TRUE;
	
	g_mutex_unlock (priv->updating);
}

gboolean
rb_media_player_prefs_save_file (RBMediaPlayerPrefs *prefs, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	gsize length;
	gchar *data = NULL;
	
	/* Save the Keyfile */
	if ( priv->key_file != NULL) {
		data = g_key_file_to_data (priv->key_file,
					   &length,
					   error);
		if (error != NULL) {
			rb_debug ("unable to save Media Player properties: %s", (*error)->message);
			return FALSE;
		}
		
		g_file_set_contents (rb_find_user_data_file ("media-player-prefs.conf", NULL),
				     data,
				     length,
				     error);
		
		g_free (data);
		
		if (error != NULL) {
			rb_debug ("unable to save Media Player properties: %s", (*error)->message);
			return FALSE;
		}
		
	}
	
	return TRUE;
}

static GKeyFile *
rb_media_player_prefs_load_file (RBMediaPlayerPrefs *prefs, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gchar *pathname = rb_find_user_data_file ("media-player-prefs.conf", NULL);
	GKeyFile *key_file = g_key_file_new();
	GKeyFileFlags flags = G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS;
	
	rb_debug ("loading device properties from \"%s\"", pathname);
	if ( !g_key_file_load_from_file (key_file, pathname, flags, error) ) {
		rb_debug ("unable to load device properties: %s", (*error)->message);
	}
	
	g_free(pathname);
	return key_file;
}

RBMediaPlayerPrefs *
rb_media_player_prefs_new (GKeyFile **key_file, GObject *source)
{
	g_return_val_if_fail (key_file != NULL, NULL);
	g_return_val_if_fail (source != NULL, NULL);
	
	RBMediaPlayerPrefs *prefs;
	RBShell *shell;
	RhythmDB *db;
	RBMediaPlayerSource *mp_source = RB_MEDIA_PLAYER_SOURCE (source);
	g_object_get (mp_source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	
	prefs = RB_MEDIA_PLAYER_PREFS (g_object_new (RB_TYPE_MEDIA_PLAYER_PREFS,
						     "source", mp_source,
						     "shell", shell,
						     "db", db,
						     NULL));
	
	g_return_val_if_fail (prefs != NULL, NULL);
	
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	GError *error = NULL;
	
	// Load the key_file if it isn't already
	if (*key_file == NULL)
		*key_file = rb_media_player_prefs_load_file(prefs, &error);
	
	priv->key_file = *key_file;
	
	if (priv->key_file == NULL) {
		g_object_unref (G_OBJECT (prefs));
		prefs = NULL;
		return NULL;
	}
	
	priv->group = rb_media_player_source_get_serial ( RB_MEDIA_PLAYER_SOURCE (priv->source) );
	if (priv->group == NULL) {
		// Couldn't get the serial, use the ipod name
		priv->group = rb_media_player_source_get_name ( RB_MEDIA_PLAYER_SOURCE (priv->source) );
	}
	
	// add the group and keys, unless it exists
	if ( !g_key_file_has_group(priv->key_file, priv->group) ) {
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_auto", FALSE);
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_music", FALSE);
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_music_all", FALSE);
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_podcasts", FALSE);
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_podcasts_all", FALSE);
		g_key_file_set_string_list (priv->key_file, priv->group, "sync_playlists_list", (const gchar * const *) "", 0);
		g_key_file_set_string_list (priv->key_file, priv->group, "sync_podcasts_list", (const gchar * const *) "", 0);
	}
	
	// Load initial values from the file
	priv->sync_auto = g_key_file_get_boolean (priv->key_file, priv->group, "sync_auto", NULL);
	priv->sync_music = g_key_file_get_boolean (priv->key_file, priv->group, "sync_music", NULL);
	priv->sync_music_all = g_key_file_get_boolean (priv->key_file, priv->group, "sync_music_all", NULL);
	priv->sync_podcasts = g_key_file_get_boolean (priv->key_file, priv->group, "sync_podcasts", NULL);
	priv->sync_podcasts_all = g_key_file_get_boolean (priv->key_file, priv->group, "sync_podcasts_all", NULL);
	
	priv->sync_playlists_list = string_list_to_hash_table ( (const gchar **) g_key_file_get_string_list (priv->key_file, priv->group,
											    "sync_playlists_list",
											    NULL,
											    NULL) );
	priv->sync_podcasts_list = string_list_to_hash_table ( (const gchar **) g_key_file_get_string_list (priv->key_file, priv->group,
											   "sync_podcasts_list",
											   NULL,
											   NULL) );
	
	priv->sync_updated = FALSE;
	
	g_assert (priv->updating == NULL);
	priv->updating = g_mutex_new ();
	
	g_assert (priv->waiting == NULL);
	priv->waiting = g_mutex_new ();
	
     	return prefs;
}

gboolean
rb_media_player_prefs_get_boolean ( RBMediaPlayerPrefs *prefs,
			    guint pref_id )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (pref_id) {
		case SYNC_AUTO:		return priv->sync_auto;
		case SYNC_MUSIC:	return priv->sync_music;
		case SYNC_MUSIC_ALL:	return priv->sync_music_all;
		case SYNC_PODCASTS:	return priv->sync_podcasts;
		case SYNC_PODCASTS_ALL:	return priv->sync_podcasts_all;
		case SYNC_UPDATED:	return priv->sync_updated;
		default:		g_assert_not_reached();
	}
}

gchar **
rb_media_player_prefs_get_string_list ( RBMediaPlayerPrefs *prefs,
				guint prop_id)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (prop_id) {
		case SYNC_PLAYLISTS_LIST:
			return hash_table_to_string_list (priv->sync_playlists_list);
		case SYNC_PODCASTS_LIST:
			return hash_table_to_string_list (priv->sync_podcasts_list);
		default:
			g_assert_not_reached();
			return NULL;
	}
}

GHashTable *
rb_media_player_prefs_get_hash_table (RBMediaPlayerPrefs *prefs,
				      guint prop_id)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (prop_id) {
		case SYNC_PLAYLISTS_LIST:
			return priv->sync_playlists_list;
		case SYNC_PODCASTS_LIST:
			return priv->sync_podcasts_list;
		default:
			g_assert_not_reached();
			return NULL;
	}
}

void
rb_media_player_prefs_set_boolean ( RBMediaPlayerPrefs *prefs,
			    guint pref_id,
			    gboolean value )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (pref_id) {
		case SYNC_AUTO:		priv->sync_auto = value;
					g_key_file_set_boolean (priv->key_file, priv->group, "sync_auto", value);
					break;
		case SYNC_MUSIC:	priv->sync_music = value;
					g_key_file_set_boolean (priv->key_file, priv->group, "sync_music", value);
					break;
		case SYNC_MUSIC_ALL:	priv->sync_music_all = value;
					g_key_file_set_boolean (priv->key_file, priv->group, "sync_music_all", value);
					break;
		case SYNC_PODCASTS:	priv->sync_podcasts = value;
					g_key_file_set_boolean (priv->key_file, priv->group, "sync_podcasts", value);
					break;
		case SYNC_PODCASTS_ALL:	priv->sync_podcasts_all = value;
					g_key_file_set_boolean (priv->key_file, priv->group, "sync_podcasts_all", value);
					break;
		case SYNC_UPDATED:	priv->sync_updated = value;
					return;
		default:		break;
	}
	
	rb_media_player_prefs_save_file (prefs, NULL);
}

void
rb_media_player_prefs_set_list ( RBMediaPlayerPrefs *prefs,
			 guint prop_id,
			 GList * list )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (prop_id) {
		case SYNC_TO_ADD:
			g_list_free(priv->sync_to_add);
			priv->sync_to_add = list;
			break;
		case SYNC_TO_REMOVE:
			g_list_free(priv->sync_to_remove);
			priv->sync_to_remove = list;
			break;
		default:
			g_assert_not_reached();
			return;
	}
}


GList *
rb_media_player_prefs_get_list ( RBMediaPlayerPrefs *prefs,
			 guint prop_id )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (prop_id) {
		case SYNC_TO_ADD:
			return priv->sync_to_add;
		case SYNC_TO_REMOVE:
			return priv->sync_to_remove;
		default:
			g_assert_not_reached();
			return NULL;
	}
}

gchar *
rb_media_player_prefs_get_entry	( RBMediaPlayerPrefs *prefs,
			  guint prop_id,
			  const gchar * entry )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (prop_id) {
		case SYNC_PLAYLISTS_LIST:
			return g_hash_table_lookup (priv->sync_playlists_list, entry);
		case SYNC_PODCASTS_LIST:
			return g_hash_table_lookup (priv->sync_podcasts_list, entry);
		default:
			g_assert_not_reached();
			return NULL;
	}
}

/* returns whether or not an entry should be synced */
gboolean
rb_media_player_prefs_get_entry_value ( RBMediaPlayerPrefs *prefs,
			  guint prop_id,
			  const gchar * entry )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	gboolean value = (gboolean) rb_media_player_prefs_get_entry (prefs, prop_id, entry);
	
	switch (prop_id) {
		case SYNC_PLAYLISTS_LIST:
			value = value && priv->sync_music;
			value = value || priv->sync_music_all;
			return value;
		
		case SYNC_PODCASTS_LIST:
			value = value && priv->sync_podcasts;
			value = value || priv->sync_podcasts_all;
			return value;
		
		default:
			g_assert_not_reached();
			return FALSE;
	}
}

void
rb_media_player_prefs_set_entry ( RBMediaPlayerPrefs *prefs,
			  guint prop_id,
			  const gchar * entry,
			  gboolean value )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	GHashTable *hash_table;
	
	switch (prop_id) {
		case SYNC_PLAYLISTS_LIST:
			hash_table = priv->sync_playlists_list;
			break;
		case SYNC_PODCASTS_LIST:
			hash_table = priv->sync_podcasts_list;
			break;
		default:
			g_assert_not_reached();
			return;
	}
	
	if (value) {
		if (!rb_media_player_prefs_get_entry (prefs, prop_id, entry))
			g_hash_table_insert (hash_table, g_strdup (entry), g_strdup (entry));
	} else {
		g_hash_table_remove (hash_table, entry);
	}
	
	switch (prop_id) {
		case SYNC_PLAYLISTS_LIST:
			g_key_file_set_string_list (priv->key_file, priv->group,
						    "sync_playlists_list",
						    (const gchar * const *) rb_media_player_prefs_get_string_list (prefs, SYNC_PLAYLISTS_LIST),
						    g_hash_table_size (priv->sync_playlists_list) );
			
			break;
		case SYNC_PODCASTS_LIST:	
			g_key_file_set_string_list (priv->key_file, priv->group,
						    "sync_podcasts_list",
						    (const gchar * const *) rb_media_player_prefs_get_string_list (prefs, SYNC_PODCASTS_LIST),
						    g_hash_table_size (priv->sync_podcasts_list) );
			break;
		default:
			g_assert_not_reached();
			return;
	}
	
	rb_media_player_prefs_save_file (prefs, NULL);
}
				  
guint64
rb_media_player_prefs_get_uint64 ( RBMediaPlayerPrefs *prefs,
			   guint prop_id )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (prop_id) {
		case SYNC_SPACE_NEEDED:
			return priv->sync_space_needed;
		default:
			g_assert_not_reached();
			return 0;
	}
}

void
rb_media_player_prefs_set_uint64 ( RBMediaPlayerPrefs *prefs,
			   guint prop_id,
			   gint value )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (prop_id) {
		case SYNC_SPACE_NEEDED:
			priv->sync_space_needed = value;
			break;
		default:
			g_assert_not_reached();
			break;
	}
}

