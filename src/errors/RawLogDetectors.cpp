#include "vix/cli/errors/RawLogDetectors.hpp"
#include "vix/cli/errors/CodeFrame.hpp"
#include "vix/cli/errors/CompilerError.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <regex>
#include <optional>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
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

        static bool is_system_path(std::string_view p) noexcept
        {
            return p.find("/usr/include/") != std::string_view::npos ||
                   p.find("/usr/lib/") != std::string_view::npos ||
                   p.find("/lib/") != std::string_view::npos ||
                   p.find("libsanitizer") != std::string_view::npos;
        }

        static std::optional<vix::cli::errors::CompilerError>
        tryExtractFirstUserFrame(const std::string &log,
                                 const std::filesystem::path &fallbackSource)
        {
            using vix::cli::errors::CompilerError;

            auto make = [](const std::string &file, int line, int col)
            {
                CompilerError e;
                e.file = file;
                e.line = line;
                e.column = col > 0 ? col : 1;
                return e;
            };

            const std::string abs = fallbackSource.empty()
                                        ? std::string{}
                                        : std::filesystem::weakly_canonical(fallbackSource).string();

            // 1) BEST: exact script absolute path appears in stack
            if (!abs.empty())
            {
                // matches "... <abs>:LINE[:COL]" even if preceded by "in main "
                const std::regex re(
                    abs + R"(:([0-9]+)(?::([0-9]+))?)");

                std::smatch m;
                if (std::regex_search(log, m, re))
                {
                    const int line = std::stoi(m[1].str());
                    const int col = (m.size() >= 3 && m[2].matched) ? std::stoi(m[2].str()) : 1;
                    return make(abs, line, col);
                }
            }

            // 2) Fallback: scan stack frames and pick first non-system file
            {
                static const std::regex re(R"((/[^:\n]+?\.(?:c|cc|cpp|cxx|h|hpp|hh)):(\d+)(?::(\d+))?)");
                std::smatch m;
                std::string::const_iterator it = log.begin();

                while (std::regex_search(it, log.cend(), m, re))
                {
                    const std::string file = m[1].str();
                    const int line = std::stoi(m[2].str());
                    const int col = (m.size() >= 4 && m[3].matched) ? std::stoi(m[3].str()) : 1;

                    if (!is_system_path(file))
                        return make(file, line, col);

                    it = m.suffix().first;
                }
            }

            // 3) Last resort: show script line 1
            if (!fallbackSource.empty())
                return make(fallbackSource.string(), 1, 1);

            return std::nullopt;
        }

        static std::vector<std::string> splitLines(const std::string &text)
        {
            std::vector<std::string> lines;
            std::istringstream iss(text);
            std::string line;
            while (std::getline(iss, line))
                lines.push_back(line);
            return lines;
        }

        static std::string_view trim_view(std::string_view s) noexcept
        {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
                s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
                s.remove_suffix(1);
            return s;
        }

        static bool startsWith(std::string_view s, std::string_view p) noexcept
        {
            return s.size() >= p.size() && s.substr(0, p.size()) == p;
        }

        static std::vector<std::string> excerptWindow(
            const std::vector<std::string> &lines,
            std::size_t center,
            std::size_t before,
            std::size_t after,
            std::size_t maxLines)
        {
            std::vector<std::string> out;
            if (lines.empty())
                return out;

            const std::size_t start = (center > before) ? (center - before) : 0;
            const std::size_t end = std::min(lines.size(), center + after + 1);

            for (std::size_t i = start; i < end && out.size() < maxLines; ++i)
                out.push_back(lines[i]);

            return out;
        }

        // Sanitizer helpers
        static std::string extractSanitizerKind(const std::vector<std::string> &lines, std::string_view san)
        {
            for (const auto &l : lines)
            {
                if (icontains(l, "ERROR:") && icontains(l, san))
                {
                    // Example: "ERROR: AddressSanitizer: heap-use-after-free on address ..."
                    const std::size_t pos = l.find(':');
                    if (pos == std::string::npos)
                        continue;
                    const std::size_t pos2 = l.find(':', pos + 1);
                    if (pos2 == std::string::npos)
                        continue;

                    std::string_view after = trim_view(std::string_view(l).substr(pos2 + 1));
                    const std::size_t sp = after.find(' ');
                    if (sp != std::string_view::npos)
                        after = after.substr(0, sp);

                    return std::string(after);
                }
            }
            return {};
        }

        static std::vector<std::string> extractSanitizerExcerpt(
            const std::vector<std::string> &lines,
            std::string_view san,
            std::size_t maxLines)
        {
            // 1) find first “interesting” line index
            std::size_t firstHit = lines.size();
            for (std::size_t i = 0; i < lines.size(); ++i)
            {
                const std::string &l = lines[i];
                const std::string_view t = trim_view(l);

                const bool interesting =
                    icontains(l, san) ||
                    icontains(l, "SUMMARY:") ||
                    icontains(l, "ERROR:") ||
                    startsWith(t, "#") ||
                    startsWith(t, "==") ||
                    icontains(l, "allocated") ||
                    icontains(l, "freed") ||
                    icontains(l, "READ") ||
                    icontains(l, "WRITE") ||
                    icontains(l, "invalid") ||
                    icontains(l, "double-free") ||
                    icontains(l, "overflow");

                if (interesting)
                {
                    firstHit = i;
                    break;
                }
            }

            // 2) use a window around it (less noise, more relevant)
            if (firstHit != lines.size())
            {
                auto out = excerptWindow(lines, firstHit, 2, 12, maxLines);
                if (!out.empty())
                    return out;
            }

            // 3) fallback: just first N lines
            std::vector<std::string> out;
            for (std::size_t i = 0; i < std::min(lines.size(), maxLines); ++i)
                out.push_back(lines[i]);
            return out;
        }

        // Runtime detectors
        static bool handleRuntimeDoubleFreeInvalidFree(
            const std::string &runtimeLog,
            const std::filesystem::path &sourceFile)
        {
            // Detect common double-free / invalid-free patterns (glibc + ASan)
            const bool hit =
                icontains(runtimeLog, "attempting double-free") ||
                icontains(runtimeLog, "double free") ||
                icontains(runtimeLog, "free(): invalid pointer") ||
                icontains(runtimeLog, "munmap_chunk(): invalid pointer") ||
                icontains(runtimeLog, "invalid pointer") ||
                (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "not malloc()-ed")) ||
                (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "attempting free")) ||
                (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "double-free"));

            if (!hit)
                return false;

            const bool hasASan = icontains(runtimeLog, "AddressSanitizer");

            // If ASan already printed the big report, don't re-print it again in our excerpt.
            // We'll show a short summary + codeframe instead.
            const bool alreadyPrintedByAsan =
                hasASan &&
                icontains(runtimeLog, "ERROR: AddressSanitizer") &&
                icontains(runtimeLog, "SUMMARY: AddressSanitizer");

            std::cerr << RED << "runtime error: invalid free (double free / invalid pointer)" << RESET << "\n\n";

            std::cerr << GRAY
                      << "A pointer is being freed incorrectly.\n"
                      << "This usually means: freeing the same pointer twice, freeing a non-heap pointer,\n"
                      << "or freeing memory not owned by the allocator.\n"
                      << RESET << "\n";

            // Only print excerpt when ASan did NOT already dump the whole stacktrace.
            if (!alreadyPrintedByAsan)
            {
                const auto lines = splitLines(runtimeLog);

                // For non-ASan glibc logs, focus around "free"/"invalid pointer"
                auto excerpt = hasASan
                                   ? extractSanitizerExcerpt(lines, "AddressSanitizer", 14)
                                   : extractSanitizerExcerpt(lines, "free", 12);

                std::cerr << GRAY << "excerpt:" << RESET << "\n";
                for (const auto &l : excerpt)
                    std::cerr << GRAY << "    " << l << RESET << "\n";
                std::cerr << "\n";
            }
            else
            {
                std::cerr << GRAY
                          << "AddressSanitizer already printed a detailed report above.\n"
                          << "Use the stack trace above; the first frame in your code is the key.\n"
                          << RESET << "\n";
            }

            std::cerr << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    ✔ don't construct multiple std::shared_ptr from the same raw pointer\n"
                      << "    ✔ prefer std::make_shared / std::make_unique\n"
                      << "    ✔ ensure each allocation is freed exactly once\n"
                      << "    ✔ do not call delete/free on stack memory or memory you don't own\n"
                      << RESET << "\n";

            if (!sourceFile.empty())
                std::cerr << GREEN << "source:" << RESET << " " << sourceFile.filename().string() << "\n";

            // Try to locate file:line from runtime log; fallback to script file (line 1)
            if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
            {
                ErrorContext ctx;
                CodeFrameOptions opt;
                opt.contextLines = 2;
                opt.maxLineWidth = 120;
                opt.tabWidth = 4;

                std::cerr << "\n"
                          << GREEN << "location:" << RESET
                          << " " << loc->file << ":" << loc->line << "\n";

                printCodeFrame(*loc, ctx, opt);
            }
            else
            {
                hint("No file:line info found in runtime log. Re-run with ASan/UBSan enabled to get an exact location.");
            }

            return true;
        }

        static bool handleRuntimeHeapUseAfterFree(
            const std::string &runtimeLog,
            const std::filesystem::path &sourceFile)
        {
            const bool hit =
                icontains(runtimeLog, "heap-use-after-free") ||
                (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "use-after-free"));

            if (!hit)
                return false;

            std::cerr << RED << "runtime error: heap-use-after-free" << RESET << "\n\n";
            std::cerr << GRAY
                      << "You are reading/writing memory after it was freed (dangling pointer/reference).\n"
                      << "This can crash later or silently corrupt data.\n"
                      << RESET << "\n";

            std::cerr << GRAY
                      << "Sanitizer report was captured and hidden to keep output clean.\n"
                      << "Use the location + code frame below.\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "fix:" << RESET << "\n"
                      << GRAY
                      << "    ✔ don't access a pointer/reference after delete/free\n"
                      << "    ✔ set pointer to nullptr after delete/free\n"
                      << "    ✔ avoid storing references/iterators to elements that may be erased\n"
                      << "    ✔ prefer std::unique_ptr/std::shared_ptr instead of raw owning pointers\n"
                      << RESET << "\n";

            if (!sourceFile.empty())
                std::cerr << GREEN << "source:" << RESET << " " << sourceFile.filename().string() << "\n";

            if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
            {
                ErrorContext ctx;
                CodeFrameOptions opt;
                opt.contextLines = 2;
                opt.maxLineWidth = 120;
                opt.tabWidth = 4;

                std::cerr << "\n"
                          << GREEN << "location:" << RESET
                          << " " << loc->file << ":" << loc->line << "\n";

                printCodeFrame(*loc, ctx, opt);
            }
            else
            {
                hint("No file:line info found. Re-run with sanitizers enabled to get an exact location.");
            }

            return true;
        }

        // alloc-dealloc-mismatch (new[] vs delete / malloc vs delete, etc.)
        static bool handleRuntimeAllocDeallocMismatch(
            const std::string &runtimeLog,
            const std::filesystem::path &sourceFile)
        {
            const bool hasAsan = icontains(runtimeLog, "AddressSanitizer");

            const bool explicitMismatch =
                icontains(runtimeLog, "alloc-dealloc-mismatch") ||
                icontains(runtimeLog, "operator new [] vs operator delete") ||
                icontains(runtimeLog, "operator new vs operator delete []") ||
                icontains(runtimeLog, "malloc vs operator delete") ||
                icontains(runtimeLog, "operator new vs free") ||
                icontains(runtimeLog, "malloc vs delete");

            const bool fuzzyMismatch =
                icontains(runtimeLog, "new[]") &&
                icontains(runtimeLog, "delete") &&
                icontains(runtimeLog, "mismatch");

            const bool hit = explicitMismatch || (hasAsan && (explicitMismatch || fuzzyMismatch));

            if (!hit)
                return false;

            std::cerr << RED << "runtime error: alloc-dealloc-mismatch" << RESET << "\n\n";
            std::cerr << GRAY
                      << "Memory was allocated with one API and freed with a different one.\n"
                      << "Example: new[] must be paired with delete[], and malloc with free().\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "fix:" << RESET << "\n"
                      << GRAY
                      << "    ✔ new    -> delete\n"
                      << "    ✔ new[]  -> delete[]\n"
                      << "    ✔ malloc -> free\n"
                      << "    ✔ prefer std::vector / std::string / std::unique_ptr<T[]> to avoid manual delete\n"
                      << RESET << "\n";

            std::cerr << GRAY
                      << "Sanitizer report was captured and hidden to keep output clean.\n"
                      << "Use the location + code frame below.\n"
                      << RESET << "\n";

            if (!sourceFile.empty())
                std::cerr << GREEN << "source:" << RESET << " " << sourceFile.filename().string() << "\n";

            if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
            {
                ErrorContext ctx;
                CodeFrameOptions opt;
                opt.contextLines = 2;
                opt.maxLineWidth = 120;
                opt.tabWidth = 4;

                std::cerr << "\n"
                          << GREEN << "location:" << RESET
                          << " " << loc->file << ":" << loc->line << "\n";

                printCodeFrame(*loc, ctx, opt);
            }
            else
            {
                hint("No file:line info found. Re-run with sanitizers enabled to get an exact location.");
            }

            return true;
        }

        // double-free (specialized message for shared_ptr raw ownership)
        static bool handleRuntimeDoubleFree(
            const std::string &runtimeLog,
            const std::filesystem::path &sourceFile)
        {
            const bool hit =
                icontains(runtimeLog, "attempting double-free") ||
                icontains(runtimeLog, "double-free") ||
                icontains(runtimeLog, "double free") ||
                (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "double-free"));

            if (!hit)
                return false;

            std::cerr << RED << "runtime error: double-free" << RESET << "\n\n";
            std::cerr << GRAY
                      << "The same heap pointer was freed more than once.\n"
                      << "This commonly happens with multiple owners of the same raw pointer.\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "fix:" << RESET << "\n"
                      << GRAY
                      << "    ✔ never create 2 std::shared_ptr from the same raw pointer\n"
                      << "        int* p = new int(1);\n"
                      << "        std::shared_ptr<int> a(p);\n"
                      << "        std::shared_ptr<int> b(p); // ❌ double delete\n"
                      << "\n"
                      << "    ✔ do this instead:\n"
                      << "        auto a = std::make_shared<int>(1);\n"
                      << "        auto b = a; // ✅ shared ownership\n"
                      << "\n"
                      << "    ✔ or use std::unique_ptr when there must be a single owner\n"
                      << RESET << "\n";

            std::cerr << GRAY
                      << "Sanitizer report was captured and hidden to keep output clean.\n"
                      << "Use the location + code frame below.\n"
                      << RESET << "\n";

            if (!sourceFile.empty())
                std::cerr << GREEN << "source:" << RESET << " " << sourceFile.filename().string() << "\n";

            if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
            {
                ErrorContext ctx;
                CodeFrameOptions opt;
                opt.contextLines = 2;
                opt.maxLineWidth = 120;
                opt.tabWidth = 4;

                std::cerr << "\n"
                          << GREEN << "location:" << RESET
                          << " " << loc->file << ":" << loc->line << "\n";

                printCodeFrame(*loc, ctx, opt);
            }
            else
            {
                hint("No file:line info found. Re-run with sanitizers enabled to get an exact location.");
            }

            return true;
        }

        // use-after-return (returned pointer/reference/view to a local)
        static bool handleRuntimeUseAfterReturn(
            const std::string &runtimeLog,
            const std::filesystem::path &sourceFile)
        {
            const bool hit =
                icontains(runtimeLog, "use-after-return") ||
                (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "use-after-return"));

            if (!hit)
                return false;

            std::cerr << RED << "runtime error: use-after-return" << RESET << "\n\n";
            std::cerr << GRAY
                      << "Likely cause: you returned a pointer/reference/view to a local variable.\n"
                      << "After the function returns, that stack memory becomes invalid.\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "fix:" << RESET << "\n"
                      << GRAY
                      << "    ✔ return by value (preferred)\n"
                      << "    ✔ or return an owning type (std::string, std::vector, unique_ptr)\n"
                      << "    ✔ never return T& / T* / string_view/span to a local variable\n"
                      << "\n"
                      << "    // bad:\n"
                      << "    std::string_view f(){ std::string s=\"hi\"; return s; }\n"
                      << "    // good:\n"
                      << "    std::string f(){ return \"hi\"; }\n"
                      << RESET << "\n";

            std::cerr << GRAY
                      << "Sanitizer report was captured and hidden to keep output clean.\n"
                      << "Use the location + code frame below.\n"
                      << RESET << "\n";

            if (!sourceFile.empty())
                std::cerr << GREEN << "source:" << RESET << " " << sourceFile.filename().string() << "\n";

            if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
            {
                ErrorContext ctx;
                CodeFrameOptions opt;
                opt.contextLines = 2;
                opt.maxLineWidth = 120;
                opt.tabWidth = 4;

                std::cerr << "\n"
                          << GREEN << "location:" << RESET
                          << " " << loc->file << ":" << loc->line << "\n";

                printCodeFrame(*loc, ctx, opt);
            }
            else
            {
                hint("No file:line info found. Re-run with sanitizers enabled to get an exact location.");
            }

            return true;
        }

        // stack-use-after-scope (cleaner message than generic overflow)
        static bool handleRuntimeStackUseAfterScope(
            const std::string &runtimeLog,
            const std::filesystem::path &sourceFile)
        {
            const bool hit =
                icontains(runtimeLog, "stack-use-after-scope") ||
                icontains(runtimeLog, "use-after-scope") ||
                (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "stack-use-after-scope"));

            if (!hit)
                return false;

            std::cerr << RED << "runtime error: stack-use-after-scope" << RESET << "\n\n";

            std::cerr << GRAY
                      << "Likely cause: a dangling reference/view/span was used after its backing storage went out of scope.\n"
                      << "Example: std::string_view sv = std::string(\"Hello\"); // dangling view\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "fix:" << RESET << "\n"
                      << GRAY
                      << "    ✔ keep backing storage alive:\n"
                      << "        std::string s = \"Hello\";\n"
                      << "        std::string_view sv = s;\n"
                      << "    ✔ or use std::string instead of std::string_view\n"
                      << RESET << "\n";

            std::cerr << GRAY
                      << "Sanitizer report was captured and hidden to keep output clean.\n"
                      << "Use the location + code frame below.\n"
                      << RESET << "\n";

            if (!sourceFile.empty())
                std::cerr << GREEN << "source:" << RESET << " " << sourceFile.filename().string() << "\n";

            if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
            {
                ErrorContext ctx;
                CodeFrameOptions opt;
                opt.contextLines = 2;
                opt.maxLineWidth = 120;
                opt.tabWidth = 4;

                std::cerr << "\n"
                          << GREEN << "location:" << RESET
                          << " " << loc->file << ":" << loc->line << "\n";

                printCodeFrame(*loc, ctx, opt);
            }
            else
            {
                hint("No file:line info found. Re-run with sanitizers enabled to get an exact location.");
            }

            return true;
        }

        static bool handleRuntimeBufferOverflow(
            const std::string &runtimeLog,
            const std::filesystem::path &sourceFile)
        {
            const bool isHeapBO = icontains(runtimeLog, "heap-buffer-overflow");
            const bool isStackBO = icontains(runtimeLog, "stack-buffer-overflow");
            const bool isGlobalBO = icontains(runtimeLog, "global-buffer-overflow");

            const bool isUseAfterScope =
                icontains(runtimeLog, "stack-use-after-scope") ||
                icontains(runtimeLog, "use-after-scope");

            const bool isUseAfterReturn =
                icontains(runtimeLog, "use-after-return");

            const bool isOutOfBounds =
                icontains(runtimeLog, "out of bounds") ||
                icontains(runtimeLog, "out-of-bounds") ||
                icontains(runtimeLog, "index out of range");

            const bool genericOverflow =
                icontains(runtimeLog, "buffer-overflow") ||
                icontains(runtimeLog, "buffer overflow") ||
                icontains(runtimeLog, "heap overflow") ||
                icontains(runtimeLog, "stack overflow") ||
                (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "overflow"));

            const bool hit =
                isHeapBO || isStackBO || isGlobalBO ||
                isUseAfterScope || isUseAfterReturn ||
                isOutOfBounds || genericOverflow;

            if (!hit)
                return false;

            std::string title = "runtime error: memory safety issue";

            if (isHeapBO)
                title = "runtime error: heap-buffer-overflow";
            else if (isStackBO)
                title = "runtime error: stack-buffer-overflow";
            else if (isGlobalBO)
                title = "runtime error: global-buffer-overflow";
            else if (isUseAfterScope)
                title = "runtime error: stack-use-after-scope";
            else if (isUseAfterReturn)
                title = "runtime error: use-after-return";
            else if (isOutOfBounds)
                title = "runtime error: out-of-bounds access";
            else
                title = "runtime error: buffer overflow";

            std::cerr << RED << title << RESET << "\n\n";

            if (isUseAfterReturn)
            {
                std::cerr << GRAY
                          << "Likely cause: you returned a pointer/reference/view to a local variable.\n"
                          << "After the function returns, that stack memory becomes invalid.\n"
                          << RESET << "\n";

                std::cerr << YELLOW << "fix:" << RESET << "\n"
                          << GRAY
                          << "    ✔ return by value (preferred)\n"
                          << "    ✔ or return an owning type (std::string, std::vector, unique_ptr)\n"
                          << "    ✔ never return T& / T* to a local variable\n"
                          << "\n"
                          << "    // bad:\n"
                          << "    const char* f(){ std::string s=\"hi\"; return s.c_str(); }\n"
                          << "    // good:\n"
                          << "    std::string f(){ return \"hi\"; }\n"
                          << RESET << "\n";
            }
            else if (isUseAfterScope)
            {
                std::cerr << GRAY
                          << "Likely cause: a dangling reference/view/span was used after its backing storage went out of scope.\n"
                          << "Example: std::string_view sv = std::string(\"Hello\"); // dangling view\n"
                          << RESET << "\n";

                std::cerr << YELLOW << "fix:" << RESET << "\n"
                          << GRAY
                          << "    ✔ keep backing storage alive:\n"
                          << "        std::string s = \"Hello\";\n"
                          << "        std::string_view sv = s;\n"
                          << "    ✔ or use std::string instead of std::string_view\n"
                          << RESET << "\n";
            }
            else
            {
                std::cerr << GRAY
                          << "A read/write went past the bounds of an allocated object.\n"
                          << "This can corrupt memory and crash later in unrelated code.\n"
                          << RESET << "\n";

                std::cerr << YELLOW << "fix:" << RESET << "\n"
                          << GRAY
                          << "    ✔ check indices (i < size)\n"
                          << "    ✔ use .at(...) in debug builds for bounds checking\n"
                          << "    ✔ ensure buffers include null terminator for C strings\n"
                          << "    ✔ prefer std::vector / std::string / std::span over raw arrays\n"
                          << RESET << "\n";
            }

            // ---- Clean output note ----
            std::cerr << GRAY
                      << "Sanitizer report was captured and hidden to keep output clean.\n"
                      << "Use the location + code frame below.\n"
                      << RESET << "\n";

            if (!sourceFile.empty())
                std::cerr << GREEN << "source:" << RESET << " " << sourceFile.filename().string() << "\n";

            // ---- Location + codeframe ----
            if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
            {
                ErrorContext ctx;
                CodeFrameOptions opt;
                opt.contextLines = 2;
                opt.maxLineWidth = 120;
                opt.tabWidth = 4;

                std::cerr << "\n"
                          << GREEN << "location:" << RESET
                          << " " << loc->file << ":" << loc->line << "\n";

                printCodeFrame(*loc, ctx, opt);
            }
            else
            {
                hint("No file:line info found. Re-run with sanitizers enabled to get an exact location.");
            }

            return true;
        }

        static bool handleSanitizers(const std::string &log, const std::filesystem::path &sourceFile)
        {
            const bool hasASan = icontains(log, "AddressSanitizer");
            const bool hasLSan = icontains(log, "LeakSanitizer");
            const bool hasUBSan = icontains(log, "UndefinedBehaviorSanitizer") || icontains(log, "runtime error:");
            const bool hasMSan = icontains(log, "MemorySanitizer");
            const bool hasTSan = icontains(log, "ThreadSanitizer");

            if (!(hasASan || hasLSan || hasUBSan || hasMSan || hasTSan))
                return false;

            const std::string san =
                hasASan    ? "AddressSanitizer"
                : hasLSan  ? "LeakSanitizer"
                : hasUBSan ? "UndefinedBehaviorSanitizer"
                : hasMSan  ? "MemorySanitizer"
                           : "ThreadSanitizer";

            auto lines = splitLines(log);
            auto kind = extractSanitizerKind(lines, san);

            std::cerr << RED << "runtime error: sanitizer reported an issue" << RESET << "\n";
            std::cerr << GRAY << "sanitizer: " << san;
            if (!kind.empty())
                std::cerr << " (" << kind << ")";
            std::cerr << RESET << "\n\n";

            std::cerr << GRAY
                      << "Sanitizer report was captured and hidden to keep output clean.\n"
                      << "Use the first frame in your code (file:line) below.\n"
                      << RESET << "\n";

            if (!sourceFile.empty())
                std::cerr << GREEN << "source:" << RESET << " " << sourceFile.filename().string() << "\n";

            if (auto loc = tryExtractFirstUserFrame(log, sourceFile))
            {
                ErrorContext ctx;
                CodeFrameOptions opt;
                opt.contextLines = 2;
                opt.maxLineWidth = 120;
                opt.tabWidth = 4;

                std::cerr << "\n"
                          << GREEN << "location:" << RESET
                          << " " << loc->file << ":" << loc->line << "\n";

                printCodeFrame(*loc, ctx, opt);
            }

            std::cerr << "\n"
                      << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    ✔ fix the mismatch: new[] must be paired with delete[]\n"
                      << "    ✔ prefer std::vector / std::unique_ptr<T[]> to avoid manual delete\n"
                      << RESET << "\n";

            return true;
        }

        static bool handleLinkerErrors(const std::string &buildLog, const std::filesystem::path &sourceFile)
        {
            bool hasUndefinedRef = false;
            bool hasLdError = false;
            std::string firstUndefinedRefLine;

            std::istringstream iss(buildLog);
            std::string line;
            while (std::getline(iss, line))
            {
                if (!hasUndefinedRef && line.find("undefined reference to") != std::string::npos)
                {
                    hasUndefinedRef = true;
                    firstUndefinedRefLine = line;
                }

                if (line.find("ld returned") != std::string::npos ||
                    line.find("collect2: error: ld returned") != std::string::npos)
                {
                    hasLdError = true;
                }
            }

            if (!(hasUndefinedRef || hasLdError))
                return false;

            std::cerr << RED << "link error: undefined reference(s)" << RESET << "\n\n";
            std::cerr << GRAY << "The linker reported one or more undefined symbols.\n"
                      << RESET << "\n";

            if (!firstUndefinedRefLine.empty())
                std::cerr << GRAY << "    " << firstUndefinedRefLine << RESET << "\n\n";

            std::cerr << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    ✔ ensure every function you call is defined\n"
                      << "    ✔ ensure required .cpp files are part of the target\n"
                      << "    ✔ verify target_link_libraries(...) in CMake\n"
                      << RESET << "\n";

            std::cerr << GREEN << "source:" << RESET << " " << sourceFile.filename().string() << "\n";
            return true;
        }
    } // namespace

    bool RawLogDetectors::handleRuntimeCrash(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile,
        [[maybe_unused]] const std::string &contextMessage)
    {
        if (handleRuntimeAllocDeallocMismatch(runtimeLog, sourceFile))
            return true;

        if (handleRuntimeDoubleFree(runtimeLog, sourceFile))
            return true;

        if (handleRuntimeDoubleFreeInvalidFree(runtimeLog, sourceFile))
            return true;

        if (handleRuntimeHeapUseAfterFree(runtimeLog, sourceFile))
            return true;

        if (handleRuntimeUseAfterReturn(runtimeLog, sourceFile))
            return true;

        if (handleRuntimeStackUseAfterScope(runtimeLog, sourceFile))
            return true;

        if (handleRuntimeBufferOverflow(runtimeLog, sourceFile))
            return true;

        if (handleSanitizers(runtimeLog, sourceFile))
            return true;

        return false;
    }

    bool RawLogDetectors::handleLinkerOrSanitizer(
        const std::string &buildLog,
        const std::filesystem::path &sourceFile,
        [[maybe_unused]] const std::string &contextMessage)
    {
        if (handleRuntimeAllocDeallocMismatch(buildLog, sourceFile))
            return true;

        if (handleRuntimeDoubleFree(buildLog, sourceFile))
            return true;

        if (handleRuntimeDoubleFreeInvalidFree(buildLog, sourceFile))
            return true;

        if (handleRuntimeHeapUseAfterFree(buildLog, sourceFile))
            return true;

        if (handleRuntimeUseAfterReturn(buildLog, sourceFile))
            return true;

        if (handleRuntimeStackUseAfterScope(buildLog, sourceFile))
            return true;

        if (handleRuntimeBufferOverflow(buildLog, sourceFile))
            return true;

        if (handleSanitizers(buildLog, sourceFile))
            return true;

        if (handleLinkerErrors(buildLog, sourceFile))
            return true;

        return false;
    }

} // namespace vix::cli::errors
