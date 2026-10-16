#include <vix/cli/project/native/NativeBuildState.hpp>

#include <vix/cli/project/native/CanonicalHash.hpp>

#include <cstdlib>
#include <sstream>
#include <system_error>

namespace vix::cli::project::native
{
  namespace fs = std::filesystem;
  namespace
  {
    constexpr std::string_view schema = "native-build-state-v2";

    void append_field(std::string &out, std::string_view field)
    {
      out += std::to_string(field.size());
      out += ':';
      out.append(field.data(), field.size());
      out += '\n';
    }

    std::string context_id(std::string_view configuration, std::string_view platform,
                           const NativeToolchainIdentity &toolchain, std::string_view generator,
                           vix::engine::SanitizerMode diagnosticMode)
    {
      std::string canonical;
      append_field(canonical, schema);
      append_field(canonical, "native-vix");
      append_field(canonical, configuration);
      append_field(canonical, platform);
      append_field(canonical, toolchain.compilerPath);
      append_field(canonical, toolchain.compilerFamily);
      append_field(canonical, toolchain.compilerVersion);
      append_field(canonical, toolchain.compilerTarget);
      append_field(canonical, toolchain.executableStatAvailable ? "stat" : "no-stat");
      if (toolchain.executableStatAvailable)
      {
        append_field(canonical, std::to_string(toolchain.executableSize));
        append_field(canonical, std::to_string(toolchain.executableMtimeTicks));
      }
      append_field(canonical, generator);
      append_field(canonical, vix::engine::sanitizer_mode_name(diagnosticMode));
      return sha256_hex(canonical).substr(0, 32);
    }

    std::string toml_string(std::string_view value)
    {
      std::string result{"\""};
      for (const char ch : value)
      {
        if (ch == '\\' || ch == '"') result += '\\';
        if (ch == '\n') result += "\\n";
        else if (ch == '\r') result += "\\r";
        else result += ch;
      }
      result += '"';
      return result;
    }

    bool is_symlink_if_present(const fs::path &path, std::string &error)
    {
      std::error_code ec;
      const auto status = fs::symlink_status(path, ec);
      if (ec == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found)
        return false;
      if (ec)
      {
        error = "cannot inspect Vix build-state path: " + path.string() + ": " + ec.message();
        return true;
      }
      if (fs::is_symlink(status))
      {
        error = "refusing to operate through a symlink in Vix build state: " + path.string();
        return true;
      }
      return false;
    }
  }

  NativeBuildStateContext make_native_build_state_context(
      const fs::path &inputRoot, std::string_view configuration,
      std::string_view platform, const NativeToolchainIdentity &toolchain,
      std::string_view generator,
      vix::engine::SanitizerMode diagnosticMode)
  {
    NativeBuildStateContext context;
    context.id = context_id(configuration, platform, toolchain, generator, diagnosticMode);
    context.configuration = configuration;
    context.platform = platform;
    context.toolchain = toolchain;
    context.generator = generator;
    context.diagnosticMode = diagnosticMode;
    std::error_code ec;
    context.projectRoot = fs::weakly_canonical(inputRoot, ec);
    if (ec)
      context.projectRoot = fs::absolute(inputRoot, ec).lexically_normal();
    const auto buildRoot = native_build_state_root(context.projectRoot);
    context.root = buildRoot / context.id;
    context.generatedSourceDir = context.root / "source";
    context.backendDir = context.root / "backend";
    context.metadataPath = context.root / "context.toml";
    context.configuredInputSignaturePath = context.root / "configured-input.sha256";
    context.snapshotCachePath = context.root / "target-snapshot.json";
    return context;
  }

  fs::path native_build_state_root(const fs::path &projectRoot)
  {
    return projectRoot / ".vix" / "build";
  }

  fs::path native_toolchain_identity_cache_path(const fs::path &projectRoot)
  {
    return native_build_state_root(projectRoot) / "toolchain-identity.json";
  }

