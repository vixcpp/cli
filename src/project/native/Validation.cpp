#include <vix/cli/project/native/Validation.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vix::cli::project::native
{
  namespace
  {
    void add(ValidationResult &result, ValidationCode code, std::string subject, std::string message)
    {
      result.issues.push_back(ValidationIssue{code, std::move(subject), std::move(message), std::nullopt});
    }

    bool alnum(char c) noexcept
    {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }

    bool lower_alnum(char c) noexcept
    {
      return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    }

    bool valid_dashed_identifier(std::string_view value) noexcept
    {
      if (value.empty() || !lower_alnum(value.front()) || !lower_alnum(value.back())) return false;
      bool previousHyphen = false;
      for (char c : value)
      {
        if (c == '-')
        {
          if (previousHyphen) return false;
          previousHyphen = true;
        }
        else
        {
          if (!lower_alnum(c)) return false;
          previousHyphen = false;
        }
      }
      return true;
    }

    bool valid_registry(std::string_view value) noexcept
    {
      if (value.empty()) return false;
      std::size_t begin = 0;
      while (begin < value.size())
      {
        const auto end = value.find('.', begin);
        const auto segment = value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (!valid_dashed_identifier(segment)) return false;
        if (end == std::string_view::npos) return true;
        begin = end + 1;
        if (begin == value.size()) return false;
      }
      return false;
    }

    bool valid_version_string_part(std::string_view s, bool numeric)
    {
      if (s.empty()) return false;
      if (numeric && s.size() > 1 && s.front() == '0') return false;
      for (char c : s)
        if (!alnum(c) && c != '-') return false;
      return true;
    }

    bool valid_prerelease(std::string_view text) noexcept
    {
      if (text.empty()) return false;
      std::size_t begin = 0;
      while (begin < text.size())
      {
        const auto end = text.find('.', begin);
        auto part = text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin);
        bool numeric = true;
        for (char c : part) if (c < '0' || c > '9') numeric = false;
        if (!valid_version_string_part(part, numeric)) return false;
        if (end == std::string_view::npos) return true;
        begin = end + 1;
        if (begin == text.size()) return false;
      }
      return false;
    }

    bool valid_version(const Version &version) noexcept
    {
      return !version.prerelease || valid_prerelease(*version.prerelease);
    }

    bool valid_commit(std::string_view value) noexcept;

    bool is_compiled(TargetKind kind) noexcept { return kind != TargetKind::interface_library; }

    bool valid_target_kind(TargetKind kind) noexcept
    {
      return kind == TargetKind::executable || kind == TargetKind::static_library ||
        kind == TargetKind::shared_library || kind == TargetKind::interface_library;
    }

    bool valid_visibility(UsageVisibility visibility) noexcept
    {
      return visibility == UsageVisibility::private_ || visibility == UsageVisibility::public_ || visibility == UsageVisibility::interface_;
    }

    bool valid_standard(CxxStandard standard) noexcept
    {
      return standard == CxxStandard::cxx20 || standard == CxxStandard::cxx23;
    }

    bool visibility_allowed(TargetKind kind, UsageVisibility visibility) noexcept
    {
      if (kind == TargetKind::executable) return visibility == UsageVisibility::private_;
      if (kind == TargetKind::interface_library) return visibility == UsageVisibility::interface_;
      return true;
    }

    std::string visibility_name(UsageVisibility visibility)
    {
      switch (visibility)
      {
      case UsageVisibility::private_: return "private";
      case UsageVisibility::public_: return "public";
      case UsageVisibility::interface_: return "interface";
      }
      return "unknown";
    }

    void validate_version_requirement(const VersionRequirement &requirement,
                                      ValidationResult &result,
                                      const std::string &subject)
    {
      std::visit([&](const auto &mode)
      {
        if (!valid_version(mode.version))
          add(result, ValidationCode::invalid_requirement, subject, "invalid semantic version");
        using Mode = std::decay_t<decltype(mode)>;
        if constexpr (std::is_same_v<Mode, CompatibleVersion>)
          if (mode.version.prerelease)
            add(result, ValidationCode::invalid_requirement, subject, "compatible requirements must use a stable version");
      }, requirement);
    }

    void validate_requirement(const PackageRequirement &requirement,
                              ValidationResult &result,
                              const std::string &scope)
    {
      const std::string subject = scope + "." + requirement.alias;
      if (!valid_package_alias(requirement.alias))
        add(result, ValidationCode::invalid_package_alias, subject, "package alias must be lowercase ASCII alphanumeric words separated by single hyphens");
      std::visit([&](const auto &source)
      {
        using Source = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<Source, RegistryRequirement>)
        {
          if (!valid_registry_id(source.identity.registry) ||
              !valid_registry_name(source.identity.publisher) ||
              !valid_registry_name(source.identity.name))
            add(result, ValidationCode::invalid_package_identity, subject, "invalid registry package identity");
          validate_version_requirement(source.version, result, subject);
        }
        else
        {
          if (source.normalizedRepository.empty() || !source.repositoryRoot)
            add(result, ValidationCode::invalid_package_identity, subject, "Git requirement must identify a repository-root package");
          std::visit([&](const auto &revision)
          {
            if (revision.value.empty())
              add(result, ValidationCode::invalid_requirement, subject, "Git revision requirement must be nonempty");
            using Revision = std::decay_t<decltype(revision)>;
            if constexpr (std::is_same_v<Revision, CommitRevision>)
            {
              if (!valid_commit(revision.value)) add(result, ValidationCode::invalid_requirement, subject, "Git commit must be 40- or 64-character lowercase hexadecimal");
            }
          }, source.revision);
        }
      }, requirement.source);
    }

    bool valid_structural_digest(std::string_view value)
    {
      if (!value.starts_with("sha256:") || value.size() != 71) return false;
      return std::all_of(value.begin() + 7, value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

    bool valid_content_digest(std::string_view value)
    {
      const auto separator = value.find(':');
      if (separator == std::string_view::npos) return false;
      const auto algorithm = value.substr(0, separator);
      const auto hex = value.substr(separator + 1);
      const bool algorithmValid = !algorithm.empty() && std::all_of(algorithm.begin(), algorithm.end(), [](char c)
      {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
      });
      const std::size_t expected = algorithm == "sha256" ? 64 : algorithm == "sha512" ? 128 : 0;
      return algorithmValid && !hex.empty() && hex.size() % 2 == 0 && (expected == 0 || hex.size() == expected) &&
        std::all_of(hex.begin(), hex.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

    bool valid_commit(std::string_view value) noexcept
    {
      return (value.size() == 40 || value.size() == 64) &&
        std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

    bool version_satisfies(const Version &resolved, const VersionRequirement &requirement)
    {
      return std::visit([&](const auto &mode)
      {
        using Mode = std::decay_t<decltype(mode)>;
        if constexpr (std::is_same_v<Mode, ExactVersion>)
          return resolved == mode.version;
        else
        {
          if (resolved.prerelease || mode.version.prerelease) return false;
          const auto &base = mode.version;
          if (resolved.major != base.major ||
              std::tie(resolved.major, resolved.minor, resolved.patch) < std::tie(base.major, base.minor, base.patch)) return false;
          if (base.major != 0) return true;
          if (base.minor != 0) return resolved.minor == base.minor;
          return resolved.minor == 0 && resolved.patch == base.patch;
        }
      }, requirement);
    }

    bool path_is_inside(const std::string &path, const std::string &root)
    {
      return path == root || (path.size() > root.size() && path.compare(0, root.size(), root) == 0 && path[root.size()] == '/');
    }

    bool path_overlaps(const std::string &a, const std::string &b)
    {
      return path_is_inside(a, b) || path_is_inside(b, a);
    }

    void visit_target(const std::string &name,
                      const std::unordered_map<std::string, const NativeTarget *> &targets,
                      std::unordered_map<std::string, int> &state,
                      std::vector<std::string> &stack,
                      ValidationResult &result)
    {
      state[name] = 1;
      stack.push_back(name);
      for (const auto &use : targets.at(name)->uses)
      {
        if (!targets.contains(use.target)) continue;
        if (state[use.target] == 0) visit_target(use.target, targets, state, stack, result);
        else if (state[use.target] == 1)
        {
          auto begin = std::find(stack.begin(), stack.end(), use.target);
          std::string cycle;
          for (auto it = begin; it != stack.end(); ++it)
          {
            if (!cycle.empty()) cycle += " -> ";
            cycle += *it;
          }
          cycle += " -> " + use.target;
          add(result, ValidationCode::target_cycle, name, "target dependency cycle: " + cycle);
        }
      }
      stack.pop_back();
      state[name] = 2;
    }

    void validate_project_paths(const NativeProject &project, ValidationResult &result)
    {
      for (const auto &target : project.targets)
      {
        std::unordered_set<std::string> sources;
        for (const auto &source : target.sources)
        {
          const auto normalized = normalize_project_relative_path(source);
          if (normalized.empty()) add(result, ValidationCode::invalid_path, target.name, "source must be a normalized project-relative path that stays within the project root");
          else if (!sources.insert(normalized).second) add(result, ValidationCode::duplicate_source, target.name, "duplicate normalized source path: " + normalized);
        }
        for (const auto &includes : target.includes)
          for (const auto &path : includes.paths)
            if (normalize_project_relative_path(path).empty())
              add(result, ValidationCode::invalid_path, target.name, "include path must be project-relative and remain inside project root: " + path);
      }
    }
  } // namespace

  std::string_view to_string(ValidationCode code) noexcept
  {
    switch (code)
    {
    case ValidationCode::unsupported_format: return "unsupported_format";
    case ValidationCode::invalid_project_name: return "invalid_project_name";
    case ValidationCode::invalid_target_name: return "invalid_target_name";
    case ValidationCode::invalid_target_kind_semantics: return "invalid_target_kind_semantics";
    case ValidationCode::duplicate_target: return "duplicate_target";
    case ValidationCode::invalid_target_sources: return "invalid_target_sources";
    case ValidationCode::duplicate_source: return "duplicate_source";
    case ValidationCode::invalid_path: return "invalid_path";
    case ValidationCode::invalid_cxx_standard: return "invalid_cxx_standard";
    case ValidationCode::invalid_visibility: return "invalid_visibility";
    case ValidationCode::invalid_compile_definition: return "invalid_compile_definition";
    case ValidationCode::unknown_target: return "unknown_target";
    case ValidationCode::self_dependency: return "self_dependency";
    case ValidationCode::target_cycle: return "target_cycle";
    case ValidationCode::invalid_test: return "invalid_test";
    case ValidationCode::duplicate_package_alias: return "duplicate_package_alias";
    case ValidationCode::invalid_package_alias: return "invalid_package_alias";
    case ValidationCode::invalid_package_identity: return "invalid_package_identity";
    case ValidationCode::invalid_requirement: return "invalid_requirement";
    case ValidationCode::unknown_package_alias: return "unknown_package_alias";
    case ValidationCode::duplicate_package_use: return "duplicate_package_use";
    case ValidationCode::invalid_package_contract: return "invalid_package_contract";
    case ValidationCode::duplicate_export: return "duplicate_export";
    case ValidationCode::invalid_include_root: return "invalid_include_root";
    case ValidationCode::invalid_export_mapping: return "invalid_export_mapping";
    case ValidationCode::unknown_lock_node: return "unknown_lock_node";
    case ValidationCode::duplicate_lock_root: return "duplicate_lock_root";
    case ValidationCode::duplicate_lock_node: return "duplicate_lock_node";
    case ValidationCode::duplicate_dependency_alias: return "duplicate_dependency_alias";
    case ValidationCode::malformed_digest: return "malformed_digest";
    case ValidationCode::lock_cycle: return "lock_cycle";
    case ValidationCode::noncanonical_empty_lock: return "noncanonical_empty_lock";
    }
    return "unknown_validation_error";
  }

  bool valid_package_alias(std::string_view value) noexcept { return valid_dashed_identifier(value); }
  bool valid_registry_id(std::string_view value) noexcept { return valid_registry(value); }
  bool valid_registry_name(std::string_view value) noexcept { return valid_dashed_identifier(value); }
  bool valid_semver(const Version &version) noexcept { return valid_version(version); }
  bool valid_package_content_digest(std::string_view value) noexcept { return valid_content_digest(value); }
  bool valid_requirement(const VersionRequirement &requirement) noexcept
  {
    return std::visit([](const auto &mode)
    {
      using Mode = std::decay_t<decltype(mode)>;
      return valid_version(mode.version) && (!std::is_same_v<Mode, CompatibleVersion> || !mode.version.prerelease);
    }, requirement);
  }

  std::string normalize_project_relative_path(std::string_view input)
  {
    if (input.empty() || input.front() == '/' || input.find('\\') != std::string_view::npos || input.find(':') != std::string_view::npos)
      return {};
    std::vector<std::string_view> parts;
    std::size_t begin = 0;
    while (begin <= input.size())
    {
      const auto end = input.find('/', begin);
      const auto part = input.substr(begin, end == std::string_view::npos ? input.size() - begin : end - begin);
      if (part.empty()) return {};
      if (part == ".") {}
      else if (part == "..")
      {
        if (parts.empty()) return {};
        parts.pop_back();
      }
      else parts.push_back(part);
      if (end == std::string_view::npos) break;
      begin = end + 1;
    }
    if (parts.empty()) return {};
    std::string result;
    for (const auto part : parts)
    {
      if (!result.empty()) result.push_back('/');
      result.append(part);
    }
    return result;
  }

  ValidationResult validate_native_project(const NativeProject &project)
  {
    ValidationResult result;
    if (project.format != 1) add(result, ValidationCode::unsupported_format, "format", "native project format must be 1");
    if (project.name.empty()) add(result, ValidationCode::invalid_project_name, "project.name", "project name must be nonempty");
    if (project.cxxStandard && !valid_standard(*project.cxxStandard))
      add(result, ValidationCode::invalid_cxx_standard, "project.cxx-standard", "project C++ standard must be c++20 or c++23");
    std::unordered_map<std::string, const NativeTarget *> targets;
    for (const auto &target : project.targets)
    {
      if (!valid_target_kind(target.kind)) add(result, ValidationCode::invalid_target_kind_semantics, target.name, "unsupported native target kind");
      if (target.cxxStandard && !valid_standard(*target.cxxStandard))
        add(result, ValidationCode::invalid_cxx_standard, target.name, "target C++ standard must be c++20 or c++23");
      if (target.name.empty()) add(result, ValidationCode::invalid_target_name, "target", "target name must be nonempty");
      if (!targets.emplace(target.name, &target).second)
        add(result, ValidationCode::duplicate_target, target.name, "target names must be unique");
      if (is_compiled(target.kind) && target.sources.empty())
        add(result, ValidationCode::invalid_target_sources, target.name, "compiled targets require at least one source");
      if (target.kind == TargetKind::interface_library && !target.sources.empty())
        add(result, ValidationCode::invalid_target_sources, target.name, "interface targets cannot declare compiled sources");
      for (const auto &group : target.includes)
      {
        if (!valid_visibility(group.visibility)) add(result, ValidationCode::invalid_visibility, target.name, "unknown include visibility");
        if (!visibility_allowed(target.kind, group.visibility))
          add(result, ValidationCode::invalid_visibility, target.name, "include visibility '" + visibility_name(group.visibility) + "' is invalid for this target kind");
      }
      for (const auto &group : target.definitions)
      {
        if (!valid_visibility(group.visibility)) add(result, ValidationCode::invalid_visibility, target.name, "unknown definition visibility");
        if (!visibility_allowed(target.kind, group.visibility))
          add(result, ValidationCode::invalid_visibility, target.name, "definition visibility '" + visibility_name(group.visibility) + "' is invalid for this target kind");
        for (const auto &definition : group.values)
        {
          const auto equal = definition.find('=');
          const std::string_view macro(definition.data(), equal == std::string::npos ? definition.size() : equal);
          const bool identifier = !macro.empty() &&
            ((macro.front() >= 'A' && macro.front() <= 'Z') || (macro.front() >= 'a' && macro.front() <= 'z') || macro.front() == '_') &&
            std::all_of(macro.begin() + 1, macro.end(), [](char c) { return alnum(c) || c == '_'; });
          if (!identifier || definition.starts_with("-D") || definition.starts_with("/D"))
            add(result, ValidationCode::invalid_compile_definition, target.name, "compile definition must be a logical NAME or NAME=value, not a compiler option");
        }
      }
      for (const auto &use : target.uses)
      {
        if (!valid_visibility(use.visibility)) add(result, ValidationCode::invalid_visibility, target.name, "unknown target-use visibility");
        if (!visibility_allowed(target.kind, use.visibility))
          add(result, ValidationCode::invalid_visibility, target.name, "target-use visibility '" + visibility_name(use.visibility) + "' is invalid for this target kind");
        if (use.target == target.name) add(result, ValidationCode::self_dependency, target.name, "target cannot use itself");
      }
      for (const auto &use : target.packageUses)
      {
        if (!valid_visibility(use.visibility)) add(result, ValidationCode::invalid_visibility, target.name, "unknown package-use visibility");
        if (!visibility_allowed(target.kind, use.visibility))
          add(result, ValidationCode::invalid_visibility, target.name, "package-use visibility '" + visibility_name(use.visibility) + "' is invalid for this target kind");
      }
    }

    for (const auto &target : project.targets)
      for (const auto &use : target.uses)
        if (!targets.contains(use.target)) add(result, ValidationCode::unknown_target, target.name, "unknown target reference: " + use.target);

    std::unordered_map<std::string, int> state;
    std::vector<std::string> stack;
    for (const auto &[name, _] : targets) state[name] = 0;
    for (const auto &[name, _] : targets)
      if (state[name] == 0) visit_target(name, targets, state, stack, result);

    std::unordered_set<std::string> packageAliases;
    for (const auto &package : project.packages)
    {
      validate_requirement(package, result, "project.packages");
      if (!packageAliases.insert(package.alias).second)
        add(result, ValidationCode::duplicate_package_alias, package.alias, "package aliases must be unique in the project");
    }
    for (const auto &target : project.targets)
    {
      std::set<std::pair<std::string, std::string>> packageReferences;
      for (const auto &use : target.packageUses)
      {
        if (!packageAliases.contains(use.requirementAlias))
          add(result, ValidationCode::unknown_package_alias, target.name, "unknown package requirement alias: " + use.requirementAlias);
        if (!valid_package_alias(use.exportName))
          add(result, ValidationCode::invalid_package_contract, target.name, "package export name is invalid");
        const auto reference = std::make_pair(use.requirementAlias, use.exportName);
        if (!packageReferences.insert(reference).second)
          add(result, ValidationCode::duplicate_package_use, target.name, "package export may be used only once per target; conflicting visibilities are not merged");
      }
    }
    for (const auto &test : project.tests)
    {
      const auto found = targets.find(test.target);
      if (test.name.empty() || test.target.empty() || found == targets.end())
        add(result, ValidationCode::invalid_test, test.name, "test requires a name and a declared executable target");
      else if (found->second->kind != TargetKind::executable)
        add(result, ValidationCode::invalid_test, test.name, "test registration target must be executable");
    }
    validate_project_paths(project, result);
    return result;
  }

  ValidationResult validate_native_project_paths(const NativeProject &project,
                                                 const std::filesystem::path &projectRoot,
                                                 bool requireExistingSources)
  {
    ValidationResult result;
    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(projectRoot, ec);
    if (ec)
    {
      add(result, ValidationCode::invalid_path, projectRoot.string(), "project root cannot be canonicalized");
      return result;
    }
    auto check = [&](const std::string &raw, bool file, const std::string &subject)
    {
      const auto normalized = normalize_project_relative_path(raw);
      if (normalized.empty())
      {
        add(result, ValidationCode::invalid_path, subject, "path must be relative, normalized, and remain inside project root");
        return;
      }
      const auto full = std::filesystem::weakly_canonical(root / std::filesystem::path(normalized), ec);
      if (ec)
      {
        ec.clear();
        add(result, ValidationCode::invalid_path, subject, "path cannot be canonicalized");
        return;
      }
      auto rel = full.lexically_relative(root);
      if (rel.empty() || rel.is_absolute() || *rel.begin() == "..")
      {
        add(result, ValidationCode::invalid_path, subject, "path resolves outside project root");
        return;
      }
      if (file && requireExistingSources && !std::filesystem::is_regular_file(full, ec))
      {
        ec.clear();
        add(result, ValidationCode::invalid_path, subject, "source file does not exist");
      }
      else if (!file && std::filesystem::exists(full, ec) && !std::filesystem::is_directory(full, ec))
        add(result, ValidationCode::invalid_path, subject, "include path exists but is not a directory");
      ec.clear();
    };
    for (const auto &target : project.targets)
    {
      for (const auto &source : target.sources) check(source, true, target.name);
      for (const auto &group : target.includes)
        for (const auto &include : group.paths) check(include, false, target.name);
    }
    return result;
  }

  ValidationResult validate_package_contract(const PackageContract &contract)
  {
    ValidationResult result;
    if (contract.format != 1) add(result, ValidationCode::unsupported_format, "format", "package contract format must be 1");
    std::visit([&](const auto &identity)
    {
      using Identity = std::decay_t<decltype(identity)>;
      if constexpr (std::is_same_v<Identity, RegistryPackageContractIdentity>)
      {
        const auto &id = identity.identity;
        if (!valid_registry_id(id.registry) || !valid_registry_name(id.publisher) || !valid_registry_name(id.name) || !valid_version(identity.version))
          add(result, ValidationCode::invalid_package_identity, "package", "invalid registry identity or release version");
      }
      else
        (void)identity;
    }, contract.identity);
    std::unordered_set<std::string> aliases;
    for (const auto &package : contract.packages)
    {
      validate_requirement(package, result, "package.packages");
      if (!aliases.insert(package.alias).second)
        add(result, ValidationCode::duplicate_package_alias, package.alias, "package-local aliases must be unique");
    }
    if (contract.exports.empty()) add(result, ValidationCode::invalid_package_contract, "exports", "package contract must declare at least one export");
    std::unordered_set<std::string> exportNames;
    for (const auto &exported : contract.exports)
    {
      if (!valid_package_alias(exported.name)) add(result, ValidationCode::invalid_package_contract, exported.name, "export name must be a valid lowercase ASCII identifier");
      if (!exportNames.insert(exported.name).second) add(result, ValidationCode::duplicate_export, exported.name, "package export names must be unique");
      const bool nativeIntegration = std::holds_alternative<NativeVixIntegration>(contract.integration);
      if (nativeIntegration != std::holds_alternative<NativeExportMapping>(exported.mapping))
        add(result, ValidationCode::invalid_export_mapping, exported.name, "export mapping does not match integration type");
      if (const auto *native = std::get_if<NativeExportMapping>(&exported.mapping); native && native->projectTarget.empty())
        add(result, ValidationCode::invalid_export_mapping, exported.name, "native export target must be nonempty");
      if (const auto *cmake = std::get_if<CMakeExportMapping>(&exported.mapping); cmake && cmake->cmakeTarget.empty())
        add(result, ValidationCode::invalid_export_mapping, exported.name, "CMake imported target must be nonempty");
    }
    if (const auto *cmake = std::get_if<CMakeConfigIntegration>(&contract.integration))
    {
      if (cmake->configName.empty()) add(result, ValidationCode::invalid_package_contract, "integration.config-name", "CMake config name must be nonempty");
    }
    return result;
  }

  ValidationResult validate_package_contract_paths(const PackageContract &contract,
                                                   const std::filesystem::path &packageRoot)
  {
    ValidationResult result;
    const auto *native = std::get_if<NativeVixIntegration>(&contract.integration);
    if (!native) return result;
    std::error_code ec;
    const auto root = std::filesystem::canonical(packageRoot, ec);
    if (ec || !std::filesystem::is_directory(root))
    {
      add(result, ValidationCode::invalid_include_root, packageRoot.string(), "package root must exist and be a directory");
      return result;
    }
    std::vector<std::string> normalizedRoots;
    for (const auto &raw : native->includeRoots)
    {
      const auto normalized = normalize_project_relative_path(raw);
      if (normalized.empty() || normalized != raw)
      {
        add(result, ValidationCode::invalid_include_root, raw, "include root must be a normalized package-relative path");
        continue;
      }
      if (normalized == "lib" || normalized.starts_with("lib/") || normalized == "bin" || normalized.starts_with("bin/"))
        add(result, ValidationCode::invalid_include_root, raw, "include root collides with Vix-managed install locations");
      if (std::find(normalizedRoots.begin(), normalizedRoots.end(), normalized) != normalizedRoots.end())
        add(result, ValidationCode::invalid_include_root, raw, "duplicate include root");
      for (const auto &prior : normalizedRoots)
        if (path_overlaps(prior, normalized)) add(result, ValidationCode::invalid_include_root, raw, "include roots must not overlap");
      normalizedRoots.push_back(normalized);
      const auto path = root / std::filesystem::path(normalized);
      bool hasSymlink = false;
      auto componentPath = root;
      for (const auto &component : std::filesystem::path(normalized))
      {
        componentPath /= component;
        const auto status = std::filesystem::symlink_status(componentPath, ec);
        if (ec) break;
        if (std::filesystem::is_symlink(status)) { hasSymlink = true; break; }
      }
      if (hasSymlink)
      {
        ec.clear();
        add(result, ValidationCode::invalid_include_root, raw, "publication root cannot traverse a symlink");
        continue;
      }
      if (ec)
      {
        ec.clear();
        add(result, ValidationCode::invalid_include_root, raw, "include root path cannot be inspected safely");
        continue;
      }
      ec.clear();
      const auto resolved = std::filesystem::canonical(path, ec);
      if (ec || !std::filesystem::is_directory(resolved))
      {
        ec.clear();
        add(result, ValidationCode::invalid_include_root, raw, "include root must exist and be a directory");
        continue;
      }
      const auto relative = resolved.lexically_relative(root);
      if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
        add(result, ValidationCode::invalid_include_root, raw, "include root resolves outside package root");
      std::filesystem::recursive_directory_iterator it(path, std::filesystem::directory_options::none, ec), end;
      for (; !ec && it != end; it.increment(ec))
        if (it->is_symlink(ec)) add(result, ValidationCode::invalid_include_root, raw, "symlinks are forbidden within publication roots");
      if (ec)
      {
        add(result, ValidationCode::invalid_include_root, raw, "publication root cannot be inspected safely");
        ec.clear();
      }
    }
    return result;
  }

  ValidationResult validate_native_package(const NativeProject &project,
                                           const PackageContract &contract,
                                           const std::filesystem::path &packageRoot)
  {
    auto result = validate_native_project(project);
    const auto contractResult = validate_package_contract(contract);
    result.issues.insert(result.issues.end(), contractResult.issues.begin(), contractResult.issues.end());
    auto pathResult = validate_package_contract_paths(contract, packageRoot);
    result.issues.insert(result.issues.end(), pathResult.issues.begin(), pathResult.issues.end());
    const auto *integration = std::get_if<NativeVixIntegration>(&contract.integration);
    if (!integration)
    {
      add(result, ValidationCode::invalid_package_contract, "integration", "native package validation requires native-vix integration");
      return result;
    }
    std::unordered_map<std::string, const NativeTarget *> targets;
    for (const auto &target : project.targets) targets.emplace(target.name, &target);
    std::unordered_set<std::string> exports;
    for (const auto &item : contract.exports)
    {
      const auto *mapping = std::get_if<NativeExportMapping>(&item.mapping);
      if (!mapping) continue;
      exports.insert(mapping->projectTarget);
      const auto found = targets.find(mapping->projectTarget);
      if (found == targets.end()) add(result, ValidationCode::invalid_export_mapping, item.name, "export references unknown native project target: " + mapping->projectTarget);
      else if (found->second->kind != TargetKind::static_library && found->second->kind != TargetKind::shared_library && found->second->kind != TargetKind::interface_library)
        add(result, ValidationCode::invalid_export_mapping, item.name, "only static, shared, and interface targets may be exported");
    }
    std::unordered_set<std::string> closure;
    std::vector<std::string> pending(exports.begin(), exports.end());
    while (!pending.empty())
    {
      auto name = std::move(pending.back()); pending.pop_back();
      if (!closure.insert(name).second) continue;
      const auto found = targets.find(name);
      if (found == targets.end()) continue;
      for (const auto &use : found->second->uses) pending.push_back(use.target);
    }
    std::unordered_set<std::string> requirements;
    for (const auto &package : contract.packages) requirements.insert(package.alias);
    for (const auto &name : closure)
    {
      const auto found = targets.find(name);
      if (found == targets.end()) continue;
      for (const auto &use : found->second->packageUses)
        if (!requirements.contains(use.requirementAlias))
          add(result, ValidationCode::unknown_package_alias, name, "export closure uses undeclared package alias: " + use.requirementAlias);
      for (const auto &group : found->second->includes)
      {
        if (group.visibility == UsageVisibility::private_) continue;
        for (const auto &path : group.paths)
        {
          const auto normalized = normalize_project_relative_path(path);
          const auto matches = std::count_if(integration->includeRoots.begin(), integration->includeRoots.end(), [&](const auto &root)
          {
            const auto normalizedRoot = normalize_project_relative_path(root);
            return !normalized.empty() && !normalizedRoot.empty() && path_is_inside(normalized, normalizedRoot);
          });
          if (matches != 1) add(result, ValidationCode::invalid_include_root, name, "public/interface include must map through exactly one package include root: " + path);
        }
      }
    }
    return result;
  }

  ValidationResult validate_lockfile(const Lockfile &lockfile)
  {
    ValidationResult result;
    if (lockfile.format != 1) add(result, ValidationCode::unsupported_format, "format", "lockfile format must be 1");
    if (lockfile.roots.empty()) add(result, ValidationCode::noncanonical_empty_lock, "roots", "format-1 lockfiles are not persisted for empty root requirements");
    if (!valid_structural_digest(lockfile.rootRequirementsDigest)) add(result, ValidationCode::malformed_digest, "root-requirements-digest", "structural digest must be sha256 plus 64 lowercase hexadecimal digits");
    std::unordered_set<std::string> roots;
    for (const auto &root : lockfile.roots)
    {
      if (!valid_package_alias(root.alias) || !roots.insert(root.alias).second)
        add(result, ValidationCode::duplicate_lock_root, root.alias, "root aliases must be valid and unique");
      if (const auto *registry = std::get_if<NormalizedDeclaredRegistry>(&root.declared))
      {
        if (!valid_registry_id(registry->identity.registry) || !valid_registry_name(registry->identity.publisher) || !valid_registry_name(registry->identity.name))
          add(result, ValidationCode::invalid_package_identity, root.alias, "invalid registry root identity");
        validate_version_requirement(registry->requirement, result, root.alias);
      }
      else
      {
        const auto &git = std::get<NormalizedDeclaredGit>(root.declared);
        if (git.normalizedRepository.empty() || !git.repositoryRoot)
          add(result, ValidationCode::invalid_package_identity, root.alias, "invalid Git root identity");
        std::visit([&](const auto &rev)
        {
          if (rev.value.empty()) add(result, ValidationCode::invalid_requirement, root.alias, "Git revision must be nonempty");
          using Revision = std::decay_t<decltype(rev)>;
          if constexpr (std::is_same_v<Revision, CommitRevision>)
            if (!valid_commit(rev.value)) add(result, ValidationCode::invalid_requirement, root.alias, "Git commit must be 40- or 64-character lowercase hexadecimal");
        }, git.revision);
      }
    }
    std::unordered_map<std::string, const LockNode *> nodes;
    std::vector<const LockNode *> uniqueNodes;
    auto idOf = [](const LockNode &node) -> const std::string &
    { return std::visit([](const auto &n) -> const std::string & { return n.id; }, node); };
    for (const auto &node : lockfile.nodes)
    {
      const auto &id = idOf(node);
      if (!valid_structural_digest(id)) add(result, ValidationCode::malformed_digest, id, "node ID must be sha256 plus 64 lowercase hexadecimal digits");
      if (id.empty() || !nodes.emplace(id, &node).second) add(result, ValidationCode::duplicate_lock_node, id, "lock node IDs must be nonempty and unique");
      else uniqueNodes.push_back(&node);
      std::visit([&](const auto &n)
      {
        using Node = std::decay_t<decltype(n)>;
        if (!valid_content_digest(n.contentDigest)) add(result, ValidationCode::malformed_digest, id, "content digest must be sha256 or sha512 with lowercase hexadecimal");
        if constexpr (std::is_same_v<Node, RegistryLockNode>)
        {
          if (!valid_registry_id(n.identity.registry) || !valid_registry_name(n.identity.publisher) || !valid_registry_name(n.identity.name) || !valid_version(n.version))
            add(result, ValidationCode::invalid_package_identity, id, "invalid registry lock node identity/version");
        }
        else
        {
          if (n.normalizedRepository.empty() || !n.repositoryRoot || !valid_commit(n.commit))
            add(result, ValidationCode::invalid_package_identity, id, "invalid Git lock node identity or commit");
        }
        std::unordered_set<std::string> aliases;
        for (const auto &edge : n.dependencies)
          if (!valid_package_alias(edge.alias) || !aliases.insert(edge.alias).second)
            add(result, ValidationCode::duplicate_dependency_alias, id, "dependency aliases must be valid and unique within a node");
      }, node);
    }
    for (std::size_t i = 0; i < uniqueNodes.size(); ++i)
      for (std::size_t j = i + 1; j < uniqueNodes.size(); ++j)
      {
        const bool sameIdentity = std::visit([&](const auto &left) -> bool
        {
          using Left = std::decay_t<decltype(left)>;
          const auto *right = std::get_if<Left>(uniqueNodes[j]);
          if (!right) return false;
          if constexpr (std::is_same_v<Left, RegistryLockNode>) return left.identity == right->identity;
          else return left.normalizedRepository == right->normalizedRepository && left.repositoryRoot == right->repositoryRoot;
        }, *uniqueNodes[i]);
        if (sameIdentity) add(result, ValidationCode::duplicate_lock_node, idOf(*uniqueNodes[j]), "one canonical package identity may have only one lock node");
      }
    for (const auto &root : lockfile.roots)
    {
      const auto found = nodes.find(root.node);
      if (found == nodes.end()) add(result, ValidationCode::unknown_lock_node, root.alias, "root references unknown lock node: " + root.node);
      else
      {
        const bool identityMatches = std::visit([&](const auto &declared) -> bool
        {
          using Declared = std::decay_t<decltype(declared)>;
          if constexpr (std::is_same_v<Declared, NormalizedDeclaredRegistry>)
          {
            const auto *node = std::get_if<RegistryLockNode>(found->second);
            return node && node->identity == declared.identity;
          }
          else
          {
            const auto *node = std::get_if<GitLockNode>(found->second);
            return node && node->normalizedRepository == declared.normalizedRepository && node->repositoryRoot == declared.repositoryRoot;
          }
        }, root.declared);
        if (!identityMatches) add(result, ValidationCode::invalid_package_identity, root.alias, "root declaration identity does not match its resolved lock node");
        if (identityMatches)
        {
          if (const auto *registry_declared = std::get_if<NormalizedDeclaredRegistry>(&root.declared))
          {
            const auto *node = std::get_if<RegistryLockNode>(found->second);
            if (node && !version_satisfies(node->version, registry_declared->requirement))
              add(result, ValidationCode::invalid_requirement, root.alias, "resolved registry version does not satisfy the declared requirement");
          }
          else if (const auto *git_declared = std::get_if<NormalizedDeclaredGit>(&root.declared))
          {
            if (const auto *node = std::get_if<GitLockNode>(found->second))
              if (const auto *commit = std::get_if<CommitRevision>(&git_declared->revision); commit && node->commit != commit->value)
                add(result, ValidationCode::invalid_requirement, root.alias, "resolved Git commit differs from the declared commit");
          }
        }
      }
    }
    for (const auto &node : lockfile.nodes)
      std::visit([&](const auto &n)
      {
        for (const auto &edge : n.dependencies)
          if (!nodes.contains(edge.node)) add(result, ValidationCode::unknown_lock_node, n.id, "dependency references unknown lock node: " + edge.node);
      }, node);

    std::unordered_map<std::string, int> state;
    std::vector<std::string> stack;
    std::function<void(const std::string &)> visit = [&](const std::string &id)
    {
      state[id] = 1; stack.push_back(id);
      const auto found = nodes.find(id);
      if (found != nodes.end()) std::visit([&](const auto &n)
      {
        for (const auto &edge : n.dependencies)
        {
          if (!nodes.contains(edge.node)) continue;
          if (state[edge.node] == 0) visit(edge.node);
          else if (state[edge.node] == 1)
          {
            auto begin = std::find(stack.begin(), stack.end(), edge.node);
            std::string cycle;
            for (auto it = begin; it != stack.end(); ++it) { if (!cycle.empty()) cycle += " -> "; cycle += *it; }
            cycle += " -> " + edge.node;
            add(result, ValidationCode::lock_cycle, id, "lock dependency cycle: " + cycle);
          }
        }
      }, *found->second);
      stack.pop_back(); state[id] = 2;
    };
    for (const auto &[id, _] : nodes) state[id] = 0;
    for (const auto &[id, _] : nodes) if (state[id] == 0) visit(id);
    return result;
  }
} // namespace vix::cli::project::native
