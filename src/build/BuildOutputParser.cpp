/**
 *
 *  @file BuildOutputParser.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */

#include <vix/cli/build/BuildOutputParser.hpp>

#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace vix::cli::build
{
  namespace
  {
    struct NinjaProgress
    {
      bool valid{false};

      std::size_t current{0};
      std::size_t total{0};

      std::string_view action;
    };

    struct ObjectInfo
    {
      std::string target;
      std::string file;
    };

    bool starts_with(
        std::string_view text,
        std::string_view prefix) noexcept
    {
      return text.size() >= prefix.size() &&
             text.substr(0, prefix.size()) == prefix;
    }

    std::string_view trim_view(
        std::string_view text) noexcept
    {
      while (!text.empty() &&
             (text.front() == ' ' ||
              text.front() == '\t' ||
              text.front() == '\r' ||
              text.front() == '\n'))
      {
        text.remove_prefix(1);
      }

      while (!text.empty() &&
             (text.back() == ' ' ||
              text.back() == '\t' ||
              text.back() == '\r' ||
              text.back() == '\n'))
      {
        text.remove_suffix(1);
      }

      return text;
    }

    bool parse_size(
        std::string_view text,
        std::size_t &value) noexcept
    {
      if (text.empty())
        return false;

      value = 0;

      const char *begin = text.data();
      const char *end = begin + text.size();

      const auto result =
          std::from_chars(begin, end, value);

      return result.ec == std::errc{} &&
             result.ptr == end;
    }

    NinjaProgress parse_ninja_progress(
        std::string_view line) noexcept
    {
      NinjaProgress result;

      line = trim_view(line);

      if (line.empty() || line.front() != '[')
        return result;

      const std::size_t slash =
          line.find('/');

      if (slash == std::string_view::npos)
        return result;

      const std::size_t close =
          line.find(']', slash + 1);

      if (close == std::string_view::npos)
        return result;

      const std::string_view currentText =
          line.substr(1, slash - 1);

      const std::string_view totalText =
          line.substr(
              slash + 1,
              close - slash - 1);

      if (!parse_size(currentText, result.current) ||
          !parse_size(totalText, result.total))
      {
        return result;
      }

      result.action =
          trim_view(line.substr(close + 1));

      result.valid = !result.action.empty();

      return result;
    }

    std::string remove_object_suffix(
        std::string value)
    {
      if (value.size() >= 4 &&
          value.compare(
              value.size() - 4,
              4,
              ".obj") == 0)
      {
        value.erase(value.size() - 4);
        return value;
      }

      if (value.size() >= 2 &&
          value.compare(
              value.size() - 2,
              2,
              ".o") == 0)
      {
        value.erase(value.size() - 2);
      }

      return value;
    }

    ObjectInfo parse_object_info(
        std::string_view value)
    {
      ObjectInfo result;

      std::string objectPath(
          trim_view(value));

      if (objectPath.empty())
        return result;

      const std::string unixMarker =
          "CMakeFiles/";

      const std::string windowsMarker =
          "CMakeFiles\\";

      std::size_t cmakeFiles =
          objectPath.find(unixMarker);

      std::size_t markerSize =
          unixMarker.size();

      if (cmakeFiles == std::string::npos)
      {
        cmakeFiles =
            objectPath.find(windowsMarker);

        markerSize =
            windowsMarker.size();
      }

      if (cmakeFiles == std::string::npos)
      {
        result.file =
            remove_object_suffix(
                std::move(objectPath));

        return result;
      }

      const std::size_t targetStart =
          cmakeFiles + markerSize;

      std::size_t dirMarker =
          objectPath.find(
              ".dir/",
              targetStart);

      std::size_t dirMarkerSize = 5;

      if (dirMarker == std::string::npos)
      {
        dirMarker =
            objectPath.find(
                ".dir\\",
                targetStart);

        dirMarkerSize = 5;
      }

      if (dirMarker == std::string::npos)
      {
        result.file =
            remove_object_suffix(
                std::move(objectPath));

        return result;
      }

      result.target =
          objectPath.substr(
              targetStart,
              dirMarker - targetStart);

      const std::size_t sourceStart =
          dirMarker + dirMarkerSize;

      if (sourceStart < objectPath.size())
      {
        result.file =
            remove_object_suffix(
                objectPath.substr(sourceStart));
      }

      return result;
    }

    bool parse_compile_action(
        std::string_view action,
        std::string_view &objectPath) noexcept
    {
      constexpr std::string_view prefixes[] = {
          "Building CXX object ",
          "Building C object ",
          "Building CUDA object ",
          "Building OBJCXX object ",
          "Building OBJC object "};

      for (const std::string_view prefix : prefixes)
      {
        if (!starts_with(action, prefix))
          continue;

        objectPath =
            trim_view(
                action.substr(prefix.size()));

        return true;
      }

      return false;
    }

    bool parse_link_action(
        std::string_view action,
        std::string_view &target) noexcept
    {
      constexpr std::string_view prefixes[] = {
          "Linking CXX executable ",
          "Linking C executable ",
          "Linking CXX shared library ",
          "Linking C shared library ",
          "Linking CXX static library ",
          "Linking C static library ",
          "Linking CUDA executable ",
          "Linking CUDA shared library ",
          "Linking CUDA static library "};

      for (const std::string_view prefix : prefixes)
      {
        if (!starts_with(action, prefix))
          continue;

        target =
            trim_view(
                action.substr(prefix.size()));

        return true;
      }

      return false;
    }

  } // anonymous namespace

  std::vector<BuildEvent>
  BuildOutputParser::consume(
      std::string_view line)
  {
    std::vector<BuildEvent> events;

    line = trim_view(line);

    if (line.empty())
      return events;

    // -----------------------------------------------------------------------
    // CMake configure stage
    // -----------------------------------------------------------------------

    if (!configureFinished_ &&
        (starts_with(line, "-- Configuring done") ||
         starts_with(
             line,
             "-- Build files have been written to:")))
    {
      BuildEvent event;

      event.kind =
          BuildEventKind::ConfigureFinished;

      event.message =
          "CMake configuration completed";

      events.push_back(
          std::move(event));

      configureFinished_ = true;
    }

    // -----------------------------------------------------------------------
    // Ninja progress
    // -----------------------------------------------------------------------

    const NinjaProgress progress =
        parse_ninja_progress(line);

    if (!progress.valid)
      return events;

    // -----------------------------------------------------------------------
    // Compile
    // -----------------------------------------------------------------------

    std::string_view objectPath;

    if (parse_compile_action(
            progress.action,
            objectPath))
    {
      const ObjectInfo info =
          parse_object_info(objectPath);

      if (!compileStarted_)
      {
        BuildEvent started;

        started.kind =
            BuildEventKind::CompileStarted;

        started.target =
            info.target;

        started.current =
            progress.current;

        started.total =
            progress.total;

        started.message =
            "Compilation started";

        events.push_back(
            std::move(started));

        compileStarted_ = true;
      }

      BuildEvent event;

      event.kind =
          BuildEventKind::CompileProgress;

      event.target =
          info.target;

      event.file =
          info.file;

      event.message =
          std::string(progress.action);

      event.current =
          progress.current;

      event.total =
          progress.total;

      events.push_back(
          std::move(event));

      return events;
    }

    // -----------------------------------------------------------------------
    // Link
    // -----------------------------------------------------------------------

    std::string_view linkTarget;

    if (parse_link_action(
            progress.action,
            linkTarget))
    {
      if (compileStarted_ &&
          !compileFinished_)
      {
        BuildEvent finished;

        finished.kind =
            BuildEventKind::CompileFinished;

        finished.current =
            progress.current > 0
                ? progress.current - 1
                : 0;

        finished.total =
            progress.total;

        finished.message =
            "Compilation completed";

        events.push_back(
            std::move(finished));

        compileFinished_ = true;
      }

      if (!linkStarted_)
      {
        BuildEvent event;

        event.kind =
            BuildEventKind::LinkStarted;

        event.target =
            std::string(linkTarget);

        event.current =
            progress.current;

        event.total =
            progress.total;

        event.message =
            std::string(progress.action);

        events.push_back(
            std::move(event));

        linkStarted_ = true;
      }

      return events;
    }

    return events;
  }

  void BuildOutputParser::reset() noexcept
  {
    configureFinished_ = false;

    compileStarted_ = false;
    compileFinished_ = false;

    linkStarted_ = false;
  }

} // namespace vix::cli::build
