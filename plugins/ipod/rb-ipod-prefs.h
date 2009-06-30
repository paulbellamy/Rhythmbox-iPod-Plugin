/*
 *  arch-tag: Helps rb-ipod-plugin pass the preferences to each RBiPodSource
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
#ifndef __RB_IPOD_PREFS_H
#define __RB_IPOD_PREFS_H

#include <gio/gio.h>
//#include <gdk-pixbuf/gdk-pixbuf.h>
#include "rb-ipod-source.h"

G_BEGIN_DECLS

#define RB_TYPE_IPOD_PREFS         (rb_ipod_prefs_get_type ())
#define RB_IPOD_PREFS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IPOD_PREFS, RBiPodPrefs))
#define RB_IPOD_PREFS_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IPOD_PREFS, RBiPodPrefsClass))
#define RB_IS_IPOD_PREFS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IPOD_PREFS))
#define RB_IS_IPOD_PREFS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IPOD_PREFS))
#define RB_IPOD_PREFS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IPOD_PREFS, RBiPodPrefsClass))

enum {
	SYNC_AUTO,
	SYNC_MUSIC,
	SYNC_MUSIC_ALL,
	SYNC_PODCASTS,
	SYNC_PODCASTS_ALL
};

typedef struct 
{
	GObject parent;
} RBiPodPrefs;

typedef struct
{
	GObjectClass parent;
} RBiPodPrefsClass;


RBiPodPrefs *rb_ipod_prefs_new (GKeyFile *key_file, RBiPodSource *source );
GType rb_ipod_prefs_get_type (void);

gboolean rb_ipod_prefs_save_file	( RBiPodPrefs *prefs,
					  GError **error );
gboolean rb_ipod_prefs_get		( RBiPodPrefs *prefs,
					  guint pref_id );
const gchar ** rb_ipod_prefs_get_entries	( RBiPodPrefs *prefs );
void	 rb_ipod_prefs_set		( RBiPodPrefs *prefs,
					  guint pref_id,
					  gboolean value );
void	 rb_ipod_prefs_set_entries	( RBiPodPrefs *prefs,
					  GList * entries,
					  gsize length );

gchar *	 rb_ipod_prefs_get_entry	( RBiPodPrefs *prefs,
					  const gchar * entry );
void	 rb_ipod_prefs_set_entry	( RBiPodPrefs *prefs,
					  const gchar * entry,
					  gboolean value );

#endif
