#include <vix/cli/cmake/Toolchain.hpp>
#include <vix/cli/process/Process.hpp>
#include <vix/cli/util/Args.hpp>

#include <cassert>
#include <type_traits>

int main()
{
  namespace process = vix::cli::process;
  namespace util = vix::cli::util;

  static_assert(std::is_same_v<process::LinkerMode, vix::engine::LinkerMode>);
  static_assert(std::is_same_v<process::LauncherMode, vix::engine::LauncherMode>);

  assert(util::parse_linker_mode("auto") == process::LinkerMode::Auto);
  assert(util::parse_linker_mode("default") == process::LinkerMode::Default);
  assert(util::parse_linker_mode("mold") == process::LinkerMode::Mold);
  assert(util::parse_linker_mode("lld") == process::LinkerMode::Lld);
  assert(util::parse_linker_mode("AUTO") == process::LinkerMode::Auto);
  assert(!util::parse_linker_mode("gold").has_value());

  assert(util::parse_launcher_mode("auto") == process::LauncherMode::Auto);
  assert(util::parse_launcher_mode("none") == process::LauncherMode::None);
  assert(util::parse_launcher_mode("sccache") == process::LauncherMode::Sccache);
  assert(util::parse_launcher_mode("ccache") == process::LauncherMode::Ccache);
  assert(util::parse_launcher_mode("CCACHE") == process::LauncherMode::Ccache);
  assert(!util::parse_launcher_mode("distcc").has_value());

  assert(vix::cli::build::infer_processor_from_triple("arm-linux-gnueabihf") == "arm");

  return 0;
}
