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
	
	/* Pointers to stuff for setting up the sync */
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
	guint64	 sync_space_needed; /* The space used after syncing */
	
} RBMediaPlayerPrefsPrivate;

enum {
	PROP_0,
	
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

static gboolean entry_is_undownloaded_podcast (RhythmDBEntry *entry);

static guint64 rb_media_player_prefs_calculate_space_needed (RBMediaPlayerPrefs *prefs);

static void rb_media_player_prefs_set_property (GObject *object,
						guint prop_id,
						const GValue *value,
						GParamSpec *pspec);
static void rb_media_player_prefs_get_property (GObject *object,
						guint prop_id,
						GValue *value,
						GParamSpec *pspec);

static gchar ** rb_media_player_prefs_get_string_list	( RBMediaPlayerPrefs *prefs,
							  enum SyncPrefKey pref_key );

static void hash_table_duplicate (GHashTable **hash1, GHashTable *hash2);
static void itinerary_insert_all_of_type (RBMediaPlayerPrefs *prefs,
					  RhythmDBEntryType entry_type);

static void itinerary_insert_some_playlists (RBMediaPlayerPrefs *prefs);
static void itinerary_insert_some_podcasts (RBMediaPlayerPrefs *prefs);
static void build_itinerary_hash_table (RBMediaPlayerPrefs *prefs);
static void build_device_hash_table (RBMediaPlayerPrefs *prefs);
static void build_sync_list (RBMediaPlayerPrefs *prefs,
			     enum SyncPrefKey pref_key);

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
	
	if (priv->group != NULL) {
		g_free (priv->group);
		priv->group = NULL;
	}

	if (priv->sync_playlists_list != NULL) {
		g_hash_table_destroy (priv->sync_playlists_list);
		priv->sync_playlists_list = NULL;
	}
	
	if (priv->sync_podcasts_list != NULL) {
		g_hash_table_destroy (priv->sync_podcasts_list);
		priv->sync_podcasts_list = NULL;
	}
	
	if (priv->sync_to_add != NULL) {
		g_list_free (priv->sync_to_add);
		priv->sync_to_add = NULL;
	}
	
	if (priv->sync_to_remove != NULL) {
		g_list_free (priv->sync_to_remove);
		priv->sync_to_remove = NULL;
	}
		
	if (priv->itinerary_hash != NULL) {
		g_hash_table_destroy (priv->itinerary_hash);
		priv->itinerary_hash = NULL;
	}
		
	if (priv->device_hash != NULL) {
		g_hash_table_destroy (priv->device_hash);
		priv->device_hash = NULL;
	}
	
	if (priv->source != NULL) {
		g_object_unref (priv->source);
		priv->source = NULL;
	}
	
	if (priv->updating != NULL) {
		g_mutex_free (priv->updating);
		priv->updating = NULL;
	}
	
	G_OBJECT_CLASS (rb_media_player_prefs_parent_class)->dispose (object);
}

static void
rb_media_player_prefs_class_init (RBMediaPlayerPrefsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_media_player_prefs_dispose;
	
	object_class->set_property = rb_media_player_prefs_set_property;
	object_class->get_property = rb_media_player_prefs_get_property;
	
	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "source",
							      "Pointer to the RBMediaPlayerSource",
							      RB_TYPE_MEDIA_PLAYER_SOURCE,
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
	GList *list;
} HashTableComparisonData;

static void
rb_media_player_prefs_hash_table_compare (gpointer key,	/* gchar * track_UUID */
				  gpointer value,	/* RhythmDBEntry * */
				  gpointer user_data)	/* HashTableComparisonData * */
{
	HashTableComparisonData *data = user_data;
	
	if ( !g_hash_table_lookup (data->other_hash_table, key) ) {
		data->list = g_list_prepend ( data->list, value );
	}

}

typedef struct {
	RBMediaPlayerPrefs *prefs;
	GHashTable *hash_table;
} HashTableInsertionData;

static gboolean
entry_is_undownloaded_podcast (RhythmDBEntry *entry)
{
	if (rhythmdb_entry_get_entry_type(entry) == RHYTHMDB_ENTRY_TYPE_PODCAST_POST)
	{
		if (!rb_podcast_manager_entry_downloaded (entry))
			return TRUE;
	}
	
	return FALSE;
}

/* This function will resolve the iter from
 * a tree view, then check that the entry is not
 * an undownloaded podcast, before inserting it into
 * the given hash table.
 * Called on each item in a tree view.
 */
