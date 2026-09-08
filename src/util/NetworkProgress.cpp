#include <vix/cli/util/NetworkProgress.hpp>
#include <vix/cli/Style.hpp>

#include <cstdlib>
#include <iostream>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace vix::cli::util
{
  namespace
  {
    bool terminal_is_interactive()
    {
#ifdef _WIN32
      if (_isatty(_fileno(stdout)) == 0)
#else
      if (::isatty(STDOUT_FILENO) == 0)
#endif
        return false;
      const char *term = std::getenv("TERM");
      return std::getenv("NO_COLOR") == nullptr &&
             (!term || std::string_view(term) != "dumb");
    }
  }

  NetworkProgress::NetworkProgress(
      std::string subject,
      NetworkProgressOptions options,
      std::ostream &output)
      : output_(output), subject_(std::move(subject)),
        interactive_(options.detectTerminal ? terminal_is_interactive() : options.interactive)
  {
  }

  NetworkProgress::~NetworkProgress() { failure(); }

  void NetworkProgress::phase(std::string text) { update(std::move(text)); }

  void NetworkProgress::update(std::string text)
  {
    std::lock_guard lock(mutex_);
    text_ = std::move(text);
    active_ = true;
    if (interactive_)
      render_locked();
    else
      stable_locked();
  }

  void NetworkProgress::success(std::string text)
  {
    std::lock_guard lock(mutex_);
    if (interactive_)
    {
      clear_locked();
      output_ << "  " << style::SUCCESS << "✔" << style::RESET << " "
              << text << "\n" << std::flush;
    }
    else if (lastStableText_ != text)
      output_ << text << ".\n" << std::flush;
    active_ = false;
  }

  void NetworkProgress::failure()
  {
    std::lock_guard lock(mutex_);
    if (active_ && interactive_)
      clear_locked();
    active_ = false;
  }

  void NetworkProgress::stop() { failure(); }

  void NetworkProgress::render_locked(bool)
  {
    static constexpr const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    output_ << "\033[?25l\r\033[2K";
    cursorHidden_ = true;
    output_ << "  " << style::ACCENT << frames[frame_++ % 10] << style::RESET
            << " " << text_ << std::flush;
  }

  void NetworkProgress::clear_locked()
  {
    output_ << "\r\033[2K";
    if (cursorHidden_)
      output_ << "\033[?25h";
    output_ << std::flush;
    cursorHidden_ = false;
  }

  void NetworkProgress::stable_locked()
  {
    if (text_.empty() || text_ == lastStableText_)
      return;
    output_ << text_ << "...\n" << std::flush;
    lastStableText_ = text_;
  }
}
