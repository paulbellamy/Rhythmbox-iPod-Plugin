/*
 *  Copyright (C) 2008 Christophe Fergeau  <teuf@gnome.org>
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

#ifndef __RB_IPOD_HELPERS_H
#define __RB_IPOD_HELPERS_H

#include <glib.h>
#include <rb-source.h>

#include "mediaplayerid.h"

G_BEGIN_DECLS
gboolean rb_ipod_helpers_show_first_time_dialog (GMount *mount,
						 const char *builder_file);

guint64 get_fs_property (const char *mountpoint, const char *attr);
guint64 rb_ipod_helpers_get_capacity (const char *mountpoint);
guint64 rb_ipod_helpers_get_free_space (const char *mountpoint);
char *rb_ipod_helpers_get_device (RBSource *source);
gboolean rb_ipod_helpers_is_ipod (GMount *mount, MPIDDevice *device_info);
char *rb_ipod_helpers_get_serial (MPIDDevice *device_info);
gboolean rb_ipod_helpers_needs_init (GMount *mount);
void rb_ipod_helpers_hash_table_insert (gpointer key,
					gpointer value,
					gpointer hash_table);
G_END_DECLS

#endif /* __RB_IPOD_HELPERS_H */
