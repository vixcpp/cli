#include <vix/cli/util/NetworkProgress.hpp>

#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
  void expect(bool value, const char *message)
  {
    if (!value)
      throw std::runtime_error(message);
  }
}

int main()
{
  {
    std::ostringstream output;
    vix::cli::util::NetworkProgress progress(
        "json", {.interactive = false, .detectTerminal = false}, output);
    progress.phase("Connecting to json");
    progress.phase("Downloading json");
    progress.phase("Downloading json");
    progress.success("json installed");
    const std::string text = output.str();
    expect(text.find("Connecting to json...\n") != std::string::npos, "non-TTY connecting line missing");
    expect(text.find("Downloading json...\n") != std::string::npos, "non-TTY download line missing");
    expect(text.find("json installed.\n") != std::string::npos, "non-TTY final line missing");
    expect(text.find("\033[") == std::string::npos, "non-TTY output contains cursor escape sequence");
    expect(text.find("Downloading json...\n", text.find("Downloading json...\n") + 1) == std::string::npos,
           "non-TTY output repeated a stable phase");
  }

  {
    std::ostringstream output;
    vix::cli::util::NetworkProgress progress(
        "json", {.interactive = true, .detectTerminal = false}, output);
    progress.phase("Downloading json");
    progress.success("json installed");
    const std::string text = output.str();
    expect(text.find("\033[?25l") != std::string::npos, "interactive progress did not hide cursor");
    expect(text.find("\033[?25h") != std::string::npos, "interactive completion did not restore cursor");
    expect(text.find("json installed") != std::string::npos, "interactive final line missing");
  }

  {
    std::ostringstream output;
    vix::cli::util::NetworkProgress progress(
        "json", {.interactive = true, .detectTerminal = false}, output);
    progress.phase("Connecting to json");
    progress.failure();
    expect(output.str().find("\033[?25h") != std::string::npos,
           "interactive failure did not restore cursor");
  }
}
