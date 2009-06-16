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
#include "rb-debug.h"

static gboolean rb_ipod_prefs_keyfile_loaded (RBiPodPrefs *prefs);

typedef struct {
	GKeyFile *key_file;
	gchar *group;
	
	gboolean sync_auto;
	gboolean sync_music;
	gboolean sync_music_all;
	gboolean sync_podcasts;
	gboolean sync_podcasts_all;
	gchar ** sync_entries;
} RbiPodPrefsPrivate;

G_DEFINE_TYPE (RbiPodPrefs, rb_ipod_prefs, G_TYPE_OBJECT)

#define IPOD_PREFS_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_IPOD_PREFS, RbIpodPrefsPrivate))

static void
rb_ipod_prefs_init (RBiPodPrefs *prefs)
{
	RBiPodPrefsPrivate *priv = RB_IPOD_PREFS_GET_PRIVATE (prefs);

	priv->key_file = NULL;
}

static void
rb_ipod_prefs_dispose (GObject *object)
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (object);
	
	priv->key_file = NULL;
	
	g_free( group );
	g_free( sync_auto );
	g_free( sync_music );
	g_free( sync_music_all );
	g_free( sync_podcasts );
	g_free( sync_podcasts_all );
	g_free( sync_entries );
	
	G_OBJECT_CLASS (rb_ipod_prefs_parent_class)->dispose (object);
}

static void
rb_ipod_prefs_class_init (RBiPodPrefsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_ipod_prefs_dispose;

	g_type_class_add_private (klass, sizeof (RBiPodPrefsPrivate));
}

static GKeyFile *
rb_ipod_prefs_load_file ()
{
	gchar *pathname;
	GKeyFile *key_file = g_key_file_new();
	GKeyFileFlags flags = G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS;
	GError *error = NULL;
	
	pathname = rb_find_user_data_file ("ipod-prefs.conf", NULL);
	rb_debug ("loading iPod data from \"%s\"", pathname);
	if ( !g_key_file_load_from_file (key_file, pathname, flags, &error) ) {
		rb_debug ("unable to load iPod data: %s", error->message);
		g_error_free (error);
		g_free(pathname);
		return FALSE;
	}
	
	g_free(pathname);
	return key_file;
}

RbiPodPrefs *
rb_ipod_prefs_new (GKeyFile *key_file, RBiPodSource *source )
{
	RBiPodPrefs *prefs = g_object_new (RB_TYPE_IPOD_PREFS, NULL);
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	RBiPodSourcePrivate *src_priv = IPOD_SOURCE_GET_PRIVATE (source);
	GMount *mount;
	
	g_return_val_if_fail (source != NULL, NULL);
	
	// Load the key_file if it isn't already
	priv->key_file = ( key_file != NULL ? key_file : rb_ipod_prefs_load_file() );
	if (priv->key_file == NULL) {
		g_object_free(prefs);
		return NULL;
	}
	
	g_object_get (source, "mount", &mount, NULL);
	if (rb_ipod_helpers_get_serial ( mount ) != NULL) {
		priv->group = rb_ipod_helpers_get_serial ( mount );
	} else {
		priv->group = rb_ipod_db_get_ipod_name (src_priv->ipod_db);
	}
	
	// add the group and keys, unless it exists
	if ( !g_key_file_has_group(priv->key_file, priv->group) ) {
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_auto", false);
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_music", false);
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_music_all", false);
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_podcasts", false);
		g_key_file_set_boolean (priv->key_file, priv->group, "sync_podcasts_all", false);
		g_key_file_set_string_list (priv->key_file, priv->group, "sync_entries", NULL, 0);
	}
	
	// Load initial values from the file
	priv->sync_auto = g_key_file_get_boolean (priv->key_file, priv->group, "sync_auto", &error);
	priv->sync_music = g_key_file_get_boolean (priv->key_file, priv->group, "sync_music", &error);
	priv->sync_music_all = g_key_file_get_boolean (priv->key_file, priv->group, "sync_music_all", &error);
	priv->sync_podcasts = g_key_file_get_boolean (priv->key_file, priv->group, "sync_podcasts", &error);
	priv->sync_podcasts_all = g_key_file_get_boolean (priv->key_file, priv->group, "sync_podcasts_all", &error);
	priv->entries = g_key_file_get_string_list (priv->key_file, priv->group, "sync_entries", NULL, &error);
	
     	return prefs;
}

static gpointer
rb_ipod_prefs_get ( RBiPodPrefs *prefs, guint pref_id )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
	switch (pref_id) {
		case SYNC_AUTO:		return priv->sync_auto;
		case SYNC_MUSIC:	return priv->sync_music;
		case SYNC_MUSIC_ALL:	return priv->sync_music_all;
		case SYNC_PODCASTS:	return priv->sync_podcasts;
		case SYNC_PODCASTS_ALL:	return priv->sync_podcasts_all;
		default:		return NULL;
	}
}

static gchar **
rb_ipod_prefs_get_entries (RBiPodPrefs *prefs)
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
	return priv->entries;
}

static void
rb_ipod_prefs_set ( RBiPodPrefs *prefs, guint pref_id, gpointer value )
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
}

static void
rb_ipod_prefs_set_entries (RBiPodPrefs *prefs, const gchar *entries[], gsize *length )
{
	RBiPodPrefsPrivate *priv = IPOD_PREFS_GET_PRIVATE (prefs);
	
	priv->sync_entries = entries;
	g_key_file_set_string_list (priv->key_file, priv->group, "sync_entries", entries, length );
}


