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
//#include "rb-removable-media-source.h"
#include "rb-media-player-source.h"
//#include "rhythmdb.h"

/* macro to create rb_media_player_source_get_type and set rb_media_player_source_parent_class */
G_DEFINE_TYPE (RBMediaPlayerSource, rb_media_player_source, G_TYPE_OBJECT);

static void
rb_media_player_source_class_init (RBMediaPlayerSourceClass *klass)
{
	/*
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	*/
}

static void
rb_media_player_source_init (RBMediaPlayerSource *self)
{
	/* initialize the object */
}