static gboolean
hash_table_insert_from_tree_view_foreach_func (GtkTreeModel *query_model,
					       GtkTreePath  *path,
					       GtkTreeIter  *iter,
					       HashTableInsertionData *data)
{
	RhythmDBEntry *entry;
	
	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (query_model), iter);
	
	if (!entry_is_undownloaded_podcast (entry))
		g_hash_table_insert (data->hash_table,
				     rb_media_player_source_track_uuid (entry),
				     entry);
	
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
	case PROP_SOURCE:
		priv->source = g_value_dup_object (value);
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
	case PROP_SOURCE:
		g_value_set_object (value, priv->source);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* Duplicate all entries from hash2 to hash1. hash1
 * needs to not be empty, duplicate entries are silently dropped
 */
static void
hash_table_duplicate (GHashTable **hash1, GHashTable *hash2)
{
	GHashTableIter iter;
	gpointer key, value;
	
	g_hash_table_iter_init (&iter, hash2);
	while (g_hash_table_iter_next (&iter, &key, &value)) {		
		g_hash_table_insert(*hash1, key, value);
	}
}

static void
itinerary_insert_all_of_type (RBMediaPlayerPrefs *prefs,
			      RhythmDBEntryType entry_type)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	GtkTreeModel *query_model;
	HashTableInsertionData data = { prefs, priv->itinerary_hash };
	RBShell *shell;
	RhythmDB *db;
	g_object_get (priv->source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	
	query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty(db));
	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (query_model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, entry_type,
				RHYTHMDB_QUERY_END);
	
	gtk_tree_model_foreach (query_model,
				(GtkTreeModelForeachFunc) hash_table_insert_from_tree_view_foreach_func,
				&data);
	
	g_object_unref (db);
	g_object_unref (shell);
}

static void
itinerary_insert_some_playlists (RBMediaPlayerPrefs *prefs)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	GList *list_iter, *items;
	GtkTreeModel *query_model;
	HashTableInsertionData data = { prefs, priv->itinerary_hash };
	RBShell *shell;
	
	g_object_get (priv->source, "shell", &shell, NULL);
	items = rb_playlist_manager_get_playlists ( (RBPlaylistManager *) rb_shell_get_playlist_manager (shell) );
	g_object_unref (shell);
	
	/* For each playlist in Rhythmbox */
	for (list_iter = items;
	     list_iter;
	     list_iter = list_iter->next )
	{
		gchar *name;
		
		g_object_get (G_OBJECT (list_iter->data), "name", &name, NULL);
		
		/* See if we should sync it */
		if ( g_hash_table_lookup (priv->sync_playlists_list, name) ) {
			query_model = GTK_TREE_MODEL (rb_playlist_source_get_query_model (list_iter->data));
			
			/* Add the entries to the hash_table */
			gtk_tree_model_foreach (query_model,
						(GtkTreeModelForeachFunc) hash_table_insert_from_tree_view_foreach_func,
						&data);
		}
		
		g_free (name);
	}
	
	g_list_free (items);
}

static void
itinerary_insert_some_podcasts (RBMediaPlayerPrefs *prefs)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	const gchar **iter;
	GtkTreeModel *query_model;
	HashTableInsertionData data = { prefs, priv->itinerary_hash };
	RBShell *shell;
	RhythmDB *db;
	g_object_get (priv->source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	
	for (iter = (const gchar **) rb_media_player_prefs_get_string_list (prefs, SYNC_PODCASTS_LIST);
	     *iter != NULL;
	     iter++)
	{

		query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty(db));
		rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (query_model),
					RHYTHMDB_QUERY_PROP_EQUALS,
					RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
					RHYTHMDB_QUERY_PROP_EQUALS,
					RHYTHMDB_PROP_ALBUM, *iter,
					RHYTHMDB_QUERY_END);
		
		gtk_tree_model_foreach (query_model,
					(GtkTreeModelForeachFunc) hash_table_insert_from_tree_view_foreach_func,
					&data);
	}
	
	g_object_unref (db);
	g_object_unref (shell);
}

static void
build_itinerary_hash_table (RBMediaPlayerPrefs *prefs)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	if (priv->itinerary_hash != NULL) {
		g_hash_table_unref (priv->itinerary_hash);
		priv->itinerary_hash = NULL;
	}
	
	priv->itinerary_hash = g_hash_table_new (g_str_hash, g_str_equal);
	
	if (priv->sync_music) {
		if (priv->sync_music_all) {
			/* Syncing all songs */
			itinerary_insert_all_of_type (prefs,
						      RHYTHMDB_ENTRY_TYPE_SONG);
		} else {
			/* Only syncing some songs */
			itinerary_insert_some_playlists (prefs);
		}
	}
	
	if (priv->sync_podcasts) {
		if (priv->sync_podcasts_all) {
			/* Syncing all podcasts */
			itinerary_insert_all_of_type (prefs,
						       RHYTHMDB_ENTRY_TYPE_PODCAST_POST);
		} else {
			/* Only syncing some podcasts */
			itinerary_insert_some_podcasts (prefs);
		}
	}
}

