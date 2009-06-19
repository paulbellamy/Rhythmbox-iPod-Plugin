 /*
 *  arch-tag: Header for RhythmDB - Rhythmbox backend queryable database
 *
 *  Copyright (C) 2003,2004 Colin Walters <walters@rhythmbox.org>
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

#ifndef RHYTHMDB_H
#define RHYTHMDB_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <stdarg.h>
#include <libxml/tree.h>

#include "config.h"
#include "rb-refstring.h"
#include "rb-string-value-map.h"
#include "rhythmdb-query-results.h"

G_BEGIN_DECLS

typedef struct _RhythmDB RhythmDB;
typedef struct _RhythmDBClass RhythmDBClass;

#define RHYTHMDB_TYPE      (rhythmdb_get_type ())
#define RHYTHMDB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE, RhythmDB))
#define RHYTHMDB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_TYPE, RhythmDBClass))
#define RHYTHMDB_IS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE))
#define RHYTHMDB_IS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE))
#define RHYTHMDB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE, RhythmDBClass))

struct RhythmDBEntry_;
typedef struct RhythmDBEntry_ RhythmDBEntry;
GType rhythmdb_entry_get_type (void);

#define RHYTHMDB_TYPE_ENTRY      (rhythmdb_entry_get_type ())
#define RHYTHMDB_ENTRY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_ENTRY, RhythmDBEntry))

typedef void (*RhythmDBEntryActionFunc) (RhythmDBEntry *entry, gpointer data);
typedef char* (*RhythmDBEntryStringFunc) (RhythmDBEntry *entry, gpointer data);
typedef gboolean (*RhythmDBEntryCanSyncFunc) (RhythmDB *db, RhythmDBEntry *entry, gpointer data);
typedef void (*RhythmDBEntrySyncFunc) (RhythmDB *db, RhythmDBEntry *entry, GError **error, gpointer data);

GType rhythmdb_entry_category_get_type (void);
#define RHYTHMDB_TYPE_ENTRY_CATEGORY (rhythmdb_entry_category_get_type ())
typedef enum {
	RHYTHMDB_ENTRY_NORMAL,		/* anything that doesn't match the other categories */
	RHYTHMDB_ENTRY_STREAM,		/* endless streams (eg shoutcast, last.fm) */
	RHYTHMDB_ENTRY_CONTAINER,	/* things that point to other entries (eg podcast feeds) */
	RHYTHMDB_ENTRY_VIRTUAL		/* import errors, ignored files */
} RhythmDBEntryCategory;

typedef struct {
	char 				*name;

	guint				entry_type_data_size;
	gboolean			save_to_disk;
	gboolean			has_playlists;
	RhythmDBEntryCategory		category;

	/* virtual functions here */
	RhythmDBEntryActionFunc		post_entry_create;
	gpointer			post_entry_create_data;
	GDestroyNotify			post_entry_create_destroy;

	RhythmDBEntryActionFunc		pre_entry_destroy;
	gpointer			pre_entry_destroy_data;
	GDestroyNotify			pre_entry_destroy_destroy;

	RhythmDBEntryStringFunc		get_playback_uri;
	gpointer			get_playback_uri_data;
	GDestroyNotify			get_playback_uri_destroy;

	RhythmDBEntryCanSyncFunc	can_sync_metadata;
	gpointer			can_sync_metadata_data;
	GDestroyNotify			can_sync_metadata_destroy;

	RhythmDBEntrySyncFunc		sync_metadata;
	gpointer			sync_metadata_data;
	GDestroyNotify			sync_metadata_destroy;
} RhythmDBEntryType_;
typedef RhythmDBEntryType_ *RhythmDBEntryType;

GType rhythmdb_entry_type_get_type (void);
#define RHYTHMDB_TYPE_ENTRY_TYPE	(rhythmdb_entry_type_get_type ())
#define RHYTHMDB_ENTRY_TYPE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_ENTRY_TYPE, RhythmDBEntryType_))
#define RHYTHMDB_IS_ENTRY_TYPE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_ENTRY_TYPE))

