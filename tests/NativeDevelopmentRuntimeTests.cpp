#include <vix/cli/project/native/NativeDevelopmentRuntime.hpp>
#include <vix/cli/project/native/NativeBuildState.hpp>
#include <vix/cli/commands/DiagnosticMode.hpp>
#include <vix/cli/manifest/RunManifestMerge.hpp>
#include <vix/cli/project/native/ProjectLockState.hpp>
#include <vix/cli/project/NativeProjectCommandDispatch.hpp>
#include <vix/cli/project/ProjectDiscovery.hpp>
#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

#include <cassert>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace project = vix::cli::project;
using namespace vix::cli::project;
using namespace vix::cli::project::native;

namespace
{
  std::string_view activeTestPhase{"test setup"};

  void set_test_phase(std::string_view phase) { activeTestPhase = phase; }

  template <typename Test>
  void run_named_test(std::string_view name, Test &&test)
  {
    activeTestPhase = "test setup";
    try
    {
      test();
    }
    catch (const std::exception &exception)
    {
      std::cerr << "FAILED IN: " << name << "\nphase: " << activeTestPhase
                << "\nexception: " << exception.what() << '\n';
      throw;
    }
    catch (...)
    {
      std::cerr << "FAILED IN: " << name << "\nphase: " << activeTestPhase
                << "\nexception: unknown exception\n";
      throw;
    }
  }

  template <typename T>
  concept HasTestTargetKind = requires { T::test; };
  static_assert(!HasTestTargetKind<project::TargetKind>);

  template <typename T>
  concept HasResolverHandle = requires(T value) { value.resolver; };
  template <typename T>
  concept HasCatalogHandle = requires(T value) { value.resolutionCatalog; };
  template <typename T>
  concept HasMetadataSourceHandle = requires(T value) { value.metadataSource; };
  static_assert(!HasResolverHandle<NativeDevelopmentHooks>);
  static_assert(!HasCatalogHandle<NativeDevelopmentHooks>);
  static_assert(!HasMetadataSourceHandle<NativeDevelopmentHooks>);

