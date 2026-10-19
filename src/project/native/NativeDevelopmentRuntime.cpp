#include <vix/cli/project/native/NativeDevelopmentRuntime.hpp>
#include <vix/env/Legacy.hpp>
#include <vix/cli/project/native/NativeBuildState.hpp>
#include <vix/cli/project/native/CanonicalHash.hpp>

#include <vix/cli/project/ProjectDiscovery.hpp>
#include <vix/cli/project/TargetSelection.hpp>
#include <vix/cli/project/TestSelection.hpp>
#include <vix/cli/project/cmake/CMakeFileApi.hpp>
#include <vix/cli/project/cmake/CMakeProjectAdapter.hpp>
#include <vix/cli/project/cmake/CTestExecution.hpp>
#include <vix/cli/project/native/GitPackageContentSource.hpp>
#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/NativePackageInstall.hpp>
#include <vix/cli/project/native/PackageBuildGraph.hpp>
#include <vix/cli/project/native/PackageCommandRuntime.hpp>
#include <vix/cli/project/native/RegistryContentTransport.hpp>
#include <vix/cli/project/native/RegistryDirectory.hpp>
#include <vix/cli/project/native/TomlParser.hpp>
#include <vix/engine/Process.hpp>
#include <vix/engine/Preset.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <map>
#include <set>
#include <cstdlib>
#include <utility>

#if defined(__linux__)
#include <features.h>
#endif

namespace vix::cli::project::native
{
  namespace fs = std::filesystem;
  namespace
  {
    struct PreparedNativeBuild
    {
      NativeProject project;
      Lockfile lock;
      std::vector<MaterializedLockedPackage> packages;
      fs::path projectRoot;
      fs::path sourceRoot;
      fs::path buildRoot;
      vix::engine::SanitizerMode diagnosticMode{vix::engine::SanitizerMode::None};
      bool snapshotFromCache{false};
      std::optional<project::ProjectSnapshot> snapshot;
    };

    NativeDevelopmentResult failure(std::string message, int exitCode = 1,
                                    NativeDevelopmentErrorCode error = NativeDevelopmentErrorCode::project_error,
                                    std::optional<project::ProjectError> projectError = std::nullopt,
                                    std::string output = {})
    {
      NativeDevelopmentResult result{};
      result.exitCode = exitCode;
      result.diagnostic = std::move(message);
      result.output = std::move(output);
      result.code = error;
      result.projectError = projectError;
      return result;
    }

    NativeDevelopmentResult success(std::string output = {})
    {
      NativeDevelopmentResult result{};
      result.exitCode = 0;
      result.diagnostic.clear();
      result.output = std::move(output);
      result.code = NativeDevelopmentErrorCode::none;
      result.projectError.reset();
      return result;
    }

    NativeDevelopmentErrorCode target_error_code(const std::optional<project::ProjectError> &error)
    {
      if (!error) return NativeDevelopmentErrorCode::project_error;
      switch (*error)
      {
      case project::ProjectError::target_not_found: return NativeDevelopmentErrorCode::target_not_found;
      case project::ProjectError::ambiguous_target: return NativeDevelopmentErrorCode::target_ambiguous;
      case project::ProjectError::target_not_buildable: return NativeDevelopmentErrorCode::target_not_buildable;
      case project::ProjectError::target_not_runnable: return NativeDevelopmentErrorCode::target_not_runnable;
      default: return NativeDevelopmentErrorCode::project_error;
      }
    }

    bool write_text(const fs::path &path, const std::string &text)
    {
      std::error_code error;
      fs::create_directories(path.parent_path(), error);
      if (error)
        return false;
      std::ofstream output(path, std::ios::binary | std::ios::trunc);
      if (!output)
        return false;
      output.write(text.data(), static_cast<std::streamsize>(text.size()));
      output.flush();
      return output.good();
    }

    bool write_text_if_changed(const fs::path &path, const std::string &text, bool &changed)
    {
      std::ifstream existing(path, std::ios::binary);
      if (existing)
      {
        const std::string old{std::istreambuf_iterator<char>{existing}, std::istreambuf_iterator<char>{}};
        if (old == text)
        {
          changed = false;
          return true;
        }
      }
      changed = true;
      return write_text(path, text);
    }

    std::string native_platform_identity()
    {
#if defined(__x86_64__) || defined(_M_X64)
      constexpr std::string_view arch{"x86_64"};
#elif defined(__aarch64__) || defined(_M_ARM64)
      constexpr std::string_view arch{"aarch64"};
#elif defined(__i386__) || defined(_M_IX86)
      constexpr std::string_view arch{"i686"};
#else
      constexpr std::string_view arch{"unknown"};
#endif
#if defined(_WIN32)
#if defined(_MSC_VER)
      return std::string{arch} + "-windows-msvc";
#elif defined(__MINGW32__)
      return std::string{arch} + "-windows-gnu";
#else
      return std::string{arch} + "-windows-unknown";
#endif
#elif defined(__APPLE__)
      return std::string{arch} + "-macos";
#elif defined(__linux__)
#if defined(__GLIBC__)
      return std::string{arch} + "-linux-gnu";
#else
      return std::string{arch} + "-linux-unknown";
#endif
#else
      return std::string{arch} + "-unknown";
#endif
    }

    std::string first_token_after(std::string_view text, std::string_view marker)
    {
      const auto position = text.find(marker);
      if (position == std::string_view::npos) return {};
      auto begin = position + marker.size();
      while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
      auto end = begin;
      while (end < text.size() && text[end] != ' ' && text[end] != '\t' &&
             text[end] != '\r' && text[end] != '\n' && text[end] != ')') ++end;
      return std::string{text.substr(begin, end - begin)};
    }

