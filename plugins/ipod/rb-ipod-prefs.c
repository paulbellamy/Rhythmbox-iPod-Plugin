/*
 *  arch-tag: Abstraction of libgpod Itdb_ItunesDB object
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

#include "rb-ipod-prefs.h"
#include "rb-file-helpers.h"
#include "rb-ipod-helpers.h"
#include "rb-ipod-db.h"
#include "rb-debug.h"
#include "rb-playlist-source.h"
#include "rb-playlist-manager.h"
#include "rb-podcast-manager.h"

typedef struct {
	GKeyFile *key_file;
	gchar *group;
	
	/* Pointers to stuff for setting up the sync */
	RBShell *shell;
	RhythmDB *db;
	RBiPodSource *ipod_source;
	
	
	/* loaded/saved to file */
	gboolean sync_auto;
	gboolean sync_music;
	gboolean sync_music_all;
	gboolean sync_podcasts;
	gboolean sync_podcasts_all;
	GHashTable * sync_playlists_list;
	GHashTable * sync_podcasts_list;
	
	/* generated on load and rb_ipod_prefs_sync_update */
	GList *  sync_to_add;
	GList *  sync_to_remove;
	gint	 sync_space_needed;
	
} RBiPodPrefsPrivate;

G_DEFINE_TYPE (RBiPodPrefs, rb_ipod_prefs, G_TYPE_OBJECT)

#define IPOD_PREFS_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_IPOD_PREFS, RBiPodPrefsPrivate))

static void
rb_ipod_prefs_init (RBiPodPrefs *prefs)
{
}

static void
rb_ipod_prefs_dispose (GObject *object)
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (object);
	
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
	
	if (priv->shell != NULL)
		g_object_unref (priv->shell);
		
	if (priv->db != NULL)
		g_object_unref (priv->db);
		
	priv->ipod_source = NULL;
	
	G_OBJECT_CLASS (rb_ipod_prefs_parent_class)->dispose (object);
}

static void
rb_ipod_prefs_class_init (RBiPodPrefsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_ipod_prefs_dispose;

	g_type_class_add_private (klass, sizeof (RBiPodPrefsPrivate));
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
rb_ipod_prefs_hash_table_compare (gpointer key,		// RhythmDBEntry *
				  gpointer value,	// Whatever
				  gpointer user_data)	// HashTableComparisonData *
{
	HashTableComparisonData *data = user_data;
	
	if ( !g_hash_table_lookup (data->other_hash_table, key) ) {
		*(data->list) = g_list_append ( *(data->list), key );
	}
}

typedef struct {
	RBiPodPrefs *ipod_prefs;
	GHashTable **hash_table;
} HashTableInsertionData;

static void
rb_ipod_prefs_hash_table_insert ( gpointer key,		// RhythmDBEntry *
				  gpointer value,	// Whatever
				  gpointer user_data )	// HashTableInsertionData *
{
	gpointer orig_key;
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (((HashTableInsertionData *)user_data)->ipod_prefs);
	HashTableInsertionData *data = user_data;
	GHashTable *hash_table = *(data->hash_table);
	if ( g_hash_table_lookup_extended ( hash_table, key, &orig_key, NULL) ) {
		rb_debug ("Hash Table Collision between:\n%s - %s - %s, and\n%s - %s - %s",
			  rhythmdb_entry_get_string (key, RHYTHMDB_PROP_TITLE),
			  rhythmdb_entry_get_string (key, RHYTHMDB_PROP_ARTIST),
			  rhythmdb_entry_get_string (key, RHYTHMDB_PROP_ALBUM),
			  rhythmdb_entry_get_string (orig_key, RHYTHMDB_PROP_TITLE),
			  rhythmdb_entry_get_string (orig_key, RHYTHMDB_PROP_ARTIST),
			  rhythmdb_entry_get_string (orig_key, RHYTHMDB_PROP_ALBUM));
		return;
	}
	
	RhythmDBEntryType entry_type = rhythmdb_entry_get_entry_type(key);
	if (entry_type == RHYTHMDB_ENTRY_TYPE_SONG && !priv->sync_music)
		return;
	if (entry_type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST
	    || entry_type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED)
	{
		if (!priv->sync_podcasts)
			return;
		if (!rb_podcast_manager_entry_downloaded (key))
			return;
	}
		
	//g_print("entry_type->name: %s\n", entry_type->name); // DEBUGGING
		
	g_hash_table_insert ( hash_table,
			      key,
			      g_strdup ( rhythmdb_entry_get_string (key, RHYTHMDB_PROP_LOCATION) ) );
}

static gboolean
rb_ipod_prefs_tree_view_insert (GtkTreeModel *query_model,
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
	
	rb_ipod_prefs_hash_table_insert (entry, NULL, insertion_data);
	
	return FALSE;
}

static gint
rb_ipod_prefs_calculate_space_needed (RBiPodPrefs *prefs)
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	GList * list_iter;
	
	priv->sync_space_needed = 0;
	
	for (list_iter = priv->sync_to_add; list_iter; list_iter = list_iter->next) {		
		priv->sync_space_needed += (gint64) rhythmdb_entry_get_uint64 ( list_iter->data,
										RHYTHMDB_PROP_FILE_SIZE );
	}
	
	for (list_iter = priv->sync_to_remove; list_iter; list_iter = list_iter->next) {		
		priv->sync_space_needed -= (gint64) rhythmdb_entry_get_uint64 ( list_iter->data,
										RHYTHMDB_PROP_FILE_SIZE );
	}
	
	// DEBUGGING
	g_print("Space Needed: %.2lf MB\n", (long int) (priv->sync_space_needed) / 1000000.0 );
	
	return priv->sync_space_needed;
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

static void
hash_table_insert_all (RBiPodPrefs *prefs,
		       GHashTable *hash,
		       RhythmDBEntryType entry_type)
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	HashTableInsertionData insertion_data = { prefs, &hash };
	GtkTreeModel *query_model;
	RhythmDB *db = priv->db;
	
	query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty(db));
	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (query_model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, entry_type,
				RHYTHMDB_QUERY_END);
	gtk_tree_model_foreach (query_model,
				(GtkTreeModelForeachFunc) rb_ipod_prefs_tree_view_insert,
				&insertion_data);
}

