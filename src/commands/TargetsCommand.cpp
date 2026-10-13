/** @file TargetsCommand.cpp */
#include <vix/cli/commands/TargetsCommand.hpp>

#include <vix/cli/Style.hpp>
#include <vix/cli/build/BuildContext.hpp>
#include <vix/cli/project/ProjectDiscovery.hpp>
#include <vix/cli/project/TargetSelection.hpp>
#include <vix/cli/project/cmake/CMakeProjectAdapter.hpp>
#include <vix/engine/ExecutionPlanning.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace vix::commands::TargetsCommand
{
  namespace
  {
    struct Options
    {
      std::filesystem::path directory;
      std::optional<std::filesystem::path> buildRoot;
      std::string preset{"dev-ninja"};
      std::optional<std::string> configuration;
    };

    std::string kind_label(vix::cli::project::TargetKind kind)
    {
      using vix::cli::project::TargetKind;
      switch (kind)
      {
      case TargetKind::executable: return "executable";
      case TargetKind::static_library: return "static library";
      case TargetKind::shared_library: return "shared library";
      case TargetKind::module_library: return "module library";
      case TargetKind::interface_library: return "interface library";
      case TargetKind::object_library: return "object library";
      case TargetKind::utility: return "utility";
      case TargetKind::unknown: return "unknown";
      }
      return "unknown";
    }

    bool take_value(
        const std::vector<std::string> &args,
        std::size_t &index,
        std::string_view option,
        std::string &value)
    {
      if (args[index] == option)
      {
        if (++index == args.size())
          return false;
        value = args[index];
        return true;
      }
      const std::string prefix = std::string(option) + "=";
      if (args[index].rfind(prefix, 0) == 0)
      {
        value = args[index].substr(prefix.size());
        return !value.empty();
      }
      return false;
    }

    std::optional<Options> parse(const std::vector<std::string> &args)
    {
      Options options;
      std::error_code error;
      options.directory = std::filesystem::current_path(error);
      if (error)
      {
        vix::cli::style::error("Unable to determine the current directory.");
        return std::nullopt;
      }

      for (std::size_t index = 0; index < args.size(); ++index)
      {
        std::string value;
        if (take_value(args, index, "--dir", value))
        {
          options.directory = value;
          continue;
        }
        if (take_value(args, index, "--preset", value))
        {
          options.preset = std::move(value);
          continue;
        }
        if (take_value(args, index, "--build-root", value))
        {
          options.buildRoot = std::filesystem::path(std::move(value));
          continue;
        }
        if (take_value(args, index, "--configuration", value))
        {
          options.configuration = std::move(value);
          continue;
        }
        if (index == args.size())
        {
          vix::cli::style::error("Missing value for a targets option.");
          return std::nullopt;
        }
        if (args[index] == "--help" || args[index] == "-h")
          return std::nullopt;

        vix::cli::style::error("Unknown or incomplete option: " + args[index]);
        return std::nullopt;
      }
      return options;
    }

    int report(vix::cli::project::ProjectError error)
    {
      using vix::cli::project::ProjectError;
      switch (error)
      {
      case ProjectError::project_not_found:
        vix::cli::style::error("No Vix-recognized project was found for this path.");
        break;
      case ProjectError::unsupported_project_system:
        vix::cli::style::error("Target listing is unavailable for this project system.");
        break;
      case ProjectError::configured_metadata_unavailable:
        vix::cli::style::error("Configured CMake metadata is unavailable for this build directory.");
        vix::cli::style::hint("Configure the project with Vix before listing targets.");
        break;
      case ProjectError::configuration_required:
        vix::cli::style::error("This configured CMake project requires --configuration <name>.");
        break;
      case ProjectError::configuration_not_found:
        vix::cli::style::error("The requested CMake configuration was not found.");
        break;
      default:
        vix::cli::style::error("Unable to load project targets (" +
            std::string(vix::cli::project::to_string(error)) + ").");
        break;
      }
      return 1;
    }
  } // namespace

  int run(const std::vector<std::string> &args)
  {
    if (!args.empty() && (args.front() == "--help" || args.front() == "-h"))
      return help();

    const auto options = parse(args);
    if (!options)
      return 1;

    const auto location = vix::cli::project::discover_project(options->directory);
    if (!location.success())
      return report(*location.error);
    if (location.location->system != vix::cli::project::ProjectSystem::cmake)
    {
      return report(vix::cli::project::ProjectError::unsupported_project_system);
    }

    const auto preset = vix::cli::build::resolve_builtin_preset(options->preset);
    if (!preset)
    {
      vix::cli::style::error("Unknown Vix build preset: " + options->preset);
      return 1;
    }

    std::filesystem::path buildRoot;
    if (options->buildRoot)
    {
      buildRoot = *options->buildRoot;
    }
    else
    {
      vix::engine::ExecutionPlanLayoutOptions layoutOptions;
      layoutOptions.userProjectDir = location.location->root;
      layoutOptions.cmakeSourceDir = location.location->root;
      layoutOptions.preset = *preset;
      const auto layout = vix::engine::make_execution_plan_layout(layoutOptions);
      if (!layout.success())
      {
        vix::cli::style::error("Unable to resolve the configured build directory.");
        return 1;
      }
      buildRoot = layout.plan.buildDir;
    }

    std::optional<vix::cli::project::ProjectSnapshot> loadedSnapshot;
    std::optional<vix::cli::project::ProjectError> snapshotError;
    const auto snapshot = vix::cli::project::cmake::load_cmake_target_snapshot(
        *location.location, {buildRoot, options->configuration});
    loadedSnapshot = snapshot.snapshot;
    snapshotError = snapshot.error;
    if (!loadedSnapshot)
      return report(*snapshotError);

    const auto targets = vix::cli::project::list_targets(*loadedSnapshot);
    std::cout << "Targets\n\n";
    for (const auto &target : targets)
    {
      std::cout << "  " << target.name << "\t" << kind_label(target.kind);
      if (target.capabilities.runnable)
        std::cout << "\trunnable";
      std::cout << "\n";
    }
    return 0;
  }

  int help()
  {
    std::cout
        << "vix targets\n"
        << "List targets from configured CMake metadata.\n\n"
        << "Usage\n"
        << "  vix targets [--dir <path>] [--preset <name>] [--build-root <path>] [--configuration <name>]\n\n"
        << "The project must already have configured CMake File API metadata.\n";
    return 0;
  }
} // namespace vix::commands::TargetsCommand