    NativeToolchainIdentity native_toolchain_identity(const fs::path &projectRoot)
    {
      const char *configured = vix::env::legacy::getenv("CXX");
#ifdef _WIN32
      const std::string command = configured && *configured ? configured : "cl";
      const char separator = ';';
#else
      const std::string command = configured && *configured ? configured : "c++";
      const char separator = ':';
#endif
      const fs::path candidate{command};
      std::error_code ec;
      NativeToolchainIdentity identity;
      if (candidate.has_parent_path() && fs::exists(candidate, ec) && !ec)
        identity.compilerPath = fs::weakly_canonical(candidate, ec).string();
      else if (const char *path = vix::env::legacy::getenv("PATH"))
      {
        std::string paths{path};
        std::size_t begin = 0;
        while (begin <= paths.size())
        {
          const auto end = paths.find(separator, begin);
          const auto directory = paths.substr(begin, end == std::string::npos ? end : end - begin);
          const auto resolved = fs::path{directory} / candidate;
          ec.clear();
          if (fs::is_regular_file(resolved, ec) && !ec)
          {
            identity.compilerPath = fs::weakly_canonical(resolved, ec).string();
            break;
          }
          if (end == std::string::npos) break;
          begin = end + 1;
        }
      }
      if (identity.compilerPath.empty())
        identity.compilerPath = "unresolved-sha256:" + sha256_hex(command);

      if (!identity.compilerPath.starts_with("unresolved-sha256:"))
      {
        const fs::path executable{identity.compilerPath};
        ec.clear();
        const auto size = fs::file_size(executable, ec);
        if (!ec)
        {
          const auto modified = fs::last_write_time(executable, ec);
          if (!ec)
          {
            identity.executableSize = size;
            identity.executableMtimeTicks = static_cast<std::int64_t>(modified.time_since_epoch().count());
            identity.executableStatAvailable = true;
          }
        }
      }

      const fs::path identityCache = native_toolchain_identity_cache_path(projectRoot);
      if (identity.executableStatAvailable)
      {
        std::error_code cacheError;
        if (fs::is_regular_file(identityCache, cacheError) && !cacheError)
        {
          std::ifstream input(identityCache, std::ios::binary);
          try
          {
            const auto cached = nlohmann::json::parse(input, nullptr, false, true);
            if (cached.is_object() && !cached.is_discarded() &&
                cached.value("schema", std::string{}) == "native-toolchain-identity-v1" &&
                cached.value("compiler_path", std::string{}) == identity.compilerPath &&
                cached.value("executable_size", std::uintmax_t{}) == identity.executableSize &&
                cached.value("executable_mtime_ticks", std::int64_t{}) == identity.executableMtimeTicks &&
                cached.contains("compiler_family") && cached.at("compiler_family").is_string() &&
                cached.contains("compiler_version") && cached.at("compiler_version").is_string() &&
                cached.contains("compiler_target") && cached.at("compiler_target").is_string())
            {
              identity.compilerFamily = cached.at("compiler_family").get<std::string>();
              identity.compilerVersion = cached.at("compiler_version").get<std::string>();
              identity.compilerTarget = cached.at("compiler_target").get<std::string>();
            }
          }
          catch (const nlohmann::json::exception &) {}
        }
      }

#ifndef _WIN32
      if (identity.compilerFamily.empty() &&
          !identity.compilerPath.starts_with("unresolved-sha256:"))
      {
        // One deterministic GCC/Clang verbose preprocessing probe supplies
        // family, version, and target in a single compiler process.
        vix::engine::process::Command probe;
        probe.argv = {identity.compilerPath, "-v", "-E", "-x", "c++", "/dev/null"};
        probe.environment.emplace_back("LC_ALL", "C");
        const auto result = vix::engine::process::execute(probe);
        if (result.success())
        {
          const std::string_view output{result.output};
          if (output.find("clang version ") != std::string_view::npos)
          {
            identity.compilerFamily = "clang";
            identity.compilerVersion = first_token_after(output, "clang version ");
          }
          else if (output.find("gcc version ") != std::string_view::npos ||
                   output.find("g++ version ") != std::string_view::npos)
          {
            identity.compilerFamily = "gcc";
            const auto marker = output.find("gcc version ") != std::string_view::npos
                                  ? std::string_view{"gcc version "}
                                  : std::string_view{"g++ version "};
            identity.compilerVersion = first_token_after(output, marker);
          }
          const auto targetMarker = output.find("Target: ");
          if (targetMarker != std::string_view::npos)
            identity.compilerTarget = first_token_after(output, "Target: ");
        }
      }
#endif
      if (identity.compilerFamily.empty()) identity.compilerFamily = "unknown";
      if (identity.compilerVersion.empty()) identity.compilerVersion = "unknown";
      if (identity.compilerTarget.empty()) identity.compilerTarget = "unknown";
      if (identity.executableStatAvailable)
      {
        try
        {
          nlohmann::json cached{
            {"schema", "native-toolchain-identity-v1"},
            {"compiler_path", identity.compilerPath},
            {"executable_size", identity.executableSize},
            {"executable_mtime_ticks", identity.executableMtimeTicks},
            {"compiler_family", identity.compilerFamily},
            {"compiler_version", identity.compilerVersion},
            {"compiler_target", identity.compilerTarget}};
          bool changed = false;
          (void)write_text_if_changed(
              native_toolchain_identity_cache_path(projectRoot), cached.dump(2) + "\n", changed);
        }
        catch (const nlohmann::json::exception &) {}
      }
      return identity;
    }

    const char *target_kind_name(project::TargetKind kind)
    {
      switch (kind)
      {
      case project::TargetKind::executable: return "executable";
      case project::TargetKind::static_library: return "static_library";
      case project::TargetKind::shared_library: return "shared_library";
      case project::TargetKind::module_library: return "module_library";
      case project::TargetKind::interface_library: return "interface_library";
      case project::TargetKind::object_library: return "object_library";
      case project::TargetKind::utility: return "utility";
      case project::TargetKind::unknown: return "unknown";
      }
      return "unknown";
    }

