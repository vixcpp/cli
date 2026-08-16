/** Incremental parser for Git's human progress records. */
#ifndef VIX_CLI_UTIL_GIT_PROGRESS_HPP
#define VIX_CLI_UTIL_GIT_PROGRESS_HPP

#include <cctype>
#include <exception>
#include <functional>
#include <optional>
#include <regex>
#include <string>
#include <string_view>

namespace vix::cli::util
{
  struct GitProgressEvent
  {
    std::string phase;
    std::optional<unsigned> current;
    std::optional<unsigned> total;
    std::optional<unsigned> percent;
    std::string transferred;
    std::string speed;
  };

  class GitProgressParser
  {
  public:
    using Callback = std::function<void(const GitProgressEvent &)>;

    explicit GitProgressParser(Callback callback) : callback_(std::move(callback)) {}

    void push(std::string_view chunk)
    {
      pending_.append(chunk.data(), chunk.size());
      std::size_t begin = 0;
      for (std::size_t i = 0; i < pending_.size(); ++i)
      {
        if (pending_[i] != '\r' && pending_[i] != '\n') continue;
        consume(pending_.substr(begin, i - begin));
        if (pending_[i] == '\r' && i + 1 < pending_.size() && pending_[i + 1] == '\n') ++i;
        begin = i + 1;
      }
      pending_.erase(0, begin);
    }

    void finish()
    {
      consume(pending_);
      pending_.clear();
    }

  private:
    void consume(const std::string &line)
    {
      static const std::regex record(
          R"(^\s*(Enumerating objects|Counting objects|Compressing objects|Receiving objects|Resolving deltas)\s*:\s*(?:(\d+)\s*%\s*\(\s*(\d+)\s*/\s*(\d+)\s*\)|(\d+)\s*,\s*done\.)\s*(?:,\s*([0-9.]+\s*(?:KiB|MiB|GiB|B))\s*\|\s*([0-9.]+\s*(?:KiB|MiB|GiB|B)/s))?.*$)");
      std::smatch match;
      if (!std::regex_match(line, match, record)) return;
      try
      {
        GitProgressEvent event;
        event.phase = match[1].str();
        for (char &c : event.phase) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (match[2].matched)
        {
          event.percent = static_cast<unsigned>(std::stoul(match[2].str()));
          event.current = static_cast<unsigned>(std::stoul(match[3].str()));
          event.total = static_cast<unsigned>(std::stoul(match[4].str()));
        }
        else if (match[5].matched)
        {
          event.current = static_cast<unsigned>(std::stoul(match[5].str()));
          event.total = event.current;
          event.percent = 100;
        }
        if (match[6].matched) event.transferred = match[6].str();
        if (match[7].matched) event.speed = match[7].str();
        callback_(event);
      }
      catch (const std::exception &) { }
    }

    Callback callback_;
    std::string pending_;
  };
}
#endif
