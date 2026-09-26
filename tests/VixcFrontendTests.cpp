#include <vix/cli/app/VixcFrontend.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
  namespace fs = std::filesystem;

  void write_file(const fs::path &path, const std::string &text)
  {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output << text;
  }

  std::string read_file(const fs::path &path)
  {
    std::ifstream input(path, std::ios::binary);
    assert(input);

    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
  }

  void test_prepares_each_translation_unit_without_collisions()
  {
    const fs::path project =
        fs::temp_directory_path() / "vix-cli-vixc-frontend-tests";
    std::error_code error;
    fs::remove_all(project, error);

    write_file(
        project / "src/client/main.cpp",
        "#include \"client.hpp\"\n"
        "int client_main() { return client_value(); }\n");
    write_file(
        project / "src/client/client.hpp",
        "int client_value();\n");
    write_file(
        project / "src/server/main.cpp",
        "#include \"server.hpp\"\n"
        "int server_main() { return server_value(); }\n");
    write_file(
        project / "src/server/server.hpp",
        "int server_value();\n");

    vix::cli::app::AppManifest manifest;
    manifest.sources = {"src/client/main.cpp", "src/server/main.cpp"};
    const auto result = vix::cli::app::prepare_vixc_sources(manifest, project);

    assert(result.success);
    assert(manifest.sources.size() == 2);
    assert(manifest.sources[0] ==
           ".vix/generated/vixc/src/client/main.cpp.vixc.cpp");
    assert(manifest.sources[1] ==
           ".vix/generated/vixc/src/server/main.cpp.vixc.cpp");
    assert(fs::exists(project / manifest.sources[0]));
    assert(fs::exists(project / manifest.sources[1]));
    assert(manifest.includeDirs.size() == 2);
    assert(manifest.includeDirs[0] ==
           (project / "src/client").generic_string());
    assert(manifest.includeDirs[1] ==
           (project / "src/server").generic_string());
    assert(read_file(project / manifest.sources[0]).find("client.hpp") !=
           std::string::npos);

    const fs::path generated = project / manifest.sources[0];
    const auto firstWrite = fs::last_write_time(generated);
    vix::cli::app::AppManifest repeatedManifest;
    repeatedManifest.sources = {"src/client/main.cpp", "src/server/main.cpp"};
    const auto second =
        vix::cli::app::prepare_vixc_sources(repeatedManifest, project);
    assert(second.success);
    assert(fs::last_write_time(generated) == firstWrite);

    fs::remove_all(project, error);
  }

  void test_reports_frontend_diagnostics()
  {
    const fs::path project =
        fs::temp_directory_path() / "vix-cli-vixc-frontend-diagnostics";
    std::error_code error;
    fs::remove_all(project, error);

    write_file(project / "src/main.cpp", "fail error;\n");

    vix::cli::app::AppManifest manifest;
    manifest.sources = {"src/main.cpp"};
    const auto result = vix::cli::app::prepare_vixc_sources(manifest, project);

    assert(!result.success);
    assert(result.diagnostics.find("VIXC2006") != std::string::npos);
    assert(!fs::exists(
        project / ".vix/generated/vixc/src/main.cpp.vixc.cpp"));

    fs::remove_all(project, error);
  }
}

int main()
{
  test_prepares_each_translation_unit_without_collisions();
  test_reports_frontend_diagnostics();
  return 0;
}
