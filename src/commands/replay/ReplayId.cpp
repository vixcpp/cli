/**
 *
 *  @file ReplayId.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/replay/ReplayId.hpp>

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <thread>

namespace vix::commands::replay
{

  namespace
  {

    /**
     * @brief Return a trimmed copy of a string.
     *
     * @param value Input string.
     * @return Trimmed string.
     */
    std::string trim_copy(std::string value)
    {
      auto is_space = [](unsigned char c)
      {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
      };

      while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
        value.pop_back();

      std::size_t start = 0;
      while (start < value.size() && is_space(static_cast<unsigned char>(value[start])))
        ++start;

      if (start > 0)
        value.erase(0, start);

      return value;
    }

    /**
     * @brief Return the current local timestamp used inside replay ids.
     *
     * @return Timestamp formatted as YYYY-MM-DD-HH-MM-SS.
     */
    std::string replay_timestamp_for_id()
    {
      const auto now = std::chrono::system_clock::now();
      const std::time_t tt = std::chrono::system_clock::to_time_t(now);

      std::tm tm{};

#if defined(_WIN32)
      localtime_s(&tm, &tt);
#else
      localtime_r(&tt, &tm);
#endif

      std::ostringstream oss;
      oss << std::put_time(&tm, "%Y-%m-%d-%H-%M-%S");
      return oss.str();
    }

    /**
     * @brief Return an entropy string for replay id generation.
     *
     * @return Entropy string.
     */
    std::string replay_entropy_seed()
    {
      const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();

      std::random_device rd;

      std::ostringstream oss;
      oss << now
          << ":"
          << rd()
          << ":"
          << std::this_thread::get_id();

      return oss.str();
    }

  } // namespace

  std::string short_hash_hex(const std::string &value, std::size_t length)
  {
    constexpr std::uint64_t offset = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;

    std::uint64_t hash = offset;

    for (unsigned char c : value)
    {
      hash ^= static_cast<std::uint64_t>(c);
      hash *= prime;
    }

    static constexpr char digits[] = "0123456789abcdef";

    std::string out(16, '0');
    for (int i = 15; i >= 0; --i)
    {
      out[static_cast<std::size_t>(i)] = digits[hash & 0x0F];
      hash >>= 4U;
    }

    if (length == 0)
      return {};

    if (length >= out.size())
      return out;

    return out.substr(0, length);
  }

  std::string make_replay_id()
  {
    return make_replay_id_from_seed(replay_entropy_seed());
  }

  std::string make_replay_id_from_seed(const std::string &seed)
  {
    const std::string timestamp = replay_timestamp_for_id();
    const std::string suffix = short_hash_hex(timestamp + ":" + seed, 8);

    return timestamp + "-" + suffix;
  }

  std::string normalize_replay_id(std::string value)
  {
    value = trim_copy(std::move(value));

    while (!value.empty() && (value.back() == '/' || value.back() == '\\'))
      value.pop_back();

    return value;
  }

  bool is_latest_selector(const std::string &value)
  {
    const std::string id = normalize_replay_id(value);
    return id == "latest" || id == "last";
  }

  bool is_failed_selector(const std::string &value)
  {
    const std::string id = normalize_replay_id(value);
    return id == "failed" || id == "fail";
  }

} // namespace vix::commands::replay
