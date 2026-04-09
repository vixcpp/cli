/**
 *
 *  @file Semver.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/util/Semver.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vix::cli::util::semver
{
  namespace
  {
    struct Identifier
    {
      bool numeric{false};
      std::string text;
    };

    struct Version
    {
      int major{0};
      int minor{0};
      int patch{0};
      std::vector<Identifier> prerelease;
    };

    enum class Op
    {
      Any,
      Eq,
      Gt,
      Gte,
      Lt,
      Lte
    };

    struct Comparator
    {
      Op op{Op::Any};
      Version version{};
    };

    struct Clause
    {
      bool any{false};
      std::vector<Comparator> comparators;
    };

    struct Range
    {
      std::vector<Clause> clauses;
    };

    struct PartialVersion
    {
      bool any{false};

      std::optional<int> major;
      std::optional<int> minor;
      std::optional<int> patch;

      bool wildcardMajor{false};
      bool wildcardMinor{false};
      bool wildcardPatch{false};

      int specifiedComponents{0};
      std::vector<Identifier> prerelease;
      bool hasPrerelease{false};
    };

    std::string trim_copy(std::string s)
    {
      auto isws = [](unsigned char c)
      { return std::isspace(c) != 0; };

      while (!s.empty() && isws(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());

      while (!s.empty() && isws(static_cast<unsigned char>(s.back())))
        s.pop_back();

      return s;
    }

    bool is_digits(const std::string &s)
    {
      if (s.empty())
        return false;

      for (char c : s)
      {
        if (!std::isdigit(static_cast<unsigned char>(c)))
          return false;
      }

      return true;
    }

    std::vector<std::string> split(const std::string &s, char delim)
    {
      std::vector<std::string> out;
      std::stringstream ss(s);
      std::string item;

      while (std::getline(ss, item, delim))
        out.push_back(item);

      return out;
    }

    std::vector<std::string> split_ws(const std::string &s)
    {
      std::vector<std::string> out;
      std::stringstream ss(s);
      std::string item;

      while (ss >> item)
        out.push_back(item);

      return out;
    }

    std::vector<std::string> split_or(const std::string &s)
    {
      std::vector<std::string> out;
      std::string current;

      for (std::size_t i = 0; i < s.size(); ++i)
      {
        if (i + 1 < s.size() && s[i] == '|' && s[i + 1] == '|')
        {
          out.push_back(trim_copy(current));
          current.clear();
          ++i;
          continue;
        }

        current.push_back(s[i]);
      }

      out.push_back(trim_copy(current));
      return out;
    }

    std::vector<Identifier> parse_identifiers(const std::string &s)
    {
      std::vector<Identifier> ids;

      if (s.empty())
        return ids;

      for (const auto &part : split(s, '.'))
      {
        Identifier id;
        id.numeric = is_digits(part);
        id.text = part;
        ids.push_back(id);
      }

      return ids;
    }

    std::optional<Version> parse_version(const std::string &raw)
    {
      std::string s = trim_copy(raw);
      if (s.empty())
        return std::nullopt;

      if (s.front() == 'v' || s.front() == 'V')
        s.erase(s.begin());

      const auto plusPos = s.find('+');
      if (plusPos != std::string::npos)
        s = s.substr(0, plusPos);

      std::string core = s;
      std::string prerelease;

      const auto dashPos = s.find('-');
      if (dashPos != std::string::npos)
      {
        core = s.substr(0, dashPos);
        prerelease = s.substr(dashPos + 1);
      }

      const auto parts = split(core, '.');
      if (parts.size() != 3)
        return std::nullopt;

      if (!is_digits(parts[0]) || !is_digits(parts[1]) || !is_digits(parts[2]))
        return std::nullopt;

      Version v;
      v.major = std::stoi(parts[0]);
      v.minor = std::stoi(parts[1]);
      v.patch = std::stoi(parts[2]);
      v.prerelease = parse_identifiers(prerelease);

      return v;
    }

    int compare_identifiers(const Identifier &a, const Identifier &b)
    {
      if (a.numeric && b.numeric)
      {
        const long long av = std::stoll(a.text);
        const long long bv = std::stoll(b.text);

        if (av < bv)
          return -1;
        if (av > bv)
          return 1;
        return 0;
      }

      if (a.numeric && !b.numeric)
        return -1;

      if (!a.numeric && b.numeric)
        return 1;

      if (a.text < b.text)
        return -1;
      if (a.text > b.text)
        return 1;
      return 0;
    }

    int compare_versions(const Version &a, const Version &b)
    {
      if (a.major != b.major)
        return (a.major < b.major) ? -1 : 1;

      if (a.minor != b.minor)
        return (a.minor < b.minor) ? -1 : 1;

      if (a.patch != b.patch)
        return (a.patch < b.patch) ? -1 : 1;

      const bool aPre = !a.prerelease.empty();
      const bool bPre = !b.prerelease.empty();

      if (!aPre && !bPre)
        return 0;

      if (!aPre && bPre)
        return 1;

      if (aPre && !bPre)
        return -1;

      const std::size_t n = std::max(a.prerelease.size(), b.prerelease.size());

      for (std::size_t i = 0; i < n; ++i)
      {
        if (i >= a.prerelease.size())
          return -1;

        if (i >= b.prerelease.size())
          return 1;

        const int cmp = compare_identifiers(a.prerelease[i], b.prerelease[i]);
        if (cmp != 0)
          return cmp;
      }

      return 0;
    }

    bool is_wildcard_token(const std::string &s)
    {
      return s == "*" || s == "x" || s == "X";
    }

    std::optional<PartialVersion> parse_partial_version(const std::string &raw)
    {
      std::string s = trim_copy(raw);
      if (s.empty())
        return std::nullopt;

      if (s == "*" || s == "x" || s == "X" || s == "latest")
      {
        PartialVersion pv;
        pv.any = true;
        return pv;
      }

      if (s.front() == 'v' || s.front() == 'V')
        s.erase(s.begin());

      const auto plusPos = s.find('+');
      if (plusPos != std::string::npos)
        s = s.substr(0, plusPos);

      std::string core = s;
      std::string prerelease;

      const auto dashPos = s.find('-');
      if (dashPos != std::string::npos)
      {
        core = s.substr(0, dashPos);
        prerelease = s.substr(dashPos + 1);
      }

      const auto parts = split(core, '.');
      if (parts.empty() || parts.size() > 3)
        return std::nullopt;

      PartialVersion pv;

      auto assign_part = [&](std::size_t index, const std::string &part) -> bool
      {
        if (is_wildcard_token(part))
        {
          if (index == 0)
            pv.wildcardMajor = true;
          else if (index == 1)
            pv.wildcardMinor = true;
          else
            pv.wildcardPatch = true;
          return true;
        }

        if (!is_digits(part))
          return false;

        const int value = std::stoi(part);

        if (index == 0)
          pv.major = value;
        else if (index == 1)
          pv.minor = value;
        else
          pv.patch = value;

        pv.specifiedComponents++;
        return true;
      };

      for (std::size_t i = 0; i < parts.size(); ++i)
      {
        if (!assign_part(i, parts[i]))
          return std::nullopt;
      }

      pv.prerelease = parse_identifiers(prerelease);
      pv.hasPrerelease = !pv.prerelease.empty();

      return pv;
    }

    Version normalize_lower(const PartialVersion &pv)
    {
      Version v;
      v.major = pv.major.value_or(0);
      v.minor = pv.minor.value_or(0);
      v.patch = pv.patch.value_or(0);
      v.prerelease = pv.prerelease;
      return v;
    }

    Version bump_major(const Version &v)
    {
      Version out = v;
      out.major += 1;
      out.minor = 0;
      out.patch = 0;
      out.prerelease.clear();
      return out;
    }

    Version bump_minor(const Version &v)
    {
      Version out = v;
      out.minor += 1;
      out.patch = 0;
      out.prerelease.clear();
      return out;
    }

    Version bump_patch(const Version &v)
    {
      Version out = v;
      out.patch += 1;
      out.prerelease.clear();
      return out;
    }

    std::vector<Comparator> make_xrange(const PartialVersion &pv)
    {
      if (pv.any || pv.wildcardMajor || !pv.major.has_value())
        return {};

      const Version lower = normalize_lower(pv);

      if (pv.wildcardMinor || !pv.minor.has_value() || pv.specifiedComponents == 1)
      {
        return {
            Comparator{Op::Gte, lower},
            Comparator{Op::Lt, bump_major(lower)}};
      }

      if (pv.wildcardPatch || !pv.patch.has_value() || pv.specifiedComponents == 2)
      {
        return {
            Comparator{Op::Gte, lower},
            Comparator{Op::Lt, bump_minor(lower)}};
      }

      return {Comparator{Op::Eq, lower}};
    }

    std::vector<Comparator> make_caret_range(const PartialVersion &pv)
    {
      if (pv.any)
        return {};

      const Version lower = normalize_lower(pv);
      Version upper = lower;

      if (lower.major > 0)
        upper = bump_major(lower);
      else if (lower.minor > 0)
        upper = bump_minor(lower);
      else
        upper = bump_patch(lower);

      return {
          Comparator{Op::Gte, lower},
          Comparator{Op::Lt, upper}};
    }

    std::vector<Comparator> make_tilde_range(const PartialVersion &pv)
    {
      if (pv.any)
        return {};

      const Version lower = normalize_lower(pv);
      Version upper = lower;

      if (!pv.minor.has_value() || pv.specifiedComponents <= 1)
        upper = bump_major(lower);
      else
        upper = bump_minor(lower);

      return {
          Comparator{Op::Gte, lower},
          Comparator{Op::Lt, upper}};
    }

    std::optional<std::vector<Comparator>> parse_token_to_comparators(const std::string &tokenRaw)
    {
      const std::string token = trim_copy(tokenRaw);
      if (token.empty())
        return std::vector<Comparator>{};

      if (token == "*" || token == "x" || token == "X" || token == "latest")
        return std::vector<Comparator>{};

      auto parse_after_prefix = [&](const std::string &rest) -> std::optional<PartialVersion>
      {
        return parse_partial_version(rest);
      };

      if (token.rfind("^", 0) == 0)
      {
        const auto pv = parse_after_prefix(token.substr(1));
        if (!pv)
          return std::nullopt;
        return make_caret_range(*pv);
      }

      if (token.rfind("~", 0) == 0)
      {
        const auto pv = parse_after_prefix(token.substr(1));
        if (!pv)
          return std::nullopt;
        return make_tilde_range(*pv);
      }

      auto make_single = [&](Op op, const std::string &rest) -> std::optional<std::vector<Comparator>>
      {
        const auto pv = parse_partial_version(rest);
        if (!pv || pv->any)
          return std::nullopt;

        return std::vector<Comparator>{
            Comparator{op, normalize_lower(*pv)}};
      };

      if (token.rfind(">=", 0) == 0)
        return make_single(Op::Gte, token.substr(2));

      if (token.rfind("<=", 0) == 0)
        return make_single(Op::Lte, token.substr(2));

      if (token.rfind(">", 0) == 0)
        return make_single(Op::Gt, token.substr(1));

      if (token.rfind("<", 0) == 0)
        return make_single(Op::Lt, token.substr(1));

      if (token.rfind("=", 0) == 0)
      {
        const auto pv = parse_partial_version(token.substr(1));
        if (!pv)
          return std::nullopt;
        return make_xrange(*pv);
      }

      const auto pv = parse_partial_version(token);
      if (!pv)
        return std::nullopt;

      return make_xrange(*pv);
    }

    std::optional<Range> parse_range(const std::string &raw)
    {
      const std::string s = trim_copy(raw);

      Range range;

      if (s.empty() || s == "*" || s == "latest")
      {
        Clause clause;
        clause.any = true;
        range.clauses.push_back(clause);
        return range;
      }

      for (const auto &clauseStr : split_or(s))
      {
        Clause clause;

        const auto tokens = split_ws(clauseStr);
        if (tokens.empty())
        {
          clause.any = true;
          range.clauses.push_back(clause);
          continue;
        }

        for (const auto &token : tokens)
        {
          const auto maybeComparators = parse_token_to_comparators(token);
          if (!maybeComparators)
            return std::nullopt;

          for (const auto &cmp : *maybeComparators)
            clause.comparators.push_back(cmp);
        }

        if (clause.comparators.empty())
          clause.any = true;

        range.clauses.push_back(clause);
      }

      if (range.clauses.empty())
      {
        Clause clause;
        clause.any = true;
        range.clauses.push_back(clause);
      }

      return range;
    }

    bool comparator_matches(const Version &v, const Comparator &cmp)
    {
      const int c = compare_versions(v, cmp.version);

      switch (cmp.op)
      {
      case Op::Any:
        return true;
      case Op::Eq:
        return c == 0;
      case Op::Gt:
        return c > 0;
      case Op::Gte:
        return c >= 0;
      case Op::Lt:
        return c < 0;
      case Op::Lte:
        return c <= 0;
      }

      return false;
    }

    bool clause_allows_prerelease(const Version &v, const Clause &clause)
    {
      if (v.prerelease.empty())
        return true;

      for (const auto &cmp : clause.comparators)
      {
        if (!cmp.version.prerelease.empty() &&
            cmp.version.major == v.major &&
            cmp.version.minor == v.minor &&
            cmp.version.patch == v.patch)
        {
          return true;
        }
      }

      return false;
    }

    bool clause_matches(const Version &v, const Clause &clause)
    {
      if (clause.any)
        return true;

      if (!clause_allows_prerelease(v, clause))
        return false;

      for (const auto &cmp : clause.comparators)
      {
        if (!comparator_matches(v, cmp))
          return false;
      }

      return true;
    }
  }

  int compare(const std::string &lhs, const std::string &rhs)
  {
    const auto a = parse_version(lhs);
    const auto b = parse_version(rhs);

    if (!a && !b)
      return 0;

    if (!a)
      return -1;

    if (!b)
      return 1;

    return compare_versions(*a, *b);
  }

  bool satisfies(const std::string &version, const std::string &range)
  {
    const auto v = parse_version(version);
    if (!v)
      return false;

    const auto r = parse_range(range);
    if (!r)
      return false;

    for (const auto &clause : r->clauses)
    {
      if (clause_matches(*v, clause))
        return true;
    }

    return false;
  }

  std::optional<std::string> resolveMaxSatisfying(
      const std::vector<std::string> &versions,
      const std::string &range)
  {
    std::optional<std::string> best;

    for (const auto &version : versions)
    {
      if (!satisfies(version, range))
        continue;

      if (!best.has_value() || compare(version, *best) > 0)
        best = version;
    }

    return best;
  }

  std::string findLatest(const std::vector<std::string> &versions)
  {
    std::string best;

    for (const auto &version : versions)
    {
      if (best.empty() || compare(version, best) > 0)
        best = version;
    }

    return best;
  }

  void sortAscending(std::vector<std::string> &versions)
  {
    std::sort(versions.begin(), versions.end(),
              [](const std::string &a, const std::string &b)
              {
                return compare(a, b) < 0;
              });
  }

  void sortDescending(std::vector<std::string> &versions)
  {
    std::sort(versions.begin(), versions.end(),
              [](const std::string &a, const std::string &b)
              {
                return compare(a, b) > 0;
              });
  }
}
