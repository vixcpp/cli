#include <vix/cli/project/native/PackageContentDigest.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>
#include <string_view>
#include <utility>

#include <vix/cli/project/native/CanonicalHash.hpp>

namespace vix::cli::project::native
{
  namespace
  {
    bool valid_utf8(std::string_view value) noexcept
    {
      std::size_t index = 0;
      while (index < value.size())
      {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7fu) { ++index; continue; }

        std::size_t count = 0;
        std::uint32_t codepoint = 0;
        if (first >= 0xc2u && first <= 0xdfu) { count = 2; codepoint = first & 0x1fu; }
        else if (first >= 0xe0u && first <= 0xefu) { count = 3; codepoint = first & 0x0fu; }
        else if (first >= 0xf0u && first <= 0xf4u) { count = 4; codepoint = first & 0x07u; }
        else return false;
        if (index + count > value.size()) return false;
        for (std::size_t offset = 1; offset < count; ++offset)
        {
          const auto next = static_cast<unsigned char>(value[index + offset]);
          if ((next & 0xc0u) != 0x80u) return false;
          codepoint = (codepoint << 6u) | (next & 0x3fu);
        }
        if ((count == 3 && codepoint < 0x800u) || (count == 4 && codepoint < 0x10000u) ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu) || codepoint > 0x10ffffu)
          return false;
        index += count;
      }
      return true;
    }

    bool bytewise_less(std::string_view left, std::string_view right) noexcept
    {
      const auto common = std::min(left.size(), right.size());
      for (std::size_t index = 0; index < common; ++index)
      {
        const auto lhs = static_cast<unsigned char>(left[index]);
        const auto rhs = static_cast<unsigned char>(right[index]);
        if (lhs != rhs) return lhs < rhs;
      }
      return left.size() < right.size();
    }

    struct BytewisePathLess
    {
      bool operator()(const std::string &left, const std::string &right) const noexcept
      { return bytewise_less(left, right); }
    };

    void sort_issues(std::vector<CanonicalPackageContentIssue> &issues)
    {
      std::sort(issues.begin(), issues.end(), [](const auto &left, const auto &right)
      {
        if (left.path != right.path) return bytewise_less(left.path, right.path);
        if (left.code != right.code) return left.code < right.code;
        return left.message < right.message;
      });
    }

    bool contains_nul(std::string_view value) noexcept
    { return value.find('\0') != std::string_view::npos; }

    CanonicalPackageContentIssue make_issue(CanonicalPackageContentIssueCode code,
      std::string path, std::string message)
    { return {code, std::move(path), std::move(message)}; }

    void append_length_field(std::string &output, std::string_view field, std::string_view value)
    {
      output.append(field);
      output += std::to_string(value.size());
      output.push_back(':');
      output.append(value.data(), value.size());
      output.push_back('\n');
    }

    bool valid_path(const std::string &path, std::vector<CanonicalPackageContentIssue> &issues)
    {
      if (path.empty() || path.front() == '/' || path.back() == '/' || contains_nul(path))
      {
        issues.push_back(make_issue(CanonicalPackageContentIssueCode::invalid_path, path,
          "canonical package path must be non-empty, relative, and contain no NUL"));
        return false;
      }
      if (!valid_utf8(path))
      {
        issues.push_back(make_issue(CanonicalPackageContentIssueCode::invalid_utf8, path,
          "canonical package path is not valid UTF-8"));
        return false;
      }
      std::size_t start = 0;
      while (start <= path.size())
      {
        const auto slash = path.find('/', start);
        const auto end = slash == std::string::npos ? path.size() : slash;
        const auto segment = std::string_view(path).substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..")
        {
          issues.push_back(make_issue(CanonicalPackageContentIssueCode::invalid_path, path,
            "canonical package path contains an empty, dot, or parent segment"));
          return false;
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
      }
      return true;
    }

    bool safe_symlink_target(const std::string &entryPath, const std::string &target)
    {
      if (target.empty() || contains_nul(target) || !valid_utf8(target) || target.front() == '/' ||
          target.front() == '\\' ||
          (target.size() >= 2 && ((target[0] >= 'A' && target[0] <= 'Z') ||
            (target[0] >= 'a' && target[0] <= 'z')) && target[1] == ':' &&
            (target.size() == 2 || target[2] == '/' || target[2] == '\\')))
        return false;

      std::vector<std::string_view> segments;
      const auto parentEnd = entryPath.rfind('/');
      if (parentEnd != std::string::npos)
      {
        std::size_t start = 0;
        while (start < parentEnd)
        {
          const auto slash = entryPath.find('/', start);
          const auto end = slash == std::string::npos || slash > parentEnd ? parentEnd : slash;
          segments.emplace_back(entryPath.data() + start, end - start);
          start = end + 1;
        }
      }

      std::size_t start = 0;
      while (start <= target.size())
      {
        const auto slash = target.find('/', start);
        const auto end = slash == std::string::npos ? target.size() : slash;
        const auto segment = std::string_view(target).substr(start, end - start);
        if (segment == "..")
        {
          if (segments.empty()) return false;
          segments.pop_back();
        }
        else if (!segment.empty() && segment != ".") segments.push_back(segment);
        if (slash == std::string::npos) break;
        start = slash + 1;
      }
      return true;
    }

    void append_entry(std::string &output, const CanonicalPackageEntry &entry)
    {
      output += "entry\n";
      append_length_field(output, "path=", entry.path);
      switch (entry.kind)
      {
      case CanonicalPackageEntryKind::file:
        output += "type=4:file\n";
        output += entry.executable ? "executable=1:1\n" : "executable=1:0\n";
        output += "content=" + std::to_string(entry.content.size()) + ":";
        for (const auto byte : entry.content)
        {
          const auto value = std::to_integer<unsigned char>(byte);
          char encodedByte{};
          std::memcpy(&encodedByte, &value, 1u);
          output.push_back(encodedByte);
        }
        output.push_back('\n');
        break;
      case CanonicalPackageEntryKind::symlink:
        output += "type=7:symlink\n";
        append_length_field(output, "target=", entry.target);
        break;
      }
    }
  } // namespace

  CanonicalPackageContentResult canonicalize_package_content_tree(const CanonicalPackageTree &tree)
  {
    CanonicalPackageContentResult result;
    std::vector<const CanonicalPackageEntry *> ordered;
    ordered.reserve(tree.entries.size());
    for (const auto &entry : tree.entries)
    {
      bool supported = true;
      switch (entry.kind)
      {
      case CanonicalPackageEntryKind::file:
        if (!entry.target.empty())
          result.issues.push_back(make_issue(CanonicalPackageContentIssueCode::unsupported_package_content_entry,
            entry.path, "regular-file entries cannot contain symlink target data"));
        break;
      case CanonicalPackageEntryKind::symlink:
        if (!entry.content.empty() || entry.executable)
          result.issues.push_back(make_issue(CanonicalPackageContentIssueCode::unsupported_package_content_entry,
            entry.path, "symlink entries cannot contain file bytes or executable mode"));
        if (!safe_symlink_target(entry.path, entry.target))
          result.issues.push_back(make_issue(CanonicalPackageContentIssueCode::invalid_symlink_target,
            entry.path, "symlink target must be valid relative UTF-8 and remain within the package root"));
        break;
      default:
        supported = false;
        result.issues.push_back(make_issue(CanonicalPackageContentIssueCode::unsupported_package_content_entry,
          entry.path, "entry type is not supported by canonical package content v1"));
        break;
      }
      if (supported && valid_path(entry.path, result.issues)) ordered.push_back(&entry);
    }
    if (!result.issues.empty())
    {
      sort_issues(result.issues);
      return result;
    }

    std::sort(ordered.begin(), ordered.end(), [](const auto *left, const auto *right)
    { return bytewise_less(left->path, right->path); });
    std::set<std::string, BytewisePathLess> paths;
    for (const auto *entry : ordered)
    {
      if (!paths.insert(entry->path).second)
        result.issues.push_back(make_issue(CanonicalPackageContentIssueCode::duplicate_package_content_path,
          entry->path, "canonical package tree contains a duplicate path"));
    }
    for (const auto *entry : ordered)
    {
      std::size_t slash = entry->path.find('/');
      while (slash != std::string::npos)
      {
        const auto ancestor = entry->path.substr(0, slash);
        if (paths.contains(ancestor))
        {
          result.issues.push_back(make_issue(CanonicalPackageContentIssueCode::package_content_tree_conflict,
            ancestor, "a leaf package entry is also an ancestor of another entry"));
          break;
        }
        slash = entry->path.find('/', slash + 1);
      }
    }
    if (!result.issues.empty())
    {
      sort_issues(result.issues);
      return result;
    }

    std::string bytes{"VIX-PACKAGE-CONTENT-v1\n"};
    bytes += "entry-count=" + std::to_string(ordered.size()) + "\n";
    for (const auto *entry : ordered) append_entry(bytes, *entry);
    auto id = PackageContentId::parse(sha256_digest(bytes));
    if (!id)
    {
      result.issues.push_back(make_issue(CanonicalPackageContentIssueCode::unsupported_package_content_entry,
        {}, "canonical SHA-256 output could not form a package content ID"));
      return result;
    }
    result.canonicalBytes = std::move(bytes);
    result.contentId = std::move(*id);
    return result;
  }

  PackageContentVerificationResult CanonicalPackageContentVerifier::verify(
    std::shared_ptr<const PackageContentCandidate> candidate, const PackageContentId &expected) const
  {
    const auto typed = std::dynamic_pointer_cast<const CanonicalPackageContentCandidate>(candidate);
    if (!typed)
      return verification_failure(expected, "candidate is not a canonical logical package tree");
    const auto actual = canonicalize_package_content_tree(typed->tree());
    if (!actual.valid() || *actual.contentId != expected)
      return verification_failure(expected, "canonical package content does not match the expected digest");
    return verified_content(std::move(candidate), expected, actual.contentId->digest());
  }
} // namespace vix::cli::project::native