    std::optional<project::TargetKind> parse_target_kind(std::string_view value)
    {
      if (value == "executable") return project::TargetKind::executable;
      if (value == "static_library") return project::TargetKind::static_library;
      if (value == "shared_library") return project::TargetKind::shared_library;
      if (value == "module_library") return project::TargetKind::module_library;
      if (value == "interface_library") return project::TargetKind::interface_library;
      if (value == "object_library") return project::TargetKind::object_library;
      if (value == "utility") return project::TargetKind::utility;
      if (value == "unknown") return project::TargetKind::unknown;
      return std::nullopt;
    }

    bool relative_path_is_safe(const fs::path &path)
    {
      if (path.empty() || path.is_absolute()) return false;
      for (const auto &part : path)
        if (part == "..") return false;
      return true;
    }

    bool snapshot_cache_is_under_backend(const fs::path &path, const fs::path &backend)
    {
      const auto relative = path.lexically_normal().lexically_relative(backend.lexically_normal());
      return relative_path_is_safe(relative);
    }

    std::optional<project::ProjectSnapshot> read_native_snapshot_cache(
        const NativeBuildStateContext &context, std::string_view generatedDigest)
    {
      std::error_code ec;
      if (!fs::is_regular_file(context.snapshotCachePath, ec) || ec) return std::nullopt;
      try
      {
        std::ifstream input(context.snapshotCachePath, std::ios::binary);
        const auto cached = nlohmann::json::parse(input, nullptr, false, true);
        if (!cached.is_object() || cached.is_discarded() ||
            cached.value("schema", std::string{}) != "native-target-snapshot-v1" ||
            cached.value("context_id", std::string{}) != context.id ||
            cached.value("generated_input_sha256", std::string{}) != generatedDigest ||
            !cached.contains("project_name") || !cached.at("project_name").is_string() ||
            !cached.contains("snapshot_configuration") || !cached.at("snapshot_configuration").is_string() ||
            !cached.contains("targets") || !cached.at("targets").is_array() ||
            !cached.contains("artifacts") || !cached.at("artifacts").is_array())
          return std::nullopt;

        project::ProjectSnapshot snapshot;
        snapshot.project.root = context.projectRoot;
        snapshot.project.name = cached.at("project_name").get<std::string>();
        snapshot.context.buildRoot = context.backendDir;
        snapshot.context.configuration = cached.at("snapshot_configuration").get<std::string>();
        std::set<std::string> targetIds;
        std::set<std::string> targetNames;
        for (const auto &entry : cached.at("targets"))
        {
          if (!entry.is_object() || !entry.contains("id") || !entry.at("id").is_string() ||
              !entry.contains("name") || !entry.at("name").is_string() ||
              !entry.contains("kind") || !entry.at("kind").is_string() ||
              !entry.contains("buildable") || !entry.at("buildable").is_boolean() ||
              !entry.contains("runnable") || !entry.at("runnable").is_boolean())
            return std::nullopt;
          const auto id = entry.at("id").get<std::string>();
          const auto name = entry.at("name").get<std::string>();
          const auto kind = parse_target_kind(entry.at("kind").get<std::string>());
          if (id.empty() || name.empty() || !kind ||
              !targetIds.insert(id).second || !targetNames.insert(name).second)
            return std::nullopt;
          snapshot.project.targets.push_back({project::TargetId{id}, name, *kind,
            {entry.at("buildable").get<bool>(), entry.at("runnable").get<bool>()}});
        }
        for (const auto &entry : cached.at("artifacts"))
        {
          if (!entry.is_object() || !entry.contains("target") || !entry.at("target").is_string() ||
              !entry.contains("path") || !entry.at("path").is_string())
            return std::nullopt;
          const auto target = entry.at("target").get<std::string>();
          const fs::path relative{entry.at("path").get<std::string>()};
          if (!targetIds.contains(target) || !relative_path_is_safe(relative)) return std::nullopt;
          snapshot.artifacts.push_back({project::TargetId{target}, context.backendDir / relative});
        }
        return snapshot;
      }
      catch (const nlohmann::json::exception &)
      {
        return std::nullopt;
      }
    }

    void write_native_snapshot_cache(const NativeBuildStateContext &context,
                                     std::string_view generatedDigest,
                                     const project::ProjectSnapshot &snapshot)
    {
      try
      {
        nlohmann::json cached{
          {"schema", "native-target-snapshot-v1"},
          {"context_id", context.id},
          {"generated_input_sha256", generatedDigest},
          {"project_name", snapshot.project.name},
          {"snapshot_configuration", snapshot.context.configuration},
          {"targets", nlohmann::json::array()},
          {"artifacts", nlohmann::json::array()}};
        for (const auto &target : snapshot.project.targets)
          cached["targets"].push_back({
            {"id", target.id.value}, {"name", target.name}, {"kind", target_kind_name(target.kind)},
            {"buildable", target.capabilities.buildable}, {"runnable", target.capabilities.runnable}});
        for (const auto &artifact : snapshot.artifacts)
        {
          const auto relative = artifact.path.lexically_normal().lexically_relative(context.backendDir.lexically_normal());
          if (!snapshot_cache_is_under_backend(artifact.path, context.backendDir)) return;
          cached["artifacts"].push_back({{"target", artifact.target.value}, {"path", relative.generic_string()}});
        }
        (void)write_text(context.snapshotCachePath, cached.dump(2) + "\n");
      }
      catch (const nlohmann::json::exception &) {}
    }

