// tools/mutator_microbench/main.cpp
//
// Paper 1, Experiment E3: pure mutator throughput microbenchmark.
//
// Three configurations (all three go through the same dlopen+dlsym path,
// only the loaded mutator .so differs — this makes the comparison fair):
//   --config vanilla     load tools/mutator_microbench/libvanilla_havoc.so,
//                        an AFL++ havoc-equivalent mutator (bit flip, arith,
//                        interesting values, splice, delete/clone blocks).
//                        Exposes the same fp_mutator_* ABI as the FuzzPilot
//                        mutator so dispatch overhead is symmetric.
//   --config fp-empty    load mutators/fuzzpilot/libfuzzpilot_mutator.so
//                        with an empty recipe store (pure dispatch path).
//   --config fp-active   load mutators/fuzzpilot/libfuzzpilot_mutator.so
//                        with a populated recipe store (real-world cost).
//
// Note on the 'vanilla' baseline: AFL++ does not export its havoc as a
// library, so libvanilla_havoc.so re-implements a representative subset
// of AFL++ havoc ops with matched cost profile. The point of F5 is not
// to compete with AFL++ havoc on quality — it is to show that the
// FuzzPilot mutator dispatch + recipe lookup is in the same order of
// magnitude as a plain AFL-style mutator. End-to-end exec/sec parity
// is verified separately from E1a vs E1b fuzzer_stats (see §6.2).
//
// Output JSON (per run):
// {
//   "config": "fp-active",
//   "iterations": 100000,
//   "seed_count": 10000,
//   "mean_ns_per_call": 1234.5,
//   "mean_exec_per_sec": 810000.0,
//   "stddev": 56.7,
//   "afl_version": "...",
//   "git_commit": "..."
// }
//
// This file is intentionally self-contained — no Fuzzpilot core deps. The
// FuzzPilot mutator is loaded as a shared library via dlopen() so we don't
// have to link the whole controller.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Mirror of the symbols exposed by mutators/fuzzpilot/afl_custom_mutator.c
extern "C" {
using fp_init_fn = void* (*)(unsigned int);
using fp_fuzz_fn = size_t (*)(void*, unsigned char*, size_t,
                              unsigned char**, unsigned char*, size_t, size_t);
using fp_deinit_fn = void (*)(void*);
}

struct Args {
  std::string config;
  std::string seeds_dir;
  uint64_t iterations = 100000;
  uint64_t seed_count = 10000;
  std::string out_path;
  std::string mutator_path = "build/mutators/fuzzpilot/libfuzzpilot_mutator.so";
  std::string vanilla_mutator_path = "build/tools/mutator_microbench/libvanilla_havoc.so";
  std::string recipe_store;
};

bool parse_args(int argc, char** argv, Args& a) {
  for (int i = 1; i < argc; ++i) {
    std::string k = argv[i];
    auto need = [&]() -> const char* {
      if (i + 1 >= argc) { std::cerr << "missing value for " << k << "\n"; std::exit(2); }
      return argv[++i];
    };
    if (k == "--config")      a.config = need();
    else if (k == "--seeds")  a.seeds_dir = need();
    else if (k == "--iterations") a.iterations = std::stoull(need());
    else if (k == "--seed-count") a.seed_count = std::stoull(need());
    else if (k == "--out")    a.out_path = need();
    else if (k == "--mutator-path") a.mutator_path = need();
    else if (k == "--vanilla-mutator-path") a.vanilla_mutator_path = need();
    else if (k == "--recipe-store") a.recipe_store = need();
    else { std::cerr << "unknown arg: " << k << "\n"; return false; }
  }
  if (a.config.empty() || a.seeds_dir.empty() || a.out_path.empty()) {
    std::cerr << "usage: mutator_microbench --config vanilla|fp-empty|fp-active "
                 "--seeds DIR --iterations N --seed-count N --out OUT.json "
                 "[--mutator-path SO] [--recipe-store DIR]\n";
    return false;
  }
  return true;
}

