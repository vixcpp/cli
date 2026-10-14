/** @file RegistryContentLayout.hpp
 *  Registry format-1 immutable content-object addressing.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_REGISTRY_CONTENT_LAYOUT_HPP
#define VIX_CLI_PROJECT_NATIVE_REGISTRY_CONTENT_LAYOUT_HPP

#include <string>
#include <string_view>

#include <vix/cli/project/native/PackageContentStore.hpp>

namespace vix::cli::project::native
{
  // Registry mirror layout is independent from P10P's local filesystem layout.
  // PackageContentId can only be created after validated SHA-256 syntax, so the
  // resulting forward-slash path is safe and digest-derived.
  [[nodiscard]] inline std::string registry_format1_content_object_path(
    const PackageContentId &content)
  {
    const std::string &digest = content.digest();
    const std::string_view hex = std::string_view{digest}.substr(7);
    return "objects/sha256/" + std::string{hex.substr(0, 2)} + "/" +
      std::string{hex.substr(2)};
  }
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_REGISTRY_CONTENT_LAYOUT_HPP
