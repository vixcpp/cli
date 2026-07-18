#include <vix/cli/registry/RegistryCatalog.hpp>

#include <vix/cli/util/Semver.hpp>
#include <vix/process/Process.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
#include <iomanip>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::cli::registry
{
  namespace
  {
    std::string to_lower(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    bool contains_icase(const std::string &hay, const std::string &needleLower)
    {
      return needleLower.empty() || to_lower(hay).find(needleLower) != std::string::npos;
    }

    std::string home_dir()
    {
#ifdef _WIN32
      const char *home = std::getenv("USERPROFILE");
#else
      const char *home = std::getenv("HOME");
#endif
      return home ? std::string(home) : std::string();
    }

    fs::path vix_root()
    {
      const std::string home = home_dir();
      return home.empty() ? fs::path(".vix") : fs::path(home) / ".vix";
    }

    json read_json_or_throw(const fs::path &path)
    {
      std::ifstream in(path);
      if (!in)
        throw std::runtime_error("cannot open: " + path.string());
      json value;
      in >> value;
      return value;
    }

    std::vector<std::string> json_string_array(const json &value)
    {
      std::vector<std::string> out;
      if (!value.is_array())
        return out;
      for (const auto &item : value)
        if (item.is_string())
          out.push_back(item.get<std::string>());
      return out;
    }

    std::string join_strings(const std::vector<std::string> &items)
    {
      std::string out;
      for (const auto &item : items)
      {
        if (!out.empty())
          out += " ";
        out += item;
      }
      return out;
    }

    std::string latest_version(const json &entry)
    {
      if (entry.contains("latest") && entry["latest"].is_string())
        return entry["latest"].get<std::string>();
      if (entry.contains("latestVersion") && entry["latestVersion"].is_string())
        return entry["latestVersion"].get<std::string>();
      if (!entry.contains("versions") || !entry["versions"].is_object())
        return {};
      std::vector<std::string> versions;
      for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
        versions.push_back(it.key());
      return vix::cli::util::semver::findLatest(versions);
    }

    const json *note_extension_json(const json &entry)
    {
      if (!entry.contains("extensions") || !entry["extensions"].is_object())
        return nullptr;
      if (!entry["extensions"].contains("note") || !entry["extensions"]["note"].is_object())
        return nullptr;
      return &entry["extensions"]["note"];
    }

    bool note_extension_compatible(const json &note)
    {
      return note.value("api", "") == "1";
    }

    bool safe_remote_icon(const std::string &value)
    {
      const std::string lower = to_lower(value);
      if (value.empty() || lower.find("javascript:") != std::string::npos || lower.rfind("file:", 0) == 0)
        return false;
      return lower.rfind("https://", 0) == 0 || lower.rfind("data:image/svg+xml", 0) == 0 || lower.rfind("data:image/png", 0) == 0 || lower.rfind("data:image/jpeg", 0) == 0 || lower.rfind("data:image/webp", 0) == 0;
    }


    bool safe_relative_icon_path(const std::string &value)
    {
      if (value.empty() || value.find('\0') != std::string::npos || value.find('\\') != std::string::npos)
        return false;
      fs::path path(value);
      if (path.is_absolute())
        return false;
      path = path.lexically_normal();
      for (const auto &part : path)
      {
        const std::string text = part.string();
        if (text.empty() || text == "..")
          return false;
      }
      const std::string ext = to_lower(path.extension().string());
      return ext == ".svg" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp";
    }

    std::optional<std::pair<std::string, std::string>> github_repo_owner_name(const std::string &repository)
    {
      std::string value = repository;
      const std::string https = "https://github.com/";
      const std::string git = "git@github.com:";
      if (value.rfind(https, 0) == 0)
        value.erase(0, https.size());
      else if (value.rfind(git, 0) == 0)
        value.erase(0, git.size());
      else
        return std::nullopt;
      if (value.ends_with(".git"))
        value.resize(value.size() - 4);
      const std::size_t slash = value.find('/');
      if (slash == std::string::npos || slash == 0 || slash + 1 >= value.size())
        return std::nullopt;
      std::string owner = value.substr(0, slash);
      std::string name = value.substr(slash + 1);
      if (name.find('/') != std::string::npos)
        name = name.substr(0, name.find('/'));
      return std::make_pair(owner, name);
    }

    std::string raw_github_icon_url(const std::string &repository, const std::string &ref, const std::string &iconPath)
    {
      if (!safe_relative_icon_path(iconPath) || ref.empty())
        return {};
      const auto repo = github_repo_owner_name(repository);
      if (!repo)
        return {};
      return "https://raw.githubusercontent.com/" + repo->first + "/" + repo->second + "/" + ref + "/" + fs::path(iconPath).lexically_normal().generic_string();
    }

    std::string version_ref(const json &entry, const std::string &version)
    {
      if (version.empty() || !entry.contains("versions") || !entry["versions"].is_object())
        return {};
      const auto it = entry["versions"].find(version);
      if (it == entry["versions"].end() || !it->is_object())
        return {};
      const std::string tag = it->value("tag", "");
      if (!tag.empty())
        return tag;
      return it->value("commit", "");
    }

    std::string entry_repository(const json &entry)
    {
      if (entry.contains("repo") && entry["repo"].is_object())
        return entry["repo"].value("url", "");
      if (entry.contains("repository") && entry["repository"].is_string())
        return entry["repository"].get<std::string>();
      return {};
    }

    PackageSummary make_summary(const json &entry, int score)
    {
      PackageSummary out;
      out.namespaceName = entry.value("namespace", "");
      out.name = entry.value("name", "");
      out.id = out.namespaceName + "/" + out.name;
      out.version = latest_version(entry);
      out.type = entry.value("type", "");
      out.description = entry.value("description", "");
      out.publisher = entry.value("publisher", out.namespaceName);
      out.repository = entry_repository(entry);
      out.featured = entry.value("featured", false);
      out.verified = entry.value("verified", false);
      out.recommendationPriority = entry.value("recommendationPriority", 0);
      out.categories = json_string_array(entry.value("categories", json::array()));
      out.score = score;
      out.raw = entry;

      if (const json *note = note_extension_json(entry))
      {
        out.extensionApi = note->value("api", "");
        out.capabilities = json_string_array(note->value("capabilities", json::array()));
        if (note->contains("runtime") && (*note)["runtime"].is_object())
        {
          out.runtimeMode = (*note)["runtime"].value("mode", "");
          out.runtimeProtocol = (*note)["runtime"].value("protocol", "");
        }
        if (note->contains("icon") && (*note)["icon"].is_string())
        {
          out.icon = (*note)["icon"].get<std::string>();
          if (safe_remote_icon(out.icon))
            out.iconData = out.icon;
        }
        if (note->contains("iconData") && (*note)["iconData"].is_string() && safe_remote_icon((*note)["iconData"].get<std::string>()))
          out.iconData = (*note)["iconData"].get<std::string>();
        if (note->contains("iconUrl") && (*note)["iconUrl"].is_string() && safe_remote_icon((*note)["iconUrl"].get<std::string>()))
          out.iconUrl = (*note)["iconUrl"].get<std::string>();
        if (note->contains("cellTypes") && (*note)["cellTypes"].is_array())
        {
          for (const auto &cell : (*note)["cellTypes"])
            if (cell.is_object() && cell.contains("id") && cell["id"].is_string())
              out.cellTypes.push_back(cell["id"].get<std::string>());
        }
      }
      if (out.icon.empty() && entry.contains("icon") && entry["icon"].is_string())
      {
        out.icon = entry["icon"].get<std::string>();
        if (safe_remote_icon(out.icon))
          out.iconData = out.icon;
      }
      if (out.iconData.empty() && entry.contains("iconData") && entry["iconData"].is_string() && safe_remote_icon(entry["iconData"].get<std::string>()))
        out.iconData = entry["iconData"].get<std::string>();
      if (out.iconUrl.empty() && entry.contains("iconUrl") && entry["iconUrl"].is_string() && safe_remote_icon(entry["iconUrl"].get<std::string>()))
        out.iconUrl = entry["iconUrl"].get<std::string>();
      if (out.iconUrl.empty() && out.iconData.empty() && safe_relative_icon_path(out.icon))
        out.iconUrl = raw_github_icon_url(out.repository, version_ref(entry, out.version), out.icon);
      return out;
    }

    std::string search_text(const json &entry, const PackageSummary &summary)
    {
      std::vector<std::string> parts = {
          summary.id,
          summary.namespaceName,
          summary.name,
          summary.description,
          summary.publisher,
          summary.type,
          join_strings(summary.capabilities),
          join_strings(summary.cellTypes),
          join_strings(summary.categories)};
      if (entry.contains("displayName") && entry["displayName"].is_string())
        parts.push_back(entry["displayName"].get<std::string>());
      if (entry.contains("keywords"))
        parts.push_back(join_strings(json_string_array(entry["keywords"])));
      return to_lower(join_strings(parts));
    }

    int score_entry(const json &entry, const SearchFilters &filters)
    {
      PackageSummary summary = make_summary(entry, 0);
      const std::string query = to_lower(filters.query);
      if (query.empty())
        return 1;
      int score = 0;
      if (contains_icase(summary.id, query))
        score += 100;
      if (contains_icase(summary.name, query))
        score += 60;
      if (contains_icase(summary.namespaceName, query))
        score += 40;
      if (contains_icase(summary.description, query))
        score += 20;
      if (contains_icase(join_strings(summary.capabilities), query))
        score += 15;
      if (contains_icase(join_strings(summary.cellTypes), query))
        score += 15;
      if (contains_icase(search_text(entry, summary), query))
        score += 5;
      return score;
    }

    bool matches_filters(const json &entry, const SearchFilters &filters)
    {
      if (!filters.packageType.empty() && to_lower(entry.value("type", "")) != to_lower(filters.packageType))
        return false;
      const bool wantsNote = filters.extensionHost.empty() || to_lower(filters.extensionHost) == "note";
      const json *note = note_extension_json(entry);
      if (!filters.extensionHost.empty() && (!wantsNote || note == nullptr))
        return false;
      if (note != nullptr && !note_extension_compatible(*note))
        return false;
      if (!filters.capability.empty())
      {
        if (note == nullptr)
          return false;
        const auto caps = json_string_array(note->value("capabilities", json::array()));
        if (std::find(caps.begin(), caps.end(), filters.capability) == caps.end())
          return false;
      }
      if (!filters.cellType.empty())
      {
        if (note == nullptr)
          return false;
        bool found = false;
        if (note->contains("cellTypes") && (*note)["cellTypes"].is_array())
        {
          for (const auto &cell : (*note)["cellTypes"])
          {
            if (cell.is_object() && to_lower(cell.value("id", "")) == to_lower(filters.cellType))
              found = true;
          }
        }
        if (!found)
          return false;
      }
      return true;
    }

    std::string iso_from_file_time(fs::file_time_type time)
    {
      using namespace std::chrono;
      const auto systemTime = time_point_cast<system_clock::duration>(time - fs::file_time_type::clock::now() + system_clock::now());
      const std::time_t tt = system_clock::to_time_t(systemTime);
      std::tm tm{};
#ifdef _WIN32
      gmtime_s(&tm, &tt);
#else
      gmtime_r(&tt, &tm);
#endif
      char buffer[32]{};
      std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
      return buffer;
    }

    bool is_stale(fs::file_time_type time)
    {
      const auto age = fs::file_time_type::clock::now() - time;
      return age > std::chrono::hours(6);
    }

    vix::process::ProcessOutput run_git(std::vector<std::string> args, const fs::path &cwd = {})
    {
      vix::process::Command command("git");
      command.args(std::move(args));
      command.search_in_path(true);
      command.stdout_mode(vix::process::PipeMode::Pipe);
      command.stderr_mode(vix::process::PipeMode::Pipe);
      if (!cwd.empty())
        command.cwd(cwd.string());
      auto result = vix::process::output(std::move(command));
      if (!result)
      {
        vix::process::ProcessOutput out;
        out.exit_code = 127;
        out.stderr_text = result.error().message();
        return out;
      }
      return result.value();
    }
  }

  RegistryCatalog::RegistryCatalog(fs::path repositoryPath)
      : repositoryPath_(repositoryPath.empty() ? default_repository_path() : std::move(repositoryPath)) {}

  const fs::path &RegistryCatalog::repository_path() const noexcept { return repositoryPath_; }
  fs::path RegistryCatalog::index_path() const { return repositoryPath_ / "index"; }
  fs::path RegistryCatalog::default_repository_path() { return vix_root() / "registry" / "index"; }
  fs::path RegistryCatalog::default_index_path() { return default_repository_path() / "index"; }

  CatalogMetadata RegistryCatalog::metadata() const
  {
    CatalogMetadata meta;
    meta.repositoryPath = repositoryPath_;
    meta.indexPath = index_path();
    std::error_code ec;
    if (!fs::exists(repositoryPath_, ec) || !fs::exists(index_path(), ec))
    {
      meta.source = "none";
      meta.error = "registry catalog is not synced";
      return meta;
    }
    meta.source = "cache";
    auto newest = fs::last_write_time(repositoryPath_, ec);
    for (const auto &entry : fs::directory_iterator(index_path(), ec))
    {
      if (ec)
        break;
      if (!entry.is_regular_file() || entry.path().extension() != ".json")
        continue;
      std::error_code fileEc;
      newest = std::max(newest, entry.last_write_time(fileEc));
    }
    meta.syncedAt = iso_from_file_time(newest);
    meta.stale = is_stale(newest);
    return meta;
  }

  std::vector<PackageSummary> RegistryCatalog::load_cached_catalog(std::string *error) const
  {
    std::vector<PackageSummary> out;
    const fs::path dir = index_path();
    std::error_code ec;
    if (!fs::exists(dir, ec))
    {
      if (error)
        *error = "registry catalog is not synced";
      return out;
    }
    for (const auto &item : fs::directory_iterator(dir, ec))
    {
      if (ec)
        break;
      if (!item.is_regular_file() || item.path().extension() != ".json")
        continue;
      try
      {
        const json entry = read_json_or_throw(item.path());
        if (!entry.is_object() || !entry.contains("namespace") || !entry.contains("name"))
          continue;
        out.push_back(make_summary(entry, 0));
      }
      catch (...)
      {
      }
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
  }

  SearchResult RegistryCatalog::search_packages(const SearchFilters &filters) const
  {
    SearchResult result;
    result.filters = filters;
    result.metadata = metadata();
    if (result.metadata.source == "none")
    {
      result.ok = false;
      result.error = result.metadata.error;
      return result;
    }
    const fs::path dir = index_path();
    std::error_code ec;
    std::vector<PackageSummary> hits;
    for (const auto &item : fs::directory_iterator(dir, ec))
    {
      if (ec)
        break;
      if (!item.is_regular_file() || item.path().extension() != ".json")
        continue;
      try
      {
        const json entry = read_json_or_throw(item.path());
        if (!matches_filters(entry, filters))
          continue;
        const int score = score_entry(entry, filters);
        if (score <= 0)
          continue;
        hits.push_back(make_summary(entry, score));
      }
      catch (...)
      {
      }
    }
    std::sort(hits.begin(), hits.end(), [](const auto &a, const auto &b) {
      if (a.score != b.score)
        return a.score > b.score;
      return a.id < b.id;
    });
    result.total = hits.size();
    const std::size_t page = std::max<std::size_t>(1, filters.page);
    const std::size_t limit = std::clamp<std::size_t>(filters.limit == 0 ? 20 : filters.limit, 1, 100);
    const std::size_t start = hits.empty() ? 0 : std::min((page - 1) * limit, hits.size());
    const std::size_t end = std::min(start + limit, hits.size());
    result.items.assign(hits.begin() + static_cast<std::ptrdiff_t>(start), hits.begin() + static_cast<std::ptrdiff_t>(end));
    return result;
  }

  SearchResult RegistryCatalog::list_note_extensions(std::size_t limit) const
  {
    SearchFilters filters;
    filters.extensionHost = "note";
    filters.limit = limit;
    return search_packages(filters);
  }

  SearchResult RegistryCatalog::recommended_note_extensions(std::size_t limit) const
  {
    SearchResult result = list_note_extensions(1000);
    if (!result.ok)
      return result;
    std::sort(result.items.begin(), result.items.end(), [](const auto &a, const auto &b) {
      if (a.featured != b.featured)
        return a.featured > b.featured;
      if (a.verified != b.verified)
        return a.verified > b.verified;
      if (a.recommendationPriority != b.recommendationPriority)
        return a.recommendationPriority > b.recommendationPriority;
      if (a.version != b.version)
        return vix::cli::util::semver::compare(a.version, b.version) > 0;
      return a.id < b.id;
    });
    if (result.items.size() > limit)
      result.items.resize(limit);
    result.total = result.items.size();
    return result;
  }

  SyncResult RegistryCatalog::sync_catalog() const
  {
    SyncResult result;
    fs::create_directories(repositoryPath_.parent_path());
    vix::process::ProcessOutput out;
    if (!fs::exists(repositoryPath_ / ".git"))
    {
      out = run_git({"clone", "--depth", "1", "https://github.com/vixcpp/registry.git", repositoryPath_.string()});
      if (out.exit_code != 0)
      {
        result.exitCode = out.exit_code;
        result.error = out.stderr_text.empty() ? out.stdout_text : out.stderr_text;
        result.metadata = metadata();
        return result;
      }
    }
    out = run_git({"fetch", "-q", "origin", "--prune"}, repositoryPath_);
    if (out.exit_code == 0)
      out = run_git({"checkout", "-q", "-B", "main", "origin/main"}, repositoryPath_);
    if (out.exit_code == 0)
      out = run_git({"reset", "-q", "--hard", "origin/main"}, repositoryPath_);
    result.exitCode = out.exit_code;
    result.ok = out.exit_code == 0;
    if (!result.ok)
      result.error = out.stderr_text.empty() ? out.stdout_text : out.stderr_text;
    result.metadata = metadata();
    if (result.ok)
      result.metadata.source = "network";
    else if (result.metadata.source == "cache")
      result.metadata.error = result.error;
    return result;
  }

  std::string package_summary_id(const PackageSummary &package) { return package.id; }

  json package_summary_json(const PackageSummary &p)
  {
    return json{{"id", p.id}, {"namespace", p.namespaceName}, {"name", p.name}, {"version", p.version}, {"type", p.type}, {"description", p.description}, {"publisher", p.publisher}, {"repository", p.repository}, {"icon", p.icon}, {"iconUrl", p.iconUrl}, {"iconData", p.iconData}, {"extensionApi", p.extensionApi}, {"runtime", {{"mode", p.runtimeMode}, {"protocol", p.runtimeProtocol}}}, {"featured", p.featured}, {"verified", p.verified}, {"recommendationPriority", p.recommendationPriority}, {"categories", p.categories}, {"capabilities", p.capabilities}, {"cellTypes", p.cellTypes}, {"source", "registry"}};
  }

  json catalog_metadata_json(const CatalogMetadata &m)
  {
    return json{{"source", m.source}, {"syncedAt", m.syncedAt.empty() ? json(nullptr) : json(m.syncedAt)}, {"stale", m.stale}, {"syncing", m.syncing}, {"error", m.error}, {"path", m.repositoryPath.string()}};
  }
}
