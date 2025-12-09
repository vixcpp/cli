#include "vix/cli/ErrorHandler.hpp"
#include <regex>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <vix/cli/Style.hpp> // RED, GREEN, YELLOW, GRAY, error(), etc.

using namespace vix::cli::style;

namespace
{
    // Handle cases where parseClangGccErrors() returns no structured errors,
    // but the raw build log still contains useful information:
    //  - linker errors (undefined reference, ld returned 1)
    //  - sanitizer reports (LeakSanitizer, AddressSanitizer, ...)
    bool handleRawBuildLogSpecialCases(
        const std::string &buildLog,
        const std::filesystem::path &sourceFile,
        const std::string &contextMessage)
    {
        (void)contextMessage; // not used yet, but we keep it for future

        // ---- Detect linker errors ---- //
        bool hasUndefinedRef = false;
        bool hasLdError = false;
        std::string firstUndefinedRefLine;

        {
            std::istringstream iss(buildLog);
            std::string line;
            while (std::getline(iss, line))
            {
                if (!hasUndefinedRef &&
                    line.find("undefined reference to") != std::string::npos)
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
        }

        if (hasUndefinedRef || hasLdError)
        {
            std::cerr << RED << "❌ LINK ERROR: undefined references during linking"
                      << RESET << "\n\n";

            std::cerr << "The linker reported one or more undefined symbols.\n";

            if (!firstUndefinedRefLine.empty())
            {
                std::cerr << GRAY
                          << "Example:\n"
                          << "    " << firstUndefinedRefLine << "\n"
                          << RESET << "\n";
            }
            else
            {
                std::cerr << "\n";
            }

            std::cerr << YELLOW << "💡 Tip:" << RESET << "\n"
                      << GRAY
                      << "    • Make sure all functions you call are defined.\n"
                      << "    • Check that every required .cpp file is part of the target.\n"
                      << "    • If you use libraries, verify target_link_libraries(...) in CMake.\n"
                      << RESET << "\n";

            std::cerr << GREEN << "📍 Source (link stage): " << RESET
                      << sourceFile.filename().string() << "\n";

            return true;
        }

        // ---- Detect Sanitizer reports (LeakSanitizer / AddressSanitizer) ---- //
        bool hasLeakSanitizer = (buildLog.find("LeakSanitizer") != std::string::npos);
        bool hasAddressSanitizer = (buildLog.find("AddressSanitizer") != std::string::npos);

        if (hasLeakSanitizer || hasAddressSanitizer)
        {
            std::string sanitizerName =
                hasLeakSanitizer ? "LeakSanitizer" : "AddressSanitizer";

            std::cerr << RED << "❌ RUNTIME ERROR: " << sanitizerName
                      << " reported issues" << RESET << "\n\n";

            std::cerr << "The sanitizer detected memory-related problems:\n";

            // Show the first few lines of the sanitizer block for context
            std::istringstream iss(buildLog);
            std::string line;
            int shown = 0;
            const int maxShown = 10;

            while (std::getline(iss, line))
            {
                if (line.find(sanitizerName) != std::string::npos ||
                    line.rfind("==", 0) == 0)
                {
                    std::cerr << GRAY << "    " << line << RESET << "\n";
                    ++shown;
                    if (shown >= maxShown)
                    {
                        std::cerr << GRAY
                                  << "    ... (more sanitizer output hidden) ..."
                                  << RESET << "\n";
                        break;
                    }
                }
            }

            std::cerr << "\n"
                      << YELLOW << "💡 Tip:" << RESET << "\n"
                      << GRAY
                      << "    • Check the stack traces above to locate the allocation site.\n"
                      << "    • Ensure every new/malloc has a matching delete/free.\n"
                      << "    • For leaks, verify that objects are destroyed before program exit.\n"
                      << RESET << "\n";

            std::cerr << GREEN << "📍 Source: " << RESET
                      << sourceFile.filename().string() << "\n";

            return true;
        }

        return false; // nothing special detected
    }

} // anonymous namespace

namespace vix::cli
{
    std::vector<CompilerError> ErrorHandler::parseClangGccErrors(
        const std::string &buildLog)
    {
        std::vector<CompilerError> out;

        // Example:
        // /path/main.cpp:3:5: error: use of undeclared identifier 'std'
        // /path/foo.hpp:1:10: fatal error: 'h.hpp' file not found
        std::regex re(R"((.+?):(\d+):(\d+):\s*(fatal error|error|warning):\s*(.+))");

        std::istringstream iss(buildLog);
        std::string line;
        while (std::getline(iss, line))
        {
            // Ignore make/ninja noise
            if (line.rfind("gmake", 0) == 0 ||
                line.rfind("make", 0) == 0 ||
                line.rfind("ninja", 0) == 0)
            {
                continue;
            }

            std::smatch m;
            if (std::regex_match(line, m, re))
            {
                CompilerError e;
                e.file = m[1].str();
                e.line = std::stoi(m[2].str());
                e.column = std::stoi(m[3].str());
                // m[4] = "fatal error" | "error" | "warning"
                e.message = m[5].str(); // e.g. "'h.hpp' file not found"
                e.raw = line;

                if (m[4].str() == "error" || m[4].str() == "fatal error")
                {
                    out.push_back(std::move(e));
                }
            }
        }

        return out;
    }

