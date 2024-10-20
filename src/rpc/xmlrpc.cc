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

#include "config.h"

#include <cctype>
#include <string>
#include <sstream>

#ifdef HAVE_XMLRPC_C
#include <stdlib.h>
#include <xmlrpc-c/server.h>
#else
#include "vendor/tinyxml2/tinyxml2.h"
#include "utils/base64.h"
#endif

#include <rak/string_manip.h>
#include <torrent/object.h>
#include <torrent/exceptions.h>

#include "xmlrpc.h"
#include "parse_commands.h"

namespace rpc {

#ifdef HAVE_XMLRPC_C

class xmlrpc_error : public torrent::base_error {
public:
  xmlrpc_error(xmlrpc_env* env) : m_type(env->fault_code), m_msg(env->fault_string) {}
  xmlrpc_error(int type, const char* msg) : m_type(type), m_msg(msg) {}
  virtual ~xmlrpc_error() throw() {}

  virtual int         type() const throw() { return m_type; }
  virtual const char* what() const throw() { return m_msg; }

private:
  int                 m_type;
  const char*         m_msg;
};

torrent::Object xmlrpc_to_object(xmlrpc_env* env, xmlrpc_value* value, int callType = 0, rpc::target_type* target = NULL);

inline torrent::Object
xmlrpc_list_entry_to_object(xmlrpc_env* env, xmlrpc_value* src, int index) {
  xmlrpc_value* tmp;
  xmlrpc_array_read_item(env, src, index, &tmp);

  if (env->fault_occurred)
    throw xmlrpc_error(env);

  torrent::Object obj = xmlrpc_to_object(env, tmp);
  xmlrpc_DECREF(tmp);

  return obj;
}

int64_t
xmlrpc_list_entry_to_value(xmlrpc_env* env, xmlrpc_value* src, int index) {
  xmlrpc_value* tmp;
  xmlrpc_array_read_item(env, src, index, &tmp);

  if (env->fault_occurred)
    throw xmlrpc_error(env);

  switch (xmlrpc_value_type(tmp)) {
  case XMLRPC_TYPE_INT:
    int v;
    xmlrpc_read_int(env, tmp, &v);
    xmlrpc_DECREF(tmp);
    return v;

#ifdef XMLRPC_HAVE_I8
  case XMLRPC_TYPE_I8:
    xmlrpc_int64 v2;
    xmlrpc_read_i8(env, tmp, &v2);
    xmlrpc_DECREF(tmp);
    return v2;
#endif

  case XMLRPC_TYPE_STRING:
  {
    const char* str;
    xmlrpc_read_string(env, tmp, &str);

    if (env->fault_occurred)
      throw xmlrpc_error(env);

    const char* end = str;
    int64_t v3 = ::strtoll(str, (char**)&end, 0);

    ::free((void*)str);

    if (*str == '\0' || *end != '\0')
      throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Invalid index.");

    return v3;
  }

  default:
    xmlrpc_DECREF(tmp);
    throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Invalid type found.");
  }
}

// Consider making a helper function that creates a target_type from a
// torrent::Object, then we can just use xmlrpc_to_object.
rpc::target_type
xmlrpc_to_target(xmlrpc_env* env, xmlrpc_value* value) {
  rpc::target_type target;

  switch (xmlrpc_value_type(value)) {
  case XMLRPC_TYPE_STRING:
  {
    const char* str;
    xmlrpc_read_string(env, value, &str);

    if (env->fault_occurred)
      throw xmlrpc_error(env);

    if (std::strlen(str) == 0) {
      // When specifying void, we require a zero-length string.
      ::free((void*)str);
      return rpc::make_target();

    } else if (std::strlen(str) < 40) {
      ::free((void*)str);
      throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Unsupported target type found.");
    }

    core::Download* download = xmlrpc.slot_find_download()(str);

    if (download == NULL) {
      ::free((void*)str);
      throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Could not find info-hash.");
    }

    if (std::strlen(str) == 40) {
      ::free((void*)str);
      return rpc::make_target(download);
    }

    if (std::strlen(str) < 42 || str[40] != ':') {
      ::free((void*)str);
      throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Unsupported target type found.");
    }

    // Files:    "<hash>:f<index>"
    // Trackers: "<hash>:t<index>"

    int index;
    const char* end_ptr = str + 42;

    switch (str[41]) {
    case 'f':
      index = ::strtol(str + 42, (char**)&end_ptr, 0);

      if (*str == '\0' || *end_ptr != '\0')
        throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Invalid index.");

      target = rpc::make_target(XmlRpc::call_file, xmlrpc.slot_find_file()(download, index));
      break;

    case 't':
      index = ::strtol(str + 42, (char**)&end_ptr, 0);

      if (*str == '\0' || *end_ptr != '\0')
        throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Invalid index.");

      target = rpc::make_target(XmlRpc::call_tracker, xmlrpc.slot_find_tracker()(download, index));
      break;

    case 'p':
    {
      torrent::HashString hash;
      const char* hash_end = torrent::hash_string_from_hex_c_str(str + 42, hash);

      if (hash_end == end_ptr || *hash_end != '\0')
        throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Not a hash string.");

      target = rpc::make_target(XmlRpc::call_peer, xmlrpc.slot_find_peer()(download, hash));
      break;
    }
    default:
      ::free((void*)str);
      throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Unsupported target type found.");
    }

    ::free((void*)str);

    // Check if the target pointer is NULL.
    if (target.second == NULL)
      throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Invalid index.");

    return target;
  }

  default:
    return rpc::make_target();
  }
}

rpc::target_type
xmlrpc_to_index_type(int index, int callType, core::Download* download) {
  void* result;

  switch (callType) {
  case XmlRpc::call_file:    result = xmlrpc.slot_find_file()(download, index); break;
  case XmlRpc::call_tracker: result = xmlrpc.slot_find_tracker()(download, index); break;
  default: result = NULL; break;
  }

  if (result == NULL)
    throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Invalid index.");
      
  return rpc::make_target(callType, result);
}

torrent::Object
xmlrpc_to_object(xmlrpc_env* env, xmlrpc_value* value, int callType, rpc::target_type* target) {
  switch (xmlrpc_value_type(value)) {
  case XMLRPC_TYPE_INT:
    int v;
    xmlrpc_read_int(env, value, &v);
      
    return torrent::Object((int64_t)v);

#ifdef XMLRPC_HAVE_I8
  case XMLRPC_TYPE_I8:
    xmlrpc_int64 v2;
    xmlrpc_read_i8(env, value, &v2);
      
    return torrent::Object((int64_t)v2);
#endif

    //     case XMLRPC_TYPE_BOOL:
    //     case XMLRPC_TYPE_DOUBLE:
    //     case XMLRPC_TYPE_DATETIME:

  case XMLRPC_TYPE_STRING:

    if (callType != XmlRpc::call_generic) {
      // When the call type is not supposed to be void, we'll try to
      // convert it to a command target. It's not that important that
      // it is converted to the right type here, as an mismatch will
      // be caught when executing the command.
      *target = xmlrpc_to_target(env, value);
      return torrent::Object();

    } else {
      const char* valueString;
      xmlrpc_read_string(env, value, &valueString);

      if (env->fault_occurred)
        throw xmlrpc_error(env);

      torrent::Object result = torrent::Object(std::string(valueString));

      // Urgh, seriously?
      ::free((void*)valueString);
      return result;
    }

  case XMLRPC_TYPE_BASE64:
  {
    size_t      valueSize;
    const char* valueString;

    xmlrpc_read_base64(env, value, &valueSize, (const unsigned char**)&valueString);

    if (env->fault_occurred)
      throw xmlrpc_error(env);

    torrent::Object result = torrent::Object(std::string(valueString, valueSize));

    // Urgh, seriously?
    ::free((void*)valueString);
    return result;
  }

  case XMLRPC_TYPE_ARRAY:
  {
    unsigned int current = 0;
    unsigned int last = xmlrpc_array_size(env, value);

    if (env->fault_occurred)
      throw xmlrpc_error(env);

    if (callType != XmlRpc::call_generic && last != 0) {
      if (last < 1)
        throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Too few arguments.");

      xmlrpc_value* tmp;
      xmlrpc_array_read_item(env, value, current++, &tmp);

      if (env->fault_occurred)
        throw xmlrpc_error(env);

      *target = xmlrpc_to_target(env, tmp);
      xmlrpc_DECREF(tmp);

      if (env->fault_occurred)
        throw xmlrpc_error(env);

      if (target->first == XmlRpc::call_download &&
          (callType == XmlRpc::call_file || callType == XmlRpc::call_tracker)) {
        // If we have a download target and the call type requires
        // another contained type, then we try to use the next
        // parameter as the index to support old-style calls.

        if (current == last)
          throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Too few arguments, missing index.");

        *target = xmlrpc_to_index_type(xmlrpc_list_entry_to_value(env, value, current++), callType, (core::Download*)target->second);
      }
    }

    if (current + 1 < last) {
      torrent::Object result = torrent::Object::create_list();
      torrent::Object::list_type& listRef = result.as_list();

      while (current != last)
        listRef.push_back(xmlrpc_list_entry_to_object(env, value, current++));

      return result;

    } else if (current + 1 == last) {
      return xmlrpc_list_entry_to_object(env, value, current);

    } else {
      return torrent::Object();
    }
  }

  //     case XMLRPC_TYPE_STRUCT:
    //     case XMLRPC_TYPE_C_PTR:
    //     case XMLRPC_TYPE_NIL:
    //     case XMLRPC_TYPE_DEAD:
  default:
    throw xmlrpc_error(XMLRPC_TYPE_ERROR, "Unsupported type found.");
  }
}

xmlrpc_value*
object_to_xmlrpc(xmlrpc_env* env, const torrent::Object& object) {
  switch (object.type()) {
  case torrent::Object::TYPE_VALUE:

#ifdef XMLRPC_HAVE_I8
    if (xmlrpc.dialect() != XmlRpc::dialect_generic)
      return xmlrpc_i8_new(env, object.as_value());
#else
    return xmlrpc_int_new(env, object.as_value());
#endif

  case torrent::Object::TYPE_STRING:
  {
#ifdef XMLRPC_HAVE_I8
    // The versions that support I8 do implicit utf-8 validation.
    xmlrpc_value* result = xmlrpc_string_new(env, object.as_string().c_str());
#else
    // In older versions, xmlrpc-c doesn't validate the utf-8 encoding itself.
    xmlrpc_validate_utf8(env, object.as_string().c_str(), object.as_string().length());

    xmlrpc_value* result = env->fault_occurred ? NULL : xmlrpc_string_new(env, object.as_string().c_str());
#endif

    if (env->fault_occurred) {
      xmlrpc_env_clean(env);
      xmlrpc_env_init(env);

      const std::string& str = object.as_string();
      char buffer[str.size() + 1];
      char* dst = buffer;
      for (std::string::const_iterator itr = str.begin(); itr != str.end(); ++itr)
        *dst++ = ((*itr < 0x20 && *itr != '\r' && *itr != '\n' && *itr != '\t') || (*itr & 0x80)) ? '?' : *itr;
      *dst = 0;

      result = xmlrpc_string_new(env, buffer);
    }

    return result;
  }

  case torrent::Object::TYPE_LIST:
  {
    xmlrpc_value* result = xmlrpc_array_new(env);
    
    for (torrent::Object::list_const_iterator itr = object.as_list().begin(), last = object.as_list().end(); itr != last; itr++) {
      xmlrpc_value* item = object_to_xmlrpc(env, *itr);
      xmlrpc_array_append_item(env, result, item);
      xmlrpc_DECREF(item);
    }

    return result;
  }

  case torrent::Object::TYPE_MAP:
  {
    xmlrpc_value* result = xmlrpc_struct_new(env);
    
    for (torrent::Object::map_const_iterator itr = object.as_map().begin(), last = object.as_map().end(); itr != last; itr++) {
      xmlrpc_value* item = object_to_xmlrpc(env, itr->second);
      xmlrpc_struct_set_value(env, result, itr->first.c_str(), item);
      xmlrpc_DECREF(item);
    }

    return result;
  }

  case torrent::Object::TYPE_DICT_KEY:
  {
    xmlrpc_value* result = xmlrpc_array_new(env);
    
    xmlrpc_value* key_item = object_to_xmlrpc(env, object.as_dict_key());
    xmlrpc_array_append_item(env, result, key_item);
    xmlrpc_DECREF(key_item);
    
    if (object.as_dict_obj().is_list()) {
      for (torrent::Object::list_const_iterator
             itr = object.as_dict_obj().as_list().begin(),
             last = object.as_dict_obj().as_list().end();
           itr != last; itr++) {
        xmlrpc_value* item = object_to_xmlrpc(env, *itr);
        xmlrpc_array_append_item(env, result, item);
        xmlrpc_DECREF(item);
      }
    } else {
      xmlrpc_value* arg_item = object_to_xmlrpc(env, object.as_dict_obj());
      xmlrpc_array_append_item(env, result, arg_item);
      xmlrpc_DECREF(arg_item);
    }

    return result;
  }

  default:
    return xmlrpc_int_new(env, 0);
  }
}

xmlrpc_value*
xmlrpc_call_command(xmlrpc_env* env, xmlrpc_value* args, void* voidServerInfo) {
  CommandMap::iterator itr = commands.find((const char*)voidServerInfo);

  if (itr == commands.end()) {
    xmlrpc_env_set_fault(env, XMLRPC_PARSE_ERROR, ("Command \"" + std::string((const char*)voidServerInfo) + "\" does not exist.").c_str());
    return NULL;
  }

  try {
    torrent::Object object;
    rpc::target_type target = rpc::make_target();

    if (itr->second.m_flags & CommandMap::flag_no_target)
      xmlrpc_to_object(env, args, XmlRpc::call_generic, &target).swap(object);
    else if (itr->second.m_flags & CommandMap::flag_file_target)
      xmlrpc_to_object(env, args, XmlRpc::call_file, &target).swap(object);
    else if (itr->second.m_flags & CommandMap::flag_tracker_target)
      xmlrpc_to_object(env, args, XmlRpc::call_tracker, &target).swap(object);
    else
      xmlrpc_to_object(env, args, XmlRpc::call_any, &target).swap(object);

    if (env->fault_occurred)
      return NULL;

    return object_to_xmlrpc(env, rpc::commands.call_command(itr, object, target));

  } catch (xmlrpc_error& e) {
    xmlrpc_env_set_fault(env, e.type(), e.what());
    return NULL;

  } catch (torrent::local_error& e) {
    xmlrpc_env_set_fault(env, XMLRPC_PARSE_ERROR, e.what());
    return NULL;
  }
}

void
XmlRpc::initialize() {
#ifndef XMLRPC_HAVE_I8
  m_dialect = dialect_generic;
#endif
  
  m_env = new xmlrpc_env;

  xmlrpc_env_init((xmlrpc_env*)m_env);
  m_registry = xmlrpc_registry_new((xmlrpc_env*)m_env);
}

void
XmlRpc::cleanup() {
  if (!is_valid())
    return;

  xmlrpc_registry_free((xmlrpc_registry*)m_registry);
  xmlrpc_env_clean((xmlrpc_env*)m_env);
  delete (xmlrpc_env*)m_env;
}

bool
XmlRpc::process(const char* inBuffer, uint32_t length, slot_write slotWrite) {
  xmlrpc_env localEnv;
  xmlrpc_env_init(&localEnv);

  xmlrpc_mem_block* memblock = xmlrpc_registry_process_call(&localEnv, (xmlrpc_registry*)m_registry, NULL, inBuffer, length);

  if (localEnv.fault_occurred && localEnv.fault_code == XMLRPC_INTERNAL_ERROR)
    throw torrent::internal_error("Internal error in XMLRPC.");

  bool result = slotWrite((const char*)xmlrpc_mem_block_contents(memblock),
                          xmlrpc_mem_block_size(memblock));

  xmlrpc_mem_block_free(memblock);
  xmlrpc_env_clean(&localEnv);
  return result;
}

void
XmlRpc::insert_command(const char* name, const char* parm, const char* doc) {
  xmlrpc_env localEnv;
  xmlrpc_env_init(&localEnv);

  xmlrpc_registry_add_method_w_doc(&localEnv, (xmlrpc_registry*)m_registry, NULL, name,
                                   &xmlrpc_call_command, const_cast<char*>(name), parm, doc);

  if (localEnv.fault_occurred)
    throw torrent::internal_error("Fault occured while inserting xmlrpc call.");

  xmlrpc_env_clean(&localEnv);
}

void
XmlRpc::set_dialect(int dialect) {
  if (!is_valid())
    throw torrent::input_error("Cannot select XMLRPC dialect before it is initialized.");

  xmlrpc_env localEnv;
  xmlrpc_env_init(&localEnv);

  switch (dialect) {
  case dialect_generic:
    break;

#ifdef XMLRPC_HAVE_I8
  case dialect_i8:
    xmlrpc_registry_set_dialect(&localEnv, (xmlrpc_registry*)m_registry, xmlrpc_dialect_i8);
    break;

  case dialect_apache:
    xmlrpc_registry_set_dialect(&localEnv, (xmlrpc_registry*)m_registry, xmlrpc_dialect_apache);
    break;
#endif

  default:
    xmlrpc_env_clean(&localEnv);
    throw torrent::input_error("Unsupported XMLRPC dialect selected.");
  }

  if (localEnv.fault_occurred) {
    xmlrpc_env_clean(&localEnv);
    throw torrent::input_error("Unsupported XMLRPC dialect selected.");
  }

  xmlrpc_env_clean(&localEnv);
  m_dialect = dialect;
}

int64_t
XmlRpc::size_limit() {
  return xmlrpc_limit_get(XMLRPC_XML_SIZE_LIMIT_ID);
}

void
XmlRpc::set_size_limit(uint64_t size) {
  if (size >= (64 << 20))
    throw torrent::input_error("Invalid XMLRPC limit size.");

  xmlrpc_limit_set(XMLRPC_XML_SIZE_LIMIT_ID, size);
}

bool
is_valid() const { return m_env != NULL; }

#else

class xmlrpc_error : public torrent::base_error {
public:
  xmlrpc_error(int type, std::string msg) : m_type(type), m_msg(msg) {}
  virtual ~xmlrpc_error() throw() {}

