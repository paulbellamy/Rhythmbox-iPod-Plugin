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

typedef struct {
	RBMediaPlayerPrefs *prefs;
	
} RBMediaPlayerSourcePrivate;

/* macro to create rb_media_player_source_get_type and set rb_media_player_source_parent_class */
G_DEFINE_TYPE (RBMediaPlayerSource, rb_media_player_source, G_TYPE_OBJECT);

#define MEDIA_PLAYER_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSourcePrivate))

static void rb_media_player_source_dispose (GObject *object);
static void rb_media_player_source_class_init (RBMediaPlayerSourceClass *klass);
static void rb_media_player_source_init (RBMediaPlayerSource *self);

static void
rb_media_player_source_dispose (GObject *object)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);
	
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
	
	object_class->dispose = rb_media_player_source_dispose;
	
	/* FIXME: These need to be hooked up based on whether it is an ipod or MTP device
	 */
	klass->impl_get_entries = NULL;
	klass->impl_get_podcasts = NULL;
	klass->impl_get_capacity = NULL;
	klass->impl_add_entries = NULL;
	klass->impl_trash_entries = NULL;
	klass->impl_get_serial = NULL;
	klass->impl_get_name = NULL;
}

static void
rb_media_player_source_init (RBMediaPlayerSource *self)
{
	/* initialize the object */
}

RBMediaPlayerSource *
rb_media_player_source_new (void)
{
	RBMediaPlayerSource *source = g_object_new (RB_TYPE_MEDIA_PLAYER_PREFS, NULL);
	
	return source;
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
		g_print("Not enough Free Space.\n");
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