    void ErrorHandler::printSingleError(const CompilerError &err)
    {
        // Simple "Go / Deno"-style:
        //   main.cpp:17:5
        //     error: message...
        std::ostringstream oss;

        oss << err.file << ":" << err.line << ":" << err.column << "\n";
        oss << "  error: " << err.message << "\n";

        error(oss.str()); // red "✖ " prefix (Style::error)
    }

    void ErrorHandler::printHints(const CompilerError &err)
    {
        const std::string &msg = err.message;

        auto printHintHeader = []()
        {
            std::cerr << "\n"
                      << YELLOW << "Hint:" << RESET << " ";
        };

        if (msg.find("use of undeclared identifier 'std'") != std::string::npos)
        {
            printHintHeader();
            std::cerr << "It looks like the C++ standard library namespace `std` is not visible here.\n"
                      << GRAY
                      << "  • Did you forget to include <iostream> ?\n"
                      << "  • Or to use 'std::cout' / 'std::endl' correctly ?\n"
                      << RESET;
        }
        else if (msg.find("expected ';'") != std::string::npos)
        {
            printHintHeader();
            std::cerr << "The compiler expected a ';' at this location.\n"
                      << GRAY
                      << "  • Check the previous line for a missing semicolon.\n"
                      << RESET;
        }
        else if (msg.find("no matching function for call to") != std::string::npos)
        {
            printHintHeader();
            std::cerr << "The function you are calling does not match any known overload.\n"
                      << GRAY
                      << "  • Check parameter types and const/reference qualifiers.\n"
                      << RESET;
        }
        // Additional generic hints can be added later ✨
    }