    vix::engine::process::Result execute(std::vector<std::string> argv, const fs::path &cwd = {},
                                         const NativeDevelopmentHooks *hooks = nullptr,
                                         vix::engine::SanitizerMode diagnosticMode = vix::engine::SanitizerMode::None)
    {
      if (hooks && hooks->execute)
      {
        const auto result = hooks->execute(argv, cwd, diagnosticMode);
        return {result.success, result.exitCode, {}, result.output, result.diagnostic, !result.output.empty()};
      }
      vix::engine::process::Command command;
      command.argv = std::move(argv);
      command.workingDirectory = cwd;
      if (diagnosticMode == vix::engine::SanitizerMode::Thread)
        command.environment.emplace_back("TSAN_OPTIONS", "halt_on_error=1:second_deadlock_stack=1:history_size=7:color=never");
      else
      {
        if (diagnosticMode == vix::engine::SanitizerMode::Undefined ||
            diagnosticMode == vix::engine::SanitizerMode::AddressUndefined)
          command.environment.emplace_back("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1:color=never");
        if (diagnosticMode == vix::engine::SanitizerMode::Address ||
            diagnosticMode == vix::engine::SanitizerMode::AddressUndefined)
          command.environment.emplace_back("ASAN_OPTIONS", "abort_on_error=1:detect_leaks=1:symbolize=1:allocator_may_return_null=1:fast_unwind_on_malloc=0:strict_init_order=1:check_initialization_order=1:color=never:quiet=1");
      }
      return vix::engine::process::execute(command);
    }

    std::optional<Lockfile> read_lock(const fs::path &root, const NativeProject &project,
                                      std::string &diagnostic, NativeDevelopmentErrorCode &errorCode)
    {
      const auto path = root / "vix.lock";
      if (project.packages.empty())
      {
        if (fs::exists(path))
        {
          const auto parsed = parse_lockfile_file(path);
          if (!parsed.valid())
          {
            diagnostic = "vix.lock is invalid";
            errorCode = NativeDevelopmentErrorCode::invalid_lock;
            return std::nullopt;
          }
          const auto state = evaluate_project_lock_state(project, parsed.value);
          if (state.state != ProjectLockState::not_required)
          {
            diagnostic = "lock state is not valid for a project without package requirements";
            errorCode = NativeDevelopmentErrorCode::invalid_lock;
            return std::nullopt;
          }
        }
        Lockfile empty;
        empty.rootRequirementsDigest = project_requirements_digest(project);
        return empty;
      }
      if (!fs::is_regular_file(path))
      {
        diagnostic = "package requirements exist but vix.lock is missing";
        errorCode = NativeDevelopmentErrorCode::missing_lock;
        return std::nullopt;
      }
      const auto parsed = parse_lockfile_file(path);
      if (!parsed.valid())
      {
        diagnostic = parsed.issues.empty() ? "vix.lock is invalid" : parsed.issues.front().message;
        errorCode = NativeDevelopmentErrorCode::invalid_lock;
        return std::nullopt;
      }
      const auto state = evaluate_project_lock_state(project, parsed.value);
      if (state.state != ProjectLockState::current)
      {
        diagnostic = "vix.lock is not current for vix.toml";
        errorCode = state.state == ProjectLockState::stale ? NativeDevelopmentErrorCode::stale_lock : NativeDevelopmentErrorCode::invalid_lock;
        return std::nullopt;
      }
      return *parsed.value;
    }

