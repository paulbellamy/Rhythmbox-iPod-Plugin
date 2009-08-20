/*
 *  arch-tag: Helps RBMediaPlayerSource track it's keyfile, and helps it sync
 *
 *  Copyright (C) 2009 Paul Bellamy <paul.a.bellamy@gmail.com>
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
#ifndef __RB_MEDIA_PLAYER_PREFS_H
#define __RB_MEDIA_PLAYER_PREFS_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define RB_TYPE_MEDIA_PLAYER_PREFS         (rb_media_player_prefs_get_type ())
#define RB_MEDIA_PLAYER_PREFS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_MEDIA_PLAYER_PREFS, RBMediaPlayerPrefs))
#define RB_MEDIA_PLAYER_PREFS_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_MEDIA_PLAYER_PREFS, RBMediaPlayerPrefsClass))
#define RB_IS_MEDIA_PLAYER_PREFS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_MEDIA_PLAYER_PREFS))
#define RB_IS_MEDIA_PLAYER_PREFS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_MEDIA_PLAYER_PREFS))
#define RB_MEDIA_PLAYER_PREFS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_MEDIA_PLAYER_PREFS, RBMediaPlayerPrefsClass))

enum SyncPrefKey {
	SYNC_AUTO,
	SYNC_MUSIC,
	SYNC_MUSIC_ALL,
	SYNC_PODCASTS,
	SYNC_PODCASTS_ALL,
	SYNC_PLAYLISTS_LIST,
	SYNC_PODCASTS_LIST,
	SYNC_TO_ADD,
	SYNC_TO_REMOVE,
	SYNC_SPACE_NEEDED,
	SYNC_UPDATED
};

typedef struct 
{
	GObject parent;
} RBMediaPlayerPrefs;

typedef struct
{
	GObjectClass parent;
} RBMediaPlayerPrefsClass;

RBMediaPlayerPrefs *rb_media_player_prefs_new (GKeyFile **key_file, GObject *source );
GType rb_media_player_prefs_get_type (void);

gboolean rb_media_player_prefs_update_sync	( RBMediaPlayerPrefs *prefs );

gboolean rb_media_player_prefs_save_file	( RBMediaPlayerPrefs *prefs,
						  GError **error );
gboolean rb_media_player_prefs_get_boolean	( RBMediaPlayerPrefs *prefs,
						  enum SyncPrefKey pref_key );
void	 rb_media_player_prefs_set_boolean	( RBMediaPlayerPrefs *prefs,
						  enum SyncPrefKey pref_key,
						  gboolean value );
GHashTable * rb_media_player_prefs_get_hash_table (RBMediaPlayerPrefs *prefs,
						   enum SyncPrefKey pref_key );
void	 rb_media_player_prefs_set_list		( RBMediaPlayerPrefs *prefs,
						  enum SyncPrefKey pref_key,
						  GList * list );
GList *  rb_media_player_prefs_get_list		( RBMediaPlayerPrefs *prefs,
						  enum SyncPrefKey pref_key );
gchar *	 rb_media_player_prefs_get_playlist	( RBMediaPlayerPrefs *prefs,
						  const gchar * name );
gchar *	 rb_media_player_prefs_get_podcast	( RBMediaPlayerPrefs *prefs,
						  const gchar * name );
void	 rb_media_player_prefs_set_playlist	( RBMediaPlayerPrefs *prefs,
						  const gchar * name );
void	 rb_media_player_prefs_set_podcast	( RBMediaPlayerPrefs *prefs,
						  const gchar * name );
void	 rb_media_player_prefs_remove_playlist	( RBMediaPlayerPrefs *prefs,
						  const gchar * name );
void	 rb_media_player_prefs_remove_podcast	( RBMediaPlayerPrefs *prefs,
						  const gchar * name );
gboolean rb_media_player_prefs_playlist_should_be_synced ( RBMediaPlayerPrefs *prefs,
							   const gchar * name );
gboolean rb_media_player_prefs_podcast_should_be_synced ( RBMediaPlayerPrefs *prefs,
							  const gchar * name );
gint64	 rb_media_player_prefs_get_int64	( RBMediaPlayerPrefs *prefs,
						  enum SyncPrefKey pref_key );
void	 rb_media_player_prefs_set_int64	( RBMediaPlayerPrefs *prefs,
						  enum SyncPrefKey pref_key,
						  guint64 value );

#endif
