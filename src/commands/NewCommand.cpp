#include <vix/cli/commands/NewCommand.hpp>
#include <vix/utils/Logger.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <stdexcept>

namespace fs = std::filesystem;

namespace Vix::Commands::NewCommand
{
    namespace
    {
        // Écrit un fichier texte (binaire pour ne pas modifier les fins de lignes par platform)
        void write_text_file(const fs::path &p, std::string_view content)
        {
            std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
            if (!ofs)
            {
                throw std::runtime_error("Cannot open file for writing: " + p.string());
            }
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!ofs)
            {
                throw std::runtime_error("Failed to write file: " + p.string());
            }
        }

        std::string cmakelists_for(const std::string &name)
        {
            // CMake minimal, avec lien optionnel vers Vix::utils (si dispo) ou spdlog
            return "cmake_minimum_required(VERSION 3.20)\n"
                   "project(" +
                   name + " LANGUAGES CXX)\n\n"
                          "set(CMAKE_CXX_STANDARD 20)\n"
                          "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n"
                          "# Optionnel: si le package Vix::utils est disponible, on link; sinon spdlog ; sinon standalone.\n"
                          "find_package(spdlog QUIET)\n"
                          "find_package(vix_utils QUIET CONFIG)\n\n"
                          "add_executable(" +
                   name + " src/main.cpp)\n\n"
                          "if (MSVC)\n"
                          "  target_compile_options(" +
                   name + " PRIVATE /W4 /permissive-)\n"
                          "else()\n"
                          "  include(CheckCXXCompilerFlag)\n"
                          "  function(add_flag_if_supported tgt flag)\n"
                          "    string(REGEX REPLACE \"[^A-Za-z0-9]\" \"_\" flag_var \"${flag}\")\n"
                          "    set(test_var \"HAVE_${flag_var}\")\n"
                          "    check_cxx_compiler_flag(\"${flag}\" ${test_var})\n"
                          "    if(${test_var})\n"
                          "      target_compile_options(${tgt} PRIVATE \"${flag}\")\n"
                          "    endif()\n"
                          "  endfunction()\n"
                          "  add_flag_if_supported(" +
                   name + " -Wall)\n"
                          "  add_flag_if_supported(" +
                   name + " -Wextra)\n"
                          "  add_flag_if_supported(" +
                   name + " -Wshadow)\n"
                          "  add_flag_if_supported(" +
                   name + " -Wconversion)\n"
                          "endif()\n\n"
                          "if (TARGET Vix::utils)\n"
                          "  target_link_libraries(" +
                   name + " PRIVATE Vix::utils)\n"
                          "elseif (spdlog_FOUND)\n"
                          "  target_link_libraries(" +
                   name + " PRIVATE spdlog::spdlog)\n"
                          "endif()\n";
        }

        constexpr std::string_view gitignore_content =
            R"(# Build artifacts
/build/
cmake-build-*/
CMakeFiles/
CMakeCache.txt
Makefile
*.o
*.obj
*.pdb
*.ilk
*.log

# OS cruft
.DS_Store
Thumbs.db
)";

        std::string readme_for(const std::string &name)
        {
            return "# " + name + R"(

Projet généré avec **vix new**.

## Build rapide

```bash
mkdir -p build && cd build
cmake ..
cmake --build . -j
./)" + name + R"(
Notes

Si Vix::utils est installé (find_package), le binaire utilisera son logger.

Sinon, si spdlog est dispo, on l'utilise directement.

À défaut, l'app reste standalone (std::cout).
)";
        }
        std::string main_cpp_for(const std::string &name)
        {
            // Compile avec ou sans Vix::utils. On garde des #if __has_include sûrs.
            return
                R"(#include <iostream>
#include <string>

#if __has_include(<vix/utils/Logger.hpp>)
#include <vix/utils/Logger.hpp>
#define VIX_HAVE_LOGGER 1
#else
#define VIX_HAVE_LOGGER 0
#endif

#if __has_include(<vix/utils/Version.hpp>)
#include <vix/utils/Version.hpp>
#define VIX_HAVE_VERSION 1
#else
#define VIX_HAVE_VERSION 0
#endif

int main()
{
#if VIX_HAVE_LOGGER
auto& log = Vix::Logger::getInstance();
log.setLevel(Vix::Logger::Level::INFO);
log.setPattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");


#if VIX_HAVE_VERSION
  log.log(Vix::Logger::Level::INFO, "Starting )" +
                name + R"(");
  log.log(Vix::Logger::Level::INFO, "Vix.cpp version: {}", std::string(Vix::utils::version()));
  // Si utils::build_info() existe chez toi :
  // log.log(Vix::Logger::Level::INFO, "Build: {}", Vix::utils::build_info());
#else
  log.log(Vix::Logger::Level::INFO, "Starting )" +
                name + R"(");
#endif
#else
std::cout << "Starting )" +
                name + R"(" << std::endl;
#endif

std::cout << "Hello from )" +
                name + R"(!" << std::endl;
return 0;


}
)";
        }
    } // namespace

    int run(const std::vector<std::string> &args)
    {
        auto &logger = Vix::Logger::getInstance();

        if (args.empty())
        {
            logger.logModule("NewCommand", Vix::Logger::Level::ERROR,
                             "Usage: vix new <project_name>");
            return 1;
        }

        const std::string name = args[0];
        const fs::path projectRoot = fs::path("vix") / name;
        const fs::path srcDir = projectRoot / "src";

        try
        {
            // Si le dossier existe et n'est pas vide -> on évite l'écrasement
            if (fs::exists(projectRoot) && !fs::is_empty(projectRoot))
            {
                logger.logModule("NewCommand", Vix::Logger::Level::ERROR,
                                 "Directory '{}' already exists and is not empty.", projectRoot.string());
                return 2;
            }

            fs::create_directories(srcDir);

            // Fichiers du template
            write_text_file(projectRoot / "CMakeLists.txt", cmakelists_for(name));
            write_text_file(projectRoot / ".gitignore", std::string(gitignore_content));
            write_text_file(projectRoot / "README.md", readme_for(name));
            write_text_file(srcDir / "main.cpp", main_cpp_for(name));

            logger.logModule("NewCommand", Vix::Logger::Level::INFO,
                             "✅ Project '{}' created at {}/", name, projectRoot.string());
            logger.logModule("NewCommand", Vix::Logger::Level::INFO,
                             "Next steps: `cd {0} && mkdir -p build && cd build && cmake .. && cmake --build . -j && ./{1}`",
                             projectRoot.string(), name);
        }
        catch (const std::exception &ex)
        {
            logger.logModule("NewCommand", Vix::Logger::Level::ERROR,
                             "Failed to create project '{}': {}", name, ex.what());
            return 1;
        }

        return 0;
    }

}