static void
build_device_hash_table (RBMediaPlayerPrefs *prefs)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	if (priv->device_hash != NULL) {
		g_hash_table_unref (priv->device_hash);
		priv->device_hash = NULL;
	}
	
	priv->device_hash = g_hash_table_new (g_str_hash, g_str_equal);
	
	if (priv->sync_music) {
		hash_table_duplicate (&priv->device_hash, rb_media_player_source_get_entries (priv->source));
	}
	
	if (priv->sync_podcasts) {
		hash_table_duplicate (&priv->device_hash, rb_media_player_source_get_podcasts (priv->source));
	}
}

static void
build_sync_list (RBMediaPlayerPrefs *prefs,
		 enum SyncPrefKey pref_key)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	HashTableComparisonData comparison_data;
	GHashTable *this_hash_table;
	
	switch (pref_key) {
		case SYNC_TO_ADD:
			this_hash_table = priv->itinerary_hash;
			comparison_data.other_hash_table = priv->device_hash;
			break;
		case SYNC_TO_REMOVE:
			this_hash_table = priv->device_hash;
			comparison_data.other_hash_table = priv->itinerary_hash;
			break;
		default:
			g_assert_not_reached();
			return;
	}
	
	comparison_data.list = NULL;
	g_hash_table_foreach (this_hash_table,
			      rb_media_player_prefs_hash_table_compare, /* function to add to add list if necessary */
			      &comparison_data );
	g_list_free (rb_media_player_prefs_get_list (prefs, pref_key));
	rb_media_player_prefs_set_list(prefs, pref_key, comparison_data.list);
}

static gboolean
rb_media_player_prefs_update_sync_helper ( RBMediaPlayerPrefs *prefs )
{
	/* Build itinerary_hash */
	build_itinerary_hash_table (prefs);
	
	/* Build device_hash */
	build_device_hash_table (prefs);
	
	/* Build Addition List */
	build_sync_list (prefs, SYNC_TO_ADD);
	
	/* Build Removal List */
	build_sync_list (prefs, SYNC_TO_REMOVE);
	
	/* Calculate how much space we need */
	rb_media_player_prefs_calculate_space_needed (prefs);
	
	return TRUE;
}

gboolean
rb_media_player_prefs_update_sync ( RBMediaPlayerPrefs *prefs )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	if (!g_mutex_trylock (priv->updating)) {
		/* If we are already updating */
		return FALSE;
	}
	
	rb_media_player_prefs_update_sync_helper (prefs);
	
	/* mark as updated */
	priv->sync_updated = TRUE;
	
	g_mutex_unlock (priv->updating);
	
	/* So it can be used as in g_idle_add */
	return FALSE;
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
	
	prefs = RB_MEDIA_PLAYER_PREFS (g_object_new (RB_TYPE_MEDIA_PLAYER_PREFS,
						     "source", RB_MEDIA_PLAYER_SOURCE (source),
						     NULL));
	
	g_return_val_if_fail (prefs != NULL, NULL);
	
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	GError *error = NULL;
	
	/* Load the key_file if it isn't already */
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
		/* Couldn't get the serial, use the device name */
		priv->group = rb_media_player_source_get_name ( RB_MEDIA_PLAYER_SOURCE (priv->source) );
	}
	
	/* add the group and keys, unless it exists */
	if ( !g_key_file_has_group(priv->key_file, priv->group) ) {
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_auto", FALSE);
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_music", FALSE);
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_music_all", TRUE);
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_podcasts", FALSE);
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_podcasts_all", TRUE);
		g_key_file_set_string_list (priv->key_file, priv->group, "sync_playlists_list", (const gchar * const *) "", 0);
		g_key_file_set_string_list (priv->key_file, priv->group, "sync_podcasts_list", (const gchar * const *) "", 0);
	}
	
	/* Load initial values from the file */
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
					return FALSE;
	}
}

static gchar **
rb_media_player_prefs_get_string_list ( RBMediaPlayerPrefs *prefs,
					enum SyncPrefKey pref_key)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (pref_key) {
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
				      enum SyncPrefKey pref_key)
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (pref_key) {
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
				    enum SyncPrefKey pref_key,
				    gboolean value )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (pref_key) {
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
		case SYNC_UPDATED:	priv->sync_updated = value; /* Not stored in key file */
					return;
		default:		g_assert_not_reached();
					return;
	}
	
	rb_media_player_prefs_save_file (prefs, NULL);
}

