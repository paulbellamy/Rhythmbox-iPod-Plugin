/*
 *  arch-tag: Implementation of mtp source object
 *
 *  Copyright (C) 2006 Peter Grundström  <pete@openfestis.org>
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

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gst/gst.h>

#include "rhythmdb.h"
#include "eel-gconf-extensions.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-plugin.h"
#include "rb-builder-helpers.h"
#include "rb-removable-media-manager.h"
#include "rb-static-playlist-source.h"
#include "rb-util.h"
#include "rb-refstring.h"
#include "rhythmdb.h"
#include "rb-encoder.h"
#include "rb-dialog.h"
#include "rb-shell-player.h"
#include "rb-player.h"
#include "rb-encoder.h"

#include "rb-mtp-source.h"

#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/mtp/paned_position"
#define CONF_STATE_SHOW_BROWSER   CONF_PREFIX "/state/mtp/show_browser"

static GObject *rb_mtp_source_constructor (GType type,
					   guint n_construct_properties,
					   GObjectConstructParam *construct_properties);
static void rb_mtp_source_dispose (GObject *object);
static void rb_mtp_source_finalize (GObject *object);

static void rb_mtp_source_set_property (GObject *object,
			                guint prop_id,
			                const GValue *value,
			                GParamSpec *pspec);
static void rb_mtp_source_get_property (GObject *object,
			                guint prop_id,
			                GValue *value,
			                GParamSpec *pspec);
static char *impl_get_browser_key (RBSource *source);
static char *impl_get_paned_key (RBBrowserSource *source);

static void rb_mtp_source_load_tracks (RBMtpSource*);

static void impl_delete (RBSource *asource);
static gboolean impl_show_popup (RBSource *source);
static GList* impl_get_ui_actions (RBSource *source);

static GList * impl_get_mime_types (RBRemovableMediaSource *source);
static gboolean impl_track_added (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *dest,
				  guint64 filesize,
				  const char *mimetype);
static char* impl_build_dest_uri (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *mimetype,
				  const char *extension);

static RhythmDB * get_db_for_source (RBMtpSource *source);
static void artwork_notify_cb (RhythmDB *db,
			       RhythmDBEntry *entry,
			       const char *property_name,
			       const GValue *metadata,
			       RBMtpSource *source);

static void rb_mtp_info_response_cb (GtkDialog *dialog,
				     int response_id,
				     RBMtpSource *source);

static void rb_mtp_sync_auto_changed_cb (GtkToggleButton *togglebutton,
					 gpointer         user_data);

static void rb_mtp_sync_music_all_changed_cb (GtkToggleButton *togglebutton,
					      gpointer         user_data);

static void rb_mtp_sync_podcasts_all_changed_cb (GtkToggleButton *togglebutton,
						 gpointer         user_data);
				      

static GHashTable *	impl_get_entries	(RBMediaPlayerSource *source);
static GHashTable *	impl_get_podcasts	(RBMediaPlayerSource *source);
static guint64		impl_get_capacity	(RBMediaPlayerSource *source);
static guint64		impl_get_free_space	(RBMediaPlayerSource *source);
static void		impl_add_entries	(RBMediaPlayerSource *source, GList *entries);
static void		impl_trash_entry	(RBMediaPlayerSource *source, RhythmDBEntry *entry);
static void		impl_trash_entries	(RBMediaPlayerSource *source, GList *entries);
static void		impl_add_playlist	(RBMediaPlayerSource *source, gchar *name, GList *entries);
static void		impl_trash_playlist	(RBMediaPlayerSource *source, gchar *name);
static gchar *		impl_get_serial		(RBMediaPlayerSource *source);
static gchar *		impl_get_name		(RBMediaPlayerSource *source);
static void		impl_show_properties	(RBMediaPlayerSource *source, RBMediaPlayerPrefs *prefs);

static void add_to_playlist (RBMtpSource *source, RhythmDBEntry *entry, RBSource *playlist);

static void add_track_to_album (RBMtpSource *source, const char *album_name, LIBMTP_track_t *track);

static void prepare_player_source_cb (RBPlayer *player,
				      const char *stream_uri,
				      GstElement *src,
				      RBMtpSource *source);
static void prepare_encoder_source_cb (RBEncoderFactory *factory,
				       const char *stream_uri,
				       GObject *src,
				       RBMtpSource *source);

typedef struct
{
	LIBMTP_mtpdevice_t *device;
	GHashTable *entry_map;
	GHashTable *album_map;
	GHashTable *artwork_request_map;
#if !defined(HAVE_GUDEV)
	char *udi;
#endif
	uint16_t supported_types[LIBMTP_FILETYPE_UNKNOWN+1];
	GList *mediatypes;
	gboolean album_art_supported;

	guint load_songs_idle_id;
} RBMtpSourcePrivate;

RB_PLUGIN_DEFINE_TYPE(RBMtpSource,
		       rb_mtp_source,
		       RB_TYPE_MEDIA_PLAYER_SOURCE)

#define MTP_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MTP_SOURCE, RBMtpSourcePrivate))

enum
{
	PROP_0,
	PROP_LIBMTP_DEVICE,
	PROP_UDI,
};

static void
report_libmtp_errors (LIBMTP_mtpdevice_t *device, gboolean use_dialog)
{
	LIBMTP_error_t *stack;

	for (stack = LIBMTP_Get_Errorstack (device); stack != NULL; stack = stack->next) {
		if (use_dialog) {
			rb_error_dialog (NULL, _("Media player device error"), "%s", stack->error_text);

			/* only display one dialog box per error */
			use_dialog = FALSE;
		} else {
			g_warning ("libmtp error: %s", stack->error_text);
		}
	}

	LIBMTP_Clear_Errorstack (device);
}

