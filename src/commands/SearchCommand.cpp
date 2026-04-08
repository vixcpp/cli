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
      int score{};
    };

    struct SearchOptions
    {
      std::string query;
      std::size_t page = 1;
      std::size_t limit = 5;
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

    bool parse_search_args(const std::vector<std::string> &args, SearchOptions &opt)
    {
      bool querySet = false;

      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &arg = args[i];

        if (arg == "--page")
        {
          if (i + 1 >= args.size())
            return false;

          std::size_t page = 0;
          if (!parse_positive_size(args[++i], page))
            return false;

          opt.page = page;
          continue;
        }

        if (arg == "--limit")
        {
          if (i + 1 >= args.size())
            return false;

          std::size_t limit = 0;
          if (!parse_positive_size(args[++i], limit))
            return false;

          opt.limit = std::clamp<std::size_t>(limit, 1, 100);
          continue;
        }

        if (arg.rfind("--page=", 0) == 0)
        {
          std::size_t page = 0;
          if (!parse_positive_size(arg.substr(7), page))
            return false;

          opt.page = page;
          continue;
        }

        if (arg.rfind("--limit=", 0) == 0)
        {
          std::size_t limit = 0;
          if (!parse_positive_size(arg.substr(8), limit))
            return false;

          opt.limit = std::clamp<std::size_t>(limit, 1, 100);
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

      return !opt.query.empty();
    }

    int score_entry(const json &e, const std::string &qLower)
    {
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
  }

  int SearchCommand::run(const std::vector<std::string> &args)
  {
    vix::cli::util::section(std::cout, "Search");

    if (args.empty())
      return help();

    SearchOptions options;
    if (!parse_search_args(args, options))
    {
      error("invalid search arguments");
      hint("Usage: vix search <query> [--page N] [--limit N]");
      hint("Example: vix search json --page 2 --limit 5");
      return 1;
    }

    vix::cli::util::kv(std::cout, "query", vix::cli::util::quote(options.query));
    vix::cli::util::kv(std::cout, "page", std::to_string(options.page));
    vix::cli::util::kv(std::cout, "limit", std::to_string(options.limit));

    const fs::path repoDir = registry_repo_dir();
    const fs::path idxDir = registry_index_dir();

    if (!registry_ready(repoDir, idxDir))
    {
      error("registry not synced");
      hint("Run: vix registry sync");
      return 1;
    }

    std::vector<Hit> hits;
    const std::string qLower = to_lower(options.query);

    for (const auto &it : fs::directory_iterator(idxDir))
    {
      if (!it.is_regular_file())
        continue;

      const fs::path p = it.path();
      if (p.extension() != ".json")
        continue;

      try
      {
        const json e = read_json_or_throw(p);

        const int s = score_entry(e, qLower);
        if (s <= 0)
          continue;

        Hit h;
        const std::string ns = e.value("namespace", "");
        const std::string name = e.value("name", "");
        h.id = ns + "/" + name;
        h.desc = e.value("description", "");
        if (e.contains("repo") && e["repo"].is_object())
          h.repo = e["repo"].value("url", "");
        h.latest = latest_version(e);
        h.score = s;

        hits.push_back(std::move(h));
      }
      catch (...)
      {
      }
    }

    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b)
              {
                if (a.score != b.score)
                  return a.score > b.score;
                return a.id < b.id; });

    if (hits.empty())
    {
      error(std::string("no results for ") + vix::cli::util::quote(options.query));
      hint("Tip: search by namespace, name, description, or keywords");
      hint("Example: vix search gaspardkirira");
      return 0;
    }

    const std::size_t total = hits.size();
    const std::size_t totalPages =
        (total + options.limit - 1) / options.limit;

    if (options.page > totalPages)
    {
      error("page out of range");
      hint("Total pages: " + std::to_string(totalPages));
      return 1;
    }

    const std::size_t start = (options.page - 1) * options.limit;
    const std::size_t end = std::min(start + options.limit, total);

    vix::cli::util::one_line_spacer(std::cout);

    for (std::size_t i = start; i < end; ++i)
    {
      const auto &h = hits[i];
      vix::cli::util::pkg_line(std::cout, h.id, h.latest, h.desc, h.repo);
      std::cout << "\n";
    }

    vix::cli::util::ok_line(
        std::cout,
        "Showing " + std::to_string(start + 1) +
            "-" + std::to_string(end) +
            " of " + std::to_string(total) +
            " result(s).");

    if (totalPages > 1)
    {
      std::cout << "  " << GRAY
                << "Page " << options.page << "/" << totalPages
                << RESET << "\n";

      if (options.page < totalPages)
      {
        std::cout << "  " << GRAY
                  << "Next: vix search "
                  << vix::cli::util::quote(options.query)
                  << " --page " << (options.page + 1)
                  << " --limit " << options.limit
                  << RESET << "\n";
      }
    }

    return 0;
  }

  int SearchCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix search <query> [--page N] [--limit N]\n\n"
        << "Description:\n"
        << "  Search packages in the local registry index (offline).\n\n"
        << "Examples:\n"
        << "  vix registry sync\n"
        << "  vix search tree\n"
        << "  vix search json --page 2\n"
        << "  vix search gaspardkirira --limit 50\n";
    return 0;
  }
}
