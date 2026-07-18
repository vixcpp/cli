/**
 *
 *  @file SearchCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/SearchCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/util/Semver.hpp>
#include <vix/utils/Env.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
    std::string to_lower(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    bool contains_icase(const std::string &hay, const std::string &needleLower)
    {
      if (needleLower.empty())
        return true;
      return to_lower(hay).find(needleLower) != std::string::npos;
    }

    std::string home_dir()
    {
#ifdef _WIN32
      const char *home = vix::utils::vix_getenv("USERPROFILE");
#else
      const char *home = vix::utils::vix_getenv("HOME");
#endif
      return home ? std::string(home) : std::string();
    }

    fs::path vix_root()
    {
      const std::string h = home_dir();
      if (h.empty())
        return fs::path(".vix");
      return fs::path(h) / ".vix";
    }

    fs::path registry_repo_dir()
    {
      return vix_root() / "registry" / "index";
    }

    fs::path registry_index_dir()
    {
      return registry_repo_dir() / "index";
    }

    bool registry_ready(const fs::path &repoDir, const fs::path &idxDir)
    {
      return fs::exists(repoDir) && fs::exists(idxDir);
    }

    json read_json_or_throw(const fs::path &p)
    {
      std::ifstream in(p);
      if (!in)
        throw std::runtime_error("cannot open: " + p.string());
      json j;
      in >> j;
      return j;
    }

    std::string join_keywords(const json &entry)
    {
      if (!entry.contains("keywords") || !entry["keywords"].is_array())
        return {};

      std::string out;
      for (const auto &k : entry["keywords"])
      {
        if (!k.is_string())
          continue;
        if (!out.empty())
          out += ", ";
        out += k.get<std::string>();
      }
      return out;
    }

    static std::string latest_version(const json &entry)
    {
      if (entry.contains("latest") && entry["latest"].is_string())
        return entry["latest"].get<std::string>();

      if (!entry.contains("versions") || !entry["versions"].is_object())
        return {};

      std::vector<std::string> versions;
      versions.reserve(entry["versions"].size());

      for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
        versions.push_back(it.key());

      return vix::cli::util::semver::findLatest(versions);
    }

    struct Hit
    {
      std::string id;
      std::string desc;
      std::string repo;
      std::string latest;
      std::string type;
      std::string extension;
      std::vector<std::string> capabilities;
      std::vector<std::string> cellTypes;
      int score{};
    };

    struct SearchOptions
    {
      std::string query;
      std::string extensionHost;
      std::string capability;
      std::string packageType;
      bool jsonOutput{false};
      std::size_t page = 1;
      std::size_t limit = 20;
    };

    bool parse_positive_size(const std::string &s, std::size_t &out)
    {
      if (s.empty())
        return false;
      std::size_t value = 0;
      for (char c : s)
      {
        if (c < '0' || c > '9')
          return false;
        value = (value * 10u) + static_cast<std::size_t>(c - '0');
      }
      if (value == 0)
        return false;
      out = value;
      return true;
    }

    bool has_filters(const SearchOptions &opt)
    {
      return !opt.extensionHost.empty() || !opt.capability.empty() || !opt.packageType.empty();
    }

    bool parse_search_args(const std::vector<std::string> &args, SearchOptions &opt)
    {
      bool querySet = false;
      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &arg = args[i];
        auto need_value = [&](std::string &target) -> bool
        {
          if (i + 1 >= args.size())
            return false;
          target = to_lower(args[++i]);
          return !target.empty();
        };

        if (arg == "--json")
        {
          opt.jsonOutput = true;
          continue;
        }
        if (arg == "--page")
        {
          std::size_t v = 0;
          if (i + 1 >= args.size() || !parse_positive_size(args[++i], v))
            return false;
          opt.page = v;
          continue;
        }
        if (arg == "--limit")
        {
          std::size_t v = 0;
          if (i + 1 >= args.size() || !parse_positive_size(args[++i], v))
            return false;
          opt.limit = std::clamp<std::size_t>(v, 1, 100);
          continue;
        }
        if (arg == "--extension")
        {
          if (!need_value(opt.extensionHost))
            return false;
          continue;
        }
        if (arg == "--capability")
        {
          if (!need_value(opt.capability))
            return false;
          continue;
        }
        if (arg == "--type")
        {
          if (!need_value(opt.packageType))
            return false;
          continue;
        }
        if (arg.rfind("--page=", 0) == 0)
        {
          std::size_t v = 0;
          if (!parse_positive_size(arg.substr(7), v))
            return false;
          opt.page = v;
          continue;
        }
        if (arg.rfind("--limit=", 0) == 0)
        {
          std::size_t v = 0;
          if (!parse_positive_size(arg.substr(8), v))
            return false;
          opt.limit = std::clamp<std::size_t>(v, 1, 100);
          continue;
        }
        if (arg.rfind("--extension=", 0) == 0)
        {
          opt.extensionHost = to_lower(arg.substr(12));
          continue;
        }
        if (arg.rfind("--capability=", 0) == 0)
        {
          opt.capability = to_lower(arg.substr(13));
          continue;
        }
        if (arg.rfind("--type=", 0) == 0)
        {
          opt.packageType = to_lower(arg.substr(7));
          continue;
        }
        if (!querySet)
        {
          opt.query = arg;
          querySet = true;
          continue;
        }
        opt.query += " ";
        opt.query += arg;
      }
      return !opt.query.empty() || has_filters(opt);
    }

    std::vector<std::string> json_string_array(const json &j)
    {
      std::vector<std::string> out;
      if (!j.is_array())
        return out;
      for (const auto &item : j)
        if (item.is_string())
          out.push_back(item.get<std::string>());
      return out;
    }

    const json *extension_json(const json &entry, const std::string &host)
    {
      if (host.empty())
        return nullptr;
      if (!entry.contains("extensions") || !entry["extensions"].is_object())
        return nullptr;
      if (!entry["extensions"].contains(host) || !entry["extensions"][host].is_object())
        return nullptr;
      return &entry["extensions"][host];
    }

    bool matches_filters(const json &entry, const SearchOptions &opt)
    {
      if (!opt.packageType.empty() && to_lower(entry.value("type", "")) != opt.packageType)
        return false;
      const json *ext = extension_json(entry, opt.extensionHost);
      if (!opt.extensionHost.empty() && ext == nullptr)
        return false;
      if (!opt.capability.empty())
      {
        if (ext == nullptr)
          return false;
        const auto caps = json_string_array(ext->value("capabilities", json::array()));
        if (std::find(caps.begin(), caps.end(), opt.capability) == caps.end())
          return false;
      }
      return true;
    }

    int score_entry(const json &e, const std::string &qLower)
    {
      if (qLower.empty())
        return 1;
      const std::string ns = e.value("namespace", "");
      const std::string name = e.value("name", "");
      const std::string id = ns + "/" + name;
      int s = 0;
      if (contains_icase(id, qLower))
        s += 100;
      if (contains_icase(name, qLower))
        s += 60;
      if (contains_icase(ns, qLower))
        s += 40;
      if (contains_icase(e.value("displayName", ""), qLower))
        s += 25;
      if (contains_icase(e.value("description", ""), qLower))
        s += 20;
      const std::string kw = join_keywords(e);
      if (contains_icase(kw, qLower))
        s += 15;
      return s;
    }

    Hit make_hit(const json &e, const SearchOptions &opt, int score)
    {
      Hit h;
      const std::string ns = e.value("namespace", "");
      const std::string name = e.value("name", "");
      h.id = ns + "/" + name;
      h.desc = e.value("description", "");
      h.type = e.value("type", "");
      if (e.contains("repo") && e["repo"].is_object())
        h.repo = e["repo"].value("url", "");
      h.latest = latest_version(e);
      h.score = score;
      if (!opt.extensionHost.empty())
      {
        if (const json *ext = extension_json(e, opt.extensionHost))
        {
          h.extension = opt.extensionHost;
          h.capabilities = json_string_array(ext->value("capabilities", json::array()));
          if (ext->contains("cellTypes") && (*ext)["cellTypes"].is_array())
          {
            for (const auto &cell : (*ext)["cellTypes"])
              if (cell.is_object() && cell.contains("id") && cell["id"].is_string())
                h.cellTypes.push_back(cell["id"].get<std::string>());
          }
        }
      }
      return h;
    }

    std::string join_strings(const std::vector<std::string> &items)
    {
      std::string out;
      for (const auto &item : items)
      {
        if (!out.empty())
          out += ", ";
        out += item;
      }
      return out;
    }
  }

  int SearchCommand::run(const std::vector<std::string> &args)
  {
    SearchOptions options;
    if (args.empty() || !parse_search_args(args, options))
    {
      if (!options.jsonOutput)
      {
        error("invalid search arguments");
        hint("Usage: vix search [query] [--extension note] [--capability kernel] [--type executable] [--json] [--page N] [--limit N]");
      }
      return help();
    }

    if (!options.jsonOutput)
    {
      vix::cli::util::section(std::cout, "Search");
      vix::cli::util::kv(std::cout, "query", vix::cli::util::quote(options.query));
      vix::cli::util::kv(std::cout, "page", std::to_string(options.page));
      vix::cli::util::kv(std::cout, "limit", std::to_string(options.limit));
    }

    const fs::path repoDir = registry_repo_dir();
    const fs::path idxDir = registry_index_dir();
    if (!registry_ready(repoDir, idxDir))
    {
      if (options.jsonOutput)
        std::cout << json({{"ok", false}, {"error", "registry not synced"}}).dump(2) << "\n";
      else
      {
        error("registry not synced");
        hint("Run: vix registry sync");
      }
      return 1;
    }

    std::vector<Hit> hits;
    const std::string qLower = to_lower(options.query);
    for (const auto &it : fs::directory_iterator(idxDir))
    {
      if (!it.is_regular_file() || it.path().extension() != ".json")
        continue;
      try
      {
        const json e = read_json_or_throw(it.path());
        if (!matches_filters(e, options))
          continue;
        const int s = score_entry(e, qLower);
        if (s <= 0)
          continue;
        hits.push_back(make_hit(e, options, s));
      }
      catch (...)
      {
      }
    }

    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b)
              {
      if (a.score != b.score) return a.score > b.score;
      return a.id < b.id; });

    const std::size_t total = hits.size();
    const std::size_t totalPages = total == 0 ? 1 : (total + options.limit - 1) / options.limit;
    if (options.page > totalPages)
    {
      if (options.jsonOutput)
        std::cout << json({{"ok", false}, {"error", "page out of range"}, {"totalPages", totalPages}}).dump(2) << "\n";
      else
      {
        error("page out of range");
        hint("Total pages: " + std::to_string(totalPages));
      }
      return 1;
    }

    const std::size_t start = total == 0 ? 0 : (options.page - 1) * options.limit;
    const std::size_t end = std::min(start + options.limit, total);

    if (options.jsonOutput)
    {
      json out = {{"query", options.query}, {"page", options.page}, {"limit", options.limit}, {"total", total}, {"items", json::array()}};
      for (std::size_t i = start; i < end; ++i)
      {
        const auto &h = hits[i];
        out["items"].push_back({{"id", h.id}, {"version", h.latest}, {"type", h.type}, {"description", h.desc}, {"extension", h.extension}, {"capabilities", h.capabilities}, {"cellTypes", h.cellTypes}});
      }
      std::cout << out.dump(2) << "\n";
      return 0;
    }

    if (hits.empty())
    {
      error(options.query.empty() ? "no results" : std::string("no results for ") + vix::cli::util::quote(options.query));
      hint("Tip: search by namespace, name, description, keywords, or extension filters");
      return 0;
    }

    vix::cli::util::one_line_spacer(std::cout);
    for (std::size_t i = start; i < end; ++i)
    {
      const auto &h = hits[i];
      std::cout << h.id << "  " << h.latest << "\n";
      if (!h.desc.empty())
        std::cout << h.desc << "\n";
      if (!h.extension.empty())
        std::cout << "Extension: " << h.extension << "\n";
      if (!h.cellTypes.empty())
        std::cout << "Cells: " << join_strings(h.cellTypes) << "\n";
      if (!h.capabilities.empty())
        std::cout << "Capabilities: " << join_strings(h.capabilities) << "\n";
      if (!h.repo.empty())
        std::cout << GRAY << h.repo << RESET << "\n";
      std::cout << "\n";
    }
    vix::cli::util::ok_line(std::cout, "Showing " + std::to_string(start + 1) + "-" + std::to_string(end) + " of " + std::to_string(total) + " result(s).");
    return 0;
  }

  int SearchCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix search <query> [--page N] [--limit N]\n"
        << "  vix search --extension note [--capability kernel] [--type executable] [--json]\n\n"
        << "Description:\n"
        << "  Search packages in the local registry index (offline).\n\n"
        << "Examples:\n"
        << "  vix registry sync\n"
        << "  vix search json\n"
        << "  vix search --extension note\n"
        << "  vix search python --extension note --json\n";
    return 0;
  }

}
