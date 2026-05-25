#pragma once

/**
 * @file BackendManifestTemplates.hpp
 * @author Gaspard Kirira
 *
 * Manifest file-content templates for the production backend `vix new`
 * template.
 */

#include <string>

#include <vix/cli/commands/new/NewTypes.hpp>

namespace vix::commands::new_cmd::templates
{

  std::string make_project_manifest_backend(
      const std::string &projectName,
      const FeaturesSelection &features);

  std::string make_vix_json_backend(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
