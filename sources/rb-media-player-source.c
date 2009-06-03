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

#include "rb-shell.h"
#include "rb-removable-media-source.h"
#include "rhythmdb.h"

/* macro to create rb_media_player_source_get_type and set rb_media_player_source_parent_class */
G_DEFINE_TYPE (RBMediaPlayerSource, rb_media_player_source, G_TYPE_OBJECT);

static GObject *
rb_media_player_source_constructor (GType			gtype,
				    guint		n_properties,
				    GObjectConstructParam *properties)
{
	Gobject *obj;
	
	{
		/* Always chain up to the parent constructor */
		RBMediaPlayerSourceClass *klass;
		GObjectClass *parent_class;
		parent_class = G_OBJECT_CLASS (rb_media_player_source_parent_class);
		obj = parent_class->constructor(gtype, n_properties, properties);
	}
	
	/* update the object state depending on constructor properties */
	
	return obj;
}

static void
rb_media_player_class_init (RBMediaPlayerSourceClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	
	gobject_class->constructor = rb_media_player_source_constructor;
}

static void
rb_media_player_source_init (RBMediaPlayerSource *self)
{
	/* initialize the object */
}
