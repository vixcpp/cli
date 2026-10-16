#include <vix/cli/project/cmake/CMakeBuildState.hpp>

#include <vix/cli/project/native/CanonicalHash.hpp>

#include <fstream>
#include <sstream>
#include <system_error>

namespace vix::cli::project::cmake
{
  namespace fs = std::filesystem;
  namespace
  {
    constexpr std::string_view schema = "external-cmake-build-state-v1";

    void append_field(std::string &out, std::string_view value)
    {
      out += std::to_string(value.size());
      out += ':';
      out.append(value.data(), value.size());
      out += '\n';
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

    std::string read_file(const fs::path &path)
    {
      std::ifstream input(path, std::ios::binary);
      if (!input)
        return {};
      std::ostringstream content;
      content << input.rdbuf();
      if (!input.good() && !input.eof())
        return {};
      return content.str();
    }

    bool reject_symlink(const fs::path &path, std::string &error)
    {
      std::error_code ec;
      const auto status = fs::symlink_status(path, ec);
      if (ec == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found)
        return false;
      if (ec)
      {
        error = "cannot inspect Vix build-state path " + path.string() + ": " + ec.message();
        return true;
      }
      if (fs::is_symlink(status))
      {
        error = "refusing to operate through a symlink in Vix build state: " + path.string();
        return true;
      }
      return false;
    }

    bool verify_context(const ExternalCMakeBuildStateContext &context, std::string &error)
    {
      const std::string expectedId =
          vix::cli::project::native::sha256_hex(context.canonicalIdentity).substr(0, 32);
      const fs::path expectedProject = context.projectRoot.lexically_normal();
      const fs::path expectedRoot = expectedProject / ".vix" / "build" / expectedId;
      if (context.id != expectedId || context.root.lexically_normal() != expectedRoot ||
          context.backendDir.lexically_normal() != expectedRoot / "backend" ||
          context.metadataPath.lexically_normal() != expectedRoot / "context.toml")
      {
        error = "refusing to operate on an unverified external CMake build context";
        return false;
      }
      return true;
    }

    bool prepare_parent(const ExternalCMakeBuildStateContext &context, std::string &error)
    {
      const fs::path vixRoot = context.projectRoot / ".vix";
      const fs::path buildRoot = vixRoot / "build";
      if (reject_symlink(vixRoot, error) || reject_symlink(buildRoot, error) ||
          reject_symlink(context.root, error))
        return false;

      std::error_code ec;
      fs::create_directories(buildRoot, ec);
      if (ec)
      {
        error = "cannot create Vix build-state root: " + ec.message();
        return false;
      }
      const fs::path canonicalProject = fs::canonical(context.projectRoot, ec);
      if (ec)
      {
        error = "cannot resolve external CMake project root: " + ec.message();
        return false;
      }
      const fs::path canonicalVix = fs::canonical(vixRoot, ec);
      if (ec || canonicalVix != canonicalProject / ".vix")
      {
        error = "cannot prove Vix build state is contained by the project root";
        return false;
      }
      const fs::path canonicalBuild = fs::canonical(buildRoot, ec);
      if (ec || canonicalBuild != canonicalVix / "build")
      {
        error = "cannot prove external CMake context is contained by Vix build state";
        return false;
      }
      return true;
    }
  }

