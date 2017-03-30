/*
  Copyright (c) 2016, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

/**
 * @file Logging interface for using and extending the logging
 * subsystem.
 */

#ifndef MYSQL_HARNESS_LOGGING_INCLUDED
#define MYSQL_HARNESS_LOGGING_INCLUDED

#include "filesystem.h"

#include <fstream>
#include <mutex>
#include <string>
#include <cstdarg>

#include <sys/types.h>
#include <unistd.h>

#ifdef _MSC_VER
#  ifdef logger_EXPORTS
/* We are building this library */
#    define LOGGER_API __declspec(dllexport)
#  else
/* We are using this library */
#    define LOGGER_API __declspec(dllimport)
#  endif
#else
#  define LOGGER_API
#endif

namespace mysql_harness {

namespace logging {

/**
 * Log level values.
 *
 * Log levels are ordered numerically from most important (lowest
 * value) to least important (highest value).
 */
enum class LogLevel {
  /** Fatal failure. Router usually exits after logging this. */
  kFatal,

  /**
   * Error message. indicate that something is not working properly and
   * actions need to be taken. However, the router continue
   * operating but the particular thread issuing the error message
   * might terminate.
   */
  kError,

  /**
   * Warning message. Indicate a potential problem that could require
   * actions, but does not cause a problem for the continous operation
   * of the router.
   */
  kWarning,

  /**
   * Informational message. Information that can be useful to check
   * the behaviour of the router during normal operation.
   */
  kInfo,

  /**
   * Debug message. Message contain internal details that can be
   * useful for debugging problematic situations, especially regarding
   * the router itself.
   */
  kDebug,

  kNotSet  // Always higher than all other log messages
};

/**
 * Default log level used by the router.
 */
const LogLevel kDefaultLogLevel = LogLevel::kWarning;

/**
 * Log level name for the default log level used by the router.
 */
const char* const kDefaultLogLevelName = "warning";

/**
 * Log record containing information collected by the logging
 * system.
 *
 * The log record is passed to the handlers together with the format
 * string and the arguments.
 */
struct Record {
  LogLevel level;
  pid_t process_id;
  time_t created;
  std::string domain;
  std::string message;
};

/**
 * Base class for log message handler.
 *
 * This class is used to implement a log message handler. You need
 * to implement the `do_log` primitive to process the log
 * record. If, for some reason, the implementation is unable to log
 * the record, and exception can be thrown that will be caught by
 * the harness.
 */
class Handler {
 public:
  virtual ~Handler() = default;

  void handle(const Record& record);

  void set_level(LogLevel level) { level_ = level; }
  LogLevel get_level() const { return level_; }

 protected:
  // ??? Does this reall have to be a member function and does it ???
  // ??? belong to the handler ???
  std::string format(const Record& record) const;

  explicit Handler(LogLevel level);

 private:
  /**
   * Log message handler primitive.
   *
   * This member function is implemented by subclasses to properly log
   * a record wherever it need to be logged.  If it is not possible to
   * log the message properly, an exception should be thrown and will
   * be caught by the caller.
   *
   * @param record Record containing information about the message.
   */
  virtual void do_log(const Record& record) = 0;

  /**
   * Log level set for the handler.
   */
  LogLevel level_;
};

/**
 * Handler to write to an output stream.
 *
 * @code
 * Logger logger("my_module");
 * ...
 * logger.add_handler(StreamHandler(std::clog));
 * @endcode
 */
class StreamHandler : public Handler {
 public:
  explicit StreamHandler(std::ostream& stream,
                         LogLevel level = LogLevel::kNotSet);

 protected:
  std::ostream& stream_;
  std::mutex stream_mutex_;

 private:
  void do_log(const Record& record) override;
};

/**
 * Handler that writes to a file.
 *
 * @code
 * Logger logger("my_module");
 * ...
 * logger.add_handler(FileHandler("/var/log/router.log"));
 * @endcode
 */
class FileHandler : public StreamHandler {
 public:
  explicit FileHandler(const Path& path, LogLevel level = LogLevel::kNotSet);
  ~FileHandler();

 private:
  std::ofstream fstream_;
};

/** Set log level for all registered loggers. */
void set_log_level(LogLevel level);

/** Set log level for the named logger. */
void set_log_level(const char* name, LogLevel level);

/**
 * Register handler for all plugins.
 *
 * This will register a handler for all plugins that have been
 * registered with the logging subsystem (normally all plugins that
 * have been loaded by `Loader`).
 *
 * @param handler Shared pointer to dynamically allocated handler.
 *
 * For example, to register a custom handler from a plugin, you would
 * do the following:
 *
 * @code
 * void init() {
 *   ...
 *   register_handler(std::make_shared<MyHandler>(...));
 *   ...
 * }
 * @endcode
 */
void register_handler(std::shared_ptr<Handler> handler);

/**
 * Unregister a handler.
 *
 * This will unregister a previously registered handler.
 *
 * @param handler Shared pointer to a previously allocated handler.
 */
void unregister_handler(std::shared_ptr<Handler> handler);

/**
 * Log message for the domain.
 *
 * This will log an error, warning, informational, or debug message
 * for the given domain. The domain have to be be registered before
 * anything is being logged. The `Loader` uses the plugin name as the
 * domain name, so normally you should provide the plugin name as the
 * first argument to this function.
 *
 * @param name Domain name to use when logging message.
 *
 * @param fmt `printf`-style format string, with arguments following.
 */
/** @{ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pre-processor symbol containing the name of the log domain. If not
 * defined explicitly when compiling, it will be the null point, which
 * means that it logs to the top log domain.
 */

#ifndef MYSQL_ROUTER_LOG_DOMAIN
#define MYSQL_ROUTER_LOG_DOMAIN nullptr
#endif

/*
 * Declare the implementation log function and define an inline
 * function that pick up the log domain defined for the module. This
 * will define a function that is namespace-aware.
 */
#define MAKE_LOG_FUNC(LEVEL)                                    \
  inline void LOGGER_API log_##LEVEL(const char* fmt, ...) {    \
    extern void LOGGER_API _vlog_##LEVEL(const char* name,      \
                                         const char *fmt,       \
                                         va_list ap);           \
    va_list ap;                                                 \
    va_start(ap, fmt);                                          \
    _vlog_##LEVEL(MYSQL_ROUTER_LOG_DOMAIN, fmt, ap);            \
    va_end(ap);                                                 \
  }

MAKE_LOG_FUNC(error)
MAKE_LOG_FUNC(warning)
MAKE_LOG_FUNC(info)
MAKE_LOG_FUNC(debug)
/** @} */

#ifdef __cplusplus
}
#endif

}  // namespace logging

}  // namespace mysql_harness

#endif // MYSQL_HARNESS_LOGGING_INCLUDED
