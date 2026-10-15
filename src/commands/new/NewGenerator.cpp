#include <vix/cli/commands/new/NewGenerator.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

#include <fstream>
#include <system_error>

namespace vix::commands::new_cmd::generator
{
  bool dir_exists(const fs::path &path)
  {
    std::error_code error;
    return fs::exists(path, error) && fs::is_directory(path, error) && !error;
  }

  bool dir_is_empty(const fs::path &path)
  {
    std::error_code error;
    if (!dir_exists(path))
      return true;
    return fs::directory_iterator(path, error) == fs::directory_iterator{} && !error;
  }

  bool ensure_dir(const fs::path &path, std::string &diagnostic)
  {
    std::error_code error;
    fs::create_directories(path, error);
    if (error)
    {
      diagnostic = error.message();
      return false;
    }
    return true;
  }

  bool is_dot_path(const std::string &value)
  {
    return value == "." || value == "./" || value == ".\\";
  }

  std::string current_dir_name()
  {
    std::error_code error;
    fs::path current = fs::weakly_canonical(fs::current_path(), error);
    if (error)
      current = fs::current_path();
    const std::string name = current.filename().string();
    return name.empty() ? std::string{"project"} : name;
  }

  bool generate_native_project(
      const fs::path &projectDir,
      const std::string &projectName,
      std::string &diagnostic)
  {
    using namespace vix::cli::project::native;

    if (!ensure_dir(projectDir / "src", diagnostic))
      return false;

    const fs::path sourcePath = projectDir / "src" / "main.cpp";
    {
      std::ofstream source(sourcePath, std::ios::binary | std::ios::trunc);
      if (!source)
      {
        diagnostic = "cannot create src/main.cpp";
        return false;
      }
      source << "int main()\n{\n  return 0;\n}\n";
      if (!source.good())
      {
        diagnostic = "cannot write src/main.cpp";
        return false;
      }
    }

    NativeProject project;
    project.name = projectName;
    NativeTarget target;
    target.name = projectName;
    target.kind = TargetKind::executable;
    target.sources = {"src/main.cpp"};
    project.targets.push_back(target);

    const auto serialized = serialize_native_manifest(project);
    if (!serialized.valid())
    {
      diagnostic = serialized.issues.empty()
          ? "native project model could not be serialized"
          : serialized.issues.front().message;
      return false;
    }

    const auto written = write_native_manifest_atomic(projectDir / "vix.toml", project);
    if (!written.valid())
    {
      diagnostic = written.diagnostic;
      return false;
    }
    return true;
  }
} // namespace vix::commands::new_cmd::generator