    // High-level, friendly messages for common C++ mistakes (script mode).
    bool ErrorHandler::handleSpecialCases(
        const std::vector<CompilerError> &errors,
        const fs::path & /*sourceFile*/,
        const std::string & /*contextMessage*/)
    {
        for (const auto &err : errors)
        {
            const std::string &msg = err.message;
            fs::path filePath(err.file);
            std::string fileName = filePath.filename().string();

            // 0) "cout" not declared → often missing std:: or <iostream>
            if (msg.find("use of undeclared identifier 'cout'") != std::string::npos ||
                msg.find("'cout' was not declared in this scope") != std::string::npos)
            {
                std::cerr << RED << "❌ ERROR: 'cout' is not declared" << RESET << "\n\n";

                std::cerr << GRAY
                          << "The identifier 'cout' is unknown here.\n"
                          << "In C++, the output stream lives in the std namespace and\n"
                          << "requires the <iostream> header.\n"
                          << RESET << "\n";

                std::cerr << YELLOW << "💡 Tip:" << RESET << "\n"
                          << GRAY
                          << "    #include <iostream>\n"
                          << "    using namespace std;        // or better: std::cout\n\n"
                          << "    std::cout << \"Hello\" << std::endl;\n"
                          << RESET << "\n";

                std::cerr << GREEN << "📍 Source: " << RESET
                          << fileName << ":" << err.line << "\n";

                return true;
            }

            // 0-bis) Header not found: "'h.hpp' file not found"
            if (msg.find("file not found") != std::string::npos &&
                msg.find(".hpp") != std::string::npos)
            {
                std::cerr << RED << "❌ ERROR: Header file not found" << RESET << "\n\n";

                std::cerr << GRAY
                          << "The compiler could not find one of the included headers.\n"
                          << RESET;

                std::cerr << "\nMissing file:\n"
                          << GRAY
                          << "    " << msg << "\n"
                          << RESET << "\n";

                std::cerr << YELLOW << "💡 Tip:" << RESET << "\n"
                          << GRAY
                          << "    • Check the #include path.\n"
                          << "    • Verify that the header is in the project or include directories.\n"
                          << "    • With CMake, make sure include_directories()/target_include_directories()\n"
                          << "      are correctly set.\n"
                          << RESET << "\n";

                std::cerr << GREEN << "📍 Source: " << RESET
                          << fileName << ":" << err.line << "\n";

                return true;
            }

            // 1) Attempting to print std::vector<T> directly with operator<<
            if (msg.find("invalid operands to binary expression") != std::string::npos &&
                msg.find("basic_ostream") != std::string::npos &&
                msg.find("std::vector") != std::string::npos)
            {
                std::cerr << RED << "❌ ERROR: Cannot print std::vector<int> using operator<<"
                          << RESET << "\n\n";

                std::cerr << YELLOW << "💡 Tip:" << RESET << "\n"
                          << GRAY
                          << "    std::cout << value;   // ❌ invalid\n"
                          << "    for (auto x : value)  // ✔ correct\n"
                          << "        std::cout << x << std::endl;\n"
                          << RESET << "\n";

                std::cerr << GREEN << "📍 Source: " << RESET
                          << fileName << ":" << err.line << "\n";

                return true;
            }

            // 2) no matching function for call to 'process' with nullptr ambiguity
            if (msg.find("no matching function for call to 'process'") != std::string::npos)
            {
                std::cerr << RED << "❌ ERROR: Ambiguous call to function 'process'"
                          << RESET << "\n\n";

                std::cerr << GRAY
                          << "You wrote something like:\n"
                          << "    process(nullptr);\n\n"
                          << RESET;

                std::cerr << "The compiler cannot choose a valid overload between, for example:\n"
                          << GRAY
                          << "  1) void process(int)\n"
                          << "  2) template <typename T> void process(T*)\n"
                          << RESET << "\n";

                std::cerr << YELLOW << "💡 Tip:" << RESET << "\n"
                          << GRAY
                          << "Be explicit about which overload you want:\n\n"
                          << "    process((int)0);         // call int version\n"
                          << "    process((int*)nullptr);  // call pointer version\n"
                          << RESET << "\n";

                std::cerr << GREEN << "📍 Source: " << RESET
                          << fileName << ":" << err.line << "\n";

                return true;
            }

            // 3) use-after-move on an object (often std::unique_ptr)
            if (msg.find("after it was moved") != std::string::npos &&
                msg.find("use of") != std::string::npos)
            {
                std::string varName = "object";
                {
                    std::regex re(R"(use of '([^']+)' after it was moved)");
                    std::smatch m;
                    if (std::regex_search(msg, m, re) && m.size() >= 2)
                    {
                        varName = m[1].str();
                    }
                }

                std::cerr << RED << "❌ ERROR: use-after-move on object" << RESET << "\n\n";

                std::cerr << "Reason:\n"
                          << GRAY
                          << "    '" << varName << "' was moved with std::move(" << varName
                          << ") and is now in a moved-from (empty) state.\n"
                          << "    Dereferencing it after the move is undefined behaviour.\n"
                          << RESET << "\n";

                std::cerr << YELLOW << "💡 Tip:" << RESET << "\n"
                          << GRAY
                          << "    // Option 1: don't move it if you still need it\n"
                          << "    // process(" << varName << "); // remove std::move\n\n"
                          << "    // Option 2: check before using it\n"
                          << "    if (" << varName << ") {\n"
                          << "        " << varName << "->run();\n"
                          << "    }\n"
                          << RESET << "\n";

                std::cerr << GREEN << "📍 Source: " << RESET
                          << fileName << ":" << err.line << "\n";

                return true;
            }

            // 4) Missing semicolon — expected ';'
            if (msg.find("expected ';'") != std::string::npos)
            {
                std::cerr << RED << "❌ ERROR: Missing ';' at the end of the statement"
                          << RESET << "\n\n";

                std::cerr << GRAY
                          << "C++ requires a semicolon at the end of most statements.\n"
                          << "The compiler reached this point and expected a ';'.\n"
                          << RESET << "\n";

                std::cerr << YELLOW << "💡 Tip:" << RESET << "\n"
                          << GRAY
                          << "    int x = 42      // ❌ missing ';'\n"
                          << "    int x = 42;     // ✔ fixed\n\n"
                          << "Check the previous line or two for a missing semicolon.\n"
                          << RESET << "\n";

                std::cerr << GREEN << "📍 Source: " << RESET
                          << fileName << ":" << err.line << "\n";

                return true;
            }
        }

        return false; // No special pattern handled → fall back to generic pipeline
    }

