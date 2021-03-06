# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright (C) 2006 - Ed Catmur <ed@catmur.co.uk>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# The Rhythmbox authors hereby grant permission for non-GPL compatible
# GStreamer plugins to be used and distributed together with GStreamer
# and Rhythmbox. This permission is above and beyond the permissions granted
# by the GPL license by which Rhythmbox is covered. If you modify this code
# you may extend this exception to your version of the code, but you are not
# obligated to do so. If you do not wish to do so, delete this exception
# statement from your version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.

import os
import rhythmdb
import gnomevfs
import rb
import gobject

IMAGE_NAMES = ["cover", "album", "albumart", ".folder", "folder"]
LOAD_DIRECTORY_FLAGS = gnomevfs.FILE_INFO_GET_MIME_TYPE | gnomevfs.FILE_INFO_FORCE_FAST_MIME_TYPE | gnomevfs.FILE_INFO_FOLLOW_LINKS
ITEMS_PER_NOTIFICATION = 10
ART_SAVE_NAME = 'Cover.jpg'
ART_SAVE_FORMAT = 'jpeg'
ART_SAVE_SETTINGS = {"quality": "100"}

def file_root (f_name):
	return os.path.splitext (f_name)[0].lower ()

def shared_prefix_length (a, b):
	l = 0
	while a[l] == b[l]:
		l = l+1
	return l


class LocalCoverArtSearch:
	def __init__ (self):
		pass

	def _load_dir_cb (self, handle, files, exception, (results, on_search_completed_cb, entry, args)):
		for f in files:
			if f.mime_type is None:
				pass
			elif f.mime_type.startswith ("image/") and f.permissions & gnomevfs.PERM_USER_READ:
				results.append (f.name)
		if exception:
			on_search_completed_cb (self, entry, results, *args)
			if not issubclass (exception, gnomevfs.EOFError):
				print "Error reading \"%s\": %s" % (self.uri.parent, exception)

	def search (self, db, entry, is_playing, on_search_completed_cb, *args):
		self.uri = None
		try:
			self.uri = gnomevfs.URI (entry.get_playback_uri())
		except TypeError:
			pass

		if self.uri is None or self.uri.scheme == 'http':
			print 'not searching for local art for %s' % (self.uri)
			on_search_completed_cb (self, entry, [], *args)
			return

		self.artist = db.entry_get (entry, rhythmdb.PROP_ARTIST)
		self.album = db.entry_get (entry, rhythmdb.PROP_ALBUM)

		print 'searching for local art for %s' % (self.uri)
		gnomevfs.async.load_directory (self.uri.parent, self._load_dir_cb, LOAD_DIRECTORY_FLAGS, ITEMS_PER_NOTIFICATION, data=([], on_search_completed_cb, entry, args))

	def search_next (self):
		return False

	def get_result_pixbuf (self, results):
		return None

	def get_best_match_urls (self, results):
		# Compare lower case, without file extension
		for name in [file_root (self.uri.short_name)] + IMAGE_NAMES:
			for f_name in results:
				if file_root (f_name) == name:
					yield self.uri.parent.append_file_name (f_name)

		# look for file names containing the artist and album (case-insensitive)
		# (mostly for jamendo downloads)
		artist = self.artist.lower()
		album = self.album.lower()
		for f_name in results:
			f_root = file_root (f_name).lower()
			if f_root.find (artist) != -1 and f_root.find (album) != -1:
				yield self.uri.parent.append_file_name (f_name).path

		# if that didn't work, look for the longest shared prefix
		# only accept matches longer than 2 to avoid weird false positives
		match = (2, None)
		for f_name in results:
			pl = shared_prefix_length(f_name, self.uri.short_name)
			if pl > match[0]:
				match = (pl, f_name)

		if match[1] is not None:
			yield self.uri.parent.append_file_name (match[1]).path

	def pixbuf_save (self, plexer, pixbuf, uri):
		gnomevfs.async.create (uri, plexer.send (), gnomevfs.OPEN_WRITE | gnomevfs.OPEN_TRUNCATE, False, 0644, gnomevfs.PRIORITY_DEFAULT)
		yield None
		_, (handle, result) = plexer.receive ()
		if result:
			print "Error creating \"%s\": %s" % (uri, result)
			return
		def pixbuf_cb (buf):
			data = [buf]
			status = []
			def write_coro (w_plexer):
				while data:
					buf = data.pop ()
					handle.write (buf, w_plexer.send ())
					yield None
					_, (_, requested, result, written) = w_plexer.receive ()
					if result:
						print "Error writing \"%s\": %s" % (uri, result)
						status.insert (0, False)
						return
					if written < requested:
						data.insert (0, buf[written:])
				status.insert (0, True)
			rb.Coroutine (write_coro).begin ()
			while not status:
				gobject.main_context_default ().iteration ()
			return status[0]
		pixbuf.save_to_callback (pixbuf_cb, ART_SAVE_FORMAT, ART_SAVE_SETTINGS)
		handle.close (plexer.send())
		yield None
		_, (_, result) = plexer.receive ()
		if result:
			print "Error closing \"%s\": %s" % (uri, result)

	def _save_dir_cb (self, handle, files, exception, (db, entry, pixbuf)):
		artist, album = [db.entry_get (entry, x) for x in [rhythmdb.PROP_ARTIST, rhythmdb.PROP_ALBUM]]
		for f in files:
			if f.mime_type.split ("/")[0] in ["image", "x-directory"]:
				continue
			uri = str (self.uri.parent.append_file_name (f.name))
			u_entry = db.entry_lookup_by_location (uri)
			if u_entry:
				u_artist, u_album = [db.entry_get (u_entry, x) for x in [rhythmdb.PROP_ARTIST, rhythmdb.PROP_ALBUM]]
				if album != u_album:
					print "Not saving local art; encountered media with different album (%s, %s, %s)" % (uri, u_artist, u_album)
					handle.cancel ()
					return
				continue
			print "Not saving local art; encountered unknown file (%s)" % uri
			handle.cancel ()
			return
		if exception:
			if issubclass (exception, gnomevfs.EOFError):
				art_uri = str (self.uri.parent.append_file_name (ART_SAVE_NAME))
				print "Saving local art to \"%s\"" % art_uri
				rb.Coroutine (self.pixbuf_save, pixbuf, art_uri).begin ()
			else:
				print "Error reading \"%s\": %s" % (self.uri.parent, exception)

	def save_pixbuf (self, db, entry, pixbuf):
		self.uri = None
		try:
			self.uri = gnomevfs.URI (entry.get_playback_uri())
		except TypeError:
			pass

		if self.uri is None or self.uri.scheme == 'http':
			print 'not saving local art for %s' % self.uri
			return

		print 'checking whether to save local art for %s' % self.uri
		gnomevfs.async.load_directory (self.uri.parent, self._save_dir_cb, LOAD_DIRECTORY_FLAGS, ITEMS_PER_NOTIFICATION, data=(db, entry, pixbuf))