static void
rb_mtp_source_class_init (RBMtpSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBRemovableMediaSourceClass *rms_class = RB_REMOVABLE_MEDIA_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);
	RBMediaPlayerSourceClass *mps_class = RB_MEDIA_PLAYER_SOURCE_CLASS (klass);

	object_class->constructor = rb_mtp_source_constructor;
	object_class->dispose = rb_mtp_source_dispose;
	object_class->finalize = rb_mtp_source_finalize;
	object_class->set_property = rb_mtp_source_set_property;
	object_class->get_property = rb_mtp_source_get_property;

	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_get_browser_key = impl_get_browser_key;

	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_paste = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;

	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_get_ui_actions = impl_get_ui_actions;
	source_class->impl_delete = impl_delete;

	browser_source_class->impl_get_paned_key = impl_get_paned_key;

	rms_class->impl_track_added = impl_track_added;
	rms_class->impl_build_dest_uri = impl_build_dest_uri;
	rms_class->impl_get_mime_types = impl_get_mime_types;
	rms_class->impl_should_paste = rb_removable_media_source_should_paste_no_duplicate;
	
	mps_class->impl_get_entries = impl_get_entries;
	mps_class->impl_get_podcasts = impl_get_podcasts;
	mps_class->impl_get_capacity = impl_get_capacity;
	mps_class->impl_get_free_space = impl_get_free_space;
	mps_class->impl_add_entries = impl_add_entries;
	mps_class->impl_trash_entries = impl_trash_entries;
	mps_class->impl_add_playlist = impl_add_playlist;
	mps_class->impl_trash_playlist = impl_trash_playlist;
	mps_class->impl_get_serial = impl_get_serial;
	mps_class->impl_get_name = impl_get_name;
	mps_class->impl_show_properties = impl_show_properties;

	g_object_class_install_property (object_class,
					 PROP_LIBMTP_DEVICE,
					 g_param_spec_pointer ("libmtp-device",
							       "libmtp-device",
							       "libmtp device",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
#if !defined(HAVE_GUDEV)
	g_object_class_install_property (object_class,
					 PROP_UDI,
					 g_param_spec_string ("udi",
						 	      "udi",
							      "udi",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
#endif

	g_type_class_add_private (klass, sizeof (RBMtpSourcePrivate));
}

static void
rb_mtp_source_name_changed_cb (RBMtpSource *source,
			       GParamSpec *spec,
			       gpointer data)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	char *name = NULL;

	g_object_get (source, "name", &name, NULL);
	if (LIBMTP_Set_Friendlyname (priv->device, name) != 0) {
		report_libmtp_errors (priv->device, TRUE);
	}
	g_free (name);
}

static void
rb_mtp_source_init (RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);

	priv->entry_map = g_hash_table_new_full (g_direct_hash,
						 g_direct_equal,
						 NULL,
						 (GDestroyNotify) LIBMTP_destroy_track_t);
	priv->album_map = g_hash_table_new_full (g_str_hash,
						 g_str_equal,
						 NULL,
						 (GDestroyNotify) LIBMTP_destroy_album_t);
	priv->artwork_request_map = g_hash_table_new (g_direct_hash, g_direct_equal);
	
	rb_media_player_source_load (RB_MEDIA_PLAYER_SOURCE (source));
}

static GObject *
rb_mtp_source_constructor (GType type, guint n_construct_properties,
			   GObjectConstructParam *construct_properties)
{
	RBMtpSource *source;
	RBMtpSourcePrivate *priv;
	RBEntryView *tracks;
	RBShell *shell;
	RBShellPlayer *shell_player;
	GObject *player_backend;
	GtkIconTheme *theme;
	GdkPixbuf *pixbuf;
	gint size;
	guint16 *types = NULL;
	guint16 num_types= 0;

	source = RB_MTP_SOURCE (G_OBJECT_CLASS (rb_mtp_source_parent_class)->
				constructor (type, n_construct_properties, construct_properties));

	priv = MTP_SOURCE_GET_PRIVATE (source);

	tracks = rb_source_get_entry_view (RB_SOURCE (source));
	rb_entry_view_append_column (tracks, RB_ENTRY_VIEW_COL_RATING, FALSE);
	rb_entry_view_append_column (tracks, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);

	/* the source element needs our cooperation */
	g_object_get (source, "shell", &shell, NULL);
	shell_player = RB_SHELL_PLAYER (rb_shell_get_player (shell));
	g_object_get (shell_player, "player", &player_backend, NULL);

	g_signal_connect_object (player_backend,
				 "prepare-source",
				 G_CALLBACK (prepare_player_source_cb),
				 source, 0);

	g_object_unref (player_backend);
	g_object_unref (shell);

	g_signal_connect_object (rb_encoder_factory_get (),
				 "prepare-source",
				 G_CALLBACK (prepare_encoder_source_cb),
				 source, 0);

	/* icon */
	theme = gtk_icon_theme_get_default ();
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	pixbuf = gtk_icon_theme_load_icon (theme, "multimedia-player", size, 0, NULL);

	rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
	g_object_unref (pixbuf);

	g_signal_connect (G_OBJECT (source), "notify::name",
			  (GCallback)rb_mtp_source_name_changed_cb, NULL);
	
	/* figure out supported file types */
	if (LIBMTP_Get_Supported_Filetypes(priv->device, &types, &num_types) == 0) {
		int i;
		gboolean has_mp3 = FALSE;
		for (i = 0; i < num_types; i++) {
			const char *mediatype;

			if (i <= LIBMTP_FILETYPE_UNKNOWN) {
				priv->supported_types[types[i]] = 1;
			}

			/* this has to work with the remapping done in 
			 * rb-removable-media-source.c:impl_paste.
			 */
			switch (types[i]) {
			case LIBMTP_FILETYPE_WAV:
				mediatype = "audio/x-wav";
				break;
			case LIBMTP_FILETYPE_MP3:
				/* special handling for mp3: always put it at the front of the list
				 * if it's supported.
				 */
				has_mp3 = TRUE;
				mediatype = NULL;
				break;
			case LIBMTP_FILETYPE_WMA:
				mediatype = "audio/x-ms-wma";
				break;
			case LIBMTP_FILETYPE_OGG:
				mediatype = "application/ogg";
				break;
			case LIBMTP_FILETYPE_MP4:
			case LIBMTP_FILETYPE_M4A:
			case LIBMTP_FILETYPE_AAC:
				mediatype = "audio/aac";
				break;
			case LIBMTP_FILETYPE_WMV:
				mediatype = "audio/x-ms-wmv";
				break;
			case LIBMTP_FILETYPE_ASF:
				mediatype = "video/x-ms-asf";
				break;
			case LIBMTP_FILETYPE_FLAC:
				mediatype = "audio/flac";
				break;

			case LIBMTP_FILETYPE_JPEG:
				rb_debug ("JPEG (album art) supported");
				mediatype = NULL;
				priv->album_art_supported = TRUE;
				break;

			default:
				rb_debug ("unknown libmtp filetype %s supported", LIBMTP_Get_Filetype_Description (types[i]));
				mediatype = NULL;
				break;
			}

			if (mediatype != NULL) {
				rb_debug ("media type %s supported", mediatype);
				priv->mediatypes = g_list_prepend (priv->mediatypes,
								   g_strdup (mediatype));
			}
		}

		if (has_mp3) {
			rb_debug ("audio/mpeg supported");
			priv->mediatypes = g_list_prepend (priv->mediatypes, g_strdup ("audio/mpeg"));
		}
	} else {
		report_libmtp_errors (priv->device, FALSE);
	}
	
	if (priv->album_art_supported) {
		RhythmDB *db;

		db = get_db_for_source (source);
		g_signal_connect_object (db, "entry-extra-metadata-notify::rb:coverArt",
					 G_CALLBACK (artwork_notify_cb), source, 0);
		g_object_unref (db);
	}

	rb_mtp_source_load_tracks (source);

	return G_OBJECT (source);
}

static void
rb_mtp_source_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_LIBMTP_DEVICE:
		priv->device = g_value_get_pointer (value);
		break;
#if !defined(HAVE_GUDEV)
	case PROP_UDI:
		priv->udi = g_value_dup_string (value);
		break;
#endif
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_mtp_source_get_property (GObject *object,
			    guint prop_id,
			    GValue *value,
			    GParamSpec *pspec)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_LIBMTP_DEVICE:
		g_value_set_pointer (value, priv->device);
		break;
#if !defined(HAVE_GUDEV)
	case PROP_UDI:
		g_value_set_string (value, priv->udi);
		break;
#endif
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_mtp_source_dispose (GObject *object)
{
	RBMtpSource *source = RB_MTP_SOURCE (object);
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	RhythmDBEntryType entry_type;
	RhythmDB *db;

	db = get_db_for_source (source);

	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	rhythmdb_entry_delete_by_type (db, entry_type);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	rhythmdb_commit (db);
	g_object_unref (db);

	if (priv->load_songs_idle_id != 0) {
		g_source_remove (priv->load_songs_idle_id);
		priv->load_songs_idle_id = 0;
	}

	G_OBJECT_CLASS (rb_mtp_source_parent_class)->dispose (object);
}

static void
rb_mtp_source_finalize (GObject *object)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (object);

	g_hash_table_destroy (priv->entry_map);
	g_hash_table_destroy (priv->album_map);
	g_hash_table_destroy (priv->artwork_request_map);

#if !defined(HAVE_GUDEV)
	g_free (priv->udi);
#endif

	LIBMTP_Release_Device (priv->device);

	G_OBJECT_CLASS (rb_mtp_source_parent_class)->finalize (object);
}