typedef GPtrArray RhythmDBQuery;
GType rhythmdb_query_get_type (void);
#define RHYTHMDB_TYPE_QUERY	(rhythmdb_query_get_type ())
#define RHYTHMDB_QUERY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_QUERY, RhythmDBQuery))
#define RHYTHMDB_IS_QUERY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_QUERY))

#define RHYTHMDB_ENTRY_TYPE_SONG (rhythmdb_entry_song_get_type ())
#define RHYTHMDB_ENTRY_TYPE_PODCAST_POST (rhythmdb_entry_podcast_post_get_type ())
#define RHYTHMDB_ENTRY_TYPE_PODCAST_FEED (rhythmdb_entry_podcast_feed_get_type ())
#define RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR (rhythmdb_entry_import_error_get_type ())
#define RHYTHMDB_ENTRY_TYPE_IGNORE (rhythmdb_entry_ignore_get_type ())
#define RHYTHMDB_ENTRY_TYPE_INVALID (GINT_TO_POINTER (-1))

typedef enum
{
	RHYTHMDB_QUERY_END,
	RHYTHMDB_QUERY_DISJUNCTION,
	RHYTHMDB_QUERY_SUBQUERY,

	/* general */
	RHYTHMDB_QUERY_PROP_EQUALS,

	/* string */
	RHYTHMDB_QUERY_PROP_LIKE,
	RHYTHMDB_QUERY_PROP_NOT_LIKE,
	RHYTHMDB_QUERY_PROP_PREFIX,
	RHYTHMDB_QUERY_PROP_SUFFIX,

	/* numerical */
	RHYTHMDB_QUERY_PROP_GREATER,
	RHYTHMDB_QUERY_PROP_LESS,

	/* synthetic query types, translated into non-synthetic ones internally */
	RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN,
	RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN,
	RHYTHMDB_QUERY_PROP_YEAR_EQUALS,
	RHYTHMDB_QUERY_PROP_YEAR_GREATER,
	RHYTHMDB_QUERY_PROP_YEAR_LESS,
} RhythmDBQueryType;

/* If you modify this enum, don't forget to modify rhythmdb_prop_get_type */
typedef enum
{
	RHYTHMDB_PROP_TYPE = 0,
	RHYTHMDB_PROP_ENTRY_ID,
	RHYTHMDB_PROP_TITLE,
	RHYTHMDB_PROP_GENRE,
	RHYTHMDB_PROP_ARTIST,
	RHYTHMDB_PROP_ALBUM,
	RHYTHMDB_PROP_TRACK_NUMBER,
	RHYTHMDB_PROP_DISC_NUMBER,
	RHYTHMDB_PROP_DURATION,
	RHYTHMDB_PROP_FILE_SIZE,
	RHYTHMDB_PROP_LOCATION,
	RHYTHMDB_PROP_MOUNTPOINT,
	RHYTHMDB_PROP_MTIME,
	RHYTHMDB_PROP_FIRST_SEEN,
	RHYTHMDB_PROP_LAST_SEEN,
	RHYTHMDB_PROP_RATING,
	RHYTHMDB_PROP_PLAY_COUNT,
	RHYTHMDB_PROP_LAST_PLAYED,
	RHYTHMDB_PROP_BITRATE,
	RHYTHMDB_PROP_DATE,
	RHYTHMDB_PROP_TRACK_GAIN,
	RHYTHMDB_PROP_TRACK_PEAK,
	RHYTHMDB_PROP_ALBUM_GAIN,
	RHYTHMDB_PROP_ALBUM_PEAK,
	RHYTHMDB_PROP_MIMETYPE,
	RHYTHMDB_PROP_TITLE_SORT_KEY,
	RHYTHMDB_PROP_GENRE_SORT_KEY,
	RHYTHMDB_PROP_ARTIST_SORT_KEY,
	RHYTHMDB_PROP_ALBUM_SORT_KEY,
	RHYTHMDB_PROP_TITLE_FOLDED,
	RHYTHMDB_PROP_GENRE_FOLDED,
	RHYTHMDB_PROP_ARTIST_FOLDED,
	RHYTHMDB_PROP_ALBUM_FOLDED,
	RHYTHMDB_PROP_LAST_PLAYED_STR,
	RHYTHMDB_PROP_HIDDEN,
	RHYTHMDB_PROP_PLAYBACK_ERROR,
	RHYTHMDB_PROP_FIRST_SEEN_STR,
	RHYTHMDB_PROP_LAST_SEEN_STR,

	/* synthetic properties */
	RHYTHMDB_PROP_SEARCH_MATCH,
	RHYTHMDB_PROP_YEAR,
	RHYTHMDB_PROP_KEYWORD, /**/

	/* Podcast properties */
	RHYTHMDB_PROP_STATUS,
	RHYTHMDB_PROP_DESCRIPTION,
	RHYTHMDB_PROP_SUBTITLE,
	RHYTHMDB_PROP_SUMMARY,
	RHYTHMDB_PROP_LANG,
	RHYTHMDB_PROP_COPYRIGHT,
	RHYTHMDB_PROP_IMAGE,
	RHYTHMDB_PROP_POST_TIME,

	RHYTHMDB_PROP_MUSICBRAINZ_TRACKID,
	RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID,
	RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID,
	RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID,
	RHYTHMDB_PROP_ARTIST_SORTNAME,
	RHYTHMDB_PROP_ALBUM_SORTNAME,

	RHYTHMDB_NUM_PROPERTIES
} RhythmDBPropType;