static void
hash_table_insert_some_playlists (RBiPodPrefs *prefs,
				  GHashTable *hash)
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	const gchar **iter;
	gchar *name;
	GList *list_iter, *items;
	GtkTreeModel *query_model;
	HashTableInsertionData insertion_data = { prefs, &hash };


	items = rb_playlist_manager_get_playlists ( (RBPlaylistManager *) rb_shell_get_playlist_manager (priv->shell) );	
	for ( iter = (const gchar **) rb_ipod_prefs_get_string_list ( prefs, SYNC_PLAYLISTS_LIST );
	      *iter != NULL;
	      iter++ )
	{
		// get item with ( g_strcmp(name, iter) == 0 )
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
							(GtkTreeModelForeachFunc) rb_ipod_prefs_tree_view_insert,
							&insertion_data);
				break;
			}
		}
	}
	g_list_free (items);
}

static void
hash_table_insert_some_podcasts (RBiPodPrefs *prefs,
				 GHashTable *hash)
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	const gchar **iter;
	GtkTreeModel *query_model;
	HashTableInsertionData insertion_data = { prefs, &hash };
	
	for (iter = (const gchar **) rb_ipod_prefs_get_string_list (prefs, SYNC_PODCASTS_LIST);
	     *iter != NULL;
	     iter++)
	{
		// DEBUGGING
		//g_print ("Found Podcast: %s\n", *iter);
		
		// It is a podcast feed
		query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty(priv->db));
		rhythmdb_do_full_query (priv->db, RHYTHMDB_QUERY_RESULTS (query_model),
					RHYTHMDB_QUERY_PROP_EQUALS,
					RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
					RHYTHMDB_QUERY_PROP_EQUALS,
					RHYTHMDB_PROP_ALBUM, *iter,
					RHYTHMDB_QUERY_END);
		
		// Add the entries to the hash_table
		gtk_tree_model_foreach (query_model,
					(GtkTreeModelForeachFunc) rb_ipod_prefs_tree_view_insert,
					&insertion_data);
	}
}