static char *
impl_get_browser_key (RBSource *source)
{
	return g_strdup (CONF_STATE_SHOW_BROWSER);
}

static char *
impl_get_paned_key (RBBrowserSource *source)
{
	return g_strdup (CONF_STATE_PANED_POSITION);
}

#if defined(HAVE_GUDEV)
RBSource *
rb_mtp_source_new (RBShell *shell,
		   LIBMTP_mtpdevice_t *device,
		   GKeyFile **key_file)
#else
RBSource *
rb_mtp_source_new (RBShell *shell,
		   LIBMTP_mtpdevice_t *device,
		   const char *udi,
		   GKeyFile **key_file)
#endif
{
	RBMtpSource *source = NULL;
	RhythmDBEntryType entry_type;
	RhythmDB *db = NULL;
	char *name = NULL;

	g_object_get (shell, "db", &db, NULL);
	name = g_strdup_printf ("MTP-%s", LIBMTP_Get_Serialnumber (device));

	entry_type = rhythmdb_entry_register_type (db, name);
	entry_type->save_to_disk = FALSE;
	entry_type->category = RHYTHMDB_ENTRY_NORMAL;

	g_free (name);
	g_object_unref (db);

	source = RB_MTP_SOURCE (g_object_new (RB_TYPE_MTP_SOURCE,
					      "entry-type", entry_type,
					      "shell", shell,
					      "visibility", TRUE,
					      "volume", NULL,
					      "source-group", RB_SOURCE_GROUP_DEVICES,
					      "libmtp-device", device,
#if !defined(HAVE_GUDEV)
					      "udi", udi,
#endif
					      "key-file", key_file,
					      NULL));

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_SOURCE (source);
}

static void
entry_set_string_prop (RhythmDB *db,
		       RhythmDBEntry *entry,
		       RhythmDBPropType propid,
		       const char *str)
{
	GValue value = {0,};

	if (!str)
		str = _("Unknown");

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_static_string (&value, str);
	rhythmdb_entry_set (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}

static void
add_mtp_track_to_db (RBMtpSource *source,
		     RhythmDB *db,
		     LIBMTP_track_t *track)
{
	RhythmDBEntry *entry = NULL;
	RhythmDBEntryType entry_type;
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	char *name = NULL;

	/* ignore everything except audio (allow audio/video types too, since they're probably pretty common) */
	if (!(LIBMTP_FILETYPE_IS_AUDIO (track->filetype) || LIBMTP_FILETYPE_IS_AUDIOVIDEO (track->filetype))) {
		rb_debug ("ignoring non-audio item %d (filetype %s)",
			  track->item_id,
			  LIBMTP_Get_Filetype_Description (track->filetype));
		return;
	}

	/* Set URI */
	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	name = g_strdup_printf ("xrbmtp://%i/%s", track->item_id, track->filename);
	entry = rhythmdb_entry_new (RHYTHMDB (db), entry_type, name);
	g_free (name);
        g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	if (entry == NULL) {
		rb_debug ("cannot create entry %i", track->item_id);
		g_object_unref (G_OBJECT (db));
		return;
	}

	/* Set track number */
	if (track->tracknumber != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, track->tracknumber);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
				    RHYTHMDB_PROP_TRACK_NUMBER,
				    &value);
		g_value_unset (&value);
	}

	/* Set length */
	if (track->duration != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, track->duration/1000);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
				    RHYTHMDB_PROP_DURATION,
				    &value);
		g_value_unset (&value);
	}

	/* Set file size */
	if (track->filesize != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_UINT64);
		g_value_set_uint64 (&value, track->filesize);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
				    RHYTHMDB_PROP_FILE_SIZE,
				    &value);
		g_value_unset (&value);
	}

	/* Set playcount */
	if (track->usecount != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, track->usecount);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_PLAY_COUNT,
					       &value);
		g_value_unset (&value);
	}
	/* Set rating */
	if (track->rating != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_DOUBLE);
		g_value_set_double (&value, track->rating/20);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_RATING,
					       &value);
		g_value_unset (&value);
	}

	/* Set title */
	entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_TITLE, track->title);

	/* Set album, artist and genre from MTP */
	entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_ARTIST, track->artist);
	entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_ALBUM, track->album);
	entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_GENRE, track->genre);

	g_hash_table_insert (priv->entry_map, entry, track);
	rhythmdb_commit (RHYTHMDB (db));
}

static gboolean
load_mtp_db_idle_cb (RBMtpSource* source)
{
	RhythmDB *db = NULL;
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	LIBMTP_track_t *tracks = NULL;
	LIBMTP_album_t *albums;
	gboolean device_forgets_albums = TRUE;

	db = get_db_for_source (source);

	g_assert (db != NULL);

	albums = LIBMTP_Get_Album_List (priv->device);
	report_libmtp_errors (priv->device, FALSE);
	if (albums != NULL) {
		LIBMTP_album_t *album;

		for (album = albums; album != NULL; album = album->next) {
			if (album->name == NULL)
				continue;

			rb_debug ("album: %s, %d tracks", album->name, album->no_tracks);
			g_hash_table_insert (priv->album_map, album->name, album);
			if (album->no_tracks != 0) {
				device_forgets_albums = FALSE;
			}
		}

		if (device_forgets_albums) {
			rb_debug ("stupid mtp device detected.  will rebuild all albums.");
		}
	} else {
		rb_debug ("No albums");
		device_forgets_albums = FALSE;
	}

	tracks = LIBMTP_Get_Tracklisting_With_Callback (priv->device, NULL, NULL);
	report_libmtp_errors (priv->device, FALSE);
	if (tracks != NULL) {
		LIBMTP_track_t *track;
		for (track = tracks; track != NULL; track = track->next) {
			add_mtp_track_to_db (source, db, track);

			if (device_forgets_albums && track->album != NULL) {
				add_track_to_album (source, track->album, track);
			}
		}
	} else {
		rb_debug ("No tracks");
	}

	/* for stupid devices, remove any albums left with no tracks */
	if (device_forgets_albums) {
		GHashTableIter iter;
		gpointer value;
		LIBMTP_album_t *album;

		g_hash_table_iter_init (&iter, priv->album_map);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			int ret;

			album = value;
			if (album->no_tracks == 0) {
				rb_debug ("pruning empty album \"%s\"", album->name); 
				ret = LIBMTP_Delete_Object (priv->device, album->album_id);
				if (ret != 0) {
					report_libmtp_errors (priv->device, FALSE);
				}
				g_hash_table_iter_remove (&iter);
			}
		}
	}


	g_object_unref (G_OBJECT (db));
	return FALSE;
}

