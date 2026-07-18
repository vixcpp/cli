/**
 * @file RegistryCatalog.hpp
 * Shared local-first access to the Vix package registry catalog.
 */
#ifndef VIX_CLI_REGISTRY_REGISTRY_CATALOG_HPP
#define VIX_CLI_REGISTRY_REGISTRY_CATALOG_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vix::cli::registry
{
  struct CatalogMetadata
  {
    std::string source{"none"};
    std::string syncedAt;
    bool stale{true};
    bool syncing{false};
    std::string error;
    std::filesystem::path repositoryPath;
    std::filesystem::path indexPath;
  };

  struct SearchFilters
  {
    std::string query;
    std::string extensionHost;
    std::string capability;
    std::string packageType;
    std::string cellType;
    std::size_t page{1};
    std::size_t limit{20};
  };

  struct PackageSummary
  {
    std::string id;
    std::string namespaceName;
    std::string name;
    std::string version;
    std::string type;
    std::string description;
    std::string publisher;
    std::string repository;
    std::string icon;
    std::string iconUrl;
    std::string iconData;
    std::string extensionApi;
    std::string runtimeMode;
    std::string runtimeProtocol;
    bool featured{false};
    bool verified{false};
    int recommendationPriority{0};
    std::vector<std::string> categories;
    std::vector<std::string> capabilities;
    std::vector<std::string> cellTypes;
    int score{0};
    nlohmann::json raw = nlohmann::json::object();
  };

  struct SearchResult
  {
    bool ok{true};
    std::string error;
    SearchFilters filters;
    CatalogMetadata metadata;
    std::size_t total{0};
    std::vector<PackageSummary> items;
  };

  struct SyncResult
  {
    bool ok{false};
    int exitCode{0};
    std::string error;
    CatalogMetadata metadata;
  };

  class RegistryCatalog
  {
  public:
    explicit RegistryCatalog(std::filesystem::path repositoryPath = {});

    const std::filesystem::path &repository_path() const noexcept;
    std::filesystem::path index_path() const;

    CatalogMetadata metadata() const;
    std::vector<PackageSummary> load_cached_catalog(std::string *error = nullptr) const;
    SearchResult search_packages(const SearchFilters &filters) const;
    SearchResult list_note_extensions(std::size_t limit = 100) const;
    SearchResult recommended_note_extensions(std::size_t limit = 8) const;
    SyncResult sync_catalog() const;

    static std::filesystem::path default_repository_path();
    static std::filesystem::path default_index_path();
  private:
    std::filesystem::path repositoryPath_;
  };

  std::string package_summary_id(const PackageSummary &package);
  nlohmann::json package_summary_json(const PackageSummary &package);
  nlohmann::json catalog_metadata_json(const CatalogMetadata &metadata);
}

#endif