enum {
	RHYTHMDB_PODCAST_STATUS_COMPLETE = 100,
	RHYTHMDB_PODCAST_STATUS_ERROR = 101,
	RHYTHMDB_PODCAST_STATUS_WAITING = 102,
	RHYTHMDB_PODCAST_STATUS_PAUSED = 103,
};

/* commonly used extra entry metadata */
#define RHYTHMDB_PROP_STREAM_SONG_TITLE		"rb:stream-song-title"
#define RHYTHMDB_PROP_STREAM_SONG_ARTIST	"rb:stream-song-artist"
#define RHYTHMDB_PROP_STREAM_SONG_ALBUM		"rb:stream-song-album"
#define RHYTHMDB_PROP_COVER_ART			"rb:coverArt"
#define RHYTHMDB_PROP_COVER_ART_URI		"rb:coverArt-uri"
#define RHYTHMDB_PROP_ALBUM_ARTIST		"rb:album-artist"
#define RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME	"rb:album-artist-sortname"

GType rhythmdb_query_type_get_type (void);
GType rhythmdb_prop_type_get_type (void);

#define RHYTHMDB_TYPE_QUERY_TYPE (rhythmdb_query_type_get_type ())
#define RHYTHMDB_TYPE_PROP_TYPE (rhythmdb_prop_type_get_type ())

typedef struct {
	guint type;
	guint propid;
	GValue *val;
	RhythmDBQuery *subquery;
} RhythmDBQueryData;

typedef struct {
	RhythmDBPropType prop;
	GValue old;
	GValue new;
} RhythmDBEntryChange;

const char *rhythmdb_entry_get_string	(RhythmDBEntry *entry, RhythmDBPropType propid);
RBRefString *rhythmdb_entry_get_refstring (RhythmDBEntry *entry, RhythmDBPropType propid);
char *rhythmdb_entry_dup_string	(RhythmDBEntry *entry, RhythmDBPropType propid);
gboolean rhythmdb_entry_get_boolean	(RhythmDBEntry *entry, RhythmDBPropType propid);
guint64 rhythmdb_entry_get_uint64	(RhythmDBEntry *entry, RhythmDBPropType propid);
gulong rhythmdb_entry_get_ulong		(RhythmDBEntry *entry, RhythmDBPropType propid);
double rhythmdb_entry_get_double	(RhythmDBEntry *entry, RhythmDBPropType propid);
gpointer rhythmdb_entry_get_pointer     (RhythmDBEntry *entry, RhythmDBPropType propid);

RhythmDBEntryType rhythmdb_entry_get_entry_type (RhythmDBEntry *entry);

typedef enum
{
	RHYTHMDB_ERROR_ACCESS_FAILED,
} RhythmDBError;

#define RHYTHMDB_ERROR (rhythmdb_error_quark ())

