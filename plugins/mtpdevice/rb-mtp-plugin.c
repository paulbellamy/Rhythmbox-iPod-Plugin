/*
 * rb-mtp-plugin.c
 *
 * Copyright (C) 2006 Peter Grundström <pete@openfestis.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>
#include <libmtp.h>

#if defined(HAVE_GUDEV)
#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>
#else
#include <hal/libhal.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#endif

#include "rb-source.h"
#include "rb-sourcelist.h"
#include "rb-mtp-source.h"
#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "rb-shell.h"
#include "rb-stock-icons.h"
#include "rb-removable-media-manager.h"


#define RB_TYPE_MTP_PLUGIN		(rb_mtp_plugin_get_type ())
#define RB_MTP_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_MTP_PLUGIN, RBMtpPlugin))
#define RB_MTP_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IPOD_PLUGIN, RBMtpPluginClass))
#define RB_IS_MTP_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_MTP_PLUGIN))
#define RB_IS_MTP_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_MTP_PLUGIN))
#define RB_MTP_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_MTP_PLUGIN, RBMtpPluginClass))


typedef struct
{
	RBPlugin parent;

	RBShell *shell;
	GtkActionGroup *action_group;
	guint ui_merge_id;

	guint create_device_source_id;

	GList *mtp_sources;

#if !defined(HAVE_GUDEV)
	LibHalContext *hal_context;
	DBusConnection *dbus_connection;
#endif
	
	GKeyFile *key_file;
} RBMtpPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBMtpPluginClass;


G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType rb_mtp_plugin_get_type (void) G_GNUC_CONST;

static void rb_mtp_plugin_init (RBMtpPlugin *plugin);
static void rb_mtp_plugin_finalize (GObject *object);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);

#if defined(HAVE_GUDEV)
static RBSource* create_source_device_cb (RBRemovableMediaManager *rmm, GObject *device, RBMtpPlugin *plugin);
#else
static void rb_mtp_plugin_device_added (LibHalContext *context, const char *udi);
static void rb_mtp_plugin_device_removed (LibHalContext *context, const char *udi);
static gboolean rb_mtp_plugin_setup_dbus_hal_connection (RBMtpPlugin *plugin);
static RBSource* create_source_cb (RBMtpPlugin *plugin, LIBMTP_mtpdevice_t *device, const char *udi);
#endif

static void rb_mtp_plugin_sync (GtkAction *action, RBMtpPlugin *plugin);
static void rb_mtp_plugin_properties (GtkAction *action, RBMtpPlugin *plugin);
static void rb_mtp_plugin_eject  (GtkAction *action, RBMtpPlugin *plugin);
static void rb_mtp_plugin_rename (GtkAction *action, RBMtpPlugin *plugin);

GType rb_mtp_src_get_type (void);

RB_PLUGIN_REGISTER(RBMtpPlugin, rb_mtp_plugin)

static GtkActionEntry rb_mtp_plugin_actions [] =
{
	{ "MTPSourceSync", GTK_STOCK_REFRESH, N_("_Sync"), NULL,
	  N_("Sync MTP-device"),
	  G_CALLBACK (rb_mtp_plugin_sync) },
	{ "MTPSourceEject", GNOME_MEDIA_EJECT, N_("_Eject"), NULL,
	  N_("Eject MTP-device"),
	  G_CALLBACK (rb_mtp_plugin_eject) },
	{ "MTPSourceRename", NULL, N_("_Rename"), NULL,
	  N_("Rename MTP-device"),
	  G_CALLBACK (rb_mtp_plugin_rename) },
	{ "MTPSourceProperties", GTK_STOCK_PROPERTIES, N_("_Properties"), NULL,
	  N_("Display device properties"),
	  G_CALLBACK (rb_mtp_plugin_properties) }
};

static void
rb_mtp_plugin_class_init (RBMtpPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->finalize = rb_mtp_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;

	/* register types used by the plugin */
	RB_PLUGIN_REGISTER_TYPE (rb_mtp_source);

	/* ensure the gstreamer src element gets linked in */
	rb_mtp_src_get_type ();
}