static void
rb_mtp_source_load_tracks (RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	char *name = NULL;

	name = impl_get_name (RB_MEDIA_PLAYER_SOURCE (source));

	g_object_set (RB_SOURCE (source),
		      "name", name,
		      NULL);

	priv->load_songs_idle_id = g_idle_add ((GSourceFunc)load_mtp_db_idle_cb, source);
	g_free (name);
}

static char *
gdate_to_char (GDate* date)
{
	return g_strdup_printf ("%04i%02i%02iT0000.0",
				g_date_get_year (date),
				g_date_get_month (date),
				g_date_get_day (date));
}

static LIBMTP_filetype_t
mimetype_to_filetype (RBMtpSource *source, const char *mimetype)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);

	if (!strcmp (mimetype, "audio/mpeg") || !strcmp (mimetype, "application/x-id3")) {
		return LIBMTP_FILETYPE_MP3;
	}  else if (!strcmp (mimetype, "audio/x-wav")) {
		return  LIBMTP_FILETYPE_WAV;
	} else if (!strcmp (mimetype, "application/ogg")) {
		return LIBMTP_FILETYPE_OGG;
	} else if (!strcmp (mimetype, "audio/x-m4a") || !strcmp (mimetype, "video/quicktime")) {
		/* try a few different filetypes that might work */
		if (priv->supported_types[LIBMTP_FILETYPE_M4A])
			return LIBMTP_FILETYPE_M4A;
		else if (!priv->supported_types[LIBMTP_FILETYPE_AAC] && priv->supported_types[LIBMTP_FILETYPE_MP4])
			return LIBMTP_FILETYPE_MP4;
		else
			return LIBMTP_FILETYPE_AAC;

	} else if (!strcmp (mimetype, "audio/x-ms-wma") || !strcmp (mimetype, "audio/x-ms-asf")) {
		return LIBMTP_FILETYPE_WMA;
	} else if (!strcmp (mimetype, "video/x-ms-asf")) {
		return LIBMTP_FILETYPE_ASF;
	} else if (!strcmp (mimetype, "audio/x-flac")) {
		return LIBMTP_FILETYPE_FLAC;
	} else {
		rb_debug ("\"%s\" is not a supported mimetype", mimetype);
		return LIBMTP_FILETYPE_UNKNOWN;
	}
}

static void
add_track_to_album (RBMtpSource *source, const char *album_name, LIBMTP_track_t *track)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	LIBMTP_album_t *album;

	album = g_hash_table_lookup (priv->album_map, album_name);
	if (album != NULL) {
		/* add track to album */

		album->tracks = realloc (album->tracks, sizeof(uint32_t) * (album->no_tracks+1));
		album->tracks[album->no_tracks] = track->item_id;
		album->no_tracks++;
		rb_debug ("adding track ID %d to album ID %d; now %d tracks",
			  track->item_id,
			  album->album_id,
			  album->no_tracks);

		if (LIBMTP_Update_Album (priv->device, album) != 0) {
			rb_debug ("LIBMTP_Update_Album failed..");
			report_libmtp_errors (priv->device, FALSE);
		}
	} else {
		/* add new album */
		album = LIBMTP_new_album_t ();
		album->name = strdup (album_name);
		album->no_tracks = 1;
		album->tracks = malloc (sizeof(uint32_t));
		album->tracks[0] = track->item_id;
		album->storage_id = track->storage_id;

		/* fill in artist and genre? */

		rb_debug ("creating new album (%s) for track ID %d", album->name, track->item_id);

		if (LIBMTP_Create_New_Album (priv->device, album) != 0) {
			LIBMTP_destroy_album_t (album);
			rb_debug ("LIBMTP_Create_New_Album failed..");
			report_libmtp_errors (priv->device, FALSE);
		} else {
			g_hash_table_insert (priv->album_map, album->name, album);
		}
	}
}

static void
remove_track_from_album (RBMtpSource *source, const char *album_name, LIBMTP_track_t *track)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	LIBMTP_album_t *album;
	int i;

	album = g_hash_table_lookup (priv->album_map, album_name);
	if (album == NULL) {
		rb_debug ("Couldn't find an album for %s", album_name);
		return;
	}

	for (i = 0; i < album->no_tracks; i++) {
		if (album->tracks[i] == track->item_id) {
			break;
		}
	}

	if (i == album->no_tracks) {
		rb_debug ("Couldn't find track %d in album %d", track->item_id, album->album_id);
		return;
	}

	memmove (album->tracks + i, album->tracks + i + 1, album->no_tracks - (i+1));
	album->no_tracks--;

	if (album->no_tracks == 0) {
		rb_debug ("deleting empty album %d", album->album_id);
		if (LIBMTP_Delete_Object (priv->device, album->album_id) != 0) {
			report_libmtp_errors (priv->device, FALSE);
		}
		g_hash_table_remove (priv->album_map, album_name);
	} else {
		rb_debug ("updating album %d: %d tracks remaining", album->album_id, album->no_tracks);
		if (LIBMTP_Update_Album (priv->device, album) != 0) {
			report_libmtp_errors (priv->device, FALSE);
		}
	}
}

static void
impl_delete (RBSource *source)
{
	GList *sel;
	RBEntryView *tracks;

	tracks = rb_source_get_entry_view (source);
	sel = rb_entry_view_get_selected_entries (tracks);
	
	impl_trash_entries (RB_MEDIA_PLAYER_SOURCE (source), sel);

	g_list_free (sel);
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/MTPSourcePopup");
	return TRUE;
}

static GList *
impl_get_ui_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("MTPSourceEject"));
	actions = g_list_prepend (actions, g_strdup ("MTPSourceSync"));

	return actions;
}

static RhythmDB *
get_db_for_source (RBMtpSource *source)
{
	RBShell *shell = NULL;
	RhythmDB *db = NULL;

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	return db;
}

