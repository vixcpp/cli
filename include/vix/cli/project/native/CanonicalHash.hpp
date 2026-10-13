/** @file CanonicalHash.hpp
 *  Deterministic format-1 structural encoding and SHA-256 helpers.
 *  Callers must supply semantically normalized values.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_CANONICAL_HASH_HPP
#define VIX_CLI_PROJECT_NATIVE_CANONICAL_HASH_HPP

#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <vix/cli/project/native/Models.hpp>

namespace vix::cli::project::native
{
  struct RegistryNodeIdentity
  {
    RegistryPackageIdentity package;
    Version version;
  };

  struct GitNodeIdentity
  {
    std::string normalizedRepository;
    std::string commit;
  };

  struct NormalizedRootGitRequirement
  {
    std::string normalizedRepository;
    GitRevisionRequirement revision;
  };

  struct RootRequirementDigestEntry
  {
    std::string alias;
    std::variant<NormalizedDeclaredRegistry, NormalizedRootGitRequirement> declared;
  };

  [[nodiscard]] std::string canonical_registry_node_bytes(const RegistryNodeIdentity &identity);
  [[nodiscard]] std::string canonical_git_node_bytes(const GitNodeIdentity &identity);
  [[nodiscard]] std::string registry_node_id(const RegistryNodeIdentity &identity);
  [[nodiscard]] std::string git_node_id(const GitNodeIdentity &identity);

  [[nodiscard]] std::string canonical_root_requirements_bytes(
    const std::vector<RootRequirementDigestEntry> &requirements);
  [[nodiscard]] std::string root_requirements_digest(
    const std::vector<RootRequirementDigestEntry> &requirements);

  // Returns 64 lowercase hexadecimal digits without an algorithm prefix.
  [[nodiscard]] std::string sha256_hex(std::string_view bytes);
  // Returns the persistent format representation: sha256:<lowercase hex>.
  [[nodiscard]] std::string sha256_digest(std::string_view bytes);
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_CANONICAL_HASH_HPP