  bool reset_native_build_state_context(const NativeBuildStateContext &context, std::string &error)
  {
    const fs::path expectedBuildRoot = (context.projectRoot / ".vix" / "build").lexically_normal();
    const auto expected = make_native_build_state_context(
      context.projectRoot, context.configuration, context.platform, context.toolchain,
      context.generator, context.diagnosticMode);
    if (context.id != expected.id || context.root.lexically_normal() != (expectedBuildRoot / expected.id).lexically_normal() ||
        context.generatedSourceDir.lexically_normal() != expected.generatedSourceDir.lexically_normal() ||
        context.backendDir.lexically_normal() != expected.backendDir.lexically_normal() ||
        context.metadataPath.lexically_normal() != expected.metadataPath.lexically_normal() ||
        context.configuredInputSignaturePath.lexically_normal() != expected.configuredInputSignaturePath.lexically_normal() ||
        context.snapshotCachePath.lexically_normal() != expected.snapshotCachePath.lexically_normal())
    {
      error = "refusing to clean an unverified native build context";
      return false;
    }

    const fs::path vixRoot = context.projectRoot / ".vix";
    if (is_symlink_if_present(vixRoot, error) || is_symlink_if_present(expectedBuildRoot, error) ||
        is_symlink_if_present(context.root, error))
      return false;

    std::error_code ec;
    fs::create_directories(expectedBuildRoot, ec);
    if (ec)
    {
      error = "cannot create Vix build-state root: " + ec.message();
      return false;
    }

    // Recheck after directory creation and prove the resolved parent remains
    // below the selected project before deleting the exact context directory.
    const auto canonicalBuildRoot = fs::canonical(expectedBuildRoot, ec);
    if (ec || canonicalBuildRoot != fs::weakly_canonical(expectedBuildRoot.parent_path(), ec) / "build")
    {
      error = "cannot prove native build context is inside the project Vix state root";
      return false;
    }
    if (is_symlink_if_present(context.root, error))
      return false;

    const bool contextExists = fs::exists(context.root, ec);
    if (ec)
    {
      error = "cannot inspect selected native build context: " + ec.message();
      return false;
    }
    if (contextExists)
    {
      fs::remove_all(context.root, ec);
      if (ec)
      {
        error = "cannot remove selected native build context: " + ec.message();
        return false;
      }
    }
    fs::create_directories(context.generatedSourceDir, ec);
    if (!ec) fs::create_directories(context.backendDir, ec);
    if (ec)
    {
      error = "cannot recreate selected native build context: " + ec.message();
      return false;
    }
    return true;
  }

  std::string native_build_context_metadata(
      const NativeBuildStateContext &context, std::string_view generatedInputDigest)
  {
    std::ostringstream out;
    out << "format = 1\n"
        << "schema = " << toml_string(schema) << "\n"
        << "id = " << toml_string(context.id) << "\n"
        << "project_kind = \"native-vix\"\n"
        << "configuration = " << toml_string(context.configuration) << "\n"
        << "platform = " << toml_string(context.platform) << "\n"
        << "compiler = " << toml_string(context.toolchain.compilerPath) << "\n"
        << "compiler_family = " << toml_string(context.toolchain.compilerFamily) << "\n"
        << "compiler_version = " << toml_string(context.toolchain.compilerVersion) << "\n"
        << "compiler_target = " << toml_string(context.toolchain.compilerTarget) << "\n"
        << "compiler_executable_stat_available = " << (context.toolchain.executableStatAvailable ? "true" : "false") << "\n"
        << "compiler_executable_size = " << context.toolchain.executableSize << "\n"
        << "compiler_executable_mtime_ticks = " << context.toolchain.executableMtimeTicks << "\n"
        << "generator = " << toml_string(context.generator) << "\n"
        << "diagnostic_mode = " << toml_string(vix::engine::sanitizer_mode_name(context.diagnosticMode)) << "\n"
        << "generated_input_sha256 = " << toml_string(generatedInputDigest) << "\n";
    return out.str();
  }
}