void
rb_ipod_prefs_update_sync ( RBiPodPrefs *prefs )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	GHashTable *itinerary_hash = g_hash_table_new (rb_ipod_helpers_track_hash, rb_ipod_helpers_track_equal);
	GHashTable *ipod_hash = g_hash_table_new (rb_ipod_helpers_track_hash, rb_ipod_helpers_track_equal);
	GList	*to_add = NULL; // Files to go onto the iPod
	GList	*to_remove = NULL; // Files to be removed from the iPod
	HashTableComparisonData comparison_data;
	
	/* Build itinerary_hash */
	if (priv->sync_music) {
		if (priv->sync_music_all) {
			// Syncing all songs
			hash_table_insert_all (prefs,
					       itinerary_hash,
					       RHYTHMDB_ENTRY_TYPE_SONG);
		} else {
			// Only syncing some songs
			hash_table_insert_some_playlists (prefs,
							  itinerary_hash);
		}
	}
	
	if (priv->sync_podcasts) {
		if (priv->sync_podcasts_all) {
			// Syncing all podcasts
			hash_table_insert_all (prefs,
					       itinerary_hash,
					       RHYTHMDB_ENTRY_TYPE_PODCAST_POST);
		} else {
			// Only syncing some podcasts
			hash_table_insert_some_podcasts (prefs,
							 itinerary_hash);
		}
	}
	
	/* Build ipod_hash */
	if (priv->sync_music) {
		hash_table_duplicate (&ipod_hash, rb_ipod_source_get_entries (priv->ipod_source));
	}
	
	if (priv->sync_podcasts) {
		hash_table_duplicate (&ipod_hash, rb_ipod_source_get_podcasts (priv->ipod_source));
	}
	
	/* Build Addition List */
	comparison_data.other_hash_table = ipod_hash;
	comparison_data.list = &to_add;
	g_hash_table_foreach (itinerary_hash,
			      rb_ipod_prefs_hash_table_compare, // function to add to add list if necessary
			      &comparison_data );
	rb_ipod_prefs_set_list(prefs, SYNC_TO_ADD, to_add);
	
	/* Build Removal List */
	comparison_data.other_hash_table = itinerary_hash;
	comparison_data.list = &to_remove;
	g_hash_table_foreach (ipod_hash,
			      rb_ipod_prefs_hash_table_compare, // function to add to remove list if necessary
			      &comparison_data );
	rb_ipod_prefs_set_list(prefs, SYNC_TO_REMOVE, to_remove);
	
	// Empty the hash tables
	g_hash_table_unref (itinerary_hash);
	g_hash_table_unref (ipod_hash);
	
	/* Calculate how much space we need */
	rb_ipod_prefs_calculate_space_needed (prefs);
}

gboolean
rb_ipod_prefs_save_file (RBiPodPrefs *prefs, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	gsize length;
	gchar *data = NULL;
	
	/* Save the Keyfile */
	if ( priv->key_file != NULL) {
		data = g_key_file_to_data (priv->key_file,
					   &length,
					   error);
		if (error != NULL) {
			rb_debug ("unable to save iPod properties: %s", (*error)->message);
			return FALSE;
		}
		
		g_file_set_contents (rb_find_user_data_file ("ipod-prefs.conf", NULL),
				     data,
				     length,
				     error);
		
		g_free (data);
		
		if (error != NULL) {
			rb_debug ("unable to save iPod properties: %s", (*error)->message);
			return FALSE;
		}
		
	}
	
	return TRUE;
}

static GKeyFile *
rb_ipod_prefs_load_file (RBiPodPrefs *prefs, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gchar *pathname = rb_find_user_data_file ("ipod-prefs.conf", NULL);
	GKeyFile *key_file = g_key_file_new();
	GKeyFileFlags flags = G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS;
	
	rb_debug ("loading iPod properties from \"%s\"", pathname);
	if ( !g_key_file_load_from_file (key_file, pathname, flags, error) ) {
		rb_debug ("unable to load iPod properties: %s", (*error)->message);
	}
	
	g_free(pathname);
	return key_file;
}

