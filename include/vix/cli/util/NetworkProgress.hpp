/** Shared terminal presentation for remote CLI operations. */
#ifndef VIX_CLI_UTIL_NETWORK_PROGRESS_HPP
#define VIX_CLI_UTIL_NETWORK_PROGRESS_HPP

#include <chrono>
#include <iostream>
#include <mutex>
#include <string>

namespace vix::cli::util
{
  struct NetworkProgressOptions
  {
    // Tests and embedders can override automatic terminal detection.
    bool interactive = false;
    bool detectTerminal = true;
  };

  class NetworkProgress
  {
  public:
    explicit NetworkProgress(
        std::string subject,
        NetworkProgressOptions options = {},
        std::ostream &output = std::cout);
    ~NetworkProgress();

    NetworkProgress(const NetworkProgress &) = delete;
    NetworkProgress &operator=(const NetworkProgress &) = delete;

    void phase(std::string text);
    void update(std::string text);
    void success(std::string text);
    void failure();
    void stop();

  private:
    void render_locked(bool final = false);
    void clear_locked();
    void stable_locked();

    std::ostream &output_;
    std::string subject_;
    std::string text_;
    std::string lastStableText_;
    bool interactive_ = false;
    bool active_ = false;
    bool cursorHidden_ = false;
    std::size_t frame_ = 0;
    std::chrono::steady_clock::time_point lastRender_{};
    std::mutex mutex_;
  };
}

#endif