  ExternalCMakeBuildStateContext make_external_cmake_build_state_context(
    const fs::path &inputRoot,
    const vix::engine::ExecutionPlan &plan,
    std::string_view platform,
    std::string_view compilerIdentity,
    std::string_view sysroot,
    vix::engine::SanitizerMode diagnosticMode,
    const std::vector<std::string> &cmakeArguments,
    std::string_view toolchainContent)
  {
    ExternalCMakeBuildStateContext context;
    std::error_code ec;
    context.projectRoot = fs::weakly_canonical(inputRoot, ec);
    if (ec)
      context.projectRoot = fs::absolute(inputRoot, ec).lexically_normal();
    context.configuration = plan.preset.name + ":" + plan.preset.buildType;
    context.platform = std::string(platform);
    context.compilerIdentity = std::string(compilerIdentity);
    context.generator = plan.preset.generator;
    context.diagnosticMode = diagnosticMode;

    std::string identity;
    append_field(identity, schema);
    append_field(identity, "external-cmake");
    append_field(identity, context.configuration);
    append_field(identity, context.platform);
    append_field(identity, context.compilerIdentity);
    append_field(identity, context.generator);
    append_field(identity, plan.preset.name);
    append_field(identity, plan.preset.buildType);
    append_field(identity, vix::engine::sanitizer_mode_name(diagnosticMode));
    append_field(identity, plan.projectFingerprint);
    append_field(identity, sysroot);
    append_field(identity, toolchainContent);
    append_field(identity, plan.launcher ? std::string_view(*plan.launcher) : std::string_view{});
    append_field(identity, plan.fastLinkerFlag ? std::string_view(*plan.fastLinkerFlag) : std::string_view{});
    for (const auto &argument : cmakeArguments)
      append_field(identity, argument);
    for (const auto &[name, value] : plan.cmakeVars)
    {
      // This generated value is context-relative; its underlying toolchain
      // contents are included independently.
      if (name == "CMAKE_TOOLCHAIN_FILE")
        continue;
      append_field(identity, name);
      append_field(identity, value);
    }
    context.canonicalIdentity = std::move(identity);
    context.id = vix::cli::project::native::sha256_hex(context.canonicalIdentity).substr(0, 32);
    context.root = context.projectRoot / ".vix" / "build" / context.id;
    context.backendDir = context.root / "backend";
    context.metadataPath = context.root / "context.toml";

    const std::string identityDigest =
        vix::cli::project::native::sha256_hex(context.canonicalIdentity);
    std::ostringstream metadata;
    metadata << "format = 1\n"
             << "schema = " << toml_string(schema) << "\n"
             << "id = " << toml_string(context.id) << "\n"
             << "project_kind = \"external-cmake\"\n"
             << "configuration = " << toml_string(context.configuration) << "\n"
             << "platform = " << toml_string(context.platform) << "\n"
             << "compiler_identity = " << toml_string(context.compilerIdentity) << "\n"
             << "generator = " << toml_string(context.generator) << "\n"
             << "diagnostic_mode = " << toml_string(vix::engine::sanitizer_mode_name(diagnosticMode)) << "\n"
             << "configuration_sha256 = " << toml_string(identityDigest) << "\n";
    context.metadata = metadata.str();
    return context;
  }

  void apply_external_cmake_build_state_context(
    vix::engine::ExecutionPlan &plan,
    const ExternalCMakeBuildStateContext &context)
  {
    plan.buildDir = context.backendDir;
    plan.configureLog = plan.buildDir / "configure.log";
    plan.buildLog = plan.buildDir / "build.log";
    plan.sigFile = plan.buildDir / ".vix-config.sig";
    plan.toolchainFile = plan.buildDir / "vix-toolchain.cmake";
  }

  bool reset_external_cmake_build_state_context(
    const ExternalCMakeBuildStateContext &context,
    std::string &error)
  {
    if (!verify_context(context, error) || !prepare_parent(context, error))
      return false;
    if (reject_symlink(context.root, error))
      return false;

    std::error_code ec;
    const bool contextExists = fs::exists(context.root, ec);
    if (ec)
    {
      error = "cannot inspect selected external CMake context: " + ec.message();
      return false;
    }
    if (contextExists)
    {
      fs::remove_all(context.root, ec);
      if (ec)
      {
        error = "cannot remove selected external CMake context: " + ec.message();
        return false;
      }
    }
    return true;
  }

  bool prepare_external_cmake_build_state_context(
    const ExternalCMakeBuildStateContext &context,
    std::string &error)
  {
    if (!verify_context(context, error) || !prepare_parent(context, error))
      return false;
    if (reject_symlink(context.root, error))
      return false;

    std::error_code ec;
    const bool exists = fs::exists(context.root, ec);
    if (ec)
    {
      error = "cannot inspect selected external CMake context: " + ec.message();
      return false;
    }
    if (exists)
    {
      if (reject_symlink(context.metadataPath, error))
        return false;
      const std::string existingMetadata = read_file(context.metadataPath);
      if (existingMetadata.empty() || existingMetadata != context.metadata)
      {
        error = "external CMake context metadata is missing, stale, or incompatible; use vix build --clean";
        return false;
      }
      return true;
    }

    fs::create_directories(context.root, ec);
    if (ec)
    {
      error = "cannot create external CMake context: " + ec.message();
      return false;
    }
    const fs::path temporary = context.metadataPath.string() + ".tmp";
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      if (!output)
      {
        error = "cannot write external CMake context metadata";
        return false;
      }
      output.write(context.metadata.data(), static_cast<std::streamsize>(context.metadata.size()));
      output.flush();
      if (!output)
      {
        error = "cannot persist external CMake context metadata";
        return false;
      }
    }
    fs::rename(temporary, context.metadataPath, ec);
    if (ec)
    {
      fs::remove(temporary, ec);
      error = "cannot activate external CMake context metadata: " + ec.message();
      return false;
    }
    return true;
  }
}
