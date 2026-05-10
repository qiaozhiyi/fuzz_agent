#include "fuzzpilot/env.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <sys/utsname.h>

namespace fuzzpilot {
namespace {

std::string first_nonempty_line_from_command(const std::string& command) {
  std::array<char, 256> buffer{};
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return {};
  }
  std::string output;
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    std::string line = buffer.data();
    if (!line.empty() && line.back() == '\n') {
      line.pop_back();
    }
    if (!line.empty()) {
      output = line;
      break;
    }
  }
  pclose(pipe);
  return output;
}

std::string json_escape(const std::string& value) {
  std::ostringstream out;
  for (const char c : value) {
    switch (c) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\n': out << "\\n"; break;
      default: out << c; break;
    }
  }
  return out.str();
}

}  // namespace

EnvSnapshot capture_env_snapshot(const std::string& afl_fuzz_path) {
  EnvSnapshot snapshot;
  utsname info{};
  if (uname(&info) == 0) {
    snapshot.os = info.sysname;
    snapshot.arch = info.machine;
    snapshot.kernel = std::string(info.release) + " " + info.version;
  }
  snapshot.afl_version = first_nonempty_line_from_command(afl_fuzz_path + " -h 2>&1");
  snapshot.compiler_version = first_nonempty_line_from_command("c++ --version 2>&1");
  return snapshot;
}

std::string env_snapshot_json(const EnvSnapshot& snapshot) {
  std::ostringstream out;
  out << "{";
  out << "\"os\":\"" << json_escape(snapshot.os) << "\",";
  out << "\"arch\":\"" << json_escape(snapshot.arch) << "\",";
  out << "\"kernel\":\"" << json_escape(snapshot.kernel) << "\",";
  out << "\"afl_version\":\"" << json_escape(snapshot.afl_version) << "\",";
  out << "\"compiler_version\":\"" << json_escape(snapshot.compiler_version) << "\"";
  out << "}";
  return out.str();
}

}  // namespace fuzzpilot