GQuark rhythmdb_error_quark (void);

typedef struct _RhythmDBPrivate RhythmDBPrivate;

struct _RhythmDB
{
	GObject parent;

	RhythmDBPrivate *priv;
};

struct _RhythmDBClass
{
	GObjectClass parent;

	/* signals */
	void	(*entry_added)		(RhythmDB *db, RhythmDBEntry *entry);
	void	(*entry_changed)	(RhythmDB *db, RhythmDBEntry *entry, GSList *changes); /* list of RhythmDBEntryChanges */
	void	(*entry_deleted)	(RhythmDB *db, RhythmDBEntry *entry);
	void	(*entry_keyword_added)	(RhythmDB *db, RhythmDBEntry *entry, RBRefString *keyword);
	void	(*entry_keyword_removed)(RhythmDB *db, RhythmDBEntry *entry, RBRefString *keyword);
	GValue *(*entry_extra_metadata_request) (RhythmDB *db, RhythmDBEntry *entry);
	void    (*entry_extra_metadata_gather) (RhythmDB *db, RhythmDBEntry *entry, RBStringValueMap *data);
	void	(*entry_extra_metadata_notify) (RhythmDB *db, RhythmDBEntry *entry, const char *field, GValue *metadata);
	void	(*load_complete)	(RhythmDB *db);
	void	(*save_complete)	(RhythmDB *db);
	void	(*load_error)		(RhythmDB *db, const char *uri, const char *msg);
	void	(*save_error)		(RhythmDB *db, const char *uri, const GError *error);
	void	(*read_only)		(RhythmDB *db, gboolean readonly);

	/* virtual methods */

	gboolean	(*impl_load)		(RhythmDB *db, GCancellable *cancel, GError **error);
	void		(*impl_save)		(RhythmDB *db);

	void		(*impl_entry_new)	(RhythmDB *db, RhythmDBEntry *entry);

	gboolean	(*impl_entry_set)	(RhythmDB *db, RhythmDBEntry *entry,
	                                 guint propid, const GValue *value);

	void		(*impl_entry_get)	(RhythmDB *db, RhythmDBEntry *entry,
						 guint propid, GValue *value);

	void		(*impl_entry_delete)	(RhythmDB *db, RhythmDBEntry *entry);

	void            (*impl_entry_delete_by_type) (RhythmDB *db, RhythmDBEntryType type);

	RhythmDBEntry *	(*impl_lookup_by_location)(RhythmDB *db, RBRefString *uri);

	RhythmDBEntry *	(*impl_lookup_by_id)    (RhythmDB *db, gint id);

	gboolean 	(*impl_evaluate_query)	(RhythmDB *db, RhythmDBQuery *query, RhythmDBEntry *entry);

	void		(*impl_entry_foreach)	(RhythmDB *db, GFunc func, gpointer data);

	gint64		(*impl_entry_count)	(RhythmDB *db);

	void		(*impl_entry_foreach_by_type) (RhythmDB *db, RhythmDBEntryType type, GFunc func, gpointer data);

	gint64		(*impl_entry_count_by_type) (RhythmDB *db, RhythmDBEntryType type);

	void		(*impl_do_full_query)	(RhythmDB *db, RhythmDBQuery *query,
						 RhythmDBQueryResults *results,
						 gboolean *cancel);

	void		(*impl_entry_type_registered) (RhythmDB *db,
						       const char *name,
						       RhythmDBEntryType type);

	gboolean	(*impl_entry_keyword_add)	(RhythmDB *db,
							 RhythmDBEntry *entry,
							 RBRefString *keyword);
	gboolean	(*impl_entry_keyword_remove)	(RhythmDB *db,
							 RhythmDBEntry *entry,
							 RBRefString *keyword);
	gboolean	(*impl_entry_keyword_has)	(RhythmDB *db,
							 RhythmDBEntry *entry,
							 RBRefString *keyword);
	GList*		(*impl_entry_keywords_get)	(RhythmDB *db,
							 RhythmDBEntry *entry);
};

GType		rhythmdb_get_type	(void);

