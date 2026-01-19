#include <vix/cli/commands/SearchCommand.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
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
      const char *home = std::getenv("USERPROFILE");
#else
      const char *home = std::getenv("HOME");
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

    std::string latest_version(const json &entry)
    {
      if (!entry.contains("versions") || !entry["versions"].is_object())
        return {};
      std::string best;
      for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
      {
        const std::string v = it.key();
        if (best.empty() || v > best)
          best = v;
      }
      return best;
    }

    struct Hit
    {
      std::string id;
      std::string desc;
      std::string repo;
      std::string latest;
      int score{};
    };

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
    if (args.empty())
      return help();

    const fs::path repoDir = registry_repo_dir();
    const fs::path idxDir = registry_index_dir();

    if (!fs::exists(repoDir) || !fs::exists(idxDir))
    {
      std::cerr << "vix: registry not synced. Run: vix registry sync\n";
      return 1;
    }

    std::string query = args[0];
    const std::string qLower = to_lower(query);

    std::vector<Hit> hits;

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
      if (a.score != b.score) return a.score > b.score;
      return a.id < b.id; });

    if (hits.empty())
    {
      std::cout << "No results for '" << query << "'.\n";
      return 0;
    }

    const std::size_t limit = 20;
    const std::size_t n = std::min<std::size_t>(hits.size(), limit);

    for (std::size_t i = 0; i < n; ++i)
    {
      const auto &h = hits[i];
      std::cout << h.id;
      if (!h.latest.empty())
        std::cout << "  (latest: " << h.latest << ")";
      std::cout << "\n";

      if (!h.desc.empty())
        std::cout << "  " << h.desc << "\n";
      if (!h.repo.empty())
        std::cout << "  repo: " << h.repo << "\n";
      std::cout << "\n";
    }

    if (hits.size() > limit)
      std::cout << "Showing " << limit << " of " << hits.size() << " results.\n";

    return 0;
  }

  int SearchCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix search <query>\n\n"
        << "Description:\n"
        << "  Search packages in the local registry index (offline).\n\n"
        << "Examples:\n"
        << "  vix registry sync\n"
        << "  vix search tree\n"
        << "  vix search gaspardkirira\n";
    return 0;
  }
}
