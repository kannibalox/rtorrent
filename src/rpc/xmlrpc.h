// rTorrent - BitTorrent client
// Copyright (C) 2005-2011, Jari Sundell
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// In addition, as a special exception, the copyright holders give
// permission to link the code of portions of this program with the
// OpenSSL library under certain conditions as described in each
// individual source file, and distribute linked combinations
// including the two.
//
// You must obey the GNU General Public License in all respects for
// all of the code used other than OpenSSL.  If you modify file(s)
// with this exception, you may extend this exception to your version
// of the file(s), but you are not obligated to do so.  If you do not
// wish to do so, delete this exception statement from your version.
// If you delete this exception statement from all source files in the
// program, then also delete it here.
//
// Contact:  Jari Sundell <jaris@ifi.uio.no>
//
//           Skomakerveien 33
//           3185 Skoppum, NORWAY

#ifndef RTORRENT_RPC_XMLRPC_H
#define RTORRENT_RPC_XMLRPC_H

#include "tinyxml2.h"
#include <functional>

#include <torrent/hash_string.h>

namespace core {
  class Download;
}

namespace torrent {
  class File;
  class Object;
  class Tracker;
}

namespace rpc {

class XmlRpc {
public:
  typedef std::function<core::Download* (const char*)>                 slot_download;
  typedef std::function<torrent::File* (core::Download*, uint32_t)>    slot_file;
  typedef std::function<torrent::Tracker* (core::Download*, uint32_t)> slot_tracker;
  typedef std::function<torrent::Peer* (core::Download*, const torrent::HashString&)> slot_peer;
  typedef std::function<bool (const char*, uint32_t)>                  slot_write;

  // These need to match CommandMap type values.
  static const int call_generic    = 0;
  static const int call_any        = 1;
  static const int call_download   = 2;
  static const int call_peer       = 3;
  static const int call_tracker    = 4;
  static const int call_file       = 5;
  static const int call_file_itr   = 6;

  // Taken from xmlrpc-c for compatibiliy
  static const int XMLRPC_INTERNAL_ERROR = -500;
  static const int XMLRPC_TYPE_ERROR = -501;
  static const int XMLRPC_INDEX_ERROR = -502;
  static const int XMLRPC_PARSE_ERROR = -503;
  static const int XMLRPC_NETWORK_ERROR = -504;
  static const int XMLRPC_TIMEOUT_ERROR = -505;
  static const int XMLRPC_NO_SUCH_METHOD_ERROR = -506;
  static const int XMLRPC_REQUEST_REFUSED_ERROR = -507;
  static const int XMLRPC_INTROSPECTION_DISABLED_ERROR = -508;
  static const int XMLRPC_LIMIT_EXCEEDED_ERROR = -509;
  static const int XMLRPC_INVALID_UTF8_ERROR = -510;

  XmlRpc() {}

  bool                process(const char* inBuffer, uint32_t length, slot_write slotWrite);

  slot_download&      slot_find_download() { return m_slotFindDownload; }
  slot_file&          slot_find_file()     { return m_slotFindFile; }
  slot_tracker&       slot_find_tracker()  { return m_slotFindTracker; }
  slot_peer&          slot_find_peer()     { return m_slotFindPeer; }


private:

  void process_document(const tinyxml2::XMLDocument* doc, tinyxml2::XMLPrinter* printer);

  slot_download       m_slotFindDownload;
  slot_file           m_slotFindFile;
  slot_tracker        m_slotFindTracker;
  slot_peer           m_slotFindPeer;
};

}

#endif