    std::optional<PreparedNativeBuild> prepare(const fs::path &inputRoot, AcquisitionMode mode,
                                               std::string_view presetName,
                                               vix::engine::SanitizerMode diagnosticMode,
                                               std::string &diagnostic,
                                               NativeDevelopmentErrorCode &errorCode,
                                               const NativeDevelopmentHooks *hooks,
                                               bool clean = false)
    {
      const fs::path root = fs::absolute(inputRoot).lexically_normal();
      const auto manifest = parse_native_manifest_file(root / "vix.toml");
      if (!manifest.valid())
      {
        diagnostic = manifest.issues.empty() ? "vix.toml is invalid" : manifest.issues.front().message;
        if (std::any_of(manifest.issues.begin(), manifest.issues.end(), [](const ParseIssue &issue)
                        { return issue.fieldPath.starts_with("tests"); }))
          errorCode = NativeDevelopmentErrorCode::invalid_test_registration;
        return std::nullopt;
      }
      const auto projectValidation = validate_native_project(*manifest.value);
      if (!projectValidation.valid())
      {
        diagnostic = projectValidation.issues.front().message;
        if (std::any_of(projectValidation.issues.begin(), projectValidation.issues.end(), [](const ValidationIssue &issue)
                        { return issue.code == ValidationCode::invalid_test; }))
          errorCode = NativeDevelopmentErrorCode::invalid_test_registration;
        return std::nullopt;
      }
      auto lock = read_lock(root, *manifest.value, diagnostic, errorCode);
      if (!lock)
        return std::nullopt;

      PreparedNativeBuild prepared;
      prepared.project = *manifest.value;
      prepared.lock = *lock;
      prepared.projectRoot = root;
      prepared.diagnosticMode = diagnosticMode;
      const auto preset = vix::engine::resolve_builtin_preset(std::string{presetName});
      if (!preset)
      {
        diagnostic = "unknown build preset";
        return std::nullopt;
      }
      const auto context = make_native_build_state_context(
          root, preset->buildType == "Release" ? "release" : "development",
          native_platform_identity(), native_toolchain_identity(root),
          preset->generator, diagnosticMode);
      prepared.sourceRoot = context.generatedSourceDir;
      prepared.buildRoot = context.backendDir;
      if (clean)
      {
        if (!reset_native_build_state_context(context, diagnostic))
        {
          errorCode = NativeDevelopmentErrorCode::build_failure;
          return std::nullopt;
        }
      }

      if (!prepared.project.packages.empty())
      {
        if (hooks && hooks->preparePackages)
        {
          auto packages = hooks->preparePackages(root, prepared.project, prepared.lock, mode, diagnostic);
          if (!packages)
          {
            errorCode = NativeDevelopmentErrorCode::package_preparation_failure;
            return std::nullopt;
          }
          prepared.packages = std::move(*packages);
        }
        else
        {
        try
        {
          const auto storePath = package_content_store_path();
          if (storePath.empty())
          {
            diagnostic = "cannot determine P10P path; set HOME or XDG_CACHE_HOME";
            errorCode = NativeDevelopmentErrorCode::package_preparation_failure;
            return std::nullopt;
          }
          FilesystemPackageContentStore store{storePath};
          const auto plan = required_package_content(prepared.lock);
          if (!plan.valid())
          {
            diagnostic = "could not derive required content from valid lock";
            errorCode = NativeDevelopmentErrorCode::package_preparation_failure;
            return std::nullopt;
          }
          bool registrySourceRequired = false;
          for (const auto &required : plan.required)
          {
            const auto inspected = store.inspect(required.id);
            if (!inspected.valid() || !inspected.value || *inspected.value == PackageContentAvailability::corrupt)
            {
              diagnostic = "locked P10P content is corrupt or cannot be inspected: " + required.id.digest();
              return std::nullopt;
            }
            if (*inspected.value == PackageContentAvailability::missing && mode == AcquisitionMode::online)
              for (const auto &nodeId : required.nodeIds)
              {
                const auto node = std::find_if(prepared.lock.nodes.begin(), prepared.lock.nodes.end(), [&](const auto &entry)
                                               { return std::visit([&](const auto &value)
                                                                   { return value.id == nodeId; }, entry); });
                if (node != prepared.lock.nodes.end() && std::holds_alternative<RegistryLockNode>(*node))
                  registrySourceRequired = true;
              }
          }

          std::vector<RegistryDefinition> definitions;
          if (registrySourceRequired)
          {
            const auto runtime = load_registry_runtime_configuration(registry_runtime_configuration_path());
            if (!runtime.valid())
            {
              diagnostic = runtime.diagnostic;
              return std::nullopt;
            }
            definitions = runtime.configuration->registries;
            for (auto &definition : definitions)
              for (const auto &location : definition.locations)
                definition.contentLocations.push_back({location.descriptor});
          }
          const auto directory = build_registry_directory(std::move(definitions));
          if (!directory.valid())
          {
            diagnostic = "Registry package-content routing configuration is invalid";
            return std::nullopt;
          }
          RequestsRegistryHttpsRequestExecutor executor;
          SchemeRegistryContentTransport transport{executor};
          TransportRegistryPackageContentSource registrySource{transport};
          NativeGitCommandRunner runner;
          const auto gitWorkRoot = fs::temp_directory_path() /
                                   ("vix-git-content-build-" + std::to_string(
                                                                   std::chrono::steady_clock::now().time_since_epoch().count()));
          GitCliPackageContentSource gitSource{gitWorkRoot, runner};
          auto installed = install_native_project_packages(root, mode, *directory.value,
                                                           registrySource, gitSource, store, package_materialization_root(root));
          if (!installed.valid())
          {
            diagnostic = installed.issues.empty() ? "locked package preparation failed" : installed.issues.front().message;
            errorCode = NativeDevelopmentErrorCode::package_preparation_failure;
            return std::nullopt;
          }

          if (!plan.valid() || plan.required.size() != installed.materializations.size())
          {
            diagnostic = "installed content does not match the deterministic lock plan";
            errorCode = NativeDevelopmentErrorCode::package_preparation_failure;
            return std::nullopt;
          }
          for (std::size_t index = 0; index < plan.required.size(); ++index)
          {
            const auto &required = plan.required[index];
            const auto &materialization = installed.materializations[index];
            if (!materialization.path)
            {
              diagnostic = "package materialization did not return its root";
              errorCode = NativeDevelopmentErrorCode::package_preparation_failure;
              return std::nullopt;
            }
            for (const auto &node : required.nodeIds)
              prepared.packages.push_back({node, required.id, *materialization.path});
          }
        }
        catch (const std::exception &error)
        {
          diagnostic = error.what();
          errorCode = NativeDevelopmentErrorCode::package_preparation_failure;
          return std::nullopt;
        }
        }
      }

      const auto graph = generate_package_build_graph(
          prepared.project, root, prepared.lock, prepared.packages, diagnosticMode);
      if (!graph.valid())
      {
        diagnostic = graph.issues.empty() ? "native package build graph is invalid" : graph.issues.front().message;
        return std::nullopt;
      }
      const auto generatedCMake = prepared.sourceRoot / "CMakeLists.txt";
      const std::string generatedDigest = sha256_hex(*graph.cmakeLists);
      bool generatedInputChanged = false;
      if (!write_text_if_changed(generatedCMake, *graph.cmakeLists, generatedInputChanged))
      {
        diagnostic = "unable to publish generated native build graph";
        return std::nullopt;
      }
      const std::string expectedMetadata = native_build_context_metadata(context, generatedDigest);
      std::ifstream configuredSignatureInput(context.configuredInputSignaturePath, std::ios::binary);
      const std::string configuredInputSignature{
          std::istreambuf_iterator<char>{configuredSignatureInput}, std::istreambuf_iterator<char>{}};
      std::error_code cacheError;
      const bool hasCMakeCache = fs::is_regular_file(prepared.buildRoot / "CMakeCache.txt", cacheError) && !cacheError;
      cacheError.clear();
      bool hasGeneratorState = true;
      if (preset->generator.starts_with("Ninja"))
        hasGeneratorState = fs::is_regular_file(prepared.buildRoot / "build.ninja", cacheError) && !cacheError;
      else if (preset->generator == "Unix Makefiles")
        hasGeneratorState = fs::is_regular_file(prepared.buildRoot / "Makefile", cacheError) && !cacheError;
      std::ifstream contextMetadataInput(context.metadataPath, std::ios::binary);
      const std::string existingMetadata{
          std::istreambuf_iterator<char>{contextMetadataInput}, std::istreambuf_iterator<char>{}};
      cacheError.clear();
      const bool metadataExists = fs::is_regular_file(context.metadataPath, cacheError) && !cacheError;
      const bool metadataValid = metadataExists && existingMetadata == expectedMetadata;
      const bool backendConfigured = hasCMakeCache && hasGeneratorState;
      bool configuredSuccessfully = false;
      if (hooks && hooks->configure)
      {
        auto configured = hooks->configure(root, prepared.sourceRoot, prepared.buildRoot,
                                           prepared.project, prepared.lock, prepared.packages, presetName,
                                           diagnosticMode);
        if (!configured.success || !configured.snapshot)
        {
          diagnostic = configured.diagnostic.empty() ? "native project configuration failed" : configured.diagnostic;
          errorCode = NativeDevelopmentErrorCode::build_failure;
          return std::nullopt;
        }
        prepared.snapshot = std::move(*configured.snapshot);
        prepared.snapshot->project.root = root;
        prepared.snapshot->project.tests.clear();
        for (std::size_t index = 0; index < prepared.project.tests.size(); ++index)
        {
          const auto &registration = prepared.project.tests[index];
          const auto target = project::select_target_by_name(*prepared.snapshot, registration.target);
          if (!target.success())
          {
            diagnostic = "registered test target is absent from the generated project snapshot";
            errorCode = NativeDevelopmentErrorCode::invalid_test_registration;
            return std::nullopt;
          }
          prepared.snapshot->project.tests.push_back({project::TestId{"native-test:" + std::to_string(index)}, registration.name,
                                                       target.target->id});
        }
        bool metadataChanged = false;
        if (!write_text_if_changed(context.metadataPath, expectedMetadata, metadataChanged))
        {
          diagnostic = "unable to write native build context metadata";
          errorCode = NativeDevelopmentErrorCode::build_failure;
          return std::nullopt;
        }
        if (!write_text(context.configuredInputSignaturePath, generatedDigest + "\n"))
        {
          diagnostic = "unable to write native configured-input signature";
          errorCode = NativeDevelopmentErrorCode::build_failure;
          return std::nullopt;
        }
        return prepared;
      }
      const bool configureRequired = !backendConfigured || !metadataValid || generatedInputChanged ||
        configuredInputSignature != generatedDigest + "\n";
      if (!configureRequired)
        prepared.snapshot = read_native_snapshot_cache(context, generatedDigest);
      const bool snapshotLoadedFromCache = prepared.snapshot.has_value();
      prepared.snapshotFromCache = snapshotLoadedFromCache;
      if (!prepared.snapshot)
      {
        if (configureRequired)
        {
          const auto query = project::cmake::prepare_cmake_file_api_query(prepared.buildRoot);
          if (!query.success())
          {
            diagnostic = "unable to prepare CMake target/artifact observation";
            return std::nullopt;
          }
          std::vector<std::string> configure{"cmake", "-S", prepared.sourceRoot.string(), "-B", prepared.buildRoot.string()};
          if (!preset->generator.empty())
          {
            configure.push_back("-G");
            configure.push_back(preset->generator);
          }
          if (!preset->buildType.empty())
            configure.push_back("-DCMAKE_BUILD_TYPE=" + preset->buildType);
          configure.push_back("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON");
          const auto configured = execute(std::move(configure));
          if (!configured.success())
          {
            diagnostic = configured.errorMessage.empty() ? configured.output : configured.errorMessage;
            errorCode = NativeDevelopmentErrorCode::build_failure;
            return std::nullopt;
          }
          configuredSuccessfully = true;
        }
        const project::ProjectLocation generated{prepared.sourceRoot, project::ProjectSystem::cmake};
        auto snapshot = project::cmake::load_cmake_target_snapshot(generated, {prepared.buildRoot, std::nullopt});
        if (!snapshot.success())
        {
          diagnostic = "unable to observe generated build targets";
          errorCode = NativeDevelopmentErrorCode::build_failure;
          return std::nullopt;
        }
        prepared.snapshot = std::move(*snapshot.snapshot);
      }
      prepared.snapshot->project.root = root;
      prepared.snapshot->project.tests.clear();
      for (std::size_t index = 0; index < prepared.project.tests.size(); ++index)
      {
        const auto &registration = prepared.project.tests[index];
        const auto target = project::select_target_by_name(*prepared.snapshot, registration.target);
        if (!target.success())
        {
          diagnostic = "registered test target is absent from the generated project snapshot";
          errorCode = NativeDevelopmentErrorCode::invalid_test_registration;
          return std::nullopt;
        }
        prepared.snapshot->project.tests.push_back({project::TestId{"native-test:" + std::to_string(index)}, registration.name,
                                                    target.target->id});
      }
      if (configuredSuccessfully && !write_text(context.configuredInputSignaturePath, generatedDigest + "\n"))
      {
        diagnostic = "unable to write native configured-input signature";
        errorCode = NativeDevelopmentErrorCode::build_failure;
        return std::nullopt;
      }
      bool metadataChanged = false;
      if (!write_text_if_changed(context.metadataPath, expectedMetadata, metadataChanged))
      {
        diagnostic = "unable to write native build context metadata";
        errorCode = NativeDevelopmentErrorCode::build_failure;
        return std::nullopt;
      }
      if (!snapshotLoadedFromCache)
        write_native_snapshot_cache(context, generatedDigest, *prepared.snapshot);
      return prepared;
    }

