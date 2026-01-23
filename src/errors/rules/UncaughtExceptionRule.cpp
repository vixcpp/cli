#include <vix/cli/errors/rules/UncaughtExceptionRule.hpp>

#include <vix/cli/errors/CodeFrame.hpp>
#include <vix/cli/errors/CompilerError.hpp>
#include <vix/cli/errors/ErrorContext.hpp>

#include <vix/cli/Style.hpp>

#include <cctype>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <string_view>

using namespace vix::cli::style;

namespace vix::cli::errors::rules
{
  namespace
  {
    static inline char toLowerAscii(unsigned char c) noexcept
    {
      return static_cast<char>(std::tolower(c));
    }

    static bool icontains(std::string_view haystack, std::string_view needle) noexcept
    {
      if (needle.empty())
        return true;
      if (haystack.size() < needle.size())
        return false;

      for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i)
      {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size(); ++j)
        {
          const unsigned char a = static_cast<unsigned char>(haystack[i + j]);
          const unsigned char b = static_cast<unsigned char>(needle[j]);
          if (toLowerAscii(a) != toLowerAscii(b))
          {
            ok = false;
            break;
          }
        }
        if (ok)
          return true;
      }
      return false;
    }

    static bool containsAny(std::string_view s, const std::initializer_list<std::string_view> &needles) noexcept
    {
      for (auto n : needles)
      {
        if (!n.empty() && icontains(s, n))
          return true;
      }
      return false;
    }

    static std::string_view trim_view(std::string_view s) noexcept
    {
      while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0)
        s.remove_prefix(1);
      while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0)
        s.remove_suffix(1);
      return s;
    }

    static std::optional<std::size_t> findLineBySubstring(const std::filesystem::path &file,
                                                          std::string_view needle)
    {
      if (file.empty() || needle.empty())
        return std::nullopt;

      std::ifstream in(file);
      if (!in)
        return std::nullopt;

      std::string line;
      std::size_t lineno = 0;
      while (std::getline(in, line))
      {
        ++lineno;
        if (line.find(std::string(needle)) != std::string::npos)
          return lineno;
      }
      return std::nullopt;
    }

    static std::optional<std::size_t> guessThrowLocation(
        const std::filesystem::path &sourceFile,
        const std::string &whatMsg)
    {
      if (sourceFile.empty())
        return std::nullopt;

      // Best: find the literal message (Weird!) in code
      if (!whatMsg.empty())
      {
        if (auto ln = findLineBySubstring(sourceFile, whatMsg))
          return ln;

        const std::string quoted = "\"" + whatMsg + "\"";
        if (auto ln = findLineBySubstring(sourceFile, quoted))
          return ln;
      }

      // Fallback: first "throw"
      if (auto ln = findLineBySubstring(sourceFile, "throw "))
        return ln;

      return std::nullopt;
    }

    static std::string extractType(const std::string &log)
    {
      std::string exType;

      // libstdc++: terminate called after throwing an instance of 'std::runtime_error'
      {
        static const std::regex re(R"(throwing an instance of '([^']+)')");
        std::smatch m;
        if (std::regex_search(log, m, re) && m.size() >= 2)
          exType = m[1].str();
      }

      // libc++abi: terminating with uncaught exception of type std::runtime_error: ...
      if (exType.empty())
      {
        static const std::regex re(R"(uncaught exception of type ([^:\n]+))");
        std::smatch m;
        if (std::regex_search(log, m, re) && m.size() >= 2)
          exType = std::string(trim_view(std::string_view(m[1].str())));
      }

      return exType;
    }

    static std::string extractWhat(const std::string &log)
    {
      // libstdc++ prints: what():  Weird!
      static const std::regex re(R"(what\(\):\s*([^\n\r]+))");
      std::smatch m;
      if (std::regex_search(log, m, re) && m.size() >= 2)
        return std::string(trim_view(std::string_view(m[1].str())));
      return {};
    }
  } // namespace

  bool handleUncaughtException(
      const std::string &runtimeLog,
      const std::filesystem::path &sourceFile)
  {
    const bool hit = containsAny(
        runtimeLog, {
                        "terminate called after throwing an instance of",
                        "terminating with uncaught exception",
                        "libc++abi: terminating with uncaught exception",
                        "std::terminate",
                    });

    if (!hit)
      return false;

    const std::string exType = extractType(runtimeLog);
    const std::string whatMsg = extractWhat(runtimeLog);

    // Output court + propre (pas de paragraphe long)
    std::cerr << RED << "runtime error: uncaught exception" << RESET << "\n";

    if (!exType.empty() && !whatMsg.empty())
      std::cerr << GRAY << exType << ": " << RESET << whatMsg << "\n";
    else if (!exType.empty())
      std::cerr << GRAY << "type: " << RESET << exType << "\n";
    else if (!whatMsg.empty())
      std::cerr << GRAY << "what: " << RESET << whatMsg << "\n";

    std::cerr << "\n"
              << YELLOW << "tip:" << RESET << " wrap main() with try/catch (to print e.what())\n";

    std::cerr << "\n"
              << YELLOW << "tip:" << RESET
              << " catch exceptions in main() and rely on RAII (no raw new/delete)\n";

    // location + code frame (si possible)
    if (!sourceFile.empty())
      std::cerr << GREEN << "source:" << RESET << " " << sourceFile.filename().string() << "\n";

    if (auto ln = guessThrowLocation(sourceFile, whatMsg))
    {
      vix::cli::errors::CompilerError loc;
      std::error_code ec;
      loc.file = std::filesystem::weakly_canonical(sourceFile, ec).string();
      if (ec)
        loc.file = sourceFile.string();
      loc.line = static_cast<int>(*ln);
      loc.column = 1;

      vix::cli::errors::ErrorContext ctx;
      vix::cli::errors::CodeFrameOptions opt;
      opt.contextLines = 2;
      opt.maxLineWidth = 120;
      opt.tabWidth = 4;

      std::cerr << "\n"
                << GREEN << "location:" << RESET
                << " " << loc.file << ":" << loc.line << "\n";

      printCodeFrame(loc, ctx, opt);
    }

    return true;
  }

} // namespace vix::cli::errors::rules