RhythmDB *	rhythmdb_new		(const char *name);

void		rhythmdb_shutdown	(RhythmDB *db);

void		rhythmdb_load		(RhythmDB *db);

void		rhythmdb_save		(RhythmDB *db);
void		rhythmdb_save_async	(RhythmDB *db);

void		rhythmdb_start_action_thread	(RhythmDB *db);

void		rhythmdb_commit		(RhythmDB *db);

gboolean	rhythmdb_entry_is_editable (RhythmDB *db, RhythmDBEntry *entry);

RhythmDBEntry *	rhythmdb_entry_new	(RhythmDB *db, RhythmDBEntryType type, const char *uri);
RhythmDBEntry *	rhythmdb_entry_example_new	(RhythmDB *db, RhythmDBEntryType type, const char *uri);

void		rhythmdb_add_uri	(RhythmDB *db, const char *uri);
void		rhythmdb_add_uri_with_types (RhythmDB *db,
					     const char *uri,
					     RhythmDBEntryType type,
					     RhythmDBEntryType ignore_type,
					     RhythmDBEntryType error_type);

void		rhythmdb_entry_get	(RhythmDB *db, RhythmDBEntry *entry, RhythmDBPropType propid, GValue *val);
void		rhythmdb_entry_set	(RhythmDB *db, RhythmDBEntry *entry,
					 guint propid, const GValue *value);

char *		rhythmdb_entry_get_playback_uri	(RhythmDBEntry *entry);

gboolean	rhythmdb_entry_is_lossless (RhythmDBEntry *entry);

gpointer	rhythmdb_entry_get_type_data (RhythmDBEntry *entry, guint expected_size);
#define		RHYTHMDB_ENTRY_GET_TYPE_DATA(e,t)	((t*)rhythmdb_entry_get_type_data((e),sizeof(t)))

void		rhythmdb_entry_delete	(RhythmDB *db, RhythmDBEntry *entry);
void            rhythmdb_entry_delete_by_type (RhythmDB *db,
					       RhythmDBEntryType type);
void		rhythmdb_entry_move_to_trash (RhythmDB *db,
					      RhythmDBEntry *entry);

RhythmDBEntry *	rhythmdb_entry_lookup_by_location (RhythmDB *db, const char *uri);

RhythmDBEntry *	rhythmdb_entry_lookup_by_id     (RhythmDB *db, gint id);

RhythmDBEntry * rhythmdb_entry_lookup_from_string (RhythmDB *db, const char *str, gboolean is_id);

gboolean	rhythmdb_evaluate_query		(RhythmDB *db, RhythmDBQuery *query,
						 RhythmDBEntry *entry);

void		rhythmdb_entry_foreach		(RhythmDB *db,
						 GFunc func,
						 gpointer data);
gint64		rhythmdb_entry_count		(RhythmDB *db);

void		rhythmdb_entry_foreach_by_type  (RhythmDB *db,
						 RhythmDBEntryType entry_type,
						 GFunc func,
						 gpointer data);
gint64		rhythmdb_entry_count_by_type	(RhythmDB *db,
						 RhythmDBEntryType entry_type);

gboolean	rhythmdb_entry_keyword_add	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 RBRefString *keyword);
gboolean	rhythmdb_entry_keyword_remove	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 RBRefString *keyword);
gboolean	rhythmdb_entry_keyword_has	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 RBRefString *keyword);
GList* /*<RBRefString>*/ rhythmdb_entry_keywords_get	(RhythmDB *db,
							 RhythmDBEntry *entry);

/*
 * Returns a freshly allocated GtkTreeModel which represents the query.
 * The extended arguments alternate between RhythmDBQueryType args
 * and their values. Items are prioritized like algebraic expressions, and
 * implicitly ANDed. Here's an example:
 *
rhythmdb_do_full_query (db,
			RHYTHMDB_QUERY_PROP_EQUALS,
 				RHYTHMDB_PROP_ARTIST, "Pink Floyd",
		RHYTHMDB_QUERY_DISJUNCTION,
			RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_GENRE, "Classical",
			RHYTHMDB_QUERY_PROP_GREATER,
				RHYTHMDB_PROP_RATING, 5,
	RHYTHMDB_QUERY_END);
 * Which means: artist = Pink Floyd OR (genre = Classical AND rating >= 5)
 */