    NativeDevelopmentResult build_prepared(PreparedNativeBuild &prepared,
                                           const std::optional<std::string> &requested, int jobs,
                                           const NativeDevelopmentHooks *hooks,
                                           bool silentSuccessfulBuild = false)
    {
      std::vector<std::string> command{"cmake", "--build", prepared.buildRoot.string()};
      if (requested)
      {
        const auto target = project::select_buildable_target_by_name(*prepared.snapshot, *requested);
        if (!target.success())
          return failure("target selection failed: " + std::string{project::to_string(*target.error)}, 2,
                         target_error_code(target.error), target.error);
        command.push_back("--target");
        command.push_back(target.target->name);
      }
      if (jobs > 0)
      {
        command.push_back("--parallel");
        command.push_back(std::to_string(jobs));
      }
      if (hooks && hooks->build)
      {
        auto result = hooks->build(prepared.sourceRoot, prepared.buildRoot, *prepared.snapshot, requested, jobs,
                                   prepared.diagnosticMode);
        if (!result.success || !result.snapshot)
          return failure(result.diagnostic.empty() ? "native build failed" : result.diagnostic,
                         result.exitCode == 0 ? 1 : result.exitCode, NativeDevelopmentErrorCode::build_failure);
        prepared.snapshot = std::move(*result.snapshot);
        return success(silentSuccessfulBuild ? std::string{} : std::move(result.output));
      }
      const auto result = execute(std::move(command));
      if (!result.success())
        return failure(result.errorMessage.empty() ? result.output : result.errorMessage,
                       result.exitCode == 0 ? 1 : result.exitCode, NativeDevelopmentErrorCode::build_failure);
      return success(silentSuccessfulBuild ? std::string{} : result.output);
    }
  } // namespace

