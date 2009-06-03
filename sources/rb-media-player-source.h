/*
 *  arch-tag: Header for the Media Player Source object
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

#ifndef __RB_MEDIA_PLAYER_SOURCE_H
#define __RB_MEDIA_PLAYER_SOURCE_H

#include "rb-shell.h"
#include "rb-removable-media-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_MEDIA_PLAYER_SOURCE	(rb_media_player_get_type ())
#define RB_MEDIA_PLAYER_SOURCE(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_MEDIA_PLAYER_SOURCE))
#define RB_IS_MEDIA_PLAYER_SOURCE(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_MEDIA_PLAYER_SOURCE))
#define RB_MEDIA_PLAYER_SOURCE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSourceClass))
#define RB_IS_MEDIA_PLAYER_SOURCE_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), RB_TYPE_MEDIA_PLAYER_SOURCE))
#define RB_MEDIA_PLAYER_SOURCE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSourceClass))

typedef struct
{
	RBRemovableMediaSource parent_instance;
	
	/* instance members */
} RBMediaPlayerSource;

typedef struct
{
	RBRemovableMediaSourceClass parent_class;
	
	/* class members */
} RBMediaPlayerSourceClass;



G_END_DECLS

#endif