RBiPodPrefs *
rb_ipod_prefs_new (GKeyFile *key_file, RBiPodSource *source )
{
	g_return_val_if_fail (source != NULL, NULL);

	RBiPodPrefs *prefs = g_object_new (RB_TYPE_IPOD_PREFS, NULL);
	
	
	g_return_val_if_fail (prefs != NULL, NULL);
	
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	GError *error = NULL;
	GMount *mount;
	
	g_object_get (source, "shell", &priv->shell, NULL);
	g_object_get (priv->shell, "db", &priv->db, NULL);
	priv->ipod_source = source;
	
	// Load the key_file if it isn't already
	priv->key_file = (key_file == NULL ? rb_ipod_prefs_load_file(prefs, &error) : key_file);
	if (priv->key_file == NULL) {
		g_object_unref (G_OBJECT (prefs));
		prefs = NULL;
		return NULL;
	}
	
	g_object_get (source, "mount", &mount, NULL);
	priv->group = rb_ipod_helpers_get_serial ( mount );
	if (priv->group == NULL) {
		// Couldn't get the serial, use the ipod name
		priv->group = g_strdup(rb_ipod_source_get_name (source));
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

     	return prefs;
}
gboolean
rb_ipod_prefs_get_boolean ( RBiPodPrefs *prefs,
			    guint pref_id )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
	switch (pref_id) {
		case SYNC_AUTO:		return priv->sync_auto;
		case SYNC_MUSIC:	return priv->sync_music;
		case SYNC_MUSIC_ALL:	return priv->sync_music_all;
		case SYNC_PODCASTS:	return priv->sync_podcasts;
		case SYNC_PODCASTS_ALL:	return priv->sync_podcasts_all;
		default:		g_assert_not_reached();
	}
}

gchar **
rb_ipod_prefs_get_string_list ( RBiPodPrefs *prefs,
				guint prop_id)
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
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

void
rb_ipod_prefs_set_boolean ( RBiPodPrefs *prefs,
			    guint pref_id,
			    gboolean value )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
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
		default:		break;
	}
	
	rb_ipod_prefs_update_sync (prefs);
	rb_ipod_prefs_save_file (prefs, NULL);
}

void
rb_ipod_prefs_set_list ( RBiPodPrefs *prefs,
			 guint prop_id,
			 GList * list )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
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
rb_ipod_prefs_get_list ( RBiPodPrefs *prefs,
			 guint prop_id )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
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
rb_ipod_prefs_get_entry	( RBiPodPrefs *prefs,
			  guint prop_id,
			  const gchar * entry )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
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

void
rb_ipod_prefs_set_entry ( RBiPodPrefs *prefs,
			  guint prop_id,
			  const gchar * entry,
			  gboolean value )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
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
		if (!rb_ipod_prefs_get_entry (prefs, prop_id, entry))
			g_hash_table_insert (hash_table, g_strdup (entry), g_strdup (entry));
	} else {
		g_hash_table_remove (hash_table, entry);
	}
	
	switch (prop_id) {
		case SYNC_PLAYLISTS_LIST:
			g_key_file_set_string_list (priv->key_file, priv->group,
						    "sync_playlists_list",
						    (const gchar * const *) rb_ipod_prefs_get_string_list (prefs, SYNC_PLAYLISTS_LIST),
						    g_hash_table_size (priv->sync_playlists_list) );
			
			break;
		case SYNC_PODCASTS_LIST:	
			g_key_file_set_string_list (priv->key_file, priv->group,
						    "sync_podcasts_list",
						    (const gchar * const *) rb_ipod_prefs_get_string_list (prefs, SYNC_PODCASTS_LIST),
						    g_hash_table_size (priv->sync_podcasts_list) );
			break;
		default:
			g_assert_not_reached();
			return;
	}
	
	rb_ipod_prefs_update_sync (prefs);
	rb_ipod_prefs_save_file (prefs, NULL);
}
				  
gint
rb_ipod_prefs_get_int ( RBiPodPrefs *prefs,
			guint prop_id )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
	switch (prop_id) {
		case SYNC_SPACE_NEEDED:
			return priv->sync_space_needed;
		default:
			g_assert_not_reached();
			return 0;
	}
}

void
rb_ipod_prefs_set_int ( RBiPodPrefs *prefs,
			guint prop_id,
			gint value )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
	switch (prop_id) {
		case SYNC_SPACE_NEEDED:
			priv->sync_space_needed = value;
			break;
		default:
			g_assert_not_reached();
			break;
	}
}