void
rb_media_player_prefs_set_list ( RBMediaPlayerPrefs *prefs,
				 enum SyncPrefKey pref_key,
				 GList * list )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (pref_key) {
		case SYNC_TO_ADD:
			priv->sync_to_add = list;
			break;
		case SYNC_TO_REMOVE:
			priv->sync_to_remove = list;
			break;
		default:
			g_assert_not_reached();
			return;
	}
}


GList *
rb_media_player_prefs_get_list ( RBMediaPlayerPrefs *prefs,
				 enum SyncPrefKey pref_key )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (pref_key) {
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
rb_media_player_prefs_get_playlist ( RBMediaPlayerPrefs *prefs,
				     const gchar * name )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	return g_hash_table_lookup (priv->sync_playlists_list, name);
}

gchar *
rb_media_player_prefs_get_podcast ( RBMediaPlayerPrefs *prefs,
				    const gchar * name )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	return g_hash_table_lookup (priv->sync_podcasts_list, name);
}

/* returns whether or not a playlist should be synced */
gboolean
rb_media_player_prefs_playlist_should_be_synced ( RBMediaPlayerPrefs *prefs,
						  const gchar * name )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	if (priv->sync_music_all)
		return TRUE;
	
	return priv->sync_music && (rb_media_player_prefs_get_playlist (prefs, name) != NULL);
}

gboolean
rb_media_player_prefs_podcast_should_be_synced ( RBMediaPlayerPrefs *prefs,
						 const gchar * name )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	if (priv->sync_podcasts_all)
		return TRUE;
	
	return priv->sync_podcasts && (rb_media_player_prefs_get_podcast (prefs, name) != NULL);
}

void
rb_media_player_prefs_set_playlist ( RBMediaPlayerPrefs *prefs,
				     const gchar * name )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	if (!rb_media_player_prefs_get_playlist (prefs, name))
		g_hash_table_insert (priv->sync_playlists_list, g_strdup(name), g_strdup (name));
			
	g_key_file_set_string_list (priv->key_file, priv->group,
				    "sync_playlists_list",
				    (const gchar * const *) rb_media_player_prefs_get_string_list (prefs, SYNC_PLAYLISTS_LIST),
				    g_hash_table_size (priv->sync_playlists_list) );
		
	rb_media_player_prefs_save_file (prefs, NULL);
}

void
rb_media_player_prefs_set_podcast ( RBMediaPlayerPrefs *prefs,
				    const gchar * name )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	if (!rb_media_player_prefs_get_podcast (prefs, name))
		g_hash_table_insert (priv->sync_podcasts_list, g_strdup(name), g_strdup (name));
		
	g_key_file_set_string_list (priv->key_file, priv->group,
				    "sync_podcasts_list",
				    (const gchar * const *) rb_media_player_prefs_get_string_list (prefs, SYNC_PODCASTS_LIST),
				    g_hash_table_size (priv->sync_podcasts_list) );
	
	rb_media_player_prefs_save_file (prefs, NULL);
}

void
rb_media_player_prefs_remove_playlist ( RBMediaPlayerPrefs *prefs,
					const gchar * name )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	g_hash_table_remove (priv->sync_playlists_list, name);
	g_key_file_set_string_list (priv->key_file, priv->group,
				    "sync_playlists_list",
				    (const gchar * const *) rb_media_player_prefs_get_string_list (prefs, SYNC_PLAYLISTS_LIST),
				    g_hash_table_size (priv->sync_playlists_list) );
	
	rb_media_player_prefs_save_file (prefs, NULL);
}

void
rb_media_player_prefs_remove_podcast ( RBMediaPlayerPrefs *prefs,
				       const gchar * name )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	g_hash_table_remove (priv->sync_podcasts_list, name);
	g_key_file_set_string_list (priv->key_file, priv->group,
				    "sync_podcasts_list",
				    (const gchar * const *) rb_media_player_prefs_get_string_list (prefs, SYNC_PODCASTS_LIST),
				    g_hash_table_size (priv->sync_podcasts_list) );
	
	rb_media_player_prefs_save_file (prefs, NULL);
}
				  
guint64
rb_media_player_prefs_get_uint64 ( RBMediaPlayerPrefs *prefs,
				   enum SyncPrefKey pref_key )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (pref_key) {
		case SYNC_SPACE_NEEDED:
			return priv->sync_space_needed;
		default:
			g_assert_not_reached();
			return 0;
	}
}

void
rb_media_player_prefs_set_uint64 ( RBMediaPlayerPrefs *prefs,
				   enum SyncPrefKey pref_key,
				   guint64 value )
{
	RBMediaPlayerPrefsPrivate *priv = MEDIA_PLAYER_PREFS_GET_PRIVATE (prefs);
	
	switch (pref_key) {
		case SYNC_SPACE_NEEDED:
			priv->sync_space_needed = value;
			break;
		default:
			g_assert_not_reached();
			break;
	}
}

