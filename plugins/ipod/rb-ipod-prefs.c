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

typedef struct {
	GKeyFile *key_file;
	gchar *group;
	
	/* loaded/saved to file */
	gboolean sync_auto;
	gboolean sync_music;
	gboolean sync_music_all;
	gboolean sync_podcasts;
	gboolean sync_podcasts_all;
	GList *  sync_playlists_list;
	GList *  sync_podcasts_list;
	
	/* generated on load */
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
		g_list_free( priv->sync_playlists_list );
	
	if (priv->sync_playlists_list != NULL)
		g_list_free( priv->sync_podcasts_list );
	
	if (priv->sync_playlists_list != NULL)
		g_list_free( priv->sync_to_add );
	
	if (priv->sync_playlists_list != NULL)
		g_list_free( priv->sync_to_remove );
	
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
g_list_to_string_list (GList * list)
{
	GList *list_iter = list;
	gchar ** strv = g_new0 (gchar *, g_list_length (list));
	int i=0;
	
	while (list_iter != NULL)
	{
		strv[i++] = g_strdup(list_iter->data);
		list_iter = list_iter->next;
	}
	
	return strv;
}

static GList *
string_list_to_g_list (const gchar * const * strv)
{
	const gchar ** strv_iter = (const gchar **) strv;
	GList *list = NULL;
	
	while (*strv_iter != NULL) {
		list = g_list_append (list, g_strdup (*strv_iter));
		strv_iter++;
	}
	
	return list;
}

void
rb_ipod_prefs_update_sync ( RBiPodPrefs *prefs )
{
	/* FIXME: Stub.  Needs to build the to_add and to_remove lists, and calculate the space needed. */
	//RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
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
	RBiPodPrefs *prefs = g_object_new (RB_TYPE_IPOD_PREFS, NULL);
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	GError *error = NULL;
	GMount *mount;
	
	g_return_val_if_fail (source != NULL, NULL);
	
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
	
	priv->sync_playlists_list = string_list_to_g_list ( (const gchar * const *) g_key_file_get_string_list (priv->key_file, priv->group,
											"sync_playlists_list",
											NULL,
											NULL) );
	priv->sync_podcasts_list = string_list_to_g_list ( (const gchar * const *) g_key_file_get_string_list (priv->key_file, priv->group,
											"sync_podcasts_list",
											NULL,
											NULL) );

	rb_ipod_prefs_update_sync (prefs);
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
			return g_list_to_string_list (priv->sync_playlists_list);
		case SYNC_PODCASTS_LIST:
			return g_list_to_string_list (priv->sync_podcasts_list);
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
		case SYNC_PLAYLISTS_LIST:
			g_list_free(priv->sync_playlists_list);
			priv->sync_playlists_list = list;
			g_key_file_set_string_list (priv->key_file,
						    priv->group,
						    "sync_playlists_list",
						    (const gchar **) g_list_to_string_list (list),
						    g_list_length (list));
			rb_ipod_prefs_update_sync (prefs);
			rb_ipod_prefs_save_file (prefs, NULL);
			break;
		case SYNC_PODCASTS_LIST:
			g_list_free(priv->sync_podcasts_list);
			priv->sync_podcasts_list = list;
			g_key_file_set_string_list (priv->key_file,
						    priv->group,
						    "sync_podcasts_list",
						    (const gchar **) g_list_to_string_list (list),
						    g_list_length (list));
			rb_ipod_prefs_update_sync (prefs);
			rb_ipod_prefs_save_file (prefs, NULL);
			break;
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
		case SYNC_PLAYLISTS_LIST:
			return priv->sync_playlists_list;
		case SYNC_PODCASTS_LIST:
			return priv->sync_podcasts_list;
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
	
	GList * iter;
	
	switch (prop_id) {
		case SYNC_PLAYLISTS_LIST:
			iter = priv->sync_playlists_list;
			break;
		case SYNC_PODCASTS_LIST:
			iter = priv->sync_podcasts_list;
			break;
		default:
			g_assert_not_reached();
			return NULL;
	}
	
	while (iter != NULL) {
		if (g_strcmp0 (iter->data, entry) == 0)
			return iter->data;
		iter  = iter->next;
	}
	
	return NULL;
}

void
rb_ipod_prefs_set_entry ( RBiPodPrefs *prefs,
			  guint prop_id,
			  const gchar * entry,
			  gboolean value )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
	GList **list;
	
	switch (prop_id) {
		case SYNC_PLAYLISTS_LIST:
			list = &priv->sync_playlists_list;
			break;
		case SYNC_PODCASTS_LIST:
			list = &priv->sync_podcasts_list;
			break;
		default:
			g_assert_not_reached();
			return;
	}
	
	if (value) {
		if (!rb_ipod_prefs_get_entry (prefs, prop_id, entry))
			*list = g_list_append (*list, g_strdup (entry));
	} else {
		GList *iter = *list;
		while (iter != NULL) {
			if (g_strcmp0 (iter->data, entry) == 0)
				*list = g_list_remove (*list, iter->data);
			iter = iter->next;
		}
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