static LIBMTP_track_t *
transfer_track (RBMtpSource *source,
		LIBMTP_mtpdevice_t *device,
		RhythmDBEntry *entry,
		const char *filename,
		guint64 filesize,
		const char *mimetype)
{
	LIBMTP_track_t *trackmeta = LIBMTP_new_track_t ();
	GDate d;
	int ret;

	trackmeta->title = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE);
	trackmeta->album = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ALBUM);
	trackmeta->artist = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST);
	trackmeta->genre = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_GENRE);
	trackmeta->filename = g_path_get_basename (filename);

	if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE) > 0) { /* Entries without a date returns 0, g_date_set_julian don't accept that */
		g_date_set_julian (&d, rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE));
		trackmeta->date	= gdate_to_char (&d);
	}
	trackmeta->tracknumber = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER);
	trackmeta->duration = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION) * 1000;
	trackmeta->rating = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_RATING) * 20;
	trackmeta->usecount = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_PLAY_COUNT);
	trackmeta->filesize = filesize;
	if (mimetype == NULL) {
		mimetype = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);
	}
	trackmeta->filetype = mimetype_to_filetype (source, mimetype);
	rb_debug ("using libmtp filetype %d (%s) for source media type %s",
		  trackmeta->filetype,
		  LIBMTP_Get_Filetype_Description (trackmeta->filetype),
		  mimetype);

	ret = LIBMTP_Send_Track_From_File (device, filename, trackmeta, NULL, NULL);
	rb_debug ("LIBMTP_Send_Track_From_File (%s) returned %d", filename, ret);
	if (ret != 0) {
		report_libmtp_errors (device, TRUE);
		LIBMTP_destroy_track_t (trackmeta);
		return NULL;
	}

	if (strcmp (trackmeta->album, _("Unknown")) != 0) {
		add_track_to_album (source, trackmeta->album, trackmeta);
	}

	return trackmeta;
}

static GList *
impl_get_mime_types (RBRemovableMediaSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	return rb_string_list_copy (priv->mediatypes);
}

static gboolean
impl_track_added (RBRemovableMediaSource *isource,
		  RhythmDBEntry *entry,
		  const char *dest,
		  guint64 filesize,
		  const char *mimetype)
{
	RBMtpSource *source = RB_MTP_SOURCE (isource);
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	GFile *file;
	char *path;
	LIBMTP_track_t *track = NULL;

	file = g_file_new_for_uri (dest);
	path = g_file_get_path (file);
	track = transfer_track (source, priv->device, entry, path, filesize, mimetype);
	g_free (path);

	g_file_delete (file, NULL, NULL);

	if (track != NULL) {
		RhythmDB *db = get_db_for_source (source);

		if (priv->album_art_supported) {
			const char *album;

			album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
			if (g_hash_table_lookup (priv->artwork_request_map, album) == NULL) {
				GValue *metadata;

				rb_debug ("requesting cover art image for album %s", album);
				g_hash_table_insert (priv->artwork_request_map, (gpointer) album, GINT_TO_POINTER (1));
				metadata = rhythmdb_entry_request_extra_metadata (db, entry, "rb:coverArt");
				if (metadata) {
					artwork_notify_cb (db, entry, "rb:coverArt", metadata, source);
					g_value_unset (metadata);
					g_free (metadata);
				}
			}
		}

		add_mtp_track_to_db (source, db, track);
		g_object_unref (db);
	}

	return FALSE;
}

static char *
impl_build_dest_uri (RBRemovableMediaSource *source,
		     RhythmDBEntry *entry,
		     const char *mimetype,
		     const char *extension)
{
	char* file = g_strdup_printf ("%s/%s-%s.%s", g_get_tmp_dir (),
				      rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST),
				      rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE),
				      extension);
	char* uri = g_filename_to_uri (file, NULL, NULL);
	g_free (file);
	return uri;
}

static void
artwork_notify_cb (RhythmDB *db,
		   RhythmDBEntry *entry,
		   const char *property_name,
		   const GValue *metadata,
		   RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	GdkPixbuf *pixbuf;
	LIBMTP_album_t *album;
	LIBMTP_filesampledata_t *albumart;
	GError *error = NULL;
	const char *album_name;
	char *image_data;
	gsize image_size;
	int ret;

	album_name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);

	/* check if we're looking for art for this entry, and if we actually got some */
	if (g_hash_table_remove (priv->artwork_request_map, album_name) == FALSE)
		return;

	if (G_VALUE_HOLDS (metadata, GDK_TYPE_PIXBUF) == FALSE)
		return;

	pixbuf = GDK_PIXBUF (g_value_get_object (metadata));

	/* we should already have created an album object */
	album = g_hash_table_lookup (priv->album_map, album_name);
	if (album == NULL) {
		rb_debug ("couldn't find an album for %s", album_name);
		return;
	}

	/* probably should scale the image down, since some devices have a size limit and they all have
	 * tiny displays anyway.
	 */

	if (gdk_pixbuf_save_to_buffer (pixbuf, &image_data, &image_size, "jpeg", &error, NULL) == FALSE) {
		rb_debug ("unable to convert album art image to a JPEG buffer: %s", error->message);
		g_error_free (error);
		return;
	}

	albumart = LIBMTP_new_filesampledata_t ();
	albumart->filetype = LIBMTP_FILETYPE_JPEG;
	albumart->data = image_data;
	albumart->size = image_size;

	ret = LIBMTP_Send_Representative_Sample (priv->device, album->album_id, albumart);
	if (ret != 0) {
		report_libmtp_errors (priv->device, TRUE);
	} else {
		rb_debug ("successfully set album art for %s (%" G_GSIZE_FORMAT " bytes)", album_name, image_size);
	}

	/* libmtp will try to free this if we don't clear the pointer */
	albumart->data = NULL;
	LIBMTP_destroy_filesampledata_t (albumart);
	g_free (image_data);
}



static GHashTable *
impl_get_entries	(RBMediaPlayerSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	GHashTable *result = g_hash_table_new (g_str_hash, g_str_equal);
	GHashTableIter iter;
	gpointer key, value;
	
	g_hash_table_iter_init (&iter, priv->entry_map);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		/* FIXME: Just checks if the genre is "Podcast".  Sort of hackish,
		 * But, because MTP players don't have a uniform way of handling
		 * Podcasts it is the best we can do for now
		 */
		if (strcmp (((LIBMTP_track_t *)value)->genre, "Podcast") != 0)
			g_hash_table_insert (result, rb_media_player_source_track_uuid (key), key);
	}
	
	return result;
}

static GHashTable *
impl_get_podcasts	(RBMediaPlayerSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	GHashTable *result = g_hash_table_new (g_str_hash, g_str_equal);
	GHashTableIter iter;
	gpointer key, value;
	
	g_hash_table_iter_init (&iter, priv->entry_map);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		/* FIXME: Just checks if the genre is "Podcast".  Sort of hackish,
		 * But, because MTP players don't have a uniform way of handling
		 * Podcasts it is the best we can do for now
		 */
		if (strcmp (((LIBMTP_track_t *)value)->genre, "Podcast") == 0)
			g_hash_table_insert (result, rb_media_player_source_track_uuid (key), key);
	}
	
	return result;
}

