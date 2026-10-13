#include <vix/cli/project/native/TomlParser.hpp>

#include <toml.hpp>

#include <charconv>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace vix::cli::project::native
{
  namespace
  {
    using namespace std::string_view_literals;

    using Table = toml::table;
    using Node = toml::node;

    struct Failure
    {
      ParseIssue issue;
    };

    ParseIssue issue(ParseErrorCode code, std::string path, std::string message,
                     const toml::source_region *source = nullptr)
    {
      ParseIssue result{code, std::move(path), std::move(message), std::nullopt};
      if (source)
      {
        ValidationLocation location;
        if (source->path)
          location.source = *source->path;
        if (source->begin)
        {
          location.line = source->begin.line;
          location.column = source->begin.column;
        }
        if (!location.source.empty() || location.line || location.column)
          result.location = std::move(location);
      }
      return result;
    }

    [[noreturn]] void fail(ParseErrorCode code, std::string_view path,
                           const std::string &message, const Node *node = nullptr)
    {
      throw Failure{issue(code, std::string{path}, message, node ? &node->source() : nullptr)};
    }

    // The returned node is owned by `table`; the non-owning path is used only
    // synchronously to construct an owned diagnostic if the key is absent.
    const Node &required(const Table &table, std::string_view key, std::string_view path)
    {
      if (const auto *node = table.get(key))
        return *node;
      fail(ParseErrorCode::missing_field,
           path.empty() ? std::string{key} : std::string{path} + "." + std::string(key),
           "required field is missing");
    }

    void strict_keys(const Table &table, std::initializer_list<std::string_view> allowed,
                     const std::string &path)
    {
      for (const auto &entry : table)
      {
        const std::string key{entry.first.str()};
        bool known = false;
        for (const auto candidate : allowed)
          if (key == candidate)
          {
            known = true;
            break;
          }
        if (!known)
          throw Failure{issue(ParseErrorCode::unknown_field,
                              path.empty() ? key : path + "." + key, "unknown field", &entry.first.source())};
      }
    }

    // The returned table is owned by `node`; path is never retained.
    const Table &as_table(const Node &node, std::string_view path)
    {
      if (const auto *table = node.as_table())
        return *table;
      fail(ParseErrorCode::wrong_type, path, "expected a table", &node);
    }

    std::string string_value(const Node &node, std::string_view path)
    {
      if (!node.is_string())
        fail(ParseErrorCode::wrong_type, path, "expected a string", &node);
      if (const auto value = node.value<std::string>())
        return *value;
      fail(ParseErrorCode::wrong_type, path, "expected a string", &node);
    }

    std::string required_string(const Table &table, std::string_view key, const std::string &path)
    {
      const auto &node = required(table, key, path);
      return string_value(node, path + "." + std::string(key));
    }

    std::optional<std::string> optional_string(const Table &table, std::string_view key,
                                               const std::string &path)
    {
      const auto *node = table.get(key);
      if (!node)
        return std::nullopt;
      return string_value(*node, path + "." + std::string(key));
    }

    const toml::array &as_array(const Node &node, std::string_view path)
    {
      if (const auto *array = node.as_array())
        return *array;
      fail(ParseErrorCode::wrong_type, path, "expected an array", &node);
    }

    std::vector<std::string> string_array(const Node &node, const std::string &path)
    {
      const auto &array = as_array(node, path);
      std::vector<std::string> values;
      values.reserve(array.size());
      std::size_t index = 0;
      for (const auto &element : array)
        values.push_back(string_value(element, path + "[" + std::to_string(index++) + "]"));
      return values;
    }

    template <typename T, typename F>
    std::vector<T> table_array(const Table &parent, std::string_view key,
                               const std::string &path, bool requiredField, F parse)
    {
      const auto collectionPath = path.empty() ? std::string{key} : path + "." + std::string(key);
      const auto *node = parent.get(key);
      if (!node)
      {
        if (requiredField)
          fail(ParseErrorCode::missing_field, collectionPath, "required array is missing");
        return {};
      }
      const auto &array = as_array(*node, collectionPath);
      // TOML has no array-of-tables spelling for an empty collection. Permit
      // `[]` only when empty; nonempty collections must be table arrays.
      if (!array.is_array_of_tables() && !array.empty())
        fail(ParseErrorCode::wrong_type, collectionPath, "expected an array of tables", node);
      std::vector<T> values;
      values.reserve(array.size());
      std::size_t index = 0;
      for (const auto &element : array)
      {
        const auto itemPath = collectionPath + "[" + std::to_string(index++) + "]";
        values.push_back(parse(as_table(element, itemPath), itemPath));
      }
      return values;
    }

    std::int64_t format_value(const Table &root)
    {
      const auto &node = required(root, "format", "");
      if (!node.is_integer())
        fail(ParseErrorCode::wrong_type, "format", "format must be an integer", &node);
      const auto integer = node.value<std::int64_t>();
      if (!integer)
        fail(ParseErrorCode::wrong_type, "format", "format must be an integer", &node);
      if (*integer != 1)
        fail(ParseErrorCode::unsupported_format, "format", "unsupported format version", &node);
      return *integer;
    }

    CxxStandard standard_value(const std::string &value, const std::string &path, const Node *node = nullptr)
    {
      if (value == "c++20")
        return CxxStandard::cxx20;
      if (value == "c++23")
        return CxxStandard::cxx23;
      fail(ParseErrorCode::invalid_value, path, "expected c++20 or c++23", node);
    }

    UsageVisibility visibility_value(const std::string &value, const std::string &path, const Node *node = nullptr)
    {
      if (value == "private")
        return UsageVisibility::private_;
      if (value == "public")
        return UsageVisibility::public_;
      if (value == "interface")
        return UsageVisibility::interface_;
      fail(ParseErrorCode::invalid_value, path, "expected private, public, or interface", node);
    }

    std::uint64_t numeric_component(std::string_view text, const std::string &path, const Node &node)
    {
      if (text.empty() || (text.size() > 1 && text.front() == '0'))
        fail(ParseErrorCode::invalid_value, path, "invalid semantic-version numeric component", &node);
      std::uint64_t value{};
      const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
      if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        fail(ParseErrorCode::invalid_value, path, "invalid semantic-version numeric component", &node);
      return value;
    }

    Version version_value(const Node &node, const std::string &path)
    {
      const auto text = string_value(node, path);
      if (text.find('+') != std::string::npos)
        fail(ParseErrorCode::invalid_value, path, "build metadata is not supported", &node);
      const auto dash = text.find('-');
      const auto core = std::string_view{text}.substr(0, dash);
      const auto first = core.find('.');
      const auto second = first == std::string_view::npos ? first : core.find('.', first + 1);
      if (first == std::string_view::npos || second == std::string_view::npos || core.find('.', second + 1) != std::string_view::npos)
        fail(ParseErrorCode::invalid_value, path, "version must be major.minor.patch with optional prerelease", &node);
      Version version;
      version.major = numeric_component(core.substr(0, first), path, node);
      version.minor = numeric_component(core.substr(first + 1, second - first - 1), path, node);
      version.patch = numeric_component(core.substr(second + 1), path, node);
      if (dash != std::string::npos)
      {
        const auto prerelease = std::string_view{text}.substr(dash + 1);
        if (prerelease.empty())
          fail(ParseErrorCode::invalid_value, path, "prerelease must be nonempty", &node);
        version.prerelease = std::string{prerelease};
      }
      return version;
    }

    std::string mode_key(const Table &table, std::initializer_list<std::string_view> modes,
                         const std::string &path, const Node &node)
    {
      std::string found;
      std::size_t count = 0;
      for (const auto mode : modes)
        if (table.contains(mode))
        {
          found = mode;
          ++count;
        }
      if (count != 1)
        fail(ParseErrorCode::invalid_discriminated_shape, path,
             "exactly one discriminator must be present", &node);
      return found;
    }

    PackageRequirement package_requirement(const Table &table, const std::string &path)
    {
      const auto alias = required_string(table, "alias", path);
      const auto sourceNode = &required(table, "source", path);
      const auto source = string_value(*sourceNode, path + ".source");
      if (source == "registry")
      {
        strict_keys(table, {"alias", "source", "registry", "publisher", "name", "requirement"}, path);
        RegistryPackageIdentity identity{
            required_string(table, "registry", path),
            required_string(table, "publisher", path),
            required_string(table, "name", path)};
        const auto &requirementNode = required(table, "requirement", path);
        const auto &requirement = as_table(requirementNode, path + ".requirement");
        strict_keys(requirement, {"exact", "compatible"}, path + ".requirement");
        const auto mode = mode_key(requirement, {"exact", "compatible"}, path + ".requirement", requirementNode);
        const auto version = version_value(required(requirement, mode, path + ".requirement"), path + ".requirement." + mode);
        if (mode == "exact")
          return {alias, RegistryRequirement{std::move(identity), ExactVersion{version}}};
        return {alias, RegistryRequirement{std::move(identity), CompatibleVersion{version}}};
      }
      if (source == "git")
      {
        strict_keys(table, {"alias", "source", "repository", "revision"}, path);
        // Preserve the declared locator byte-for-byte here. Host-aware Git
        // normalization belongs to the later resolution/canonicalization layer.
        auto repository = required_string(table, "repository", path);
        const auto &revisionNode = required(table, "revision", path);
        const auto &revision = as_table(revisionNode, path + ".revision");
        strict_keys(revision, {"commit", "tag", "branch"}, path + ".revision");
        const auto mode = mode_key(revision, {"commit", "tag", "branch"}, path + ".revision", revisionNode);
        auto value = string_value(required(revision, mode, path + ".revision"), path + ".revision." + mode);
        GitRevisionRequirement parsed = mode == "commit" ? GitRevisionRequirement{CommitRevision{std::move(value)}} : mode == "tag" ? GitRevisionRequirement{TagRevision{std::move(value)}}
                                                                                                                                    : GitRevisionRequirement{BranchRevision{std::move(value)}};
        return {alias, GitRequirement{std::move(repository), true, std::move(parsed)}};
      }
      fail(ParseErrorCode::invalid_value, path + ".source", "source must be registry or git", sourceNode);
    }

    std::vector<PackageRequirement> requirements(const Table &root, const std::string &path)
    {
      return table_array<PackageRequirement>(root, "packages", path, false,
                                             [](const Table &table, const std::string &itemPath)
                                             { return package_requirement(table, itemPath); });
    }

    std::vector<std::string> visibility_array(const Table &table, std::string_view key, const std::string &path)
    {
      const auto *node = table.get(key);
      if (!node)
        return {};
      return string_array(*node, path + "." + std::string(key));
    }

    std::vector<IncludeRequirement> include_requirements(const Table &root, const std::string &path)
    {
      const auto *node = root.get("includes");
      if (!node)
        return {};
      const auto &table = as_table(*node, path + ".includes");
      strict_keys(table, {"private", "public", "interface"}, path + ".includes");
      std::vector<IncludeRequirement> result;
      for (const auto &[key, visibility] : {std::pair{"private"sv, UsageVisibility::private_},
                                            std::pair{"public"sv, UsageVisibility::public_},
                                            std::pair{"interface"sv, UsageVisibility::interface_}})
      {
        auto values = visibility_array(table, key, path + ".includes");
        if (!values.empty() || table.contains(key))
          result.push_back({visibility, std::move(values)});
      }
      return result;
    }

    std::vector<InternalTargetUse> internal_uses(const Table &root, const std::string &path)
    {
      const auto *node = root.get("uses");
      if (!node)
        return {};
      const auto &table = as_table(*node, path + ".uses");
      strict_keys(table, {"private", "public", "interface"}, path + ".uses");
      std::vector<InternalTargetUse> result;
      for (const auto &[key, visibility] : {std::pair{"private"sv, UsageVisibility::private_},
                                            std::pair{"public"sv, UsageVisibility::public_},
                                            std::pair{"interface"sv, UsageVisibility::interface_}})
        for (auto &target : visibility_array(table, key, path + ".uses"))
          result.push_back({visibility, std::move(target)});
      return result;
    }

    std::vector<CompileDefinitions> definitions(const Table &root, const std::string &path)
    {
      const auto *node = root.get("definitions");
      if (!node)
        return {};
      const auto &table = as_table(*node, path + ".definitions");
      strict_keys(table, {"private", "public", "interface"}, path + ".definitions");
      std::vector<CompileDefinitions> result;
      for (const auto &[key, visibility] : {std::pair{"private"sv, UsageVisibility::private_},
                                            std::pair{"public"sv, UsageVisibility::public_},
                                            std::pair{"interface"sv, UsageVisibility::interface_}})
      {
        auto values = visibility_array(table, key, path + ".definitions");
        if (!values.empty() || table.contains(key))
          result.push_back({visibility, std::move(values)});
      }
      return result;
    }

    NativeTarget target_value(const Table &table, const std::string &path)
    {
      strict_keys(table, {"name", "kind", "sources", "cxx-standard", "includes", "uses", "definitions", "package-uses"}, path);
      NativeTarget target;
      target.name = required_string(table, "name", path);
      const auto &kindNode = required(table, "kind", path);
      const auto kind = string_value(kindNode, path + ".kind");
      if (kind == "executable")
        target.kind = TargetKind::executable;
      else if (kind == "static")
        target.kind = TargetKind::static_library;
      else if (kind == "shared")
        target.kind = TargetKind::shared_library;
      else if (kind == "interface")
        target.kind = TargetKind::interface_library;
      else
        fail(ParseErrorCode::invalid_value, path + ".kind", "unknown target kind", &kindNode);
      if (const auto *sources = table.get("sources"))
        target.sources = string_array(*sources, path + ".sources");
      if (const auto standard = optional_string(table, "cxx-standard", path))
        target.cxxStandard = standard_value(*standard, path + ".cxx-standard", table.get("cxx-standard"));
      target.includes = include_requirements(table, path);
      target.uses = internal_uses(table, path);
      target.definitions = definitions(table, path);
      target.packageUses = table_array<PackageTargetUse>(table, "package-uses", path, false,
                                                         [](const Table &use, const std::string &usePath)
                                                         {
                                                           strict_keys(use, {"package", "target", "visibility"}, usePath);
                                                           const auto &visibilityNode = required(use, "visibility", usePath);
                                                           const auto visibility = visibility_value(string_value(visibilityNode, usePath + ".visibility"), usePath + ".visibility", &visibilityNode);
                                                           return PackageTargetUse{visibility, required_string(use, "package", usePath), required_string(use, "target", usePath)};
                                                         });
      return target;
    }

    NativeProject native_project(const Table &root)
    {
      strict_keys(root, {"format", "project", "packages", "targets", "tests"}, "");
      NativeProject project;
      project.format = format_value(root);
      const auto &projectNode = required(root, "project", "");
      const auto &projectTable = as_table(projectNode, "project");
      strict_keys(projectTable, {"name", "cxx-standard"}, "project");
      project.name = required_string(projectTable, "name", "project");
      if (const auto standard = optional_string(projectTable, "cxx-standard", "project"))
        project.cxxStandard = standard_value(*standard, "project.cxx-standard", projectTable.get("cxx-standard"));
      project.packages = requirements(root, "");
      project.targets = table_array<NativeTarget>(root, "targets", "", true, target_value);
      project.tests = table_array<TestRegistration>(root, "tests", "", false,
                                                    [](const Table &table, const std::string &path)
                                                    {
                                                      strict_keys(table, {"name", "target"}, path);
                                                      return TestRegistration{required_string(table, "name", path), required_string(table, "target", path)};
                                                    });
      return project;
    }

    PackageContract package_contract(const Table &root)
    {
      strict_keys(root, {"format", "package", "packages", "integration", "exports"}, "");
      PackageContract contract;
      contract.format = format_value(root);
      const auto &identityNode = required(root, "package", "");
      const auto &identity = as_table(identityNode, "package");
      const auto sourceNode = &required(identity, "source", "package");
      const auto source = string_value(*sourceNode, "package.source");
      if (source == "registry")
      {
        strict_keys(identity, {"source", "registry", "publisher", "name", "version"}, "package");
        RegistryPackageIdentity packageIdentity{required_string(identity, "registry", "package"),
                                                required_string(identity, "publisher", "package"), required_string(identity, "name", "package")};
        const auto &versionNode = required(identity, "version", "package");
        contract.identity = RegistryPackageContractIdentity{std::move(packageIdentity), version_value(versionNode, "package.version")};
      }
      else if (source == "git")
      {
        strict_keys(identity, {"source"}, "package");
        contract.identity = GitPackageContractIdentity{};
      }
      else
        fail(ParseErrorCode::invalid_value, "package.source", "source must be registry or git", sourceNode);

      contract.packages = requirements(root, "");
      const auto &integrationNode = required(root, "integration", "");
      const auto &integration = as_table(integrationNode, "integration");
      const auto typeNode = &required(integration, "type", "integration");
      const auto type = string_value(*typeNode, "integration.type");
      if (type == "native-vix")
      {
        strict_keys(integration, {"type", "include-roots"}, "integration");
        const auto &rootsNode = required(integration, "include-roots", "integration");
        contract.integration = NativeVixIntegration{string_array(rootsNode, "integration.include-roots")};
        contract.exports = table_array<PackageExport>(root, "exports", "", true,
                                                      [](const Table &table, const std::string &path)
                                                      {
                                                        strict_keys(table, {"name", "project-target"}, path);
                                                        return PackageExport{required_string(table, "name", path), NativeExportMapping{required_string(table, "project-target", path)}};
                                                      });
      }
      else if (type == "cmake-config")
      {
        strict_keys(integration, {"type", "config-name"}, "integration");
        const auto configName = required_string(integration, "config-name", "integration");
        contract.integration = CMakeConfigIntegration{configName};
        contract.exports = table_array<PackageExport>(root, "exports", "", true,
                                                      [](const Table &table, const std::string &path)
                                                      {
                                                        strict_keys(table, {"name", "cmake-target"}, path);
                                                        return PackageExport{required_string(table, "name", path), CMakeExportMapping{required_string(table, "cmake-target", path)}};
                                                      });
      }
      else
        fail(ParseErrorCode::invalid_value, "integration.type", "type must be native-vix or cmake-config", typeNode);
      return contract;
    }

    std::variant<NormalizedDeclaredRegistry, NormalizedDeclaredGit> declared_value(const Node &node, const std::string &path)
    {
      const auto &table = as_table(node, path);
      const auto sourceNode = &required(table, "source", path);
      const auto source = string_value(*sourceNode, path + ".source");
      if (source == "registry")
      {
        strict_keys(table, {"source", "registry", "publisher", "name", "exact", "compatible"}, path);
        RegistryPackageIdentity identity{required_string(table, "registry", path), required_string(table, "publisher", path), required_string(table, "name", path)};
        const auto mode = mode_key(table, {"exact", "compatible"}, path, node);
        auto version = version_value(required(table, mode, path), path + "." + mode);
        if (mode == "exact")
          return NormalizedDeclaredRegistry{std::move(identity), ExactVersion{std::move(version)}};
        return NormalizedDeclaredRegistry{std::move(identity), CompatibleVersion{std::move(version)}};
      }
      if (source == "git")
      {
        strict_keys(table, {"source", "repository", "commit", "tag", "branch"}, path);
        // As above, schema extraction does not normalize source locators.
        auto repository = required_string(table, "repository", path);
        const auto mode = mode_key(table, {"commit", "tag", "branch"}, path, node);
        auto value = required_string(table, mode, path);
        GitRevisionRequirement revision = mode == "commit" ? GitRevisionRequirement{CommitRevision{std::move(value)}} : mode == "tag" ? GitRevisionRequirement{TagRevision{std::move(value)}}
                                                                                                                                      : GitRevisionRequirement{BranchRevision{std::move(value)}};
        return NormalizedDeclaredGit{std::move(repository), true, std::move(revision)};
      }
      fail(ParseErrorCode::invalid_value, path + ".source", "source must be registry or git", sourceNode);
    }

    std::vector<LockDependency> dependencies(const Table &table, const std::string &path)
    {
      const auto *node = table.get("dependencies");
      if (!node)
        return {};
      const auto *array = node->as_array();
      if (!array || !array->is_array_of_tables())
        fail(ParseErrorCode::invalid_discriminated_shape, path + ".dependencies", "dependencies must be omitted when empty and otherwise use nested array-of-tables", node);
      std::vector<LockDependency> result;
      std::size_t index = 0;
      for (const auto &entry : *array)
      {
        const auto itemPath = path + ".dependencies[" + std::to_string(index++) + "]";
        const auto &dependency = as_table(entry, itemPath);
        strict_keys(dependency, {"alias", "node"}, itemPath);
        result.push_back({required_string(dependency, "alias", itemPath), required_string(dependency, "node", itemPath)});
      }
      return result;
    }

    Lockfile lockfile(const Table &root)
    {
      strict_keys(root, {"format", "root-requirements-digest", "roots", "nodes"}, "");
      Lockfile lock;
      lock.format = format_value(root);
      lock.roots = table_array<LockRoot>(root, "roots", "", true,
                                         [](const Table &table, const std::string &path)
                                         {
                                           strict_keys(table, {"alias", "declared", "node"}, path);
                                           const auto &declared = required(table, "declared", path);
                                           return LockRoot{required_string(table, "alias", path), declared_value(declared, path + ".declared"), required_string(table, "node", path)};
                                         });
      lock.nodes = table_array<LockNode>(root, "nodes", "", true,
                                         [](const Table &table, const std::string &path) -> LockNode
                                         {
                                           const auto sourceNode = &required(table, "source", path);
                                           const auto source = string_value(*sourceNode, path + ".source");
                                           if (source == "registry")
                                           {
                                             strict_keys(table, {"id", "source", "registry", "publisher", "name", "version", "digest", "dependencies"}, path);
                                             RegistryLockNode node;
                                             node.id = required_string(table, "id", path);
                                             node.identity = {required_string(table, "registry", path), required_string(table, "publisher", path), required_string(table, "name", path)};
                                             node.version = version_value(required(table, "version", path), path + ".version");
                                             node.contentDigest = required_string(table, "digest", path);
                                             node.dependencies = dependencies(table, path);
                                             return node;
                                           }
                                           if (source == "git")
                                           {
                                             strict_keys(table, {"id", "source", "repository", "commit", "digest", "dependencies"}, path);
                                             GitLockNode node;
                                             node.id = required_string(table, "id", path);
                                             node.normalizedRepository = required_string(table, "repository", path);
                                             node.commit = required_string(table, "commit", path);
                                             node.contentDigest = required_string(table, "digest", path);
                                             node.dependencies = dependencies(table, path);
                                             return node;
                                           }
                                           fail(ParseErrorCode::invalid_value, path + ".source", "source must be registry or git", sourceNode);
                                         });
      // Validate nested tables before reporting absent document metadata. This
      // preserves precise diagnostics when root metadata was accidentally
      // placed inside an array-of-tables entry.
      lock.rootRequirementsDigest = required_string(root, "root-requirements-digest", "");
      return lock;
    }

    template <typename T, typename Convert>
    ParseResult<T> parse_text(std::string_view text, std::string_view source, Convert convert)
    {
      try
      {
        auto document = toml::parse(text, source);
        return ParseResult<T>{convert(document), {}};
      }
      catch (const Failure &failure)
      {
        return ParseResult<T>{std::nullopt, {failure.issue}};
      }
      catch (const toml::parse_error &error)
      {
        return ParseResult<T>{std::nullopt, {issue(ParseErrorCode::toml_syntax_error, "", std::string{error.description()}, &error.source())}};
      }
      catch (const std::exception &error)
      {
        return ParseResult<T>{std::nullopt, {issue(ParseErrorCode::toml_syntax_error, "", error.what())}};
      }
    }

    template <typename T, typename Parse>
    ParseResult<T> parse_file(const std::filesystem::path &path, Parse parse)
    {
      std::ifstream input(path, std::ios::binary);
      if (!input)
        return {std::nullopt, {issue(ParseErrorCode::io_error, "", "unable to open input file: " + path.string())}};
      std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
      return parse(text, path.string());
    }
  } // namespace

  std::string_view to_string(ParseErrorCode code) noexcept
  {
    switch (code)
    {
    case ParseErrorCode::toml_syntax_error:
      return "toml_syntax_error";
    case ParseErrorCode::unsupported_format:
      return "unsupported_format";
    case ParseErrorCode::missing_field:
      return "missing_field";
    case ParseErrorCode::unknown_field:
      return "unknown_field";
    case ParseErrorCode::wrong_type:
      return "wrong_type";
    case ParseErrorCode::invalid_value:
      return "invalid_value";
    case ParseErrorCode::invalid_discriminated_shape:
      return "invalid_discriminated_shape";
    case ParseErrorCode::io_error:
      return "io_error";
    }
    return "invalid_value";
  }

  ParseResult<NativeProject> parse_native_manifest_text(std::string_view text, std::string_view source)
  {
    return parse_text<NativeProject>(text, source, native_project);
  }
  ParseResult<NativeProject> parse_native_manifest_file(const std::filesystem::path &path)
  {
    return parse_file<NativeProject>(path, parse_native_manifest_text);
  }

  ParseResult<PackageContract> parse_package_contract_text(std::string_view text, std::string_view source)
  {
    return parse_text<PackageContract>(text, source, package_contract);
  }
  ParseResult<PackageContract> parse_package_contract_file(const std::filesystem::path &path)
  {
    return parse_file<PackageContract>(path, parse_package_contract_text);
  }

  ParseResult<Lockfile> parse_lockfile_text(std::string_view text, std::string_view source)
  {
    return parse_text<Lockfile>(text, source, lockfile);
  }
  ParseResult<Lockfile> parse_lockfile_file(const std::filesystem::path &path)
  {
    return parse_file<Lockfile>(path, parse_lockfile_text);
  }
} // namespace vix::cli::project::native