static void
rb_mtp_plugin_init (RBMtpPlugin *plugin)
{
	rb_debug ("RBMtpPlugin initialising");
	LIBMTP_Init ();
}

static void
rb_mtp_plugin_finalize (GObject *object)
{
	rb_debug ("RBMtpPlugin finalising");

	G_OBJECT_CLASS (rb_mtp_plugin_parent_class)->finalize (object);
}

static void
impl_activate (RBPlugin *bplugin, RBShell *shell)
{
	RBMtpPlugin *plugin = RB_MTP_PLUGIN (bplugin);
	GtkUIManager *uimanager = NULL;
	RBRemovableMediaManager *rmm;
	char *file = NULL;
#if defined(HAVE_GUDEV)
	gboolean rmm_scanned = FALSE;
#else
	int num, i, ret;
	char **devices;
	LIBMTP_device_entry_t *entries;
	int numentries;
#endif

	plugin->shell = shell;

	g_object_get (G_OBJECT (shell),
		     "ui-manager", &uimanager,
		     "removable-media-manager", &rmm,
		     NULL);
		     
	plugin->key_file = NULL;

	/* ui */
	plugin->action_group = gtk_action_group_new ("MTPActions");
	gtk_action_group_set_translation_domain (plugin->action_group,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (plugin->action_group,
				      rb_mtp_plugin_actions, G_N_ELEMENTS (rb_mtp_plugin_actions),
				      plugin);
	gtk_ui_manager_insert_action_group (uimanager, plugin->action_group, 0);
	file = rb_plugin_find_file (bplugin, "mtp-ui.xml");
	plugin->ui_merge_id = gtk_ui_manager_add_ui_from_file (uimanager, file, NULL);
	g_object_unref (G_OBJECT (uimanager));

	/* device detection */
#if defined(HAVE_GUDEV)
	plugin->create_device_source_id =
		g_signal_connect_object (rmm,
					 "create-source-device",
					 G_CALLBACK (create_source_device_cb),
					 plugin,
					 0);

	/* only scan if we're being loaded after the initial scan has been done */
	g_object_get (rmm, "scanned", &rmm_scanned, NULL);
	if (rmm_scanned)
		rb_removable_media_manager_scan (rmm);
#else
	if (rb_mtp_plugin_setup_dbus_hal_connection (plugin) == FALSE) {
		rb_debug ("not scanning for MTP devices because we couldn't get a HAL context");
		g_object_unref (rmm);
		return;
	}

	rb_profile_start ("scanning for MTP devices");
	devices = libhal_get_all_devices (plugin->hal_context, &num, NULL);
	ret = LIBMTP_Get_Supported_Devices_List (&entries, &numentries);
	if (ret == 0) {

		for (i = 0; i < num; i++) {
			int vendor_id;
			int product_id;
			const char *tmpudi;
			int  p;

			tmpudi = devices[i];
			vendor_id = libhal_device_get_property_int (plugin->hal_context, tmpudi, "usb.vendor_id", NULL);
			product_id = libhal_device_get_property_int (plugin->hal_context, tmpudi, "usb.product_id", NULL);
			for (p = 0; p < numentries; p++) {

				if (entries[p].vendor_id == vendor_id && entries[p].product_id == product_id) {
					LIBMTP_mtpdevice_t *device = LIBMTP_Get_First_Device ();
					if (device != NULL) {
						create_source_cb (plugin, device, tmpudi);
						break;
					} else {
						rb_debug ("error, could not get a hold on the device. Reset and Restart");
					}
				}
			}
		}
	} else {
		rb_debug ("Couldn't list mtp devices");
	}

	libhal_free_string_array (devices);
	rb_profile_end ("scanning for MTP devices");

#endif

	g_object_unref (rmm);
}

static void
impl_deactivate (RBPlugin *bplugin, RBShell *shell)
{
	RBMtpPlugin *plugin = RB_MTP_PLUGIN (bplugin);
	GtkUIManager *uimanager = NULL;
	RBRemovableMediaManager *rmm = NULL;

	g_object_get (G_OBJECT (shell),
		      "ui-manager", &uimanager,
		      "removable-media-manager", &rmm,
		      NULL);

	gtk_ui_manager_remove_ui (uimanager, plugin->ui_merge_id);
	gtk_ui_manager_remove_action_group (uimanager, plugin->action_group);

	g_list_foreach (plugin->mtp_sources, (GFunc)rb_source_delete_thyself, NULL);
	g_list_free (plugin->mtp_sources);
	plugin->mtp_sources = NULL;

#if defined(HAVE_GUDEV)
	g_signal_handler_disconnect (rmm, plugin->create_device_source_id);
	plugin->create_device_source_id = 0;
#else
	if (plugin->hal_context != NULL) {
		DBusError error;
		dbus_error_init (&error);
		libhal_ctx_shutdown (plugin->hal_context, &error);
		libhal_ctx_free (plugin->hal_context);
		dbus_error_free (&error);

		plugin->hal_context = NULL;
	}

	if (plugin->dbus_connection != NULL) {
		dbus_connection_unref (plugin->dbus_connection);
		plugin->dbus_connection = NULL;
	}
#endif
	
	if (plugin->key_file) {
		g_key_file_free ( plugin->key_file );
		plugin->key_file = NULL;
	}

	g_object_unref (uimanager);
	g_object_unref (rmm);
}

static void
rb_mtp_plugin_source_deleted (RBMtpSource *source, RBMtpPlugin *plugin)
{
	plugin->mtp_sources = g_list_remove (plugin->mtp_sources, source);
}

#if defined(HAVE_GUDEV)
static RBSource *
create_source_device_cb (RBRemovableMediaManager *rmm, GObject *device, RBMtpPlugin *plugin)
{
	GUdevDeviceNumber device_number;
	int i;
	int num_raw_devices;
	const char *devnum_str;
	int devnum;
	LIBMTP_raw_device_t *raw_devices;

	/* check subsystem == usb? */
	if (g_strcmp0 (g_udev_device_get_subsystem (G_UDEV_DEVICE (device)), "usb") != 0) {
		rb_debug ("this is not a USB device");
		return NULL;
	}

	device_number = g_udev_device_get_device_number (G_UDEV_DEVICE (device));
	if (device_number == 0) {
		rb_debug ("can't get udev device number for this device");
		return NULL;
	}
	/* fun thing: usb device numbers are zero padded, which causes strtol to
	 * interpret them as octal if you don't specify a base.
	 */
	devnum_str = g_udev_device_get_property (G_UDEV_DEVICE (device), "DEVNUM");
	if (devnum_str == NULL) {
		rb_debug ("device doesn't have a USB device number");
		return NULL;
	}
	devnum = strtol (devnum_str, NULL, 10);

	rb_debug ("trying to match device %x (usb device %d) against detected mtp devices",
		  device_number, devnum);

	/* see what devices libmtp can find */
	if (LIBMTP_Detect_Raw_Devices (&raw_devices, &num_raw_devices) == 0) {
		for (i = 0; i < num_raw_devices; i++) {
			LIBMTP_mtpdevice_t *device;
			RBSource *source;

			rb_debug ("detected mtp device: device number %d", raw_devices[i].devnum);

			/* check bus number/device location somehow */
			if (devnum != raw_devices[i].devnum) {
				rb_debug ("device number mismatches: %d vs %d", devnum, raw_devices[i].devnum);
				continue;
			}

			device = LIBMTP_Open_Raw_Device (&raw_devices[i]);
			if (device == NULL) {
				rb_debug ("unable to open device.  weird.");
				break;
			}
			
			GtkAction *sync_action = gtk_action_group_get_action (plugin->action_group, "MTPSourceSync");

			rb_debug ("device matched, creating a source");
			source = rb_mtp_source_new (plugin->shell, device, &plugin->key_file, sync_action);
			plugin->mtp_sources = g_list_prepend (plugin->mtp_sources, source);
			g_signal_connect_object (G_OBJECT (source),
						"deleted", G_CALLBACK (source_deleted_cb),
						plugin, 0);
			return source;
		}
	}

	rb_debug ("device didn't match anything");
	return NULL;
}

#else

static RBSource *
create_source_cb (RBMtpPlugin *plugin, LIBMTP_mtpdevice_t *device, const char *udi)
{
	RBSource *source;

	GtkAction *sync_action = gtk_action_group_get_action (plugin->action_group, "MTPSourceSync");

	source = RB_SOURCE (rb_mtp_source_new (plugin->shell, device, udi, &plugin->key_file, sync_action));

	rb_shell_append_source (plugin->shell, source, NULL);
	plugin->mtp_sources = g_list_prepend (plugin->mtp_sources, source);

	g_signal_connect_object (G_OBJECT (source),
				"deleted", G_CALLBACK (rb_mtp_plugin_source_deleted),
				plugin, 0);

	return source;
}

#endif


static void
rb_mtp_plugin_eject (GtkAction *action, RBMtpPlugin *plugin)
{
	RBSourceList *sourcelist = NULL;
	RBSource *source = NULL;

	g_object_get (G_OBJECT (plugin->shell),
		      "selected-source", &source,
		      NULL);
	if ((source == NULL) || !RB_IS_MTP_SOURCE (source)) {
		g_warning ("got MTPSourceEject action for non-mtp source");
		if (source != NULL)
			g_object_unref (source);
		return;
	}

	g_object_get (plugin->shell, "sourcelist", &sourcelist, NULL);

	rb_source_delete_thyself (source);

	g_object_unref (sourcelist);
	g_object_unref (source);
}

static void
rb_mtp_plugin_sync (GtkAction *action,
			RBMtpPlugin *plugin)
{
	RBSource *source = NULL;

	g_object_get (G_OBJECT (plugin->shell), 
		      "selected-source", &source,
		      NULL);
	if ((source == NULL) || !RB_IS_MTP_SOURCE (source)) {
		g_critical ("got MtpSourceSync action for non-MTP source");
		return;
	}

	rb_media_player_source_sync (RB_MEDIA_PLAYER_SOURCE (source));
	g_object_unref (G_OBJECT (source));
}


static void
rb_mtp_plugin_properties (GtkAction *action,
			  RBMtpPlugin *plugin)
{
	RBSource *source = NULL;

	g_object_get (G_OBJECT (plugin->shell), 
		      "selected-source", &source,
		      NULL);
	if ((source == NULL) || !RB_IS_MTP_SOURCE (source)) {
		g_critical ("got MTPSourceProperties action for non-ipod source");
		return;
	}

	rb_media_player_source_show_properties (RB_MEDIA_PLAYER_SOURCE (source));
	g_object_unref (G_OBJECT (source));
}

static void
rb_mtp_plugin_rename (GtkAction *action, RBMtpPlugin *plugin)
{
	RBSourceList *sourcelist = NULL;
	RBSource *source = NULL;

	g_object_get (G_OBJECT (plugin->shell),
		      "selected-source", &source,
		      NULL);
	if ((source == NULL) || !RB_IS_MTP_SOURCE (source)) {
		g_warning ("got MTPSourceEject action for non-mtp source");
		if (source != NULL)
			g_object_unref (source);
		return;
	}

	g_object_get (plugin->shell, "sourcelist", &sourcelist, NULL);

	rb_sourcelist_edit_source_name (sourcelist, source);

	g_object_unref (sourcelist);
	g_object_unref (source);
}

#if !defined(HAVE_GUDEV)

static void
rb_mtp_plugin_device_added (LibHalContext *context, const char *udi)
{
	RBMtpPlugin *plugin = (RBMtpPlugin *) libhal_ctx_get_user_data (context);
	LIBMTP_device_entry_t *entries;
	int numentries;
	int vendor_id;
	int product_id;
	int ret;

	if (g_list_length (plugin->mtp_sources) > 0) {
		rb_debug ("plugin only supports one device at the time right now.");
		return;
	}

	vendor_id = libhal_device_get_property_int (context, udi, "usb.vendor_id", NULL);
	product_id = libhal_device_get_property_int (context, udi, "usb.product_id", NULL);

	ret = LIBMTP_Get_Supported_Devices_List (&entries, &numentries);
	if (ret == 0) {
		int i, p;

		for (i = 0; i < numentries; i++) {
			if ((entries[i].vendor_id==vendor_id) && (entries[i].product_id == product_id)) {
				/*
				 * FIXME:
				 *
				 * It usualy takes a while for the device to set itself up.
				 * Solving that by trying 10 times with some sleep in between.
				 * There is probably a better solution, but this works.
				 */
				rb_debug ("adding device source");
				for (p = 0; p < 10; p++) {
					LIBMTP_mtpdevice_t *device = LIBMTP_Get_First_Device ();
					if (device != NULL) {
						create_source_cb (plugin, device, udi);
						break;
					}
					usleep (200000);
				}
			}
		}
	}
}

static void
rb_mtp_plugin_device_removed (LibHalContext *context, const char *udi)
{
	RBMtpPlugin *plugin = (RBMtpPlugin *) libhal_ctx_get_user_data (context);
	GList *list = plugin->mtp_sources;
	GList *tmp;

	for (tmp = list; tmp != NULL; tmp = tmp->next) {
		RBSource *source = (RBSource *)tmp->data;
		char *source_udi;

		g_object_get (source, "udi", &source_udi, NULL);
		if (strcmp (udi, source_udi) == 0) {
			rb_debug ("removing device %s, %p", udi, source);
			plugin->mtp_sources = g_list_remove (plugin->mtp_sources, source);
			rb_source_delete_thyself (source);
		}
		g_free (source_udi);
	}
}

static gboolean
rb_mtp_plugin_setup_dbus_hal_connection (RBMtpPlugin *plugin)
{
	DBusError error;

	dbus_error_init (&error);
	plugin->dbus_connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (plugin->dbus_connection == NULL) {
		rb_debug ("error: dbus_bus_get: %s: %s\n", error.name, error.message);
		dbus_error_free (&error);
		return FALSE;
	}

	dbus_connection_setup_with_g_main (plugin->dbus_connection, NULL);

	rb_debug ("connected to: %s", dbus_bus_get_unique_name (plugin->dbus_connection));

	plugin->hal_context = libhal_ctx_new ();
	if (plugin->hal_context == NULL) {
		dbus_error_free (&error);
		return FALSE;
	}
	libhal_ctx_set_dbus_connection (plugin->hal_context, plugin->dbus_connection);

	libhal_ctx_set_user_data (plugin->hal_context, (void *)plugin);
	libhal_ctx_set_device_added (plugin->hal_context, rb_mtp_plugin_device_added);
	libhal_ctx_set_device_removed (plugin->hal_context, rb_mtp_plugin_device_removed);
	libhal_device_property_watch_all (plugin->hal_context, &error);

	if (!libhal_ctx_init (plugin->hal_context, &error)) {
		rb_debug ("error: libhal_ctx_init: %s: %s\n", error.name, error.message);
		dbus_error_free (&error);
		return FALSE;
	}

	dbus_error_free (&error);
	return TRUE;
}

#endif