static guint64
impl_get_capacity	(RBMediaPlayerSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	guint64 capacity = 0;
	int ret;
	LIBMTP_devicestorage_t *storage;
	
	ret = LIBMTP_Get_Storage (priv->device, LIBMTP_STORAGE_SORTBY_NOTSORTED);
	if (ret != 0) {
		report_libmtp_errors (priv->device, TRUE);
		return 0;
	}
	
	for (storage = priv->device->storage;
	     storage != NULL;
	     storage = storage->next)
	{
		capacity += storage->MaxCapacity;
	}
	
	return capacity;
}

static guint64
impl_get_free_space	(RBMediaPlayerSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	guint64 free = 0;
	int ret;
	LIBMTP_devicestorage_t *storage;
	
	ret = LIBMTP_Get_Storage (priv->device, LIBMTP_STORAGE_SORTBY_NOTSORTED);
	if (ret != 0) {
		report_libmtp_errors (priv->device, TRUE);
		return 0;
	}
	
	for (storage = priv->device->storage;
	     storage != NULL;
	     storage = storage->next)
	{
		free += storage->FreeSpaceInBytes;
	}
	
	return free;
}

static void
impl_add_entries	(RBMediaPlayerSource *source, GList *entries)
{
	rb_source_paste ((RBSource *)source, entries);
}

static void
impl_trash_entry	(RBMediaPlayerSource *source, RhythmDBEntry *entry)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	LIBMTP_track_t *track;
	const char *uri;
	const char *album_name;
	
	RhythmDB *db = get_db_for_source (RB_MTP_SOURCE (source));
	
	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	track = g_hash_table_lookup (priv->entry_map, entry);
	if (track == NULL) {
		rb_debug ("Couldn't find track on mtp-device! (%s)", uri);
		return;
	}
	
	if (LIBMTP_Delete_Object (priv->device, track->item_id) != 0) {
		rb_debug ("Delete track %d failed", track->item_id);
		report_libmtp_errors (priv->device, TRUE);
		return;
	}

	album_name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
	if (strcmp (album_name, _("Unknown")) != 0) {
		remove_track_from_album (RB_MTP_SOURCE (source), album_name, track);
	}
	g_hash_table_remove (priv->entry_map, entry);
	rhythmdb_entry_delete (db, entry);
	
	rhythmdb_commit (db);
}

static void
impl_trash_entries	(RBMediaPlayerSource *source, GList *entries)
{
	GList *iter;
	
	for (iter = entries; iter != NULL; iter = iter->next) {
		impl_trash_entry (source, (RhythmDBEntry *)iter->data);
	}
}

static void
add_to_playlist (RBMtpSource *source, RhythmDBEntry *entry, RBSource *playlist)
{
	/* FIXME: Stub */
}

static void
impl_add_playlist	(RBMediaPlayerSource *source,
			 gchar *name,
			 GList *entries)
{
	/* FIXME: Stub */
	
	/* FIXME: This keeps it from saying 'add_to_playlist never used' */
	add_to_playlist (NULL, NULL, NULL);
}

static void
impl_trash_playlist	(RBMediaPlayerSource *source,
			 gchar *name)
{
	/* FIXME: Stub */
}

static gchar *
impl_get_serial		(RBMediaPlayerSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	
	return LIBMTP_Get_Serialnumber(priv->device);
}

static gchar *
impl_get_name		(RBMediaPlayerSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	gchar *name = NULL;
	
	name = LIBMTP_Get_Friendlyname (priv->device);
	/* ignore some particular broken device names */
	if (name == NULL || strcmp (name, "?????") == 0) {
		g_free (name);
		name = LIBMTP_Get_Modelname (priv->device);
	}
	if (name == NULL) {
		name = g_strdup (_("Digital Audio Player"));
	}
	
	return name;
}

static void
rb_mtp_info_response_cb (GtkDialog *dialog,
 			 int response_id,
 			 RBMtpSource *source)
{
	if (response_id == GTK_RESPONSE_CLOSE) {
 		
		gtk_widget_destroy (GTK_WIDGET (dialog));
	}
}


typedef struct {
	RBMediaPlayerPrefs *prefs;
	GtkTreeStore *tree_store;
	GtkTreeView  *tree_view;
	GtkProgressBar *preview_bar;
	RBMediaPlayerSource *source;
} RBMtpSyncEntriesChangedData;

static void
rb_mtp_sync_auto_changed_cb (GtkToggleButton *togglebutton,
			     gpointer         user_data)
{
	RBMtpSyncEntriesChangedData *data = user_data;
	rb_media_player_prefs_set_boolean (data->prefs,
				   	   SYNC_AUTO,
				   	   gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (togglebutton)));
}

static void
set_treeview_children (RBMtpSyncEntriesChangedData *data,
		       GtkTreeIter *parent,
		       guint list,
		       gboolean value)
{
	GtkTreeIter iter;
//	GtkCellRendererToggle *toggle;
	gchar *name;
	gboolean valid;
	
	valid = gtk_tree_model_iter_children (GTK_TREE_MODEL (data->tree_store), &iter, parent);
		
	while (valid) {
		gtk_tree_model_get (GTK_TREE_MODEL (data->tree_store), &iter,
				    1, &name,
				    -1);
		gtk_tree_store_set (data->tree_store, &iter,
				    0, rb_media_player_prefs_get_entry_value (data->prefs, list, name),
				    2, value,
				    -1);
		
		g_free (name);
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (data->tree_store), &iter);
	}
}

static void
rb_mtp_sync_music_all_changed_cb (GtkToggleButton *togglebutton,
				   gpointer         user_data)
{
	RBMtpSyncEntriesChangedData *data = user_data;
	gboolean value = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (togglebutton));
	rb_media_player_prefs_set_boolean (data->prefs,
				   SYNC_MUSIC_ALL,
				   value);
	
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (data->tree_store), &iter, "0") == TRUE) {
		gtk_tree_store_set (data->tree_store, &iter,
		/* Activatable */   2, !value,
				    -1);
		
		set_treeview_children (user_data,
				       &iter,
				       SYNC_PLAYLISTS_LIST,
				       !value);
	}
	
}

static void
rb_mtp_sync_podcasts_all_changed_cb (GtkToggleButton *togglebutton,
				      gpointer         user_data)
{
	RBMtpSyncEntriesChangedData *data = user_data;
	gboolean value = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (togglebutton));
	rb_media_player_prefs_set_boolean (data->prefs,
				   SYNC_PODCASTS_ALL,
				   value);
	
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (data->tree_store), &iter, "1") == TRUE) {
		gtk_tree_store_set (data->tree_store, &iter,
		/* Activatable */   2, !value,
				    -1);
		
		set_treeview_children (user_data,
				       &iter,
				       SYNC_PODCASTS_LIST,
				       !value);
	}
}