  struct Root
  {
    fs::path path{fs::temp_directory_path() / ("vix-development-runtime-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    ~Root() { std::error_code ec; fs::remove_all(path, ec); }
  };

  void write(const fs::path &path, const std::string &bytes)
  {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    assert(out.good());
  }

  std::string read(const fs::path &path)
  {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
  }

  const std::string manifest =
    "format = 1\n[project]\nname = \"runtime-test\"\n"
    "[[targets]]\nname = \"app\"\nkind = \"executable\"\nsources = [\"main.cpp\"]\n";

  project::ProjectSnapshot snapshot(std::vector<std::pair<std::string, TargetCapabilities>> targets,
                                    const fs::path &artifact)
  {
    project::ProjectSnapshot result;
    result.project.name = "runtime-test";
    for (const auto &[name, capabilities] : targets)
      result.project.targets.push_back({TargetId{"fake:" + name}, name,
        capabilities.runnable ? project::TargetKind::executable : project::TargetKind::utility, capabilities});
    if (!targets.empty()) result.artifacts.push_back({TargetId{"fake:" + targets.front().first}, artifact});
    return result;
  }

  NativeDevelopmentHooks hooks_for(const fs::path &artifact,
                                   std::vector<std::pair<std::string, TargetCapabilities>> targets,
                                   int &configureCalls, int &buildCalls,
                                   std::vector<std::string> &executed,
                                   std::optional<std::string> *builtTarget = nullptr,
                                   vix::engine::SanitizerMode *configuredMode = nullptr,
                                   vix::engine::SanitizerMode *builtMode = nullptr,
                                   vix::engine::SanitizerMode *executedMode = nullptr)
  {
    NativeDevelopmentHooks hooks;
    hooks.configure = [&, artifactForSnapshot = artifact, targetsForSnapshot = std::move(targets), configuredMode](
                          const fs::path &, const fs::path &, const fs::path &, const NativeProject &,
                          const Lockfile &, const std::vector<MaterializedLockedPackage> &, std::string_view,
                          vix::engine::SanitizerMode diagnosticMode)
    {
      ++configureCalls;
      if (configuredMode) *configuredMode = diagnosticMode;
      NativeDevelopmentCallbackResult result;
      result.success = true;
      result.snapshot = snapshot(targetsForSnapshot, artifactForSnapshot);
      return result;
    };
    hooks.build = [&, builtTarget, builtMode](const fs::path &, const fs::path &, const project::ProjectSnapshot &input,
                      const std::optional<std::string> &requested, int, vix::engine::SanitizerMode diagnosticMode)
    {
      ++buildCalls;
      if (builtMode) *builtMode = diagnosticMode;
      if (builtTarget) *builtTarget = requested;
      NativeDevelopmentCallbackResult result;
      result.success = true;
      result.snapshot = input;
      return result;
    };
    hooks.execute = [&, executedMode](const std::vector<std::string> &argv, const fs::path &,
                                     vix::engine::SanitizerMode diagnosticMode)
    {
      executed = argv;
      if (executedMode) *executedMode = diagnosticMode;
      NativeDevelopmentCallbackResult result;
      result.success = true;
      result.exitCode = 0;
      return result;
    };
    return hooks;
  }

  void diagnostic_mode_parser_and_runtime_propagation_tests()
  {
    using Mode = vix::engine::SanitizerMode;
    const std::pair<std::string_view, Mode> modes[] = {
      {"--san", Mode::AddressUndefined}, {"--asan", Mode::Address},
      {"--ubsan", Mode::Undefined}, {"--tsan", Mode::Thread}};
    for (const auto &[option, expected] : modes)
    {
      std::optional<Mode> selected;
      std::string error;
      assert(vix::cli::commands::select_diagnostic_mode_option(option, selected, error));
      assert(error.empty() && selected == expected);
      assert(!vix::cli::commands::select_diagnostic_mode_option("--sanitize", selected, error));
      assert(!vix::cli::commands::select_diagnostic_mode_option("--sanitize=address", selected, error));
      assert(!vix::cli::commands::select_diagnostic_mode_option("--no-san", selected, error));
      assert(vix::cli::commands::select_diagnostic_mode_option(option, selected, error));
      assert(!error.empty());

      std::optional<vix::cli::commands::DiagnosticRequest> request;
      std::string requestError;
      assert(vix::cli::commands::select_diagnostic_request_option(
          option, request, requestError));
      assert(requestError.empty() && request);
      assert(request->kind == vix::cli::commands::DiagnosticRequestKind::Explicit);
      assert(request->mode == expected);
    }

    std::optional<vix::cli::commands::DiagnosticRequest> request;
    std::string requestError;
    assert(vix::cli::commands::select_diagnostic_request_option(
        "--diagnostics=off", request, requestError));
    assert(request && request->kind == vix::cli::commands::DiagnosticRequestKind::Off);
    assert(requestError.empty());
    for (const auto invalid : {"--diagnostics", "--diagnostics=on", "--diagnostics=san",
                               "--diagnostics=asan", "--diagnostics=ubsan", "--diagnostics=tsan"})
    {
      std::optional<vix::cli::commands::DiagnosticRequest> invalidRequest;
      std::string error;
      assert(vix::cli::commands::select_diagnostic_request_option(invalid, invalidRequest, error));
      assert(!error.empty() && !invalidRequest);
    }
    for (const auto conflict : {"--san", "--asan", "--diagnostics=off"})
    {
      std::string error;
      assert(vix::cli::commands::select_diagnostic_request_option(conflict, request, error));
      assert(!error.empty());
    }
    std::optional<vix::cli::commands::DiagnosticRequest> duplicateMode;
    std::string duplicateError;
    assert(vix::cli::commands::select_diagnostic_request_option(
        "--san", duplicateMode, duplicateError));
    assert(vix::cli::commands::select_diagnostic_request_option(
        "--san", duplicateMode, duplicateError));
    assert(!duplicateError.empty());

    Root buildRoot;
    fs::create_directories(buildRoot.path);
    write(buildRoot.path / "vix.toml", manifest);
    write(buildRoot.path / "main.cpp", "int main() { return 0; }\n");
    int configureCalls = 0, buildCalls = 0;
    std::vector<std::string> executed;
    Mode configured = Mode::None, builtMode = Mode::None;
    auto hooks = hooks_for(buildRoot.path / "artifact", {{"app", {true, true}}},
                           configureCalls, buildCalls, executed, nullptr,
                           &configured, &builtMode);
    const auto built = build_native_project(buildRoot.path, std::nullopt, 0, "dev",
        AcquisitionMode::online, &hooks, Mode::Address);
    assert(built.success());
    assert(configured == Mode::Address && builtMode == Mode::Address);

    Root runRoot;
    fs::create_directories(runRoot.path);
    write(runRoot.path / "vix.toml", manifest);
    write(runRoot.path / "main.cpp", "int main() { return 0; }\n");
    configureCalls = 0;
    buildCalls = 0;
    Mode runConfigured = Mode::None, runBuilt = Mode::None, runExecuted = Mode::None;
    auto runHooks = hooks_for(runRoot.path / "artifact", {{"app", {true, true}}},
                              configureCalls, buildCalls, executed, nullptr,
                              &runConfigured, &runBuilt, &runExecuted);
    const auto ran = run_native_project(runRoot.path, std::nullopt, {}, "dev",
        AcquisitionMode::online, &runHooks, Mode::Thread);
    assert(ran.success());
    assert(runConfigured == Mode::Thread && runBuilt == Mode::Thread && runExecuted == Mode::Thread);
    const auto automaticRunMode = vix::cli::commands::resolve_diagnostic_mode(
        vix::cli::commands::DiagnosticCommand::Run, false,
        vix::cli::commands::DiagnosticProjectKind::NativeVix, {});
    const auto automaticallyRan = run_native_project(
        runRoot.path, std::nullopt, {}, "dev", AcquisitionMode::online,
        &runHooks, automaticRunMode);
    assert(automaticallyRan.success());
    assert(runConfigured == Mode::AddressUndefined && runBuilt == Mode::AddressUndefined &&
           runExecuted == Mode::AddressUndefined);

    Root testRoot;
    fs::create_directories(testRoot.path);
    write(testRoot.path / "vix.toml", manifest + "\n[[tests]]\nname = \"smoke\"\ntarget = \"app\"\n");
    write(testRoot.path / "main.cpp", "int main() { return 0; }\n");
    configureCalls = 0;
    buildCalls = 0;
    Mode testConfigured = Mode::None, testBuilt = Mode::None, testExecuted = Mode::None;
    auto testHooks = hooks_for(testRoot.path / "artifact", {{"app", {true, true}}},
                               configureCalls, buildCalls, executed, nullptr,
                               &testConfigured, &testBuilt, &testExecuted);
    const auto tested = test_native_project(testRoot.path, std::string{"smoke"}, false, {}, {}, "dev",
        AcquisitionMode::online, &testHooks, Mode::Undefined);
    assert(tested.success());
    assert(testConfigured == Mode::Undefined && testBuilt == Mode::Undefined && testExecuted == Mode::Undefined);
    const auto automaticTestMode = vix::cli::commands::resolve_diagnostic_mode(
        vix::cli::commands::DiagnosticCommand::Test, false,
        vix::cli::commands::DiagnosticProjectKind::NativeVix, {});
    const auto automaticallyTested = test_native_project(
        testRoot.path, std::string{"smoke"}, false, {}, {}, "dev",
        AcquisitionMode::online, &testHooks, automaticTestMode);
    assert(automaticallyTested.success());
    assert(testConfigured == Mode::AddressUndefined && testBuilt == Mode::AddressUndefined &&
           testExecuted == Mode::AddressUndefined);
  }

  void run_manifest_diagnostic_mode_merge_tests()
  {
    using Mode = vix::cli::commands::DiagnosticMode;
    using RequestKind = vix::cli::commands::DiagnosticRequestKind;
    using Command = vix::cli::commands::DiagnosticCommand;
    using ProjectKind = vix::cli::commands::DiagnosticProjectKind;
    using RunOptions = vix::commands::RunCommand::detail::Options;

    vix::cli::manifest::Manifest manifestOptions;
    RunOptions cliOptions;

    const auto resolveRun = [](const RunOptions &options, ProjectKind projectKind, bool release = false)
    {
      return vix::cli::commands::resolve_diagnostic_mode(
          Command::Run, release, projectKind, options.diagnosticRequest,
          options.projectDiagnosticRequest);
    };

    assert(!manifestOptions.san);
    auto merged = vix::cli::manifest::merge_options(manifestOptions, cliOptions);
    assert(merged.projectDiagnosticRequest.kind == RequestKind::Unspecified);
    assert(resolveRun(merged, ProjectKind::NativeVix) == Mode::AddressUndefined);
    assert(resolveRun(merged, ProjectKind::StandaloneCpp) == Mode::AddressUndefined);
    assert(resolveRun(merged, ProjectKind::StandaloneCpp, true) == Mode::None);
    assert(resolveRun(merged, ProjectKind::ExternalCMake) == Mode::None);

    manifestOptions.san = "asan_ubsan";
    merged = vix::cli::manifest::merge_options(manifestOptions, cliOptions);
    assert(merged.projectDiagnosticRequest.kind == RequestKind::Explicit);
    assert(resolveRun(merged, ProjectKind::NativeVix) == Mode::AddressUndefined);

    manifestOptions.san = "ubsan";
    merged = vix::cli::manifest::merge_options(manifestOptions, cliOptions);
    assert(resolveRun(merged, ProjectKind::NativeVix) == Mode::Undefined);

    manifestOptions.san = "off";
    merged = vix::cli::manifest::merge_options(manifestOptions, cliOptions);
    assert(merged.projectDiagnosticRequest.kind == RequestKind::Off);
    assert(resolveRun(merged, ProjectKind::NativeVix) == Mode::None);

    cliOptions.diagnosticRequest = {RequestKind::Explicit, Mode::Undefined};
    manifestOptions.san = "ubsan";
    merged = vix::cli::manifest::merge_options(manifestOptions, cliOptions);
    assert(resolveRun(merged, ProjectKind::NativeVix) == Mode::Undefined);

    cliOptions.diagnosticRequest = {RequestKind::Off, Mode::None};
    manifestOptions.san = "asan_ubsan";
    merged = vix::cli::manifest::merge_options(manifestOptions, cliOptions);
    assert(resolveRun(merged, ProjectKind::NativeVix) == Mode::None);

    cliOptions.diagnosticRequest = {RequestKind::Explicit, Mode::Undefined};
    manifestOptions.san = "off";
    merged = vix::cli::manifest::merge_options(manifestOptions, cliOptions);
    assert(resolveRun(merged, ProjectKind::NativeVix) == Mode::Undefined);

    assert(vix::cli::commands::resolve_diagnostic_mode(
               Command::Run, true, ProjectKind::NativeVix,
               {RequestKind::Explicit, Mode::AddressUndefined}) == Mode::AddressUndefined);
    assert(vix::cli::commands::resolve_diagnostic_mode(
               Command::Run, true, ProjectKind::NativeVix,
               {RequestKind::Explicit, Mode::Undefined}) == Mode::Undefined);
    assert(vix::cli::commands::resolve_diagnostic_mode(
               Command::Build, false, ProjectKind::NativeVix, {}) == Mode::None);
    assert(vix::cli::commands::resolve_diagnostic_mode(
               Command::Test, false, ProjectKind::NativeVix, {}) == Mode::AddressUndefined);
    assert(vix::cli::commands::resolve_diagnostic_mode(
               Command::Test, false, ProjectKind::NativeVix,
               {RequestKind::Off, Mode::None}) == Mode::None);
    assert(vix::cli::commands::resolve_diagnostic_mode(
               Command::Test, false, ProjectKind::NativeVix,
               {RequestKind::Explicit, Mode::Thread}) == Mode::Thread);
    assert(vix::cli::commands::resolve_diagnostic_mode(
               Command::Dev, false, ProjectKind::NativeVix, {}) == Mode::AddressUndefined);
    assert(vix::cli::commands::resolve_diagnostic_mode(
               Command::Dev, false, ProjectKind::StandaloneCpp, {}) == Mode::AddressUndefined);
    assert(vix::cli::commands::resolve_diagnostic_mode(
               Command::Dev, true, ProjectKind::NativeVix, {}) == Mode::None);
    assert(vix::cli::commands::resolve_diagnostic_mode(
               Command::Run, true, ProjectKind::NativeVix,
               {}, {RequestKind::Explicit, Mode::Undefined}) == Mode::Undefined);
    assert(vix::cli::commands::resolve_diagnostic_mode(
               Command::Test, false, ProjectKind::ExternalCMake, {}) == Mode::None);
  }

  void build_run_state_and_artifact_tests()
  {
    Root root;
    fs::create_directories(root.path);
    write(root.path / "vix.toml", manifest);
    write(root.path / "main.cpp", "int main() { return 0; }\n");
    write(root.path / "vix.json", "deliberately invalid legacy data");
    write(root.path / "vix.app", "also deliberately invalid");
    const auto before = read(root.path / "vix.toml");
    const fs::path artifact = root.path / "misleading" / "exact-target-artifact";
    int configureCalls = 0, buildCalls = 0;
    std::vector<std::string> executed;
    std::optional<std::string> builtTarget;
    auto hooks = hooks_for(artifact, {{"app", {true, true}}}, configureCalls, buildCalls, executed, &builtTarget);

    set_test_phase("native build invocation");
    const auto built = build_native_project(root.path, std::string{"app"}, 0, "dev", AcquisitionMode::online, &hooks);
    assert(built.success() && built.code == NativeDevelopmentErrorCode::none);
    assert(configureCalls == 1 && buildCalls == 1 && builtTarget == std::optional<std::string>{"app"});
    assert(!fs::exists(root.path / "vix.lock"));
    assert(read(root.path / "vix.toml") == before);

    executed.clear();
    const std::vector<std::string> args{"one", "two words"};
    set_test_phase("native run invocation");
    const auto ran = run_native_project(root.path, std::nullopt, args, "dev", AcquisitionMode::online, &hooks);
    assert(ran.success());
    assert(executed == std::vector<std::string>({artifact.string(), "one", "two words"}));
    assert(read(root.path / "vix.toml") == before && !fs::exists(root.path / "vix.lock"));

    auto noRunnable = hooks_for(artifact, {{"utility", {true, false}}}, configureCalls, buildCalls, executed);
    executed.clear();
    set_test_phase("run with zero runnable targets");
    const auto noRun = run_native_project(root.path, std::nullopt, {}, "dev", AcquisitionMode::online, &noRunnable);
    assert(!noRun.success() && noRun.code == NativeDevelopmentErrorCode::target_not_runnable);
    assert(executed.empty());
    set_test_phase("run explicitly selected non-runnable target");
    const auto selectedNonRunnable = run_native_project(root.path, std::string{"utility"}, {}, "dev",
                                                        AcquisitionMode::online, &noRunnable);
    assert(!selectedNonRunnable.success() && selectedNonRunnable.code == NativeDevelopmentErrorCode::target_not_runnable);
    assert(executed.empty());
    assert(read(root.path / "vix.toml") == before && !fs::exists(root.path / "vix.lock"));

    const int buildsBeforeRejectedTarget = buildCalls;
    auto nonBuildable = hooks_for(artifact, {{"utility", {false, false}}}, configureCalls, buildCalls, executed);
    set_test_phase("build non-buildable target");
    const auto buildRejected = build_native_project(root.path, std::string{"utility"}, 0, "dev", AcquisitionMode::online, &nonBuildable);
    assert(!buildRejected.success() && buildRejected.code == NativeDevelopmentErrorCode::target_not_buildable);
    assert(buildCalls == buildsBeforeRejectedTarget && read(root.path / "vix.toml") == before);

    auto ambiguous = hooks_for(artifact, {{"a", {true, true}}, {"b", {true, true}}}, configureCalls, buildCalls, executed);
    executed.clear();
    set_test_phase("run with ambiguous runnable targets");
    const auto ambiguousRun = run_native_project(root.path, std::nullopt, {}, "dev", AcquisitionMode::online, &ambiguous);
    assert(!ambiguousRun.success() && ambiguousRun.code == NativeDevelopmentErrorCode::target_ambiguous);
    assert(executed.empty());
    assert(read(root.path / "vix.toml") == before && !fs::exists(root.path / "vix.lock"));

    auto failedBuild = hooks;
    failedBuild.build = [&](const fs::path &, const fs::path &, const project::ProjectSnapshot &,
                            const std::optional<std::string> &, int, vix::engine::SanitizerMode)
    {
      NativeDevelopmentCallbackResult result;
      result.diagnostic = "fake build failure";
      return result;
    };
    set_test_phase("injected build failure");
    const auto failed = build_native_project(root.path, std::nullopt, 0, "dev", AcquisitionMode::online, &failedBuild);
    assert(!failed.success() && failed.code == NativeDevelopmentErrorCode::build_failure);
    assert(read(root.path / "vix.toml") == before && !fs::exists(root.path / "vix.lock"));

    auto failedRun = hooks;
    failedRun.execute = [&](const std::vector<std::string> &, const fs::path &, vix::engine::SanitizerMode)
    {
      NativeDevelopmentCallbackResult result;
      result.diagnostic = "fake process failure";
      result.exitCode = 9;
      return result;
    };
    set_test_phase("injected process failure");
    const auto runFailure = run_native_project(root.path, std::nullopt, {}, "dev", AcquisitionMode::online, &failedRun);
    assert(!runFailure.success() && runFailure.code == NativeDevelopmentErrorCode::process_execution_failure);
    assert(read(root.path / "vix.toml") == before && !fs::exists(root.path / "vix.lock"));
  }

  void package_preparation_failure_stops_operations()
  {
    Root root;
    fs::create_directories(root.path);
    const std::string packageManifest =
      "format = 1\n[project]\nname = \"runtime-test\"\n"
      "[[packages]]\nalias = \"lib\"\nsource = \"registry\"\nregistry = \"vix\"\n"
      "publisher = \"demo\"\nname = \"lib\"\nrequirement = { exact = \"1.0.0\" }\n"
      "[[targets]]\nname = \"app\"\nkind = \"executable\"\nsources = [\"main.cpp\"]\n"
      "[[tests]]\nname = \"smoke\"\ntarget = \"app\"\n";
    write(root.path / "vix.toml", packageManifest);
    write(root.path / "main.cpp", "int main() { return 0; }\n");
    const auto parsed = parse_native_manifest_text(packageManifest);
    assert(parsed.valid());
    Lockfile lock;
    const auto &requirement = std::get<RegistryRequirement>(parsed.value->packages.front().source);
    const Version version{1, 0, 0, std::nullopt};
    RegistryLockNode node;
    node.identity = requirement.identity;
    node.version = version;
    node.id = registry_node_id({node.identity, node.version});
    node.contentDigest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    lock.roots.push_back({"lib", NormalizedDeclaredRegistry{requirement.identity, requirement.version}, node.id});
    lock.nodes.emplace_back(node);
    lock.rootRequirementsDigest = project_requirements_digest(*parsed.value);
    const auto serialized = serialize_lockfile(lock);
    assert(serialized.valid() && serialized.text);
    write(root.path / "vix.lock", *serialized.text);
    const auto beforeManifest = read(root.path / "vix.toml");
    const auto beforeLock = read(root.path / "vix.lock");
    int prepareCalls = 0, configureCalls = 0, buildCalls = 0;
    std::vector<std::string> executed;
    auto hooks = hooks_for(root.path / "artifact", {{"app", {true, true}}}, configureCalls, buildCalls, executed);
    hooks.preparePackages = [&](const fs::path &, const NativeProject &, const Lockfile &, AcquisitionMode, std::string &message)
      -> std::optional<std::vector<MaterializedLockedPackage>>
    {
      ++prepareCalls;
      message = "synthetic package preparation failure";
      return std::nullopt;
    };
    set_test_phase("package project build after preparation failure");
    const auto built = build_native_project(root.path, std::nullopt, 0, "dev", AcquisitionMode::offline, &hooks);
    set_test_phase("package project run after preparation failure");
    const auto ran = run_native_project(root.path, std::nullopt, {}, "dev", AcquisitionMode::offline, &hooks);
    set_test_phase("package project test after preparation failure");
    const auto tested = test_native_project(root.path, std::nullopt, false, {}, {}, "dev", AcquisitionMode::offline, &hooks);
    assert(!built.success() && built.code == NativeDevelopmentErrorCode::package_preparation_failure);
    assert(!ran.success() && ran.code == NativeDevelopmentErrorCode::package_preparation_failure);
    assert(!tested.success() && tested.code == NativeDevelopmentErrorCode::package_preparation_failure);
    assert(prepareCalls == 3 && buildCalls == 0 && configureCalls == 0 && executed.empty());
    assert(read(root.path / "vix.toml") == beforeManifest && read(root.path / "vix.lock") == beforeLock);
  }

  void lock_failures_stop_before_preparation_or_build()
  {
    const std::string packageManifest =
      "format = 1\n[project]\nname = \"runtime-test\"\n"
      "[[packages]]\nalias = \"lib\"\nsource = \"registry\"\nregistry = \"vix\"\n"
      "publisher = \"demo\"\nname = \"lib\"\nrequirement = { exact = \"1.0.0\" }\n"
      "[[targets]]\nname = \"app\"\nkind = \"executable\"\nsources = [\"main.cpp\"]\n";
    const auto parsed = parse_native_manifest_text(packageManifest);
    assert(parsed.valid());
    int prepareCalls = 0, configureCalls = 0, buildCalls = 0;
    std::vector<std::string> executed;
    auto hooks = hooks_for("/tmp/not-used", {{"app", {true, true}}}, configureCalls, buildCalls, executed);
    hooks.preparePackages = [&](const fs::path &, const NativeProject &, const Lockfile &, AcquisitionMode, std::string &)
      -> std::optional<std::vector<MaterializedLockedPackage>>
    { ++prepareCalls; return std::vector<MaterializedLockedPackage>{}; };

    auto assertRejected = [&](std::string_view lockBytes, NativeDevelopmentErrorCode expected, bool createLock)
    {
      Root root;
      fs::create_directories(root.path);
      write(root.path / "vix.toml", packageManifest);
      write(root.path / "main.cpp", "int main() { return 0; }\n");
      if (createLock) write(root.path / "vix.lock", std::string{lockBytes});
      const auto beforeManifest = read(root.path / "vix.toml");
      const bool lockExisted = fs::exists(root.path / "vix.lock");
      const std::string beforeLock = lockExisted ? read(root.path / "vix.lock") : std::string{};
      set_test_phase("build with rejected lock state");
      const auto result = build_native_project(root.path, std::nullopt, 0, "dev", AcquisitionMode::online, &hooks);
      assert(!result.success() && result.code == expected);
      assert(prepareCalls == 0 && configureCalls == 0 && buildCalls == 0);
      assert(read(root.path / "vix.toml") == beforeManifest);
      assert(fs::exists(root.path / "vix.lock") == lockExisted);
      if (lockExisted) assert(read(root.path / "vix.lock") == beforeLock);
    };

    assertRejected(std::string_view{}, NativeDevelopmentErrorCode::missing_lock, false);
    assertRejected("not a lock\n", NativeDevelopmentErrorCode::invalid_lock, true);

    NativeProject other = *parsed.value;
    other.packages.front().alias = "other";
    const auto &requirement = std::get<RegistryRequirement>(other.packages.front().source);
    const Version version{1, 0, 0, std::nullopt};
    RegistryLockNode node;
    node.identity = requirement.identity;
    node.version = version;
    node.id = registry_node_id({node.identity, node.version});
    node.contentDigest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    Lockfile stale;
    stale.roots.push_back({"other", NormalizedDeclaredRegistry{requirement.identity, requirement.version}, node.id});
    stale.nodes.emplace_back(node);
    stale.rootRequirementsDigest = project_requirements_digest(other);
    const auto staleText = serialize_lockfile(stale);
    assert(staleText.valid() && staleText.text);
    assertRejected(*staleText.text, NativeDevelopmentErrorCode::stale_lock, true);
  }

  void valid_locked_packages_flow_through_build_graph()
  {
    Root root;
    const auto packageRoot = root.path / "materialized" / "lib";
    fs::create_directories(root.path);
    write(root.path / "vix.toml",
      "format = 1\n[project]\nname = \"app\"\n"
      "[[packages]]\nalias = \"lib\"\nsource = \"registry\"\nregistry = \"vix\"\n"
      "publisher = \"demo\"\nname = \"lib\"\nrequirement = { exact = \"1.0.0\" }\n"
      "[[targets]]\nname = \"app\"\nkind = \"executable\"\nsources = [\"main.cpp\"]\n"
      "[[targets.package-uses]]\npackage = \"lib\"\ntarget = \"core\"\nvisibility = \"private\"\n");
    write(root.path / "main.cpp", "int main() { return 0; }\n");
    write(packageRoot / "vix.package.toml",
      "format = 1\n[package]\nsource = \"registry\"\nregistry = \"vix\"\npublisher = \"demo\"\n"
      "name = \"lib\"\nversion = \"1.0.0\"\n[integration]\ntype = \"native-vix\"\n"
      "include-roots = [\"include\"]\n[[exports]]\nname = \"core\"\nproject-target = \"lib_core\"\n");
    write(packageRoot / "vix.toml",
      "format = 1\n[project]\nname = \"lib\"\n[[targets]]\nname = \"lib_core\"\n"
      "kind = \"static\"\nsources = [\"lib.cpp\"]\n");
    write(packageRoot / "lib.cpp", "int lib_answer() { return 42; }\n");
    write(packageRoot / "include" / "lib.hpp", "int lib_answer();\n");
    const auto parsed = parse_native_manifest_file(root.path / "vix.toml");
    assert(parsed.valid());
    const auto &requirement = std::get<RegistryRequirement>(parsed.value->packages.front().source);
    const Version version{1, 0, 0, std::nullopt};
    RegistryLockNode node;
    node.identity = requirement.identity;
    node.version = version;
    node.id = registry_node_id({node.identity, node.version});
    node.contentDigest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    Lockfile lock;
    lock.roots.push_back({"lib", NormalizedDeclaredRegistry{requirement.identity, requirement.version}, node.id});
    lock.nodes.emplace_back(node);
    lock.rootRequirementsDigest = project_requirements_digest(*parsed.value);
    const auto lockText = serialize_lockfile(lock);
    assert(lockText.valid() && lockText.text);
    write(root.path / "vix.lock", *lockText.text);
    const auto beforeManifest = read(root.path / "vix.toml");
    const auto beforeLock = read(root.path / "vix.lock");
    int prepareCalls = 0, configureCalls = 0, buildCalls = 0;
    std::vector<std::string> executed;
    auto hooks = hooks_for(root.path / "artifact", {{"app", {true, true}}}, configureCalls, buildCalls, executed);
    hooks.preparePackages = [&](const fs::path &, const NativeProject &, const Lockfile &received, AcquisitionMode mode,
                                std::string &)
      -> std::optional<std::vector<MaterializedLockedPackage>>
    {
      ++prepareCalls;
      assert(mode == AcquisitionMode::offline && received.nodes.size() == 1);
      const auto &locked = std::get<RegistryLockNode>(received.nodes.front());
      assert(locked.id == node.id);
      return std::vector<MaterializedLockedPackage>{{node.id, *PackageContentId::parse(node.contentDigest), packageRoot}};
    };
    hooks.configure = [&](const fs::path &projectRoot, const fs::path &sourceRoot, const fs::path &buildRoot,
                          const NativeProject &projectValue, const Lockfile &lockValue,
                          const std::vector<MaterializedLockedPackage> &packages, std::string_view preset,
                          vix::engine::SanitizerMode diagnosticMode)
    {
      ++configureCalls;
      (void)projectRoot; (void)buildRoot; (void)projectValue; (void)lockValue; (void)packages; (void)preset;
      const auto generated = read(sourceRoot / "CMakeLists.txt");
      assert(generated.find("set(CMAKE_EXPORT_COMPILE_COMMANDS ON)") != std::string::npos);
      assert(generated.find("target_link_libraries(app PRIVATE lib_core)") != std::string::npos);
      assert(diagnosticMode == vix::engine::SanitizerMode::None);
      NativeDevelopmentCallbackResult configured;
      configured.success = true;
      configured.snapshot = snapshot({{"app", {true, true}}}, root.path / "artifact");
      return configured;
    };
    set_test_phase("locked package build through PackageBuildGraph");
    const auto result = build_native_project(root.path, std::nullopt, 0, "dev", AcquisitionMode::offline, &hooks);
    assert(result.success() && prepareCalls == 1 && configureCalls == 1 && buildCalls == 1);
    assert(read(root.path / "vix.toml") == beforeManifest && read(root.path / "vix.lock") == beforeLock);
  }

  void registered_test_and_invalid_registration_tests()
  {
    Root root;
    fs::create_directories(root.path);
    const std::string withTest = manifest + "\n[[tests]]\nname = \"smoke\"\ntarget = \"app\"\n";
    write(root.path / "vix.toml", withTest);
    write(root.path / "main.cpp", "int main() { return 0; }\n");
    const auto before = read(root.path / "vix.toml");
    int configureCalls = 0, buildCalls = 0;
    std::vector<std::string> executed;
    auto hooks = hooks_for(root.path / "unrelated-name", {{"app", {true, true}}}, configureCalls, buildCalls, executed);
    const auto configure = hooks.configure;
    hooks.configure = [&](const fs::path &projectRoot, const fs::path &sourceRoot, const fs::path &buildRoot,
                          const NativeProject &projectValue, const Lockfile &lockValue,
                          const std::vector<MaterializedLockedPackage> &packages, std::string_view preset,
                          vix::engine::SanitizerMode diagnosticMode)
    {
      const auto graph = read(sourceRoot / "CMakeLists.txt");
      assert(graph.find("add_test(NAME [[smoke]] COMMAND $<TARGET_FILE:app>)") != std::string::npos);
      return configure(projectRoot, sourceRoot, buildRoot, projectValue, lockValue, packages, preset, diagnosticMode);
    };
    set_test_phase("registered test execution");
    const auto tested = test_native_project(root.path, std::string{"smoke"}, false, {}, {}, "dev", AcquisitionMode::online, &hooks);
    assert(tested.success() && configureCalls == 1 && buildCalls == 1);
    assert(!executed.empty() && executed.front() == "ctest");
    assert(std::find(executed.begin(), executed.end(), "^smoke$") != executed.end());
    assert(read(root.path / "vix.toml") == before && !fs::exists(root.path / "vix.lock"));

    set_test_phase("unknown registered test selection");
    const auto unknownTest = test_native_project(root.path, std::string{"missing"}, false, {}, {}, "dev",
                                                 AcquisitionMode::online, &hooks);
    assert(!unknownTest.success() && unknownTest.code == NativeDevelopmentErrorCode::test_not_found);
    assert(read(root.path / "vix.toml") == before && !fs::exists(root.path / "vix.lock"));

    Root invalid;
    fs::create_directories(invalid.path);
    const auto invalidManifest = manifest + "\n[[tests]]\nname = \"broken\"\ntarget = \"absent\"\n";
    write(invalid.path / "vix.toml", invalidManifest);
    write(invalid.path / "main.cpp", "int main() { return 0; }\n");
    auto invalidHooks = hooks_for(root.path / "artifact", {{"app", {true, true}}}, configureCalls, buildCalls, executed);
    const auto invalidResult = test_native_project(invalid.path, std::nullopt, false, {}, {}, "dev", AcquisitionMode::online, &invalidHooks);
    assert(!invalidResult.success() && invalidResult.code == NativeDevelopmentErrorCode::invalid_test_registration);
    assert(read(invalid.path / "vix.toml") == invalidManifest && !fs::exists(invalid.path / "vix.lock"));

    Root nonRunnable;
    fs::create_directories(nonRunnable.path);
    write(nonRunnable.path / "vix.toml", withTest);
    write(nonRunnable.path / "main.cpp", "int main() { return 0; }\n");
    std::vector<std::string> noExecution;
    auto nonRunnableHooks = hooks_for(nonRunnable.path / "not-used", {{"app", {true, false}}}, configureCalls, buildCalls, noExecution);
    const auto nonRunnableResult = test_native_project(nonRunnable.path, std::string{"smoke"}, false, {}, {}, "dev",
                                                        AcquisitionMode::online, &nonRunnableHooks);
    assert(!nonRunnableResult.success() && nonRunnableResult.code == NativeDevelopmentErrorCode::target_not_runnable);
    assert(noExecution.empty() && !fs::exists(nonRunnable.path / "vix.lock"));

    auto failedTestHooks = hooks_for(nonRunnable.path / "artifact", {{"app", {true, true}}}, configureCalls, buildCalls, noExecution);
    failedTestHooks.execute = [&](const std::vector<std::string> &, const fs::path &,
                                  vix::engine::SanitizerMode diagnosticMode)
    {
      assert(diagnosticMode == vix::engine::SanitizerMode::Undefined);
      NativeDevelopmentCallbackResult result;
      result.exitCode = 4;
      result.diagnostic = "synthetic ctest failure";
      result.output = "UndefinedBehaviorSanitizer: runtime error at source.cpp:7:3\n";
      return result;
    };
    const auto failedTest = test_native_project(nonRunnable.path, std::string{"smoke"}, false, {}, {}, "dev",
                                                AcquisitionMode::online, &failedTestHooks,
                                                vix::engine::SanitizerMode::Undefined);
    assert(!failedTest.success() && failedTest.code == NativeDevelopmentErrorCode::process_execution_failure);
    assert(failedTest.output.find("source.cpp:7:3") != std::string::npos);
    assert(read(nonRunnable.path / "vix.toml") == withTest && !fs::exists(nonRunnable.path / "vix.lock"));
  }

  void project_discovery_command_routing_tests()
  {
    Root native;
    fs::create_directories(native.path);
    write(native.path / "vix.toml", manifest);
    write(native.path / "vix.json", "conflicting historical project data");
    write(native.path / "vix.app", "conflicting historical app data");
    const auto nativeProject = project::discover_project(native.path);
    assert(nativeProject.success());
    assert(nativeProject.location->system == project::ProjectSystem::native_vix);

    int nativeBuildCalls = 0;
    int nativeRunCalls = 0;
    int nativeTestCalls = 0;
    int historicalCalls = 0;
    std::optional<std::string> requestedTarget;
    std::vector<std::string> receivedArguments;

    const std::optional<std::string> buildTarget{"app"};
    set_test_phase("BuildCommand native dispatch assertion");
    const auto buildRoute = project::dispatch_native_build(nativeProject, buildTarget, 3, "dev",
      [&](const project::ProjectLocation &location, const std::optional<std::string> &target,
          int jobs, std::string_view preset)
      {
        ++nativeBuildCalls;
        assert(location.root == nativeProject.location->root);
        assert(target == buildTarget && jobs == 3 && preset == "dev");
        return NativeDevelopmentResult{1, "synthetic structured build failure"};
      });
    if (!buildRoute) ++historicalCalls;
    assert(buildRoute && buildRoute->code == NativeDevelopmentErrorCode::project_error);

    const std::vector<std::string> commandArguments{"one", "two words"};
    set_test_phase("RunCommand native dispatch assertion");
    const auto runRoute = project::dispatch_native_run(nativeProject, buildTarget, commandArguments, "dev",
      [&](const project::ProjectLocation &location, const std::optional<std::string> &target,
          const std::vector<std::string> &arguments, std::string_view preset)
      {
        ++nativeRunCalls;
        assert(location.root == nativeProject.location->root);
        assert(preset == "dev");
        requestedTarget = target;
        receivedArguments = arguments;
        return NativeDevelopmentResult{1, "synthetic structured run failure"};
      });
    if (!runRoute) ++historicalCalls;
    assert(runRoute && runRoute->code == NativeDevelopmentErrorCode::project_error);
    assert(requestedTarget == std::optional<std::string>{"app"});
    assert(receivedArguments == std::vector<std::string>({"one", "two words"}));

    const std::optional<std::string> testName{"smoke"};
    const std::vector<std::string> ctestArguments{"--output-on-failure"};
    set_test_phase("TestsCommand native dispatch assertion");
    const auto testRoute = project::dispatch_native_tests(
      nativeProject, testName, false, ctestArguments, "smoke.*", "dev",
      [&](const project::ProjectLocation &location, const std::optional<std::string> &receivedTest,
          bool listOnly, const std::vector<std::string> &receivedCtestArguments,
          std::string_view pattern, std::string_view preset)
      {
        ++nativeTestCalls;
        assert(location.root == nativeProject.location->root);
        assert(receivedTest == testName && !listOnly);
        assert(receivedCtestArguments == ctestArguments && pattern == "smoke.*" && preset == "dev");
        return NativeDevelopmentResult{1, "synthetic structured test failure"};
      });
    if (!testRoute) ++historicalCalls;
    assert(testRoute && testRoute->code == NativeDevelopmentErrorCode::project_error);
    assert(nativeBuildCalls == 1 && nativeRunCalls == 1 && nativeTestCalls == 1);
    assert(historicalCalls == 0);

    Root cmake;
    fs::create_directories(cmake.path);
    write(cmake.path / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(external)\n");
    const auto external = project::discover_project(cmake.path);
    assert(external.success() && external.location->system == project::ProjectSystem::cmake);
    const auto externalRoute = project::dispatch_native_project(external,
      [&](const project::ProjectLocation &) { ++nativeBuildCalls; return 0; });
    assert(!externalRoute && nativeBuildCalls == 1);

    Root ambiguous;
    fs::create_directories(ambiguous.path);
    write(ambiguous.path / "vix.toml", manifest);
    write(ambiguous.path / "CMakeLists.txt", "project(ambiguous)\n");
    const auto conflict = project::discover_project(ambiguous.path);
    assert(!conflict.success() && conflict.error == project::ProjectError::ambiguous_project_authority);
    const auto ambiguousRoute = project::dispatch_native_project(conflict,
      [&](const project::ProjectLocation &) { ++nativeBuildCalls; return 0; });
    assert(!ambiguousRoute && nativeBuildCalls == 1);
  }

  void native_build_state_context_tests()
  {
    Root root;
    fs::create_directories(root.path);
    const NativeToolchainIdentity gcc13{
      "/usr/bin/x86_64-linux-gnu-g++-13", "gcc", "13.3.0", "x86_64-linux-gnu", 123456, 987654, true};
    const NativeToolchainIdentity sameGcc13{
      "/usr/bin/x86_64-linux-gnu-g++-13", "gcc", "13.3.0", "x86_64-linux-gnu", 123456, 987654, true};
    const NativeToolchainIdentity gcc14{
      "/usr/bin/x86_64-linux-gnu-g++-13", "gcc", "14.1.0", "x86_64-linux-gnu", 123456, 987654, true};
    const NativeToolchainIdentity gcc13OtherTarget{
      "/usr/bin/x86_64-linux-gnu-g++-13", "gcc", "13.3.0", "aarch64-linux-gnu", 123456, 987654, true};
    const auto normal = make_native_build_state_context(
      root.path, "development", "x86_64-linux-gnu", gcc13, "Ninja",
      vix::engine::SanitizerMode::None);
    const auto same = make_native_build_state_context(
      root.path, "development", "x86_64-linux-gnu", sameGcc13, "Ninja",
      vix::engine::SanitizerMode::None);
    const auto sanitized = make_native_build_state_context(
      root.path, "development", "x86_64-linux-gnu", gcc13, "Ninja",
      vix::engine::SanitizerMode::AddressUndefined);
    const auto release = make_native_build_state_context(
      root.path, "release", "x86_64-linux-gnu", gcc13, "Ninja",
      vix::engine::SanitizerMode::None);
    const auto otherPlatform = make_native_build_state_context(
      root.path, "development", "aarch64-linux-gnu", gcc13, "Ninja",
      vix::engine::SanitizerMode::None);
    const auto changedVersion = make_native_build_state_context(
      root.path, "development", "x86_64-linux-gnu", gcc14, "Ninja",
      vix::engine::SanitizerMode::None);
    const auto changedTarget = make_native_build_state_context(
      root.path, "development", "x86_64-linux-gnu", gcc13OtherTarget, "Ninja",
      vix::engine::SanitizerMode::None);
    const NativeToolchainIdentity clang{
      "/usr/bin/clang++", "clang", "18.1.0", "x86_64-unknown-linux-gnu", 111000, 987654, true};
    const auto otherCompiler = make_native_build_state_context(
      root.path, "development", "x86_64-linux-gnu", clang, "Ninja",
      vix::engine::SanitizerMode::None);
    const auto otherGenerator = make_native_build_state_context(
      root.path, "development", "x86_64-linux-gnu", gcc13, "Unix Makefiles",
      vix::engine::SanitizerMode::None);
    assert(normal.id == same.id);
    assert(normal.id != sanitized.id);
    assert(normal.id != release.id);
    assert(normal.id != otherPlatform.id);
    assert(normal.id != changedVersion.id);
    assert(normal.id != changedTarget.id);
    assert(normal.id != otherCompiler.id);
    assert(normal.id != otherGenerator.id);
    assert(normal.root == normal.projectRoot / ".vix" / "build" / normal.id);
    assert(normal.generatedSourceDir == normal.root / "source");
    assert(normal.backendDir == normal.root / "backend");
    assert(normal.metadataPath == normal.root / "context.toml");
    assert(normal.configuredInputSignaturePath == normal.root / "configured-input.sha256");
    assert(normal.snapshotCachePath == normal.root / "target-snapshot.json");
    assert(native_build_state_root(root.path) == root.path / ".vix" / "build");
    assert(native_toolchain_identity_cache_path(root.path) ==
           root.path / ".vix" / "build" / "toolchain-identity.json");
    const auto metadata = native_build_context_metadata(normal, "test-digest");
    assert(metadata.find("schema = \"native-build-state-v2\"") != std::string::npos);
    assert(metadata.find("compiler_family = \"gcc\"") != std::string::npos);
    assert(metadata.find("compiler_version = \"13.3.0\"") != std::string::npos);
    assert(metadata.find("compiler_target = \"x86_64-linux-gnu\"") != std::string::npos);
    assert(metadata.find("diagnostic_mode = \"none\"") != std::string::npos);

    fs::create_directories(normal.root);
    fs::create_directories(sanitized.root);
    fs::create_directories(root.path / ".vix" / "packages");
    fs::create_directories(root.path / "build");
    write(normal.root / "keep", "other-context");
    write(sanitized.root / "remove", "selected-context");
    write(root.path / ".vix" / "packages" / "keep", "package-state");
    write(root.path / "build" / "keep", "user-build-tree");

    std::string error;
    assert(reset_native_build_state_context(sanitized, error));
    assert(!fs::exists(sanitized.root / "remove"));
    assert(fs::is_directory(sanitized.generatedSourceDir));
    assert(fs::is_directory(sanitized.backendDir));
    assert(fs::exists(normal.root / "keep"));
    assert(fs::exists(root.path / ".vix" / "packages" / "keep"));
    assert(fs::exists(root.path / "build" / "keep"));

    Root runtimeRoot;
    fs::create_directories(runtimeRoot.path);
    write(runtimeRoot.path / "vix.toml", manifest);
    write(runtimeRoot.path / "main.cpp", "int main() { return 0; }\n");
    fs::path configuredSource;
    fs::path configuredBackend;
    int configureCalls = 0;
    int buildCalls = 0;
    std::vector<std::string> executed;
    auto hooks = hooks_for(runtimeRoot.path / "artifact", {{"app", {true, true}}},
                           configureCalls, buildCalls, executed);
    hooks.configure = [&](const fs::path &, const fs::path &sourceRoot, const fs::path &backendRoot,
                          const NativeProject &, const Lockfile &,
                          const std::vector<MaterializedLockedPackage> &, std::string_view,
                          vix::engine::SanitizerMode)
    {
      configuredSource = sourceRoot;
      configuredBackend = backendRoot;
      NativeDevelopmentCallbackResult configured;
      configured.success = true;
      configured.snapshot = snapshot({{"app", {true, true}}}, runtimeRoot.path / "artifact");
      return configured;
    };
    const auto built = build_native_project(runtimeRoot.path, std::nullopt, 0, "dev",
      AcquisitionMode::online, &hooks);
    assert(built.success());
    assert(configuredSource.parent_path() == configuredBackend.parent_path());
    assert(configuredSource.filename() == "source" && configuredBackend.filename() == "backend");
    assert(configuredSource.parent_path().parent_path() == runtimeRoot.path / ".vix" / "build");
    assert(fs::is_regular_file(configuredSource / "CMakeLists.txt"));
    assert(fs::is_regular_file(configuredSource.parent_path() / "context.toml"));
    assert(fs::is_regular_file(configuredSource.parent_path() / "configured-input.sha256"));
    const auto firstContextRoot = configuredSource.parent_path();
    const auto targetBuild = build_native_project(runtimeRoot.path, std::string{"app"}, 0, "dev",
      AcquisitionMode::online, &hooks);
    assert(targetBuild.success());
    assert(configuredSource.parent_path() == firstContextRoot);
  }
}

int main()
{
  run_named_test("diagnostic_mode_parser_and_runtime_propagation_tests", diagnostic_mode_parser_and_runtime_propagation_tests);
  run_named_test("run_manifest_diagnostic_mode_merge_tests", run_manifest_diagnostic_mode_merge_tests);
  run_named_test("build_run_state_and_artifact_tests", build_run_state_and_artifact_tests);
  run_named_test("package_preparation_failure_stops_operations", package_preparation_failure_stops_operations);
  run_named_test("lock_failures_stop_before_preparation_or_build", lock_failures_stop_before_preparation_or_build);
  run_named_test("valid_locked_packages_flow_through_build_graph", valid_locked_packages_flow_through_build_graph);
  run_named_test("registered_test_and_invalid_registration_tests", registered_test_and_invalid_registration_tests);
  run_named_test("project_discovery_command_routing_tests", project_discovery_command_routing_tests);
  run_named_test("native_build_state_context_tests", native_build_state_context_tests);
}