  virtual int         type() const throw() { return m_type; }
  virtual const char* what() const throw() { return m_msg.c_str(); }

private:
  int                 m_type;
  std::string         m_msg;
};

const tinyxml2::XMLElement* element_access(const tinyxml2::XMLElement* elem, std::string elemNames) {
  // Helper function to check each step of a element access, in liueu of XPath
  char token = ',';
  std::stringstream ss(elemNames);
  std::string item;
  std::string stack("");
  const tinyxml2::XMLElement* result = elem;
  while (getline (ss, item, token)) {
    stack.append(item);
    result = result->FirstChildElement(item.c_str());
    if (result == nullptr) {
      throw xmlrpc_error(XmlRpc::XMLRPC_PARSE_ERROR, "could not find expected element " + stack);
    }
    stack.append("->");
  }
  return result;
}

torrent::Object xml_value_to_object(const tinyxml2::XMLNode* elem) {
  if (elem == nullptr) {
    throw xmlrpc_error(XmlRpc::XMLRPC_INTERNAL_ERROR, "received null element to convert");
  }
  if (std::string("value") != elem->Value()) {
    throw xmlrpc_error(XmlRpc::XMLRPC_INTERNAL_ERROR, "received non-value element to convert");
  }
  auto valueElem = elem->FirstChild();
  auto elemType = std::string(valueElem->Value());
  if (elemType == "string") {
    auto elemChild = valueElem->FirstChild();
    if (elemChild == nullptr) {
      return torrent::Object("");
    }
    return torrent::Object(elemChild->ToText()->Value());
  } else if (elemType == "i4" or elemType == "i8" or elemType == "int") {
    auto elemValue = std::string(valueElem->FirstChild()->ToText()->Value());
    return torrent::Object(std::stol(elemValue));
  } else if (elemType == "boolean") {
    auto boolText = std::string(valueElem->FirstChild()->ToText()->Value());
    if (boolText == "1") {
      return torrent::Object((int64_t)1);
    } else if (boolText == "0") {
      return torrent::Object((int64_t)0);
    }
  } else if (elemType == "array") {
    auto array = torrent::Object::create_list();
    auto dataElem = element_access(valueElem->ToElement(), "data");
    for (auto arrayValueElem = dataElem->FirstChildElement("value"); arrayValueElem; arrayValueElem = arrayValueElem->NextSiblingElement("value")) {
      array.as_list().push_back(xml_value_to_object(arrayValueElem));
    }
    return array;
  } else if (elemType == "struct") {
    auto map = torrent::Object::create_map();
    for (auto memberElem = valueElem->FirstChildElement("member"); memberElem; memberElem = memberElem->NextSiblingElement("member")) {
      auto key = memberElem->FirstChildElement("name")->GetText();
      auto valElem = xml_value_to_object(element_access(memberElem, "value"));
      map.as_map()[key] = valElem;
    }
    return map;
  } else if (elemType == "base64") {
    auto elemChild = valueElem->FirstChild();
    if (elemChild == nullptr) {
      return torrent::Object("");
    }
    auto base64string = std::string(elemChild->ToText()->Value());
    base64string.erase(std::remove_if(base64string.begin(), base64string.end(),
                                      [](char c) { return c == '\n' || c == '\r'; }),
                       base64string.end());
    return torrent::Object(utils::base64decode(base64string));
    return torrent::Object("");
  } else {
    throw xmlrpc_error(XmlRpc::XMLRPC_INTERNAL_ERROR, "received unsupported value type: " + elemType);
  }
  return torrent::Object();
}

void print_object_xml(const torrent::Object& obj, tinyxml2::XMLPrinter* printer) {
  switch (obj.type()) {
  case torrent::Object::TYPE_STRING:
    printer->OpenElement("string", true);
    printer->PushText(obj.as_string().c_str());
    printer->CloseElement(true);
    break;
  case torrent::Object::TYPE_VALUE:
    if (obj.as_value() > ((torrent::Object::value_type)2 << 30) || obj.as_value() < -((torrent::Object::value_type)2 << 30)) {
      printer->OpenElement("i8", true);
    } else {
      printer->OpenElement("i4", true);
    }
    printer->PushText(std::to_string(obj.as_value()).c_str());
    printer->CloseElement(true);
    break;
  case torrent::Object::TYPE_LIST:
    printer->OpenElement("array", true);
    for (const auto& itr : obj.as_list()) {
      printer->OpenElement("value", true);
      print_object_xml(itr, printer);
      printer->CloseElement(true);
    }
    printer->CloseElement(true);
    break;
  case torrent::Object::TYPE_MAP:
    printer->OpenElement("struct", true);
    for (const auto& itr : obj.as_map()) {
      printer->OpenElement("member", true);
      printer->OpenElement("name", true);
      printer->PushText(itr.first.c_str());
      printer->CloseElement(true);
      printer->OpenElement("value", true);
      print_object_xml(itr.second, printer);
      printer->CloseElement(true);
      printer->CloseElement(true);
    }
    printer->CloseElement(true);
    break;
  default:
    throw xmlrpc_error(XmlRpc::XMLRPC_INTERNAL_ERROR, "unsupport object type received");
  }
}

void object_to_target(const torrent::Object& obj, int callFlags, rpc::target_type* target) {
  if (!obj.is_string()) {
    throw torrent::input_error("invalid parameters: target must be a string");
  }
  std::string targetString = obj.as_string();
  bool requireIndex = (callFlags & (CommandMap::flag_tracker_target | CommandMap::flag_file_target));
  if (targetString.size() == 0 && !requireIndex) {
    return;
  }

  // Length of SHA1 hash is 40
  if (targetString.size() < 40) {
    throw torrent::input_error("invalid parameters: invalid target");
  }

  char type = 'd';
  std::string hash;
  std::string index;
  const auto& delimPos = targetString.find_first_of(':', 40);
  if (delimPos == targetString.npos ||
      delimPos + 2 >= targetString.size()) {
	if (requireIndex) {
      throw torrent::input_error("invalid parameters: no index");
    }
    hash = targetString;
  } else {
    hash  = targetString.substr(0, delimPos);
    type  = targetString[delimPos + 1];
    index = targetString.substr(delimPos + 2);
  }
  core::Download* download = xmlrpc.slot_find_download()(hash.c_str());

  if (download == nullptr)
    throw torrent::input_error("invalid parameters: info-hash not found");

  try {
    switch (type) {
      case 'd':
        *target = rpc::make_target(download);
        break;
      case 'f':
        *target = rpc::make_target(
          command_base::target_file,
          xmlrpc.slot_find_file()(download, std::stoi(std::string(index))));
        break;
      case 't':
        *target = rpc::make_target(
          command_base::target_tracker,
          xmlrpc.slot_find_tracker()(download, std::stoi(std::string(index))));
        break;
      case 'p': {
          if (index.size() < 40) {
            throw xmlrpc_error(XmlRpc::XMLRPC_TYPE_ERROR, "Not a hash string.");
          }
          torrent::HashString hash;
          torrent::hash_string_from_hex_c_str(index.c_str(), hash);
          *target = rpc::make_target(
                                     command_base::target_peer,
                                     xmlrpc.slot_find_peer()(download, hash));
          break;
      }
      default:
        throw torrent::input_error(
          "invalid parameters: unexpected target type");
    }
  } catch (const std::logic_error&) {
    throw torrent::input_error("invalid parameters: invalid index");
  }
}

torrent::Object execute_command(std::string methodName, const tinyxml2::XMLElement* paramsElem) {
  if (paramsElem == nullptr) {
    throw xmlrpc_error(XmlRpc::XMLRPC_INTERNAL_ERROR, "invalid parameters: null");
  }
  CommandMap::iterator cmdItr = commands.find(methodName.c_str());
  if (cmdItr == commands.end() || !(cmdItr->second.m_flags & CommandMap::flag_public_xmlrpc)) {
    throw xmlrpc_error(XmlRpc::XMLRPC_NO_SUCH_METHOD_ERROR, "Method '" + std::string(methodName) + "' not defined");
  }
  torrent::Object paramsRaw = torrent::Object::create_list();
  torrent::Object::list_type& params = paramsRaw.as_list();
  if (paramsElem != nullptr) {
    for (auto paramElem = paramsElem->FirstChildElement("param"); paramElem; paramElem = paramElem->NextSiblingElement("param")) {
      params.push_back(xml_value_to_object(paramElem->FirstChildElement("value")));
    }
  }
  rpc::target_type target = rpc::make_target();
  if (params.size() == 0 && (cmdItr->second.m_flags & (CommandMap::flag_file_target | CommandMap::flag_tracker_target))) {
    throw xmlrpc_error(XmlRpc::XMLRPC_INTERNAL_ERROR, "invalid parameters: too few");
  }
  if (params.size() > 0) {
    object_to_target(params.front(), cmdItr->second.m_flags, &target);
    params.erase(params.begin());
  }
  return rpc::commands.call_command(cmdItr, paramsRaw, target);
}

void process_document(const tinyxml2::XMLDocument* doc, tinyxml2::XMLPrinter* printer) {
  if (doc->Error()) {
    throw xmlrpc_error(XmlRpc::XMLRPC_PARSE_ERROR, doc->ErrorStr());
  }
  if (doc->FirstChildElement("methodCall") == nullptr) {
    throw xmlrpc_error(XmlRpc::XMLRPC_PARSE_ERROR, "methodCall element not found");
  }
  auto methodName = element_access(doc->FirstChildElement("methodCall"), "methodName")->GetText();
  torrent::Object result;

  // Add a shim here for system.multicall to allow better code reuse, and
  // because system.multicall is one of the few methods that doesn't take a target
  if (methodName == std::string("system.multicall")) {
    result = torrent::Object::create_list();
    torrent::Object::list_type& resultList = result.as_list();
    auto valueElems = element_access(doc->RootElement(), "params,param,value,array,data");
    for (auto structElem = valueElems->FirstChildElement("value"); structElem; structElem = structElem->NextSiblingElement("value")) {
      auto subMethodName = element_access(structElem, "struct,member,value,string")->GetText();
      //auto subMethodName = structElem->FirstChildElement("value")->FirstChildElement("string")->GetText();
      auto subParams = element_access(structElem, "struct,member")->NextSiblingElement("member")->FirstChildElement("value");
      try {
        auto subResult = torrent::Object::create_list();
        subResult.as_list().push_back(execute_command(subMethodName, subParams));
        resultList.push_back(subResult);
      } catch (xmlrpc_error& e) {
        auto fault = torrent::Object::create_map();
        fault.as_map()["faultString"] = e.what();
        fault.as_map()["faultCode"] = e.type();
        resultList.push_back(fault);
      } catch (torrent::local_error& e) {
        auto fault = torrent::Object::create_map();
        fault.as_map()["faultString"] = e.what();
        fault.as_map()["faultCode"] = XmlRpc::XMLRPC_INTERNAL_ERROR;
        resultList.push_back(fault);
      }
    }
  } else {
    result = execute_command(methodName, doc->FirstChildElement("methodCall")->FirstChildElement("params"));
  }

  printer->PushHeader(false, true);
  printer->OpenElement("methodReponse", true);
  printer->OpenElement("params", true);

  printer->OpenElement("param", true);
  printer->OpenElement("value", true);
  print_object_xml(result, printer);
  printer->CloseElement(true);
  printer->CloseElement(true);

  printer->CloseElement(true);
  printer->CloseElement(true);
  tinyxml2::XMLDocument resultDoc;
}


void print_xmlrpc_fault(int faultCode, std::string faultString, tinyxml2::XMLPrinter* printer) {
  printer->OpenElement("methodReponse", true);
  printer->OpenElement("fault", true);
  printer->OpenElement("struct", true);

  printer->OpenElement("member", true);
  printer->OpenElement("name", true);
  printer->PushText("faultCode");
  printer->CloseElement(true);
  printer->OpenElement("value", true);
  printer->OpenElement("int", true);
  printer->PushText(faultCode);
  printer->CloseElement(true);
  printer->CloseElement(true);
  printer->CloseElement(true);

  printer->OpenElement("member", true);
  printer->OpenElement("name", true);
  printer->PushText("faultString");
  printer->CloseElement(true);
  printer->OpenElement("value", true);
  printer->OpenElement("string", true);
  printer->PushText(faultString.c_str());
  printer->CloseElement(true);
  printer->CloseElement(true);
  printer->CloseElement(true);

  printer->CloseElement(true);
  printer->CloseElement(true);
  printer->CloseElement(true);
}

bool
XmlRpc::process(const char* inBuffer, uint32_t length, slot_write slotWrite) {
  tinyxml2::XMLPrinter printer(nullptr, true, 0);
  tinyxml2::XMLDocument doc;
  doc.Parse(inBuffer, length);
  try {
    process_document(&doc, &printer);
  } catch (xmlrpc_error& e) {
    printer.ClearBuffer();
    printer.PushHeader(false, true);
    print_xmlrpc_fault(e.type(), e.what(), &printer);
  } catch (torrent::local_error& e) {
    printer.ClearBuffer();
    printer.PushHeader(false, true);
    print_xmlrpc_fault(XMLRPC_INTERNAL_ERROR, e.what(), &printer);
  }
  return slotWrite(printer.CStr(), printer.CStrSize()-1);
}

void XmlRpc::initialize() { m_isValid = true; }
void XmlRpc::cleanup() {}

void XmlRpc::insert_command(__UNUSED const char* name, __UNUSED const char* parm, __UNUSED const char* doc) {}
void XmlRpc::set_dialect(__UNUSED int dialect) {}

int64_t XmlRpc::size_limit() { return 0; }
void    XmlRpc::set_size_limit(uint64_t size) {}

bool    XmlRpc::is_valid() const { return m_isValid; }

#endif

}

