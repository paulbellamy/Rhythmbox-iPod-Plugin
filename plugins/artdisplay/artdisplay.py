# -*- coding: utf-8 -*-
# artdisplay.py
#
# created by James Livingston, Gareth Murphy
#
# Copyright (C) 2006 - James Livingston
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.

from xml.dom import minidom
import rhythmdb, rb
import gobject, gtk
import os
import urllib

LICENSE_KEY = "18C3VZN9HCECM5G3HQG2"
LOCALE = "uk"
ASSOCIATE = "webservices-20"

_supportedLocales = {
		"us" : (None, "xml.amazon.com"),
		"uk" : ("uk", "xml-eu.amazon.com"),
		"de" : ("de", "xml-eu.amazon.com"),
		"jp" : ("jp", "xml.amazon.co.jp")
	}

class AmazonError(Exception): pass

class Bag: pass

class ArtDisplayPlugin(rb.Plugin):

	def __init__(self):
		rb.Plugin.__init__(self)
		
	def activate(self, shell):
		self.shell = shell
		sp = shell.get_player()
		self.pec_id = sp.connect('playing-song-changed', self.playing_entry_changed)
		self.pc_id = sp.connect('playing-changed', self.playing_changed)
		self.art_widget = gtk.Image()
		self.art_widget.set_padding(0, 5)
		shell.add_widget(self.art_widget, rb.SHELL_UI_LOCATION_SIDEBAR)
		self.sa_id = self.art_widget.connect('size-allocate', self.size_allocated)
		self.current_pixbuf = None
		self.resize_id = 0
		self.resize_in_progress = False
		self.old_width = 0
		entry = sp.get_playing_entry()
		self.playing_entry_changed(sp, entry)
	
	def deactivate(self, shell):
		self.shell = None
		sp = shell.get_player()
		sp.disconnect(self.pec_id)
		sp.disconnect(self.pc_id)
		shell.remove_widget(self.art_widget, rb.SHELL_UI_LOCATION_SIDEBAR)
		self.art_widget.disconnect(self.sa_id)
		self.art_widget = None
		self.current_pixbuf = None
		if self.resize_id != 0:
			gobject.source_remove(self.resize_id)

	def playing_changed(self, sp, playing):
		self.set_entry(sp.get_playing_entry())

	def playing_entry_changed(self, sp, entry):
		self.set_entry(entry)

	def size_allocated(self, widget, allocation):
		if self.old_width == allocation.width:
			return
		if self.resize_id == 0:
			self.resize_id = gobject.idle_add(self.do_resize)
		self.resize_in_progress = True
		self.old_width = allocation.width

	def do_resize(self):
		if self.resize_in_progress:
			self.resize_in_progress = False
			self.update_displayed_art(True)
			ret = True
		else:
			self.update_displayed_art(False)
			self.resize_id = 0
			ret = False
		return ret

	def set_entry(self, entry):
		self.current_pixbuf = self.get_art(entry)
		self.update_displayed_art(False)

	def update_displayed_art(self, quick):
		if self.current_pixbuf is None:
			self.art_widget.clear()
			self.art_widget.hide()
		else:
			width = self.art_widget.parent.allocation.width 
			height = self.current_pixbuf.get_height() * width / self.current_pixbuf.get_width()
			if quick:
				mode = gtk.gdk.INTERP_NEAREST
			else:
				mode = gtk.gdk.INTERP_HYPER
			self.art_widget.set_from_pixbuf(self.current_pixbuf.scale_simple (width, height, mode))
			self.art_widget.show()

	def get_art(self, entry):
		if entry is None:
			return None
		pixbuf = None
		art_folder = os.path.expanduser('~/.gnome2/rhythmbox/covers')
		if not os.path.exists(art_folder):
			os.mkdir(art_folder)
		db = self.shell.get_property("db")
		st_artist = db.entry_get(entry, rhythmdb.PROP_ARTIST)
		st_album = db.entry_get(entry, rhythmdb.PROP_ALBUM)
		
		# the following block of code is messy and needs to be redone ASAP
		art_location = art_folder + '/%s - %s.jpg' % (st_artist.replace('/', '-'), st_album.replace('/', '-'))
		blist_location = art_folder + '/%s - %s.rb-blist' % (st_artist.replace('/', '-'), st_album.replace('/', '-'))
		KEYWORD = "%s %s" % (st_artist, st_album)
		for term in ["(Disc 1)", "(Disc 2)", "(Volume 1)"]:
			KEYWORD = KEYWORD.replace(term, '')
			
		if os.path.exists(art_location):
			pixbuf = gtk.gdk.pixbuf_new_from_file(art_location)
		elif os.path.exists(blist_location):
			pixbuf = None
		else:
			try:
				pic_url = self.search(KEYWORD.strip())[0].ImageUrlLarge
				url_file = urllib.urlopen(pic_url)
				if int(url_file.info()['Content-Length']) < 1000:
					url_file.close()
					f = file(blist_location, 'w')
					f.close()
					pixbuf = None	
				else:
					f = file(art_location, 'wb')
					f.write(url_file.read())
					f.close()
					url_file.close()		
					pixbuf = gtk.gdk.pixbuf_new_from_file(art_location)
			except AmazonError:
				f = file(blist_location, 'w')
				f.close()
				pixbuf = None			
		return pixbuf
	
	# Amazon part starts here - taken from pyamazon and edited
	
	def unmarshal(self, element):
		rc = Bag()
		if isinstance(element, minidom.Element) and (element.tagName == 'Details'):
			rc.URL = element.attributes["url"].value
		childElements = [e for e in element.childNodes if isinstance(e, minidom.Element)]
		if childElements:
			for child in childElements:
				key = child.tagName
				if hasattr(rc, key):
					if type(getattr(rc, key)) <> type([]):
						setattr(rc, key, [getattr(rc, key)])
					setattr(rc, key, getattr(rc, key) + [self.unmarshal(child)])
				elif isinstance(child, minidom.Element) and (child.tagName == 'Details'):
					setattr(rc,key,[self.unmarshal(child)])
				else:
					setattr(rc, key, self.unmarshal(child))
		else:
			rc = "".join([e.data for e in element.childNodes if isinstance(e, minidom.Text)])
			if element.tagName == 'SalesRank':
				rc = rc.replace('.', '')
				rc = rc.replace(',', '')
				rc = int(rc)
		return rc
	
	def buildURL(self, keyword):
		url = "http://" + _supportedLocales[LOCALE][1] + "/onca/xml3?f=xml"
		url += "&t=%s" % ASSOCIATE
		url += "&dev-t=%s" % LICENSE_KEY
		url += "&type=%s" % 'lite'
		url += "&locale=%s" % _supportedLocales[LOCALE][0]
		url += "&mode=%s" % 'music'
		url += "&%s=%s" % ('KeywordSearch', urllib.quote(keyword))
		return url

	def search(self, keyword):
		url = self.buildURL(keyword)
		o_url = urllib.urlopen(url)
		xmldoc = minidom.parse(o_url)
		o_url.close()
		data = self.unmarshal(xmldoc).ProductInfo
		if hasattr(data, 'ErrorMsg'):
			raise AmazonError, data.ErrorMsg
		else:
			return data.Details