    void ErrorHandler::printBuildErrors(
        const std::string &buildLog,
        const fs::path &sourceFile,
        const std::string &contextMessage)
    {
        auto errors = parseClangGccErrors(buildLog);

        if (errors.empty())
        {
            // Nothing matched "file:line:column" format; try linker/sanitizer patterns.
            if (handleRawBuildLogSpecialCases(buildLog, sourceFile, contextMessage))
            {
                return;
            }

            // Fallback: show the raw log
            error(contextMessage + " (see compiler output below):");
            std::cerr << buildLog << "\n";
            return;
        }

        // Script-friendly special cases (override default formatting)
        if (handleSpecialCases(errors, sourceFile, contextMessage))
        {
            return;
        }

        // Group by (file + message) to avoid visual duplicates
        std::unordered_map<std::string, int> counts;
        for (const auto &e : errors)
        {
            std::string key = e.file + "|" + e.message;
            counts[key]++;
        }

        std::vector<CompilerError> unique;
        unique.reserve(errors.size());
        std::unordered_set<std::string> seen;
        for (const auto &e : errors)
        {
            std::string key = e.file + "|" + e.message;
            if (seen.insert(key).second)
            {
                unique.push_back(e);
            }
        }

        if (unique.empty())
        {
            error(contextMessage + " (no unique errors found, see compiler output below):");
            std::cerr << buildLog << "\n";
            return;
        }

        // Generic build error header
        error(contextMessage + ":");

        std::size_t maxToShow = std::min<std::size_t>(unique.size(), 3);
        for (std::size_t i = 0; i < maxToShow; ++i)
        {
            const auto &err = unique[i];

            std::cerr << "\n";
            printSingleError(err);
            printHints(err);

            std::string key = err.file + "|" + err.message;
            auto it = counts.find(key);
            if (it != counts.end() && it->second > 1)
            {
                std::cerr << "\n  (" << (it->second - 1)
                          << " similar error(s) with the same message hidden)\n";
            }
        }

        if (unique.size() > maxToShow)
        {
            std::cerr << "\n… " << (unique.size() - maxToShow)
                      << " more distinct errors hidden. Run the build manually for full output.\n";
        }

        std::cerr << "\nSource file: " << sourceFile << "\n";
    }

} // namespace vix::cli
