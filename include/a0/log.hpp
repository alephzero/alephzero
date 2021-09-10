#pragma once

#include <a0/c_wrap.hpp>
#include <a0/file.hpp>
#include <a0/log.h>
#include <a0/packet.hpp>

#include <string>

namespace a0 {

struct LogTopic {
  std::string name;
  File::Options file_opts{File::Options::DEFAULT};

  LogTopic() = default;

  LogTopic(const char* name)  // NOLINT(google-explicit-constructor)
      : LogTopic(std::string(name)) {}

  LogTopic(  // NOLINT(google-explicit-constructor)
      std::string name,
      File::Options file_opts = File::Options::DEFAULT)
      : name{std::move(name)}, file_opts{file_opts} {}
};

enum class LogLevel {
  CRIT = A0_LOG_LEVEL_CRIT,
  ERR = A0_LOG_LEVEL_ERR,
  WARN = A0_LOG_LEVEL_WARN,
  INFO = A0_LOG_LEVEL_INFO,
  DBG = A0_LOG_LEVEL_DBG,
  MIN = A0_LOG_LEVEL_MIN,
  MAX = A0_LOG_LEVEL_MAX,
  UNKNOWN = A0_LOG_LEVEL_UNKNOWN,
};

struct Logger : details::CppWrap<a0_logger_t> {
  Logger();
  explicit Logger(LogTopic);

  void log(LogLevel, Packet);
  void log(LogLevel lvl, string_view sv) { log(lvl, Packet(sv, ref)); }

  void crit(Packet);
  void crit(string_view sv) { crit(Packet(sv, ref)); }

  void err(Packet);
  void err(string_view sv) { err(Packet(sv, ref)); }

  void warn(Packet);
  void warn(string_view sv) { warn(Packet(sv, ref)); }

  void info(Packet);
  void info(string_view sv) { info(Packet(sv, ref)); }

  void dbg(Packet);
  void dbg(string_view sv) { dbg(Packet(sv, ref)); }
};

struct LogListener : details::CppWrap<a0_log_listener_t> {
  LogListener() = default;
  LogListener(LogLevel, std::function<void(Packet)>);
  LogListener(LogTopic, LogLevel, std::function<void(Packet)>);
};

}  // namespace a0