std::vector<std::vector<uint8_t>> load_seeds(const fs::path& dir, size_t limit) {
  std::vector<std::vector<uint8_t>> seeds;
  if (!fs::exists(dir)) {
    std::cerr << "seeds dir missing: " << dir << "\n";
    return seeds;
  }
  for (auto& p : fs::directory_iterator(dir)) {
    if (!p.is_regular_file()) continue;
    std::ifstream f(p.path(), std::ios::binary);
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (!buf.empty()) seeds.push_back(std::move(buf));
    if (seeds.size() >= limit) break;
  }
  // If we have fewer than requested, fill by cycling existing
  if (seeds.empty()) {
    seeds.push_back({'{', '}', 0});
  }
  while (seeds.size() < limit) seeds.push_back(seeds[seeds.size() % seeds.size()]);
  return seeds;
}

double run_fp(const Args& a, const std::vector<std::vector<uint8_t>>& seeds, uint64_t iters) {
  void* handle = dlopen(a.mutator_path.c_str(), RTLD_NOW);
  if (!handle) {
    std::cerr << "dlopen failed: " << dlerror() << "\n";
    std::cerr << "ensure FuzzPilot mutator built: " << a.mutator_path << "\n";
    std::exit(3);
  }
  auto init_fn = (fp_init_fn)dlsym(handle, "fp_mutator_init");
  auto fuzz_fn = (fp_fuzz_fn)dlsym(handle, "fp_mutator_fuzz");
  auto deinit_fn = (fp_deinit_fn)dlsym(handle, "fp_mutator_deinit");
  if (!init_fn || !fuzz_fn || !deinit_fn) {
    std::cerr << "dlsym failed\n"; std::exit(3);
  }

  if (!a.recipe_store.empty()) {
    setenv("FUZZPILOT_RECIPE_STORE", a.recipe_store.c_str(), 1);
  }

  void* state = init_fn(0xDEADBEEF);
  std::vector<uint8_t> buf(1 << 16);
  unsigned char* out = nullptr;

  auto t0 = std::chrono::steady_clock::now();
  for (uint64_t i = 0; i < iters; ++i) {
    const auto& s = seeds[i % seeds.size()];
    std::memcpy(buf.data(), s.data(), std::min(s.size(), buf.size()));
    fuzz_fn(state, buf.data(), s.size(), &out, nullptr, 0, buf.size());
  }
  auto t1 = std::chrono::steady_clock::now();
  deinit_fn(state);
  dlclose(handle);
  return std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(iters);
}

int main(int argc, char** argv) {
  Args a;
  if (!parse_args(argc, argv, a)) return 2;

  std::cerr << "loading seeds from " << a.seeds_dir << " (limit " << a.seed_count << ")\n";
  auto seeds = load_seeds(a.seeds_dir, a.seed_count);
  std::cerr << "loaded " << seeds.size() << " seeds; running " << a.iterations
            << " iterations under config '" << a.config << "'\n";

  double ns_per_call = 0.0;
  if (a.config == "vanilla") {
    Args copy = a;
    copy.mutator_path = a.vanilla_mutator_path;
    copy.recipe_store.clear();
    ns_per_call = run_fp(copy, seeds, a.iterations);
  } else if (a.config == "fp-empty") {
    Args copy = a; copy.recipe_store.clear();
    ns_per_call = run_fp(copy, seeds, a.iterations);
  } else if (a.config == "fp-active") {
    ns_per_call = run_fp(a, seeds, a.iterations);
  } else {
    std::cerr << "unknown --config: " << a.config << "\n";
    return 2;
  }

  double exec_per_sec = 1e9 / ns_per_call;

  std::ostringstream oss;
  oss << "{"
      << "\"config\":\"" << a.config << "\","
      << "\"iterations\":" << a.iterations << ","
      << "\"seed_count\":" << seeds.size() << ","
      << "\"mean_ns_per_call\":" << ns_per_call << ","
      << "\"mean_exec_per_sec\":" << exec_per_sec << ","
      << "\"stddev\":0.0"  // single-shot here; multiple-run stddev computed by aggregate.py
      << "}";

  fs::create_directories(fs::path(a.out_path).parent_path());
  std::ofstream out(a.out_path);
  out << oss.str() << "\n";
  std::cerr << "wrote " << a.out_path << "\n"
            << oss.str() << "\n";
  return 0;
}
