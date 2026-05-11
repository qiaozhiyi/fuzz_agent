#include "fuzzpilot/env.hpp"

#include "fuzzpilot/runner/process.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <sys/utsname.h>

namespace fuzzpilot {
namespace {

std::string first_nonempty_line(const std::string& text) {
  std::istringstream input(text);
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      return line;
    }
  }
  return {};
}

std::string first_nonempty_line_from_process(const std::string& executable,
                                             const std::vector<std::string>& argv) {
  const auto result = run_process_capture(executable, argv, {}, true);
  if (!result.spawned) {
    return {};
  }
  return first_nonempty_line(result.output);
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
  snapshot.afl_version = first_nonempty_line_from_process(afl_fuzz_path,
                                                          {afl_fuzz_path, "-h"});
  snapshot.compiler_version = first_nonempty_line_from_process("c++", {"c++", "--version"});
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
