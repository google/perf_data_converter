// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "perf_option_parser.h"

#include <algorithm>
#include <map>

#include "string_utils.h"

namespace quipper {

namespace {

enum class OptionType {
  Boolean,  // has no value
  Value,    // uses another argument.
};

const std::map<std::string, OptionType>& GetPerfRecordOptions() {
  static const auto* kPerfRecordOptions = new std::map<std::string, OptionType>{
      {"-e", OptionType::Value},
      {"--event", OptionType::Value},
      {"--filter", OptionType::Value},
      {"-p", OptionType::Value},
      {"--pid", OptionType::Value},
      {"-t", OptionType::Value},
      {"--tid", OptionType::Value},
      {"-r", OptionType::Value},
      {"--realtime", OptionType::Value},
      /* Banned: {"--no-buffering", OptionType::Boolean}, */
      {"-R", OptionType::Boolean},
      {"--raw-samples", OptionType::Boolean},
      {"-a", OptionType::Boolean},
      {"--all-cpus", OptionType::Boolean},
      {"-C", OptionType::Value},
      {"--cpu", OptionType::Value},
      {"-c", OptionType::Value},
      {"--count", OptionType::Value},
      {"--code-page-size", OptionType::Boolean},
      {"--data-page-size", OptionType::Boolean},
      /* Banned: {"-o", OptionType::Value},
       * {"--output", OptionType::Value}, */
      {"-i", OptionType::Boolean},
      {"--no-inherit", OptionType::Boolean},
      {"-F", OptionType::Value},
      {"--freq", OptionType::Value},
      /* Banned: {"-m", OptionType::Value},
       * {"--mmap-pages", OptionType::Value}, */
      {"--group", OptionType::Boolean}, /* new? */
      {"-g", OptionType::Boolean}, /* NB: in stat, this is short for --group */
      {"--call-graph", OptionType::Value},
      /* Banned: {"-v", OptionType::Boolean},
       * {"--verbose", OptionType::Boolean}, */
      /* Banned: {"-q", OptionType::Boolean},
       * {"--quiet", OptionType::Boolean}, */
      {"-s", OptionType::Boolean},
      {"--stat", OptionType::Boolean},
      {"-d", OptionType::Boolean},
      {"--data", OptionType::Boolean},
      {"-T", OptionType::Boolean},
      {"--timestamp", OptionType::Boolean},
      {"-P", OptionType::Boolean},       /* new? */
      {"--period", OptionType::Boolean}, /* new? */
      {"-n", OptionType::Boolean},
      {"--no-samples", OptionType::Boolean},
      {"-N", OptionType::Boolean},
      {"--no-buildid-cache", OptionType::Boolean},
      {"-B", OptionType::Boolean},           /* new? */
      {"--no-buildid", OptionType::Boolean}, /* new? */
      {"-G", OptionType::Value},
      {"--cgroup", OptionType::Value},
      /* Changed between v3.13 to v3.14 from:
      {"-D", OptionType::Boolean},
      {"--no-delay", OptionType::Boolean},
       * to:
      {"-D", OptionType::Value},
      {"--delay", OptionType::Value},
       * ... So just ban it until the new option is universal on ChromeOS perf.
       */
      {"-u", OptionType::Value},
      {"--uid", OptionType::Value},
      {"-b", OptionType::Boolean},
      {"--branch-any", OptionType::Boolean},
      {"-j", OptionType::Value},
      {"--branch-filter", OptionType::Value},
      {"-W", OptionType::Boolean},
      {"--weight", OptionType::Boolean},
      {"--transaction", OptionType::Boolean},
      /* Banned: {"--per-thread", OptionType::Boolean},
       * Only briefly present in v3.12-v3.13, but also banned:
       * {"--force-per-cpu", OptionType::Boolean}, */
      /* Banned: {"-I", OptionType::Boolean},  // may reveal PII
      {"--intr-regs", OptionType::Boolean}, */
      {"--running-time", OptionType::Boolean},
      {"-k", OptionType::Value},
      {"--clockid", OptionType::Value},
      {"-S", OptionType::Value},
      {"--snapshot", OptionType::Value},

      {"--pfm-events", OptionType::Value},
  };
  return *kPerfRecordOptions;
}

const std::map<std::string, OptionType>& GetPerfStatOptions() {
  static const auto* kPerfStatOptions = new std::map<std::string, OptionType>{
      {"-T", OptionType::Boolean},
      {"--transaction", OptionType::Boolean},
      {"-e", OptionType::Value},
      {"--event", OptionType::Value},
      {"--filter", OptionType::Value},
      {"-i", OptionType::Boolean},
      {"--no-inherit", OptionType::Boolean},
      {"-p", OptionType::Value},
      {"--pid", OptionType::Value},
      {"-t", OptionType::Value},
      {"--tid", OptionType::Value},
      {"-a", OptionType::Boolean},
      {"--all-cpus", OptionType::Boolean},
      {"-g", OptionType::Boolean},
      {"--group", OptionType::Boolean},
      {"-c", OptionType::Boolean},
      {"--scale", OptionType::Boolean},
      /* Banned: {"-v", OptionType::Boolean},
       * {"--verbose", OptionType::Boolean}, */
      /* Banned: {"-r", OptionType::Value},
       * {"--repeat", OptionType::Value}, */
      /* Banned: {"-n", OptionType::Boolean},
       * {"--null", OptionType::Boolean}, */
      /* Banned: {"-d", OptionType::Boolean},
       * {"--detailed", OptionType::Boolean}, */
      /* Banned: {"-S", OptionType::Boolean},
       * {"--sync", OptionType::Boolean}, */
      /* Banned: {"-B", OptionType::Boolean},
       * {"--big-num", OptionType::Boolean}, */
      {"-C", OptionType::Value},
      {"--cpu", OptionType::Value},
      {"-A", OptionType::Boolean},
      {"--no-aggr", OptionType::Boolean},
      /* Banned: {"-x", OptionType::Value},
       * {"--field-separator", OptionType::Value}, */
      {"-G", OptionType::Value},
      {"--cgroup", OptionType::Value},
      /* Banned: {"-o", OptionType::Value},
       * {"--output", OptionType::Value}, */
      /* Banned: {"--append", OptionType::Value}, */
      /* Banned: {"--log-fd", OptionType::Value}, */
      /* Banned: {"--pre", OptionType::Value}, */
      /* Banned: {"--post", OptionType::Value}, */
      /* Banned: {"-I", OptionType::Value},
       * {"--interval-print", OptionType::Value}, */
      {"--per-socket", OptionType::Boolean},
      {"--per-core", OptionType::Boolean},
      {"-D", OptionType::Value},
      {"--delay", OptionType::Value},
  };
  return *kPerfStatOptions;
}

const std::map<std::string, OptionType>& GetPerfMemOptions() {
  static const auto* kPerfMemOptions = new std::map<std::string, OptionType>{
      {"-t", OptionType::Value},   {"--type", OptionType::Value},
      {"-D", OptionType::Boolean}, {"--dump-raw-samples", OptionType::Boolean},
      {"-x", OptionType::Value},   {"--field-separator", OptionType::Value},
      {"-C", OptionType::Value},   {"--cpu-list", OptionType::Value},
  };
  return *kPerfMemOptions;
}

const std::map<std::string, OptionType>& GetPerfInjectOptions() {
  static const auto* kPerfInjectOptions = new std::map<std::string, OptionType>{
      {"-b", OptionType::Boolean},
      {"--build-ids", OptionType::Boolean},
      {"--buildid-all", OptionType::Boolean},
      /* Not applicable: {"-v", OptionType::Boolean},
      {"--verbose", OptionType::Boolean}, */ // stdout is not read
      /* Banned: {"-i", OptionType::Value},
      {"--input", OptionType::Value}, */ // security, file added by quipper
      /* Banned: {"-o", OptionType::Value},
      {"--output", OptionType::Value}, */ // security, file added by quipper
      {"-s", OptionType::Boolean},
      {"--sched-stat", OptionType::Boolean},
      {"--kallsyms", OptionType::Value},
      // The flag itself needs to be in the form of --itrace=<opt>, we ignore
      // the opt part when validating flags.
      {"--itrace", OptionType::Boolean},
      {"--strip", OptionType::Boolean},
      {"-j", OptionType::Boolean},
      {"--jit", OptionType::Boolean},
      /* Banned: {"-f", OptionType::Boolean},
      {"--force", OptionType::Boolean}, */ // added by quipper when needed
  };
  return *kPerfInjectOptions;
}

bool ValidatePerfCommandLineOptions(
    std::vector<std::string>::const_iterator begin_arg,
    std::vector<std::string>::const_iterator end_arg,
    const std::map<std::string, OptionType>& options) {
  for (auto args_iter = begin_arg; args_iter != end_arg; ++args_iter) {
    std::vector<std::string> opt_parts;
    std::string opt;
    quipper::SplitString(*args_iter, '=', &opt_parts);
    if (!opt_parts.empty()) opt = opt_parts[0];

    const auto& it = options.find(opt);
    if (it == options.end()) {
      return false;
    }
    if (it->second == OptionType::Value) {
      ++args_iter;
      if (args_iter == end_arg) {
        return false;  // missing value
      }
    }
  }
  return true;
}

}  // namespace

bool ValidatePerfCommandLine(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    return false;
  }
  if (args[0] != "perf") {
    return false;
  }
  if (args[1] == "record") {
    return ValidatePerfCommandLineOptions(args.begin() + 2, args.end(),
                                          GetPerfRecordOptions());
  }
  if (args[1] == "mem") {
    auto record_arg_iter = std::find(args.begin(), args.end(), "record");
    if (record_arg_iter == args.end()) return false;

    return ValidatePerfCommandLineOptions(args.begin() + 2, record_arg_iter,
                                          GetPerfMemOptions()) &&
           ValidatePerfCommandLineOptions(record_arg_iter + 1, args.end(),
                                          GetPerfRecordOptions());
  }
  if (args[1] == "stat") {
    return ValidatePerfCommandLineOptions(args.begin() + 2, args.end(),
                                          GetPerfStatOptions());
  }
  if (args[1] == "inject") {
    return ValidatePerfCommandLineOptions(args.begin() + 2, args.end(),
                                          GetPerfInjectOptions());
  }
  return false;
}

}  // namespace quipper
