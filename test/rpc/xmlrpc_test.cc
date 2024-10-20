#include "config.h"
#include <string>
#include <iostream>

#include "command_helpers.h"
#include "rpc/command_map.h"

#include "xmlrpc_test.h"
#include "control.h"
#include "globals.h"

CPPUNIT_TEST_SUITE_REGISTRATION(XmlrpcTest);

// #undef CMD2_A_FUNCTION

// #define CMD2_A_FUNCTION(key, function, slot, parm, doc)      \
//   m_map.insert_slot<rpc::command_base_is_type<rpc::function>::type>(key, slot, &rpc::function,   \
//                     rpc::CommandMap::flag_dont_delete | rpc::CommandMap::flag_public_xmlrpc, NULL, NULL);

torrent::Object xmlprpc_cmd_test_map_a(rpc::target_type t, const torrent::Object& obj) { return obj; }
torrent::Object xmlprpc_cmd_test_map_b(rpc::target_type t, const torrent::Object& obj, uint64_t c) { return torrent::Object(c); }

torrent::Object xmlprpc_cmd_test_any_string(__UNUSED rpc::target_type target, const std::string& rawArgs) { return (int64_t)3; }

void initialize_command_dynamic();

void
XmlrpcTest::setUp() {
  m_commandItr = m_commands;
  m_xmlrpc = rpc::XmlRpc();
    setlocale(LC_ALL, "");
    cachedTime = rak::timer::current();
    control = new Control;
    //initialize_command_dynamic();
    CMD2_ANY("test_a", &xmlprpc_cmd_test_map_a);
    CMD2_ANY("test_b", std::bind(&xmlprpc_cmd_test_map_b, std::placeholders::_1, std::placeholders::_2, (uint64_t)2));
    CMD2_ANY_STRING("any_string", &xmlprpc_cmd_test_any_string);
}

void
XmlrpcTest::test_basics() {
  std::ifstream file; file.open("rpc/xmlrpc_test_data.txt");
  CPPUNIT_ASSERT(file.good());
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::string line;
  int index = 0;
  // Read file into inputs/outputs
  while (std::getline(file, line)) {
    std::cout << "doc: " << index << " " << line << "\n";
    if (line[0] == '#') {
      continue;
    }
    if (index % 2) {
      outputs.push_back(line);
    } else {
      inputs.push_back(line);
    }
    index++;
  }

  for (int i = 0; i < inputs.size(); i++) {    
    auto output = std::string("");
    m_xmlrpc.process(inputs[i].c_str(), inputs[i].size(), [&output](const char* c, uint32_t l){ output.append(c, l); return true;});
    CPPUNIT_ASSERT_EQUAL(std::string(outputs[i]), output);
  }
}