void		rhythmdb_do_full_query			(RhythmDB *db,
							 RhythmDBQueryResults *results,
							 ...);
void		rhythmdb_do_full_query_parsed		(RhythmDB *db,
							 RhythmDBQueryResults *results,
							 RhythmDBQuery *query);

void		rhythmdb_do_full_query_async		(RhythmDB *db,
							 RhythmDBQueryResults *results,
							 ...);

void		rhythmdb_do_full_query_async_parsed	(RhythmDB *db,
							 RhythmDBQueryResults *results,
							 RhythmDBQuery *query);

RhythmDBQuery *	rhythmdb_query_parse			(RhythmDB *db, ...);
void		rhythmdb_query_append			(RhythmDB *db, RhythmDBQuery *query, ...);
void		rhythmdb_query_append_params		(RhythmDB *db, RhythmDBQuery *query, RhythmDBQueryType type, RhythmDBPropType prop, const GValue *value);
void		rhythmdb_query_append_prop_multiple	(RhythmDB *db, RhythmDBQuery *query, RhythmDBPropType propid, GList *items);
void		rhythmdb_query_concatenate		(RhythmDBQuery *query1, RhythmDBQuery *query2);
void		rhythmdb_query_free			(RhythmDBQuery *query);
RhythmDBQuery *	rhythmdb_query_copy			(RhythmDBQuery *array);
void		rhythmdb_query_preprocess		(RhythmDB *db, RhythmDBQuery *query);

void		rhythmdb_query_serialize		(RhythmDB *db, RhythmDBQuery *query,
							 xmlNodePtr node);

RhythmDBQuery *	rhythmdb_query_deserialize		(RhythmDB *db, xmlNodePtr node);

char *		rhythmdb_query_to_string		(RhythmDB *db, RhythmDBQuery *query);

gboolean	rhythmdb_query_is_time_relative		(RhythmDB *db, RhythmDBQuery *query);

const xmlChar *	rhythmdb_nice_elt_name_from_propid	(RhythmDB *db, RhythmDBPropType propid);
int		rhythmdb_propid_from_nice_elt_name	(RhythmDB *db, const xmlChar *name);

void		rhythmdb_emit_entry_added		(RhythmDB *db, RhythmDBEntry *entry);
void		rhythmdb_emit_entry_deleted		(RhythmDB *db, RhythmDBEntry *entry);

GValue *	rhythmdb_entry_request_extra_metadata	(RhythmDB *db, RhythmDBEntry *entry, const gchar *property_name);
RBStringValueMap* rhythmdb_entry_gather_metadata	(RhythmDB *db, RhythmDBEntry *entry);
void		rhythmdb_emit_entry_extra_metadata_notify (RhythmDB *db, RhythmDBEntry *entry, const gchar *property_name, const GValue *metadata);

gboolean	rhythmdb_is_busy			(RhythmDB *db);
char *		rhythmdb_compute_status_normal		(gint n_songs, glong duration,
							 guint64 size,
							 const char *singular,
							 const char *plural);

RhythmDBEntryType rhythmdb_entry_register_type          (RhythmDB *db, const char *name);
RhythmDBEntryType rhythmdb_entry_type_get_by_name       (RhythmDB *db, const char *name);

RhythmDBEntryType rhythmdb_entry_song_get_type          (void);
RhythmDBEntryType rhythmdb_entry_podcast_post_get_type  (void);
RhythmDBEntryType rhythmdb_entry_podcast_feed_get_type  (void);
RhythmDBEntryType rhythmdb_entry_import_error_get_type	(void);
RhythmDBEntryType rhythmdb_entry_ignore_get_type        (void);

GType rhythmdb_get_property_type (RhythmDB *db, guint property_id);

RhythmDBEntry* rhythmdb_entry_ref (RhythmDBEntry *entry);
void rhythmdb_entry_unref (RhythmDBEntry *entry);

G_END_DECLS

#endif /* __RHYTHMBDB_H */
