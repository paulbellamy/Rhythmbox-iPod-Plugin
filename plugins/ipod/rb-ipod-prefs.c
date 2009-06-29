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
	
	gboolean sync_auto;
	gboolean sync_music;
	gboolean sync_music_all;
	gboolean sync_podcasts;
	gboolean sync_podcasts_all;
	gchar ** sync_entries;
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

	if (priv->sync_entries != NULL)
		g_strfreev( priv->sync_entries );
	
	
	G_OBJECT_CLASS (rb_ipod_prefs_parent_class)->dispose (object);
}

static void
rb_ipod_prefs_class_init (RBiPodPrefsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_ipod_prefs_dispose;

	g_type_class_add_private (klass, sizeof (RBiPodPrefsPrivate));
}

gboolean
rb_ipod_prefs_save_file (RBiPodPrefs *prefs, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	gsize length;
	gchar *data = NULL;
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
		
		g_free(data);
		
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
		g_key_file_set_string_list (priv->key_file, priv->group, "sync_entries", (const gchar * const *) "", 0);
	}
	
	// Load initial values from the file
	priv->sync_auto = g_key_file_get_boolean (priv->key_file, priv->group, "sync_auto", NULL);
	priv->sync_music = g_key_file_get_boolean (priv->key_file, priv->group, "sync_music", NULL);
	priv->sync_music_all = g_key_file_get_boolean (priv->key_file, priv->group, "sync_music_all", NULL);
	priv->sync_podcasts = g_key_file_get_boolean (priv->key_file, priv->group, "sync_podcasts", NULL);
	priv->sync_podcasts_all = g_key_file_get_boolean (priv->key_file, priv->group, "sync_podcasts_all", NULL);
	priv->sync_entries = g_key_file_get_string_list (priv->key_file, priv->group, "sync_entries", NULL, NULL);
	
     	return prefs;
}

gboolean
rb_ipod_prefs_get ( RBiPodPrefs *prefs,
		    guint pref_id )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
	switch (pref_id) {
		case SYNC_AUTO:		return priv->sync_auto;
		case SYNC_MUSIC:	return priv->sync_music;
		case SYNC_MUSIC_ALL:	return priv->sync_music_all;
		case SYNC_PODCASTS:	return priv->sync_podcasts;
		case SYNC_PODCASTS_ALL:	return priv->sync_podcasts_all;
		default:		return FALSE;
	}
}

const gchar **
rb_ipod_prefs_get_entries (RBiPodPrefs *prefs)
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
	return (const gchar **) priv->sync_entries;
}

void
rb_ipod_prefs_set ( RBiPodPrefs *prefs,
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
	
	rb_ipod_prefs_save_file (prefs, NULL);
}

void
rb_ipod_prefs_set_entries (RBiPodPrefs *prefs,
			   gchar ** entries,
			   gsize length )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
	if (priv->sync_entries != NULL)
		g_strfreev(priv->sync_entries);
	
	priv->sync_entries = g_strdupv(entries);
	g_key_file_set_string_list (priv->key_file, priv->group, "sync_entries", (const gchar * const *)priv->sync_entries, length );
	
	rb_ipod_prefs_save_file (prefs, NULL);
}

gchar *
rb_ipod_prefs_get_entry	( RBiPodPrefs *prefs,
			  gchar * entry )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
	gchar **iter;
	
	for (iter = priv->sync_entries;
	     *iter != NULL;
	     iter++)
	{
		if (g_strcmp0 (*iter, entry) == 0)
			return *iter;
	}
	
	return NULL;
}

void
rb_ipod_prefs_set_entry ( RBiPodPrefs *prefs,
			  gchar * entry,
			  gboolean value )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
	// See if it is in the list
	gchar ** iter;

	if (rb_ipod_prefs_get_entry (prefs, entry)) {
		// If it is in the list, remove it
		if (!value) {
			gchar * dest[g_strv_length (priv->sync_entries) - 1];
			gchar **dest_iter = dest;
			for (iter = priv->sync_entries;
			     *iter != NULL;
			     iter++)
			{
				if (g_strcmp0 (*dest_iter, entry))
					iter++;
				
				*dest_iter = g_strdup (*iter);
				dest_iter++;
			}
			
			iter = priv->sync_entries;
			priv->sync_entries = dest;
			g_strfreev (iter);
		
			rb_ipod_prefs_save_file (prefs, NULL);
		}
	} else {
		// If not in list yet, add it
		if (value) {
			iter = priv->sync_entries;
			while (*iter != NULL) iter++;
			*iter = g_strdup(entry);
			
			rb_ipod_prefs_save_file (prefs, NULL);
		}
	}
}

