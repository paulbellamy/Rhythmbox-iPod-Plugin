/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * heavily based on code from Gedit
 *
 * Copyright (C) 2002-2005 Paolo Maggi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>

#include "rb-plugin.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "eel-gconf-extensions.h"

G_DEFINE_TYPE (RBPlugin, rb_plugin, G_TYPE_OBJECT)
#define RB_PLUGIN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PLUGIN, RBPluginPrivate))

static void rb_plugin_finalise (GObject *o);
static void rb_plugin_set_property (GObject *object,
				    guint prop_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void rb_plugin_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
				    GParamSpec *pspec);

typedef struct {
	char *name;
} RBPluginPrivate;

enum
{
	PROP_0,
	PROP_NAME,
};

static gboolean
is_configurable (RBPlugin *plugin)
{
	return (RB_PLUGIN_GET_CLASS (plugin)->create_configure_dialog != NULL);
}

static void
rb_plugin_class_init (RBPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_plugin_finalise;
	object_class->get_property = rb_plugin_get_property;
	object_class->set_property = rb_plugin_set_property;

	klass->activate = NULL;
	klass->deactivate = NULL;
	klass->create_configure_dialog = NULL;
	klass->is_configurable = is_configurable;

	/* this should be a construction property, but due to the python plugin hack can't be */
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "name",
							      "name",
							      NULL,
							      G_PARAM_READWRITE /*| G_PARAM_CONSTRUCT_ONLY*/));

	g_type_class_add_private (klass, sizeof (RBPluginPrivate));
}

static void
rb_plugin_init (RBPlugin *plugin)
{
	/* Empty */
}

static void
rb_plugin_finalise (GObject *object)
{
	RBPluginPrivate *priv = RB_PLUGIN_GET_PRIVATE (object);

	g_free (priv->name);

	G_OBJECT_CLASS (rb_plugin_parent_class)->finalize (object);
}

static void
rb_plugin_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	RBPluginPrivate *priv = RB_PLUGIN_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_free (priv->name);
		priv->name = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_plugin_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	RBPluginPrivate *priv = RB_PLUGIN_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_plugin_activate (RBPlugin *plugin,
		    RBShell *shell)
{
	g_return_if_fail (RB_IS_PLUGIN (plugin));
	g_return_if_fail (RB_IS_SHELL (shell));

	if (RB_PLUGIN_GET_CLASS (plugin)->activate)
		RB_PLUGIN_GET_CLASS (plugin)->activate (plugin, shell);
}

void
rb_plugin_deactivate	(RBPlugin *plugin,
			 RBShell *shell)
{
	g_return_if_fail (RB_IS_PLUGIN (plugin));
	g_return_if_fail (RB_IS_SHELL (shell));

	if (RB_PLUGIN_GET_CLASS (plugin)->deactivate)
		RB_PLUGIN_GET_CLASS (plugin)->deactivate (plugin, shell);
}

gboolean
rb_plugin_is_configurable (RBPlugin *plugin)
{
	g_return_val_if_fail (RB_IS_PLUGIN (plugin), FALSE);

	return RB_PLUGIN_GET_CLASS (plugin)->is_configurable (plugin);
}

GtkWidget *
rb_plugin_create_configure_dialog (RBPlugin *plugin)
{
	g_return_val_if_fail (RB_IS_PLUGIN (plugin), NULL);

	if (RB_PLUGIN_GET_CLASS (plugin)->create_configure_dialog)
		return RB_PLUGIN_GET_CLASS (plugin)->create_configure_dialog (plugin);
	else
		return NULL;
}

#define UNINSTALLED_PLUGINS_LOCATION "plugins"

GList *
rb_get_plugin_paths (void)
{
	GList *paths;
	char  *path;

	paths = NULL;

	if (!eel_gconf_get_boolean (CONF_PLUGIN_DISABLE_USER)) {
		/* deprecated path, should be removed some time in the future */
		path = g_build_filename (rb_dot_dir (), "plugins", NULL);
		paths = g_list_prepend (paths, path);

		path = g_build_filename (rb_user_data_dir (), "plugins", NULL);
		paths = g_list_prepend (paths, path);
	}

#ifdef USE_UNINSTALLED_DIRS
	path = g_build_filename (UNINSTALLED_PLUGINS_LOCATION, NULL);
	paths = g_list_prepend (paths, path);
	path = g_build_filename ("..", UNINSTALLED_PLUGINS_LOCATION, NULL);
	paths = g_list_prepend (paths, path);
#endif

	path = g_strdup (RB_PLUGIN_DIR);
	paths = g_list_prepend (paths, path);

	paths = g_list_reverse (paths);

	return paths;
}


char *
rb_plugin_find_file (RBPlugin *plugin,
		     const char *file)
{
	RBPluginPrivate *priv = RB_PLUGIN_GET_PRIVATE (plugin);
	GList *paths;
	GList *l;
	char *ret = NULL;

	paths = rb_get_plugin_paths ();

	for (l = paths; l != NULL; l = l->next) {
		if (ret == NULL && priv->name) {
			char *tmp;

			tmp = g_build_filename (l->data, priv->name, file, NULL);

			if (g_file_test (tmp, G_FILE_TEST_EXISTS)) {
				ret = tmp;
				break;
			}
			g_free (tmp);
		}
	}

	g_list_foreach (paths, (GFunc)g_free, NULL);
	g_list_free (paths);

	/* global data files */
	if (ret == NULL) {
		const char *f;

		f = rb_file (file);
		if (f)
			ret = g_strdup (f);
	}

	rb_debug ("found '%s' when searching for file '%s' for plugin '%s'",
		  ret, file, priv->name);

	/* ensure it's an absolute path, so doesn't confuse rb_builder_load et al */
	if (ret != NULL && ret[0] != '/') {
		char *pwd = g_get_current_dir ();
		char *path = g_strconcat (pwd, G_DIR_SEPARATOR_S, ret, NULL);
		g_free (ret);
		g_free (pwd);
		ret = path;
	}
	return ret;
}