  NativeDevelopmentResult build_native_project(const fs::path &root,
                                               const std::optional<std::string> &target, int jobs, std::string_view preset,
                                               AcquisitionMode mode, const NativeDevelopmentHooks *hooks,
                                               vix::engine::SanitizerMode diagnosticMode, bool clean)
  {
    std::string diagnostic;
      NativeDevelopmentErrorCode errorCode{NativeDevelopmentErrorCode::project_error};
      auto prepared = prepare(root, mode, preset, diagnosticMode, diagnostic, errorCode, hooks, clean);
    if (!prepared)
      return failure(std::move(diagnostic), 1, errorCode);
    return build_prepared(*prepared, target, jobs, hooks);
  }

  NativeDevelopmentResult build_native_runnable_project(const fs::path &root,
                                                        const std::optional<std::string> &target,
                                                        std::string_view preset, AcquisitionMode mode,
                                                        const NativeDevelopmentHooks *hooks,
                                                        vix::engine::SanitizerMode diagnosticMode)
  {
    std::string preparationDiagnostic;
    NativeDevelopmentErrorCode errorCode{NativeDevelopmentErrorCode::project_error};
    auto prepared = prepare(root, mode, preset, diagnosticMode, preparationDiagnostic, errorCode, hooks);
    if (!prepared)
      return failure(std::move(preparationDiagnostic), 1, errorCode);
    auto selected = target ? project::select_runnable_target_by_name(*prepared->snapshot, *target)
                           : project::select_default_runnable_target(*prepared->snapshot);
    if (!selected.success())
      return failure("run target selection failed: " + std::string{project::to_string(*selected.error)}, 2,
                     target_error_code(selected.error), selected.error);
    if (prepared->snapshotFromCache)
    {
      const auto cachedArtifacts = project::artifacts_for_target(*prepared->snapshot, selected.target->id);
      std::error_code artifactError;
      const bool cachedArtifactMissing = cachedArtifacts.empty() ||
        (cachedArtifacts.size() == 1 &&
         (!fs::is_regular_file(cachedArtifacts.front().path, artifactError) || artifactError));
      if (cachedArtifactMissing)
      {
        const auto refreshed = project::cmake::load_cmake_target_snapshot(
            {prepared->sourceRoot, project::ProjectSystem::cmake}, {prepared->buildRoot, std::nullopt});
        if (!refreshed.success())
          return failure("cached TargetArtifact metadata was invalid and could not be refreshed", 1,
                         NativeDevelopmentErrorCode::artifact_unavailable);
        prepared->snapshot = *refreshed.snapshot;
        prepared->snapshot->project.root = prepared->projectRoot;
        prepared->snapshotFromCache = false;
        selected = target ? project::select_runnable_target_by_name(*prepared->snapshot, *target)
                          : project::select_default_runnable_target(*prepared->snapshot);
        if (!selected.success())
          return failure("run target selection failed after refreshing configured metadata", 2,
                         target_error_code(selected.error), selected.error);
      }
    }
    const auto built = build_prepared(*prepared, selected.target->name, 0, hooks, true);
    if (!built.success())
      return built;
    const auto rebuiltTarget = project::select_runnable_target_by_name(
        *prepared->snapshot, selected.target->name);
    if (!rebuiltTarget.success())
      return failure("selected runnable target disappeared from build snapshot", 1,
                     target_error_code(rebuiltTarget.error), rebuiltTarget.error);
    const auto targetArtifacts = project::artifacts_for_target(*prepared->snapshot, rebuiltTarget.target->id);
    fs::path artifactPath;
    if (hooks)
    {
      if (targetArtifacts.size() != 1)
        return failure("runnable target must have exactly one observed TargetArtifact", 1,
                       NativeDevelopmentErrorCode::artifact_unavailable);
      artifactPath = targetArtifacts.front().path;
    }
    else if (targetArtifacts.size() == 1)
    {
      artifactPath = targetArtifacts.front().path;
    }
    else
    {
      const auto artifact = project::cmake::resolve_runnable_artifact(
          {prepared->sourceRoot, project::ProjectSystem::cmake},
          {prepared->buildRoot, std::nullopt}, rebuiltTarget.target->id);
      if (!artifact.success())
        return failure("selected runnable target has no configured TargetArtifact", 1,
                       NativeDevelopmentErrorCode::artifact_unavailable);
      const auto selectedArtifact = std::find_if(targetArtifacts.begin(), targetArtifacts.end(),
          [&](const auto &item) { return item.path == *artifact.artifact; });
      if (selectedArtifact == targetArtifacts.end())
        return failure("configured runnable artifact is absent from ProjectSnapshot", 1,
                       NativeDevelopmentErrorCode::artifact_unavailable);
      artifactPath = selectedArtifact->path;
    }
    if (!hooks)
    {
      if (!snapshot_cache_is_under_backend(artifactPath, prepared->buildRoot))
        return failure("configured TargetArtifact is outside the Vix-owned backend directory", 1,
                       NativeDevelopmentErrorCode::artifact_unavailable);
      std::error_code artifactError;
      if (!fs::is_regular_file(artifactPath, artifactError) || artifactError)
        return failure("configured TargetArtifact is missing after backend freshness check", 1,
                       NativeDevelopmentErrorCode::artifact_unavailable);
    }
    auto result = success();
    result.artifactPath = std::move(artifactPath);
    return result;
  }

