/** @file TomlParser.hpp
 *  Strict TOML format-1 readers for native project/package artifacts.
 *  This interface intentionally contains no third-party TOML types.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_TOML_PARSER_HPP
#define VIX_CLI_PROJECT_NATIVE_TOML_PARSER_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/project/native/Models.hpp>
#include <vix/cli/project/native/Validation.hpp>

namespace vix::cli::project::native
{
  enum class ParseErrorCode
  {
    toml_syntax_error,
    unsupported_format,
    missing_field,
    unknown_field,
    wrong_type,
    invalid_value,
    invalid_discriminated_shape,
    io_error
  };

  struct ParseIssue
  {
    ParseErrorCode code{ParseErrorCode::invalid_value};
    std::string fieldPath;
    std::string message;
    std::optional<ValidationLocation> location;
  };

  enum class NativeProjectMutationErrorCode
  {
    invalid_project,
    duplicate_requirement_alias,
    requirement_not_found
  };

  struct NativeProjectMutationIssue
  {
    NativeProjectMutationErrorCode code{NativeProjectMutationErrorCode::invalid_project};
    std::string subject;
    std::string message;
  };

  struct NativeProjectMutationResult
  {
    std::vector<NativeProjectMutationIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return issues.empty(); }
  };

  struct NativeManifestSerializationResult
  {
    std::optional<std::string> text;
    std::vector<ValidationIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return text.has_value() && issues.empty(); }
  };

  struct NativeManifestWriteResult
  {
    bool written{false};
    std::string diagnostic;
    [[nodiscard]] bool valid() const noexcept { return written && diagnostic.empty(); }
  };

  template <typename T>
  struct ParseResult
  {
    std::optional<T> value;
    std::vector<ParseIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return value.has_value() && issues.empty(); }
  };

  [[nodiscard]] ParseResult<NativeProject> parse_native_manifest_text(
      std::string_view text, std::string_view source = "<text>");
  [[nodiscard]] ParseResult<NativeProject> parse_native_manifest_file(const std::filesystem::path &path);

  // CLI spelling follows the native requirement discriminator names:
  // "exact=1.2.3" or "compatible=1.2.3". The semver value is parsed by the
  // same strict semantic parser used by vix.toml.
  [[nodiscard]] ParseResult<VersionRequirement> parse_version_requirement_constraint(
    std::string_view constraint, std::string_view source = "<constraint>");

  [[nodiscard]] NativeManifestSerializationResult serialize_native_manifest(const NativeProject &project);
  [[nodiscard]] NativeProjectMutationResult add_root_package_requirement(
    NativeProject &project, PackageRequirement requirement);
  [[nodiscard]] NativeProjectMutationResult remove_root_package_requirement(
    NativeProject &project, std::string_view alias);
  [[nodiscard]] NativeProjectMutationResult replace_root_package_requirement(
    NativeProject &project, std::string_view alias, PackageRequirement replacement);
  [[nodiscard]] NativeManifestWriteResult write_native_manifest_atomic(
    const std::filesystem::path &path, const NativeProject &project);

  [[nodiscard]] ParseResult<PackageContract> parse_package_contract_text(
      std::string_view text, std::string_view source = "<text>");
  [[nodiscard]] ParseResult<PackageContract> parse_package_contract_file(const std::filesystem::path &path);

  [[nodiscard]] ParseResult<Lockfile> parse_lockfile_text(
      std::string_view text, std::string_view source = "<text>");
  [[nodiscard]] ParseResult<Lockfile> parse_lockfile_file(const std::filesystem::path &path);

  // Semantic validation remains explicit and is performed by the P10A APIs:
  // validate_native_project, validate_package_contract, and validate_lockfile.
  [[nodiscard]] std::string_view to_string(ParseErrorCode code) noexcept;
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_TOML_PARSER_HPP
