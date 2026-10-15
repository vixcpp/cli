#include <vix/cli/project/native/NativeDevelopmentRuntime.hpp>

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

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <map>
#include <set>
#include <utility>

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

    vix::engine::process::Result execute(std::vector<std::string> argv, const fs::path &cwd = {},
                                         const NativeDevelopmentHooks *hooks = nullptr)
    {
      if (hooks && hooks->execute)
      {
        const auto result = hooks->execute(argv, cwd);
        return {result.success, result.exitCode, {}, result.output, result.diagnostic, !result.output.empty()};
      }
      vix::engine::process::Command command;
      command.argv = std::move(argv);
      command.workingDirectory = cwd;
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
                                               std::string &diagnostic,
                                               NativeDevelopmentErrorCode &errorCode,
                                               const NativeDevelopmentHooks *hooks)
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
      prepared.sourceRoot = root / ".vix" / "build" / "native-source";
      const auto preset = vix::engine::resolve_builtin_preset(std::string{presetName});
      if (!preset)
      {
        diagnostic = "unknown build preset";
        return std::nullopt;
      }
      prepared.buildRoot = root / ".vix" / "build" / preset->buildDirName;

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

      const auto graph = generate_package_build_graph(prepared.project, root, prepared.lock, prepared.packages);
      if (!graph.valid())
      {
        diagnostic = graph.issues.empty() ? "native package build graph is invalid" : graph.issues.front().message;
        return std::nullopt;
      }
      if (!write_text(prepared.sourceRoot / "CMakeLists.txt", *graph.cmakeLists))
      {
        diagnostic = "unable to publish generated native build graph";
        return std::nullopt;
      }
      if (hooks && hooks->configure)
      {
        auto configured = hooks->configure(root, prepared.sourceRoot, prepared.buildRoot,
                                           prepared.project, prepared.lock, prepared.packages, presetName);
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
        return prepared;
      }
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
      const project::ProjectLocation generated{prepared.sourceRoot, project::ProjectSystem::cmake};
      auto snapshot = project::cmake::load_cmake_target_snapshot(generated, {prepared.buildRoot, std::nullopt});
      if (!snapshot.success())
      {
        diagnostic = "unable to observe generated build targets";
        errorCode = NativeDevelopmentErrorCode::build_failure;
        return std::nullopt;
      }
      snapshot.snapshot->project.root = root;
      snapshot.snapshot->project.tests.clear();
      for (std::size_t index = 0; index < prepared.project.tests.size(); ++index)
      {
        const auto &registration = prepared.project.tests[index];
        const auto target = project::select_target_by_name(*snapshot.snapshot, registration.target);
        if (!target.success())
        {
          diagnostic = "registered test target is absent from the generated project snapshot";
          errorCode = NativeDevelopmentErrorCode::invalid_test_registration;
          return std::nullopt;
        }
        snapshot.snapshot->project.tests.push_back({project::TestId{"native-test:" + std::to_string(index)}, registration.name,
                                                    target.target->id});
      }
      prepared.snapshot = std::move(*snapshot.snapshot);
      return prepared;
    }

    NativeDevelopmentResult build_prepared(PreparedNativeBuild &prepared,
                                           const std::optional<std::string> &requested, int jobs,
                                           const NativeDevelopmentHooks *hooks)
    {
      std::vector<std::string> command{"cmake", "--build", prepared.buildRoot.string()};
      if (requested)
      {
        const auto target = project::select_buildable_target_by_name(*prepared.snapshot, *requested);
        if (!target.success())
          return failure("target selection failed: " + std::string{project::to_string(*target.error)}, 2,
                         target_error_code(target.error), target.error);
        if (hooks && hooks->build)
        {
          command.push_back("--target");
          command.push_back(target.target->name);
        }
        else
        {
        const auto resolved = project::cmake::resolve_native_build_target(
            {prepared.sourceRoot, project::ProjectSystem::cmake}, {prepared.buildRoot, std::nullopt}, target.target->id);
        if (!resolved.success())
          return failure("build target is unavailable in generated graph", 2, NativeDevelopmentErrorCode::target_not_found,
                         resolved.error);
        command.push_back("--target");
        command.push_back(*resolved.target);
        }
      }
      if (jobs > 0)
      {
        command.push_back("--parallel");
        command.push_back(std::to_string(jobs));
      }
      if (hooks && hooks->build)
      {
        auto result = hooks->build(prepared.sourceRoot, prepared.buildRoot, *prepared.snapshot, requested, jobs);
        if (!result.success || !result.snapshot)
          return failure(result.diagnostic.empty() ? "native build failed" : result.diagnostic,
                         result.exitCode == 0 ? 1 : result.exitCode, NativeDevelopmentErrorCode::build_failure);
        prepared.snapshot = std::move(*result.snapshot);
        return success(std::move(result.output));
      }
      const auto result = execute(std::move(command));
      if (!result.success())
        return failure(result.errorMessage.empty() ? result.output : result.errorMessage,
                       result.exitCode == 0 ? 1 : result.exitCode, NativeDevelopmentErrorCode::build_failure);
      const auto finalSnapshot = project::cmake::load_cmake_target_snapshot(
          {prepared.sourceRoot, project::ProjectSystem::cmake}, {prepared.buildRoot, std::nullopt});
      if (!finalSnapshot.success())
        return failure("build succeeded but configured target artifacts could not be observed", 1,
                       NativeDevelopmentErrorCode::artifact_unavailable);
      prepared.snapshot = *finalSnapshot.snapshot;
      prepared.snapshot->project.root = prepared.projectRoot;
      prepared.snapshot->project.tests.clear();
      for (std::size_t index = 0; index < prepared.project.tests.size(); ++index)
      {
        const auto &registration = prepared.project.tests[index];
        const auto target = project::select_target_by_name(*prepared.snapshot, registration.target);
        if (!target.success())
          return failure("registered test target is absent from the generated project snapshot", 1,
                         NativeDevelopmentErrorCode::invalid_test_registration);
        prepared.snapshot->project.tests.push_back({project::TestId{"native-test:" + std::to_string(index)}, registration.name,
                                                    target.target->id});
      }
      return success(result.output);
    }
  } // namespace

  NativeDevelopmentResult build_native_project(const fs::path &root,
                                               const std::optional<std::string> &target, int jobs, std::string_view preset,
                                               AcquisitionMode mode, const NativeDevelopmentHooks *hooks)
  {
    std::string diagnostic;
      NativeDevelopmentErrorCode errorCode{NativeDevelopmentErrorCode::project_error};
      auto prepared = prepare(root, mode, preset, diagnostic, errorCode, hooks);
    if (!prepared)
      return failure(std::move(diagnostic), 1, errorCode);
    return build_prepared(*prepared, target, jobs, hooks);
  }

  NativeDevelopmentResult run_native_project(const fs::path &root,
                                             const std::optional<std::string> &target, const std::vector<std::string> &arguments,
                                             std::string_view preset, AcquisitionMode mode,
                                             const NativeDevelopmentHooks *hooks)
  {
    std::string preparationDiagnostic;
    NativeDevelopmentErrorCode errorCode{NativeDevelopmentErrorCode::project_error};
    auto prepared = prepare(root, mode, preset, preparationDiagnostic, errorCode, hooks);
    if (!prepared)
      return failure(std::move(preparationDiagnostic), 1, errorCode);
    auto selected = target ? project::select_runnable_target_by_name(*prepared->snapshot, *target)
                           : project::select_default_runnable_target(*prepared->snapshot);
    if (!selected.success())
      return failure("run target selection failed: " + std::string{project::to_string(*selected.error)}, 2,
                     target_error_code(selected.error), selected.error);
    const auto built = build_prepared(*prepared, selected.target->name, 0, hooks);
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
        return failure("runnable target must have exactly one observed TargetArtifact", 1, NativeDevelopmentErrorCode::artifact_unavailable);
      artifactPath = targetArtifacts.front().path;
    }
    else
    {
      const auto artifact = project::cmake::resolve_runnable_artifact(
          {prepared->sourceRoot, project::ProjectSystem::cmake},
          {prepared->buildRoot, std::nullopt}, rebuiltTarget.target->id);
      if (!artifact.success())
        return failure("selected runnable target has no configured TargetArtifact", 1, NativeDevelopmentErrorCode::artifact_unavailable);
      const auto selectedArtifact = std::find_if(targetArtifacts.begin(), targetArtifacts.end(),
                                                 [&](const auto &item) { return item.path == *artifact.artifact; });
      if (selectedArtifact == targetArtifacts.end())
        return failure("configured runnable artifact is absent from ProjectSnapshot", 1, NativeDevelopmentErrorCode::artifact_unavailable);
      artifactPath = selectedArtifact->path;
    }
    std::vector<std::string> argv{artifactPath.string()};
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    const auto execution = execute(std::move(argv), root, hooks);
    std::string output = built.output;
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
                                              const NativeDevelopmentHooks *hooks)
  {
    std::string diagnostic;
    NativeDevelopmentErrorCode errorCode{NativeDevelopmentErrorCode::project_error};
    auto prepared = prepare(root, mode, preset, diagnostic, errorCode, hooks);
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
        const auto built = build_prepared(*prepared, selected.target->name, 0, hooks);
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
    const auto result = execute(argv, root, hooks);
    return result.success() ? success(result.output)
                            : failure(result.errorMessage.empty() ? result.output : result.errorMessage,
                                      result.exitCode == 0 ? 1 : result.exitCode, NativeDevelopmentErrorCode::process_execution_failure);
  }
} // namespace vix::cli::project::native