  NativeDevelopmentResult run_native_project(const fs::path &root,
                                             const std::optional<std::string> &target, const std::vector<std::string> &arguments,
                                             std::string_view preset, AcquisitionMode mode,
                                             const NativeDevelopmentHooks *hooks,
                                             vix::engine::SanitizerMode diagnosticMode,
                                             const fs::path &runtimeWorkingDirectory)
  {
    auto prepared = build_native_runnable_project(root, target, preset, mode, hooks, diagnosticMode);
    if (!prepared.success() || !prepared.artifactPath)
      return prepared.success()
          ? failure("selected runnable target has no configured TargetArtifact", 1,
                    NativeDevelopmentErrorCode::artifact_unavailable)
          : prepared;
    std::vector<std::string> argv{prepared.artifactPath->string()};
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    const auto execution = execute(std::move(argv),
                                   runtimeWorkingDirectory.empty() ? root : runtimeWorkingDirectory,
                                   hooks, diagnosticMode);
    std::string output = prepared.output;
    output += execution.output;
    const int exitCode = execution.success() ? execution.exitCode : (execution.exitCode == 0 ? 1 : execution.exitCode);
    const std::string executionDiagnostic = execution.errorMessage.empty() && exitCode != 0
                                       ? "program execution failed with exit code " + std::to_string(exitCode)
                                       : execution.errorMessage;
    if (exitCode == 0)
      return success(std::move(output));
    return failure(executionDiagnostic, exitCode, NativeDevelopmentErrorCode::process_execution_failure,
                   std::nullopt, std::move(output));
  }

  NativeDevelopmentResult test_native_project(const fs::path &root,
                                              const std::optional<std::string> &testName, bool listOnly,
                                              const std::vector<std::string> &extraCtestArguments, std::string_view testPattern,
                                              std::string_view preset, AcquisitionMode mode,
                                              const NativeDevelopmentHooks *hooks,
                                              vix::engine::SanitizerMode diagnosticMode)
  {
    std::string diagnostic;
    NativeDevelopmentErrorCode errorCode{NativeDevelopmentErrorCode::project_error};
    auto prepared = prepare(root, mode, preset, diagnosticMode, diagnostic, errorCode, hooks);
    if (!prepared)
      return failure(std::move(diagnostic), 1, errorCode);
    if (testName)
    {
      const auto selectedTest = project::select_test_by_name(*prepared->snapshot, *testName);
      if (!selectedTest.success())
          return failure("test registration was not found or is ambiguous", 2, NativeDevelopmentErrorCode::test_not_found);
    }
    if (prepared->snapshot->project.tests.empty())
      return testName ? failure("no registered tests", 2, NativeDevelopmentErrorCode::test_not_found)
                      : success();
    std::set<std::string> targets;
    for (const auto &test : prepared->snapshot->project.tests)
    {
      if (testName && test.name != *testName)
        continue;
      if (!test.target)
        return failure("registered test has no associated build target", 1, NativeDevelopmentErrorCode::invalid_test_registration);
      const auto target = project::find_target_by_id(*prepared->snapshot, *test.target);
      if (!target.success())
        return failure("registered test references an unavailable target", 1, NativeDevelopmentErrorCode::invalid_test_registration);
      if (!target.target->capabilities.runnable)
        return failure("registered test target is not runnable", 1, NativeDevelopmentErrorCode::target_not_runnable);
      targets.insert(target.target->name);
    }
    if (targets.empty() && !listOnly)
      return failure("no registered tests selected", 2);
    if (!listOnly)
      for (const auto &targetName : targets)
      {
        const auto selected = project::select_buildable_target_by_name(*prepared->snapshot, targetName);
        if (!selected.success())
          return failure("registered test target is not buildable", 2, NativeDevelopmentErrorCode::target_not_buildable, selected.error);
        const auto built = build_prepared(*prepared, selected.target->name, 0, hooks, true);
        if (!built.success())
          return built;
      }
    std::vector<std::string> ctestArguments;
    if (listOnly)
      ctestArguments.push_back("-N");
    if (!testPattern.empty())
    {
      ctestArguments.push_back("-R");
      ctestArguments.emplace_back(testPattern);
    }
    ctestArguments.insert(ctestArguments.end(), extraCtestArguments.begin(), extraCtestArguments.end());
    const auto argv = project::cmake::ctest_execution_argv(prepared->buildRoot, std::nullopt,
                                                           testName, ctestArguments);
    const auto result = execute(argv, root, hooks, diagnosticMode);
    return result.success() ? success(result.output)
                            : failure(result.errorMessage.empty() ? result.output : result.errorMessage,
                                      result.exitCode == 0 ? 1 : result.exitCode,
                                      NativeDevelopmentErrorCode::process_execution_failure,
                                      std::nullopt, result.output);
  }
} // namespace vix::cli::project::native
