/**
 * @file CMakeFileApi.cpp
 * @brief Bounded CMake File API reader; CMake remains the metadata authority.
 */
#include <vix/cli/project/cmake/CMakeFileApi.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace vix::cli::project::cmake
{
  namespace
  {
    using json = nlohmann::json;

    [[nodiscard]] Path normalize(const Path &path)
    {
      std::error_code error;
      Path absolute = std::filesystem::absolute(path, error);
      return (error ? path : absolute).lexically_normal();
    }

    [[nodiscard]] std::optional<json> read_json(const Path &path)
    {
      std::ifstream input(path, std::ios::binary);
      if (!input)
        return std::nullopt;

      return json::parse(input, nullptr, false, true);
    }

    [[nodiscard]] const json *object_member(const json &value, const char *name)
    {
      if (!value.is_object())
        return nullptr;
      const auto found = value.find(name);
      return found == value.end() ? nullptr : &*found;
    }

    [[nodiscard]] const json *string_member(const json &value, const char *name)
    {
      const json *member = object_member(value, name);
      return member != nullptr && member->is_string() ? member : nullptr;
    }

    [[nodiscard]] std::optional<std::string> string_value(const json &value, const char *name)
    {
      const json *member = string_member(value, name);
      return member == nullptr ? std::nullopt : std::optional<std::string>{member->get<std::string>()};
    }

    [[nodiscard]] bool bool_value(const json &value, const char *name)
    {
      const json *member = object_member(value, name);
      return member != nullptr && member->is_boolean() && member->get<bool>();
    }

    [[nodiscard]] std::optional<Path> json_path(const json &value, const char *name)
    {
      const auto raw = string_value(value, name);
      return raw ? std::optional<Path>{Path{*raw}} : std::nullopt;
    }

    [[nodiscard]] std::optional<Path> current_index_path(const Path &replyRoot)
    {
      std::error_code error;
      if (!std::filesystem::is_directory(replyRoot, error) || error)
        return std::nullopt;

      struct Candidate { Path path; std::filesystem::file_time_type modified; };
      std::optional<Candidate> current;
      for (std::filesystem::directory_iterator it(replyRoot, error), end; !error && it != end; it.increment(error))
      {
        const Path path = it->path();
        const std::string name = path.filename().string();
        if (!it->is_regular_file(error) || error || name.rfind("index-", 0) != 0 || path.extension() != ".json")
          continue;
        const auto modified = std::filesystem::last_write_time(path, error);
        if (error)
          continue;
        if (!current || modified > current->modified ||
            (modified == current->modified && path.filename().string() > current->path.filename().string()))
          current = Candidate{path, modified};
      }
      return current ? std::optional<Path>{current->path} : std::nullopt;
    }

    [[nodiscard]] const json *client_codemodel_response(const json &index)
    {
      const json *reply = object_member(index, "reply");
      const json *client = reply == nullptr ? nullptr : object_member(*reply, "client-vix");
      return client == nullptr ? nullptr : object_member(*client, "codemodel-v2");
    }

    [[nodiscard]] bool codemodel_version(const json &value, int &major, int &minor)
    {
      const json *version = object_member(value, "version");
      if (version == nullptr || !version->is_object())
        return false;
      const json *majorValue = object_member(*version, "major");
      const json *minorValue = object_member(*version, "minor");
      if (majorValue == nullptr || minorValue == nullptr ||
          !majorValue->is_number_integer() || !minorValue->is_number_integer())
        return false;
      major = majorValue->get<int>();
      minor = minorValue->get<int>();
      return true;
    }

    [[nodiscard]] std::optional<CMakeFileApiTarget> parse_target(
        const Path &replyRoot,
        const json &targetReference,
        const Path &metadataBuildRoot)
    {
      const auto jsonFile = string_value(targetReference, "jsonFile");
      if (!jsonFile)
        return std::nullopt;
      const auto target = read_json(replyRoot / *jsonFile);
      if (!target || target->is_discarded() || !target->is_object())
        return std::nullopt;

      const auto id = string_value(*target, "id");
      const auto name = string_value(*target, "name");
      const auto type = string_value(*target, "type");
      if (!id || !name || !type)
        return std::nullopt;

      CMakeFileApiTarget parsed;
      parsed.id = *id;
      parsed.name = *name;
      if (const auto nameOnDisk = string_value(*target, "nameOnDisk"))
        parsed.nameOnDisk = *nameOnDisk;
      parsed.type = *type;
      parsed.generatorProvided = bool_value(*target, "isGeneratorProvided");
      parsed.imported = bool_value(*target, "imported");
      parsed.abstract = bool_value(*target, "abstract");

      const json *artifacts = object_member(*target, "artifacts");
      if (artifacts != nullptr)
      {
        if (!artifacts->is_array())
          return std::nullopt;
        for (const json &artifact : *artifacts)
        {
          const auto artifactPath = json_path(artifact, "path");
          if (!artifactPath)
            return std::nullopt;
          parsed.artifacts.push_back(
              artifactPath->is_absolute() ? artifactPath->lexically_normal()
                                          : (metadataBuildRoot / *artifactPath).lexically_normal());
        }
      }
      return parsed;
    }
  } // namespace

  CMakeFileApiResult prepare_cmake_file_api_query(const Path &buildRoot)
  {
    std::error_code error;
    const Path queryRoot = buildRoot / ".cmake" / "api" / "v1" / "query" / "client-vix";
    std::filesystem::create_directories(queryRoot, error);
    if (error)
      return {ProjectError::configured_metadata_unavailable};

    std::ofstream query(queryRoot / "codemodel-v2", std::ios::binary | std::ios::trunc);
    if (!query)
      return {ProjectError::configured_metadata_unavailable};
    return {};
  }

  CMakeFileApiCodemodelResult load_cmake_file_api_codemodel(const Path &buildRoot)
  {
    const Path normalizedBuildRoot = normalize(buildRoot);
    const Path replyRoot = normalizedBuildRoot / ".cmake" / "api" / "v1" / "reply";
    const auto indexPath = current_index_path(replyRoot);
    if (!indexPath)
      return {std::nullopt, ProjectError::configured_metadata_unavailable};

    const auto index = read_json(*indexPath);
    if (!index || index->is_discarded() || !index->is_object())
      return {std::nullopt, ProjectError::configured_metadata_unavailable};
    const json *response = client_codemodel_response(*index);
    const auto codemodelFile = response == nullptr ? std::nullopt : string_value(*response, "jsonFile");
    if (!codemodelFile)
      return {std::nullopt, ProjectError::configured_metadata_unavailable};

    int major = 0;
    int minor = 0;
    if (!codemodel_version(*response, major, minor) || major != 2)
      return {std::nullopt, ProjectError::configured_metadata_unavailable};

    const auto document = read_json(replyRoot / *codemodelFile);
    if (!document || document->is_discarded() || !document->is_object())
      return {std::nullopt, ProjectError::configured_metadata_unavailable};
    int documentMajor = 0;
    int documentMinor = 0;
    if (!codemodel_version(*document, documentMajor, documentMinor) || documentMajor != 2)
      return {std::nullopt, ProjectError::configured_metadata_unavailable};

    const json *paths = object_member(*document, "paths");
    const auto sourceRoot = paths == nullptr ? std::nullopt : json_path(*paths, "source");
    const auto metadataBuildRoot = paths == nullptr ? std::nullopt : json_path(*paths, "build");
    const json *configurations = object_member(*document, "configurations");
    if (!sourceRoot || !metadataBuildRoot || configurations == nullptr || !configurations->is_array())
      return {std::nullopt, ProjectError::configured_metadata_unavailable};

    CMakeFileApiCodemodel model;
    model.major = documentMajor;
    model.minor = documentMinor;
    model.sourceRoot = normalize(*sourceRoot);
    model.buildRoot = normalize(*metadataBuildRoot);
    if (model.buildRoot != normalizedBuildRoot)
      return {std::nullopt, ProjectError::configured_metadata_unavailable};

    for (const json &configuration : *configurations)
    {
      if (!configuration.is_object())
        return {std::nullopt, ProjectError::configured_metadata_unavailable};
      CMakeFileApiConfiguration parsed;
      const auto name = string_value(configuration, "name");
      if (!name)
        return {std::nullopt, ProjectError::configured_metadata_unavailable};
      parsed.name = *name;

      const json *projects = object_member(configuration, "projects");
      if (projects != nullptr && projects->is_array())
      {
        for (const json &candidate : *projects)
        {
          const auto candidateName = string_value(candidate, "name");
          const auto candidateSource = json_path(candidate, "sourceDirectory");
          if (candidateName && candidateSource && normalize(*candidateSource) == model.sourceRoot)
          {
            model.projectName = *candidateName;
            break;
          }
        }
        if (model.projectName.empty() && !projects->empty())
        {
          const auto candidateName = string_value((*projects)[0], "name");
          if (candidateName)
            model.projectName = *candidateName;
        }
      }

      const json *targets = object_member(configuration, "targets");
      if (targets == nullptr || !targets->is_array())
        return {std::nullopt, ProjectError::configured_metadata_unavailable};
      for (const json &targetReference : *targets)
      {
        const auto target = parse_target(replyRoot, targetReference, model.buildRoot);
        if (!target)
          return {std::nullopt, ProjectError::configured_metadata_unavailable};
        parsed.targets.push_back(*target);
      }
      model.configurations.push_back(std::move(parsed));
    }

    if (model.configurations.empty() || model.projectName.empty())
      return {std::nullopt, ProjectError::configured_metadata_unavailable};
    return {std::move(model), std::nullopt};
  }
} // namespace vix::cli::project::cmake