static void
update_sync_preview_bar (RBMtpSyncEntriesChangedData *data) {
	char *text;
	char *used;
	char *capacity;
	
	rb_media_player_prefs_update_sync (data->prefs);
	
	used = g_format_size_for_display (rb_media_player_prefs_get_uint64 (data->prefs, SYNC_SPACE_NEEDED));
	capacity = g_format_size_for_display (impl_get_capacity(data->source));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (data->preview_bar), 
				       (double)(rb_media_player_prefs_get_uint64 (data->prefs, SYNC_SPACE_NEEDED))/(double)impl_get_capacity (data->source));
	/* Translators: this is used to display the amount of storage space which will be
	 * used and the total storage space on an device after it is synced.
	 */
	text = g_strdup_printf (_("%s of %s"), used, capacity);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (data->preview_bar), text);
	g_free (text);
	g_free (capacity);
	g_free (used);
}

static void
rb_mtp_sync_entries_changed_cb (GtkCellRendererToggle *cell_renderer,
				gchar	      *path,
				RBMtpSyncEntriesChangedData *data)
{
	// FIXME: path may not be correct
	GtkTreeIter   iter;
	
	if ( gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (data->tree_store), &iter, path) == TRUE )
	{
		gchar *name;
		gboolean value;
		
		gtk_tree_model_get(GTK_TREE_MODEL (data->tree_store), &iter, 1, &name, -1);
		value = !gtk_cell_renderer_toggle_get_active (cell_renderer);
		
		gtk_tree_store_set (data->tree_store,
				    &iter,
				    0, value,
				    -1 );
		
		if (g_strcmp0 (name, "Music Playlists") == 0) {
			rb_media_player_prefs_set_boolean (data->prefs,		// RBMediaPlayerPrefs *
						   SYNC_MUSIC,		// int
						   value);		// gboolean
			// Enable/Disable the children of this node.
			set_treeview_children (data, &iter, SYNC_PLAYLISTS_LIST, value);
		} else if (g_strcmp0 (name, "Podcasts") == 0) {
			rb_media_player_prefs_set_boolean (data->prefs,		// RBMediaPlayerPrefs *
						   SYNC_PODCASTS,	// int
						   value);		// gboolean
			// Enable/Disable the children of this node.
			set_treeview_children (data, &iter, SYNC_PODCASTS_LIST, value);
		} else {
			if (path[0] == '0')
				rb_media_player_prefs_set_entry (data->prefs,	// RBMediaPlayerPrefs *
							 SYNC_PLAYLISTS_LIST, //guint
						 	 name,		// gchar * of the entry changed
						 	 value);	// gboolean
			else
				rb_media_player_prefs_set_entry (data->prefs,	// RBMediaPlayerPrefs *
							 SYNC_PODCASTS_LIST, //guint
						 	 name,		// gchar * of the entry changed
						 	 value);	// gboolean
		}
	}
	
	update_sync_preview_bar (data);
}

static void
impl_show_properties	(RBMediaPlayerSource *source, RBMediaPlayerPrefs *prefs)
{
	GtkBuilder *builder;
	GObject *dialog;
	GObject *label;
	char *text;
	char *used;
	char *capacity;
	char *builder_file;
 	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
 	
	RBPlugin *plugin;
	
	RBShell *shell;

	if (priv->device == NULL) {
		rb_debug ("can't show device properties with no device");
		return;
	}

	g_object_get (source, "plugin", &plugin, NULL);
	builder_file = rb_plugin_find_file (plugin, "mtp-info.ui");
	g_object_unref (plugin);

	if (builder_file == NULL) {
		g_warning ("Couldn't find mtp-info.ui");
		return;
	}

	builder = rb_builder_load (builder_file, NULL);
	g_free (builder_file);

 	if (builder == NULL) {
 		rb_debug ("Couldn't load mtp-info.ui");
 		return;
 	}
	
 	dialog = gtk_builder_get_object (builder, "mtp-information");
 	g_signal_connect_object (dialog,
 				 "response",
 				 G_CALLBACK (rb_mtp_info_response_cb),
 				 source, 0);
 
 	label = gtk_builder_get_object (builder, "label-number-track-number");
 	text = g_strdup_printf ("%u", g_hash_table_size (priv->entry_map));
 	gtk_label_set_text (GTK_LABEL (label), text);
 	g_free (text);
 
 	label = gtk_builder_get_object (builder, "entry-mtp-name");
 	gtk_entry_set_text (GTK_ENTRY (label), impl_get_name (source));
 	g_signal_connect (label, "focus-out-event",
 			  (GCallback)rb_mtp_source_name_changed_cb, source);
 
	/* FIXME: Not working yet
	 *
 	label = gtk_builder_get_object (builder, "label-number-playlist-number");
 	text = g_strdup_printf ("%u", g_list_length (rb_ipod_db_get_playlists (priv->ipod_db)));
 	gtk_label_set_text (GTK_LABEL (label), text);
 	g_free (text);
 
 	label = gtk_builder_get_object (builder, "label-mount-point-value");
	mp = rb_ipod_db_get_mount_path (priv->ipod_db);
 	gtk_label_set_text (GTK_LABEL (label), mp);
 	*/
 	
	label = gtk_builder_get_object (builder, "progressbar-mtp-usage");
	used = g_format_size_for_display (impl_get_capacity (source) - impl_get_free_space (source));
	capacity = g_format_size_for_display (impl_get_capacity(source));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (label), 
				       (double)(impl_get_capacity (source) - impl_get_free_space (source))/(double)impl_get_capacity (source));
	/* Translators: this is used to display the amount of storage space
	 * used and the total storage space on an device.
	 */
	text = g_strdup_printf (_("%s of %s"), used, capacity);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (label), text);
	g_free (text);
	g_free (capacity);
	g_free (used);
	
	// Set tree models for each treeview
	// tree_store columns are: Active, Name, Activatable
	GtkTreeStore *tree_store = gtk_tree_store_new (3, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_BOOLEAN);
	GtkTreeIter  tree_iter;
	GtkTreeIter  parent_iter;
	GList * list_iter;
	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer;
	gchar *name;
	RhythmDB *library_db;
	
	RBMtpSyncEntriesChangedData *entries_changed_data = g_new0 (RBMtpSyncEntriesChangedData, 1);
	entries_changed_data->prefs = prefs;
	entries_changed_data->tree_store = tree_store;
	label = gtk_builder_get_object (builder, "progressbar-mtp-sync-preview");
	entries_changed_data->preview_bar = GTK_PROGRESS_BAR (label);
	label = gtk_builder_get_object (builder, "treeview-mtp-sync");
	entries_changed_data->tree_view = GTK_TREE_VIEW (label);
	entries_changed_data->source = source;
		
	g_object_get (RB_SOURCE (source), "shell", &shell, NULL);
	g_object_get (shell, "db", &library_db, NULL);
	
	// Set up the treestore
	
	// Append the Music Library Parent
	gtk_tree_store_append (tree_store,
			       &parent_iter,
			       NULL);
	gtk_tree_store_set (tree_store, &parent_iter,
			    0, rb_media_player_prefs_get_boolean (prefs, SYNC_MUSIC),
			    1, "Music Playlists",
			    2, TRUE,
			    -1);
	
	list_iter = rb_playlist_manager_get_playlists ( (RBPlaylistManager *) rb_shell_get_playlist_manager (shell) );
	while (list_iter) {
		gtk_tree_store_append (tree_store, &tree_iter, &parent_iter);
		// set playlists data here
		g_object_get (G_OBJECT (list_iter->data), "name", &name, NULL);
		
		// set this row's data
		gtk_tree_store_set (tree_store, &tree_iter,
				    0, rb_media_player_prefs_get_entry (prefs, SYNC_PLAYLISTS_LIST, name)
				    	&& rb_media_player_prefs_get_boolean (prefs, SYNC_MUSIC),
				    1, name,
				    2, rb_media_player_prefs_get_boolean (prefs, SYNC_MUSIC),
				    -1);
                
		list_iter = list_iter->next;
	}
	
	// Append the Podcasts Parent
	gtk_tree_store_append (tree_store,
			       &parent_iter,
			       NULL);
	gtk_tree_store_set (tree_store, &parent_iter,
			    0, rb_media_player_prefs_get_boolean (prefs, SYNC_PODCASTS),
			    1, "Podcasts",
			    2, TRUE,
			    -1);
	
	GtkTreeModel *query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty(library_db));
	rhythmdb_do_full_query (library_db, RHYTHMDB_QUERY_RESULTS (query_model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_FEED,
				RHYTHMDB_QUERY_END);
	GtkTreeIter tree_iter2;
	gboolean valid = gtk_tree_model_get_iter_first (query_model, &tree_iter);
	while (valid) {
		RhythmDBEntry *entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (query_model), &tree_iter);
		gtk_tree_store_append (tree_store, &tree_iter2, &parent_iter);
		
		// set up this row
		name = strdup(rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE));
		gtk_tree_store_set (tree_store, &tree_iter2,
				    0, rb_media_player_prefs_get_entry (prefs, SYNC_PODCASTS_LIST, name)
				    	&& rb_media_player_prefs_get_boolean (prefs, SYNC_PODCASTS),
				    1, name,
				    2, rb_media_player_prefs_get_boolean (prefs, SYNC_PODCASTS),
				    -1);
		g_free (name);
		
		valid = gtk_tree_model_iter_next (query_model, &tree_iter);
	}
	
	// Set up the treeview
	
	// First column
	renderer = gtk_cell_renderer_toggle_new();
	col = gtk_tree_view_column_new_with_attributes (NULL,
							renderer,
							"active", 0,
							"sensitive", 2,
							"activatable", 2,
							NULL);
	g_signal_connect (G_OBJECT(renderer),
			  "toggled", G_CALLBACK (rb_mtp_sync_entries_changed_cb),
			  entries_changed_data);
	gtk_tree_view_append_column(GTK_TREE_VIEW(label), col);
	
	// Second column
	renderer = gtk_cell_renderer_text_new();
	col = gtk_tree_view_column_new_with_attributes (NULL,
							renderer,
							"text", 1,
							"sensitive", 2,
							NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(label), col);
	gtk_tree_view_set_model (GTK_TREE_VIEW(label),
				 GTK_TREE_MODEL(tree_store));
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(label)),
				    GTK_SELECTION_NONE);
	
	g_object_unref (shell);
	g_object_unref (tree_store);
	
	label = gtk_builder_get_object (builder, "checkbutton-mtp-sync-auto");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (label),
				      rb_media_player_prefs_get_boolean (prefs, SYNC_AUTO));
 	g_signal_connect (label, "toggled",
 			  (GCallback)rb_mtp_sync_auto_changed_cb,
 			  entries_changed_data);
	
	label = gtk_builder_get_object (builder, "checkbutton-mtp-sync-music-all");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (label),
				      rb_media_player_prefs_get_boolean (prefs, SYNC_MUSIC_ALL));
 	g_signal_connect (label, "toggled",
 			  (GCallback)rb_mtp_sync_music_all_changed_cb,
 			  entries_changed_data);
	
	label = gtk_builder_get_object (builder, "checkbutton-mtp-sync-podcasts-all");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (label),
				      rb_media_player_prefs_get_boolean (prefs, SYNC_PODCASTS_ALL));
 	g_signal_connect (label, "toggled",
 			  (GCallback)rb_mtp_sync_podcasts_all_changed_cb,
 			  entries_changed_data);
	
	// Set up the Sync Preview Bar
	update_sync_preview_bar (entries_changed_data);

	/* FIXME: Not working, yet.
	label = gtk_builder_get_object (builder, "label-device-node-value");
	text = rb_ipod_helpers_get_device (RB_SOURCE(source));
	gtk_label_set_text (GTK_LABEL (label), text);
	g_free (text);
	*/
 	label = gtk_builder_get_object (builder, "label-mtp-model-value");
 	gtk_label_set_text (GTK_LABEL (label), LIBMTP_Get_Modelname (priv->device));

	/* FIXME: Not working yet.
 	label = gtk_builder_get_object (builder, "label-database-version-value");
	text = g_strdup_printf ("%u", rb_ipod_db_get_database_version (priv->ipod_db));
 	gtk_label_set_text (GTK_LABEL (label), text);
	g_free (text);
	*/
	
 	label = gtk_builder_get_object (builder, "label-serial-number-value");
	gtk_label_set_text (GTK_LABEL (label), impl_get_serial (source));

 	label = gtk_builder_get_object (builder, "label-firmware-version-value");
	gtk_label_set_text (GTK_LABEL (label), LIBMTP_Get_Deviceversion (priv->device));

 	gtk_widget_show (GTK_WIDGET (dialog));

	g_object_unref (builder);
}

static void
prepare_source (RBMtpSource *source, const char *stream_uri, GObject *src)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	RhythmDBEntry *entry;
	RhythmDB *db;

	/* make sure this stream is for a file on our device */
	if (g_str_has_prefix (stream_uri, "xrbmtp://") == FALSE)
		return;

	db = get_db_for_source (source);
	entry = rhythmdb_entry_lookup_by_location (db, stream_uri);
	g_object_unref (db);
	if (entry == NULL)
		return;

	if (_rb_source_check_entry_type (RB_SOURCE (source), entry) == FALSE) {
		rhythmdb_entry_unref (entry);
		return;
	}

	rb_debug ("setting device %p for stream %s", priv->device, stream_uri);
	g_object_set (src, "device", priv->device, NULL);
	rhythmdb_entry_unref (entry);
}

static void
prepare_player_source_cb (RBPlayer *player,
			  const char *stream_uri,
			  GstElement *src,
			  RBMtpSource *source)
{
	prepare_source (source, stream_uri, G_OBJECT (src));
}

static void
prepare_encoder_source_cb (RBEncoderFactory *factory,
			   const char *stream_uri,
			   GObject *src,
			   RBMtpSource *source)
{
	prepare_source (source, stream_uri, src);
}

