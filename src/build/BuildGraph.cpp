/**
 *
 *  @file BuildGraph.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Incremental build graph
 *
 */

#include <vix/cli/build/BuildGraph.hpp>
#include <vix/cli/build/CompileCommands.hpp>
#include <vix/cli/build/BuildNinja.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <system_error>

namespace vix::cli::build
{
  namespace
  {
    static constexpr std::uint64_t FNV_OFFSET = 1469598103934665603ull;
    static constexpr std::uint64_t FNV_PRIME = 1099511628211ull;
    static constexpr const char *BUILD_GRAPH_MAGIC = "vix-build-graph";

    static std::uint64_t fnv_mix(
        std::uint64_t h,
        const void *data,
        std::size_t len)
    {
      const auto *p = static_cast<const unsigned char *>(data);

      for (std::size_t i = 0; i < len; ++i)
      {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= FNV_PRIME;
      }

      return h;
    }

    static std::uint64_t fnv_mix_string(
        std::uint64_t h,
        const std::string &value)
    {
      return fnv_mix(h, value.data(), value.size());
    }

    static std::string hex64(std::uint64_t value)
    {
      static constexpr char digits[] = "0123456789abcdef";

      std::string out(16, '0');

      for (int i = 15; i >= 0; --i)
      {
        out[static_cast<std::size_t>(i)] = digits[value & 0x0f];
        value >>= 4;
      }

      return out;
    }

    static bool is_source_extension(const std::string &ext)
    {
      return ext == ".cpp" ||
             ext == ".cc" ||
             ext == ".cxx" ||
             ext == ".c";
    }

    static bool is_header_extension(const std::string &ext)
    {
      return ext == ".hpp" ||
             ext == ".hh" ||
             ext == ".hxx" ||
             ext == ".h" ||
             ext == ".ipp";
    }

    static bool is_config_file(const fs::path &path)
    {
      const std::string name = path.filename().string();
      const std::string ext = path.extension().string();

      return name == "CMakeLists.txt" ||
             name == "CMakePresets.json" ||
             name == "vix.json" ||
             name == "vix.toml" ||
             name == "vix.lock" ||
             ext == ".cmake";
    }

    static bool should_skip_dir(const fs::path &path)
    {
      const std::string name = path.filename().string();

      if (name == ".git" ||
          name == ".hg" ||
          name == ".svn" ||
          name == ".vix" ||
          name == "node_modules" ||
          name == ".cache" ||
          name == ".idea" ||
          name == ".vscode")
      {
        return true;
      }

      if (name.rfind("build", 0) == 0)
        return true;

      return false;
    }

    static std::string normalize_path_string(const fs::path &path)
    {
      return path.lexically_normal().generic_string();
    }

    static std::string sanitize_object_component(std::string value)
    {
      for (char &c : value)
      {
        const unsigned char uc = static_cast<unsigned char>(c);

        if (!(std::isalnum(uc) || c == '.' || c == '_' || c == '-'))
          c = '_';
      }

      if (value.empty())
        return "unknown";

      return value;
    }

    static std::uint64_t hash_file_content_u64(const fs::path &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return 0;

      std::uint64_t h = FNV_OFFSET;
      char buffer[64 * 1024];

      while (in)
      {
        in.read(buffer, sizeof(buffer));
        const std::streamsize n = in.gcount();

        if (n > 0)
        {
          h = fnv_mix(
              h,
              buffer,
              static_cast<std::size_t>(n));
        }
      }

      return h;
    }

    static std::string hash_file_content(const fs::path &path)
    {
      return hex64(hash_file_content_u64(path));
    }

    static bool write_text_file_atomic(
        const fs::path &path,
        const std::string &content)
    {
      const fs::path parent = path.parent_path();

      if (!parent.empty())
      {
        std::error_code ec;
        fs::create_directories(parent, ec);
      }

      const fs::path tmp = path.string() + ".tmp";

      {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
          return false;

        out << content;

        if (!out)
          return false;
      }

      std::error_code ec;
      fs::rename(tmp, path, ec);

      if (!ec)
        return true;

      fs::remove(path, ec);
      ec.clear();
      fs::rename(tmp, path, ec);

      return !ec;
    }

    static std::string escape_field(const std::string &value)
    {
      std::string out;
      out.reserve(value.size() + 8);

      for (char c : value)
      {
        switch (c)
        {
        case '\\':
          out += "\\\\";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        case '|':
          out += "\\p";
          break;
        default:
          out.push_back(c);
          break;
        }
      }

      return out;
    }

    static std::string unescape_field(const std::string &value)
    {
      std::string out;
      out.reserve(value.size());

      bool escaped = false;

      for (char c : value)
      {
        if (!escaped)
        {
          if (c == '\\')
          {
            escaped = true;
            continue;
          }

          out.push_back(c);
          continue;
        }

        switch (c)
        {
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'p':
          out.push_back('|');
          break;
        case '\\':
          out.push_back('\\');
          break;
        default:
          out.push_back(c);
          break;
        }

        escaped = false;
      }

      if (escaped)
        out.push_back('\\');

      return out;
    }

    static std::vector<std::string> split_fields(const std::string &line)
    {
      std::vector<std::string> fields;
      std::string current;

      bool escaped = false;

      for (char c : line)
      {
        if (escaped)
        {
          current.push_back('\\');
          current.push_back(c);
          escaped = false;
          continue;
        }

        if (c == '\\')
        {
          escaped = true;
          continue;
        }

        if (c == '|')
        {
          fields.push_back(unescape_field(current));
          current.clear();
          continue;
        }

        current.push_back(c);
      }

      if (escaped)
        current.push_back('\\');

      fields.push_back(unescape_field(current));
      return fields;
    }

    static std::vector<std::string> split_list(const std::string &value)
    {
      std::vector<std::string> out;
      std::string current;

      for (char c : value)
      {
        if (c == ';')
        {
          if (!current.empty())
            out.push_back(current);
          current.clear();
          continue;
        }

        current.push_back(c);
      }

      if (!current.empty())
        out.push_back(current);

      return out;
    }

    static std::string join_list(const std::vector<std::string> &items)
    {
      std::ostringstream out;

      for (std::size_t i = 0; i < items.size(); ++i)
      {
        if (i > 0)
          out << ";";

        out << items[i];
      }

      return out.str();
    }

    static std::uint64_t parse_u64_or_zero(const std::string &value)
    {
      try
      {
        std::size_t pos = 0;
        const auto parsed = std::stoull(value, &pos, 10);

        if (pos != value.size())
          return 0;

        return static_cast<std::uint64_t>(parsed);
      }
      catch (...)
      {
        return 0;
      }
    }

    static std::string compile_task_id_for_source(const fs::path &source)
    {
      std::uint64_t h = FNV_OFFSET;

      const std::string normalized = normalize_path_string(source);
      h = fnv_mix_string(h, "compile:");
      h = fnv_mix_string(h, normalized);

      return "compile:" + hex64(h);
    }

    static std::vector<std::string> make_dependency_enabled_command(
        const std::vector<std::string> &arguments,
        const fs::path &dependencyFile)
    {
      std::vector<std::string> out;
      out.reserve(arguments.size() + 6);

      bool hasMMD = false;
      bool hasMP = false;
      bool hasMF = false;
      bool hasDependencyMode = false;

      for (std::size_t i = 0; i < arguments.size(); ++i)
      {
        const std::string &arg = arguments[i];

        if (arg == "-MMD")
          hasMMD = true;

        if (arg == "-MP")
          hasMP = true;

        if (arg == "-MF")
        {
          hasMF = true;
          out.push_back(arg);

          if (i + 1 < arguments.size())
          {
            out.push_back(dependencyFile.string());
            ++i;
          }
          else
          {
            out.push_back(dependencyFile.string());
          }

          continue;
        }

        if (arg.rfind("-MF", 0) == 0 && arg.size() > 3)
        {
          hasMF = true;
          out.push_back("-MF");
          out.push_back(dependencyFile.string());
          continue;
        }

        if (arg == "-MD" ||
            arg == "-MMD" ||
            arg == "-M" ||
            arg == "-MM")
        {
          hasDependencyMode = true;
        }

        out.push_back(arg);
      }

      if (!hasDependencyMode && !hasMMD)
        out.push_back("-MMD");

      if (!hasMP)
        out.push_back("-MP");

      if (!hasMF)
      {
        out.push_back("-MF");
        out.push_back(dependencyFile.string());
      }

      return out;
    }

    static BuildNodeKind node_kind_from_ninja_edge_output(
        const NinjaEdge &edge,
        const fs::path &path)
    {
      const std::string ext = path.extension().string();

      if (edge.kind == NinjaEdgeKind::Archive)
        return BuildNodeKind::Library;

      if (edge.kind == NinjaEdgeKind::Link)
      {
        if (ext == ".a" ||
            ext == ".so" ||
            ext == ".dylib" ||
            ext == ".dll" ||
            ext == ".lib")
        {
          return BuildNodeKind::Library;
        }

        return BuildNodeKind::Executable;
      }

      if (edge.kind == NinjaEdgeKind::Copy ||
          edge.kind == NinjaEdgeKind::Install)
      {
        return BuildNodeKind::Config;
      }

      /*
       * Utility/phony/generated outputs are intentionally imported as Config.
       *
       * They are useful as graph dependencies, but they are not safe direct
       * Graph Executor targets yet. BuildGraphExecutor will reject them and
       * fallback to CMake/Ninja.
       */
      if (edge.kind == NinjaEdgeKind::Utility)
        return BuildNodeKind::Config;

      return BuildNodeKind::Unknown;
    }

    static BuildNodeKind node_kind_from_ninja_input(const fs::path &path)
    {
      const std::string ext = path.extension().string();

      if (ext == ".o" || ext == ".obj")
        return BuildNodeKind::Object;

      if (ext == ".a" ||
          ext == ".so" ||
          ext == ".dylib" ||
          ext == ".dll" ||
          ext == ".lib")
      {
        return BuildNodeKind::Library;
      }

      if (is_source_extension(ext))
        return BuildNodeKind::Source;

      if (is_header_extension(ext))
        return BuildNodeKind::Header;

      return BuildNodeKind::Config;
    }

    static BuildTaskKind build_task_kind_from_ninja_edge_kind(NinjaEdgeKind kind)
    {
      switch (kind)
      {
      case NinjaEdgeKind::Archive:
        return BuildTaskKind::Archive;
      case NinjaEdgeKind::Link:
        return BuildTaskKind::Link;
      case NinjaEdgeKind::Copy:
        return BuildTaskKind::Copy;
      case NinjaEdgeKind::Install:
        return BuildTaskKind::Copy;
      case NinjaEdgeKind::Utility:
        return BuildTaskKind::Generate;
      case NinjaEdgeKind::Compile:
        return BuildTaskKind::Compile;
      case NinjaEdgeKind::Unknown:
      default:
        return BuildTaskKind::Unknown;
      }
    }

    static std::string ninja_task_id_for_edge(const NinjaEdge &edge)
    {
      std::uint64_t h = FNV_OFFSET;

      h = fnv_mix_string(h, "ninja:");
      h = fnv_mix_string(h, to_string(edge.kind));
      h = fnv_mix_string(h, edge.rule);

      for (const fs::path &output : edge.outputs)
        h = fnv_mix_string(h, normalize_path_string(output));

      return "ninja:" + hex64(h);
    }

    static std::string command_hash_for_argv(
        const std::vector<std::string> &command)
    {
      std::uint64_t h = FNV_OFFSET;

      h = fnv_mix_string(h, "command:");

      for (const std::string &arg : command)
      {
        h = fnv_mix_string(h, arg);
        h = fnv_mix_string(h, "\0");
      }

      return hex64(h);
    }

    static bool should_import_ninja_edge(const NinjaEdge &edge)
    {
      if (!edge.valid())
        return false;

      /*
       * Compile commands are imported from compile_commands.json because that
       * gives Vix the exact compiler argv, working directory and object output.
       */
      if (edge.kind == NinjaEdgeKind::Compile)
        return false;

      if (edge.kind == NinjaEdgeKind::Unknown)
        return false;

      /*
       * Import Link/Archive/Copy/Install/Utility edges.
       *
       * The executor will decide later if a target is safe to execute through
       * Graph Executor. Importing the DAG is useful even when execution falls
       * back to CMake/Ninja.
       */
      return true;
    }
  } // namespace

  bool BuildGraphConfig::valid() const
  {
    return !projectDir.empty() && !buildDir.empty();
  }

  BuildGraph::BuildGraph(BuildGraphConfig config)
      : config_(std::move(config))
  {
    if (config_.objectDir.empty() && !config_.buildDir.empty())
      config_.objectDir = config_.buildDir / ".vix" / "obj";
  }

  const BuildGraphConfig &BuildGraph::config() const
  {
    return config_;
  }

  void BuildGraph::set_config(BuildGraphConfig config)
  {
    config_ = std::move(config);

    if (config_.objectDir.empty() && !config_.buildDir.empty())
      config_.objectDir = config_.buildDir / ".vix" / "obj";
  }

  void BuildGraph::clear()
  {
    nodes_.clear();
    tasks_.clear();
  }

  bool BuildGraph::empty() const
  {
    return nodes_.empty() && tasks_.empty();
  }

  bool BuildGraph::add_node(const BuildNode &node)
  {
    if (!node.valid())
      return false;

    nodes_[node.id] = node;
    return true;
  }

  bool BuildGraph::add_task(const BuildTask &task)
  {
    if (!task.valid())
      return false;

    tasks_[task.id] = task;
    return true;
  }

  BuildNode *BuildGraph::find_node(const std::string &id)
  {
    const auto it = nodes_.find(id);
    if (it == nodes_.end())
      return nullptr;

    return &it->second;
  }

  const BuildNode *BuildGraph::find_node(const std::string &id) const
  {
    const auto it = nodes_.find(id);
    if (it == nodes_.end())
      return nullptr;

    return &it->second;
  }

  BuildTask *BuildGraph::find_task(const std::string &id)
  {
    const auto it = tasks_.find(id);
    if (it == tasks_.end())
      return nullptr;

    return &it->second;
  }

  const BuildTask *BuildGraph::find_task(const std::string &id) const
  {
    const auto it = tasks_.find(id);
    if (it == tasks_.end())
      return nullptr;

    return &it->second;
  }

  const std::unordered_map<std::string, BuildNode> &BuildGraph::nodes() const
  {
    return nodes_;
  }

  const std::unordered_map<std::string, BuildTask> &BuildGraph::tasks() const
  {
    return tasks_;
  }

  std::vector<std::string> BuildGraph::sorted_node_ids() const
  {
    std::vector<std::string> ids;
    ids.reserve(nodes_.size());

    for (const auto &kv : nodes_)
      ids.push_back(kv.first);

    std::sort(ids.begin(), ids.end());
    return ids;
  }

  std::vector<std::string> BuildGraph::sorted_task_ids() const
  {
    std::vector<std::string> ids;
    ids.reserve(tasks_.size());

    for (const auto &kv : tasks_)
      ids.push_back(kv.first);

    std::sort(ids.begin(), ids.end());
    return ids;
  }

  BuildGraphScanResult BuildGraph::scan_project()
  {
    BuildGraphScanResult result;

    if (!config_.valid())
      return result;

    const fs::path root = fs::absolute(config_.projectDir).lexically_normal();

    std::error_code ec;
    fs::recursive_directory_iterator it(
        root,
        fs::directory_options::skip_permission_denied,
        ec);

    const fs::recursive_directory_iterator end;

    while (!ec && it != end)
    {
      const fs::path current = it->path();

      if (it->is_directory(ec))
      {
        if (should_skip_dir(current))
          it.disable_recursion_pending();

        ++it;
        continue;
      }

      if (!it->is_regular_file(ec))
      {
        ++it;
        continue;
      }

      const std::string ext = current.extension().string();

      BuildNodeKind kind = BuildNodeKind::Unknown;

      if (is_source_extension(ext))
        kind = BuildNodeKind::Source;
      else if (is_header_extension(ext))
        kind = BuildNodeKind::Header;
      else if (is_config_file(current))
        kind = BuildNodeKind::Config;
      else
      {
        ++it;
        continue;
      }

      BuildNode node = make_file_build_node(kind, current);
      node.hash = hash_file_content(current);
      add_node(node);

      if (kind == BuildNodeKind::Source)
        ++result.sources;
      else if (kind == BuildNodeKind::Header)
        ++result.headers;
      else if (kind == BuildNodeKind::Config)
        ++result.configs;

      ++it;
    }

    result.tasks = tasks_.size();
    return result;
  }

  std::size_t BuildGraph::load_compile_commands(const fs::path &path)
  {
    const auto compileCommands = read_compile_commands(path);

    if (!compileCommands)
      return 0;

    std::size_t imported = 0;

    for (const CompileCommandEntry &entry : *compileCommands)
    {
      if (!entry.valid() || !entry.has_output())
        continue;

      const fs::path sourcePath = entry.source.lexically_normal();
      const fs::path objectPath = entry.output.lexically_normal();
      const fs::path dependencyPath = dependency_file_for_object(objectPath);

      BuildNode sourceNode =
          make_file_build_node(BuildNodeKind::Source, sourcePath);

      sourceNode.hash = hash_file_content(sourcePath);
      add_node(sourceNode);

      BuildNode objectNode =
          make_file_build_node(BuildNodeKind::Object, objectPath);

      objectNode.id = make_build_node_id(BuildNodeKind::Object, objectPath);
      objectNode.hash = hash_file_content(objectPath);
      objectNode.add_dependency(sourceNode.id);

      add_node(objectNode);

      std::vector<std::string> command =
          make_dependency_enabled_command(
              entry.arguments,
              dependencyPath);

      BuildTask task =
          make_compile_task(
              sourceNode.id,
              objectNode.id,
              command,
              entry.directory);

      task.id = compile_task_id_for_source(sourcePath);
      task.workingDirectory = entry.directory;
      task.logFile = dependencyPath;
      task.commandHash = command_hash_for_argv(task.command);

      add_task(task);

      ++imported;
    }

    return imported;
  }

  std::size_t BuildGraph::load_ninja_build(const fs::path &path)
  {
    const auto ninjaBuild = read_build_ninja(path);

    if (!ninjaBuild)
      return 0;

    std::size_t imported = 0;

    std::unordered_map<std::string, std::string> outputToTask;

    for (const auto &kv : tasks_)
    {
      const BuildTask &task = kv.second;

      for (const std::string &outputId : task.outputs)
        outputToTask[outputId] = task.id;
    }

    for (const NinjaEdge &edge : ninjaBuild->edges)
    {
      if (!should_import_ninja_edge(edge))
        continue;

      const BuildTaskKind taskKind =
          build_task_kind_from_ninja_edge_kind(edge.kind);

      if (taskKind == BuildTaskKind::Unknown)
        continue;

      BuildTask task;
      task.id = ninja_task_id_for_edge(edge);
      task.kind = taskKind;
      task.state = BuildTaskState::Pending;
      task.workingDirectory = ninjaBuild->directory;

      /*
       * We intentionally delegate non-compile Ninja edges back to Ninja.
       *
       * Vix imports the DAG and target metadata here, but does not yet expand
       * Ninja variables or reimplement every CMake-generated build rule.
       */
      task.command = {
          "ninja",
          "-C",
          ninjaBuild->directory.string(),
          edge.primary_output().string()};

      task.commandHash = command_hash_for_argv(task.command);

      std::vector<std::string> inputNodeIds;

      auto import_input = [&](const fs::path &input)
      {
        const BuildNodeKind inputKind = node_kind_from_ninja_input(input);

        BuildNode inputNode = make_file_build_node(inputKind, input);

        /*
         * Keep Ninja import cheap.
         * Real source/header hashes are provided by scan_project() and .d files.
         * Ninja can reference many generated/internal files.
         */
        inputNode.hash.clear();

        add_node(inputNode);

        task.add_input(inputNode.id);
        inputNodeIds.push_back(inputNode.id);

        const auto producerIt = outputToTask.find(inputNode.id);
        if (producerIt != outputToTask.end())
          task.add_dependency(producerIt->second);
      };

      for (const fs::path &input : edge.explicitInputs)
        import_input(input);

      for (const fs::path &input : edge.implicitInputs)
        import_input(input);

      for (const fs::path &input : edge.orderOnlyInputs)
        import_input(input);

      for (const fs::path &output : edge.outputs)
      {
        const BuildNodeKind outputKind =
            node_kind_from_ninja_edge_output(edge, output);

        if (outputKind == BuildNodeKind::Unknown)
          continue;

        BuildNode outputNode = make_file_build_node(outputKind, output);
        outputNode.hash.clear();

        for (const auto &inputId : inputNodeIds)
          outputNode.add_dependency(inputId);

        add_node(outputNode);
        task.add_output(outputNode.id);
      }

      if (task.outputs.empty())
        continue;

      add_task(task);

      for (const std::string &outputId : task.outputs)
        outputToTask[outputId] = task.id;

      ++imported;
    }

    return imported;
  }

  void BuildGraph::load_dependency_files()
  {
    for (auto &kv : tasks_)
    {
      BuildTask &task = kv.second;

      if (task.kind != BuildTaskKind::Compile)
        continue;

      if (task.outputs.empty())
        continue;

      const BuildNode *objectNode = find_node(task.outputs.front());
      if (!objectNode)
        continue;

      const fs::path depPath = dependency_file_for_object(objectNode->path);
      const auto depFile = read_dependency_file(depPath);

      if (!depFile)
        continue;

      for (const auto &dep : depFile->dependencies)
      {
        fs::path depPathResolved = dep;

        if (depPathResolved.is_relative())
          depPathResolved = config_.projectDir / depPathResolved;

        depPathResolved = depPathResolved.lexically_normal();

        BuildNodeKind depKind =
            is_header_extension(depPathResolved.extension().string())
                ? BuildNodeKind::Header
                : BuildNodeKind::Source;

        BuildNode depNode = make_file_build_node(depKind, depPathResolved);
        depNode.hash = hash_file_content(depPathResolved);

        add_node(depNode);

        if (BuildNode *obj = find_node(objectNode->id))
          obj->add_dependency(depNode.id);

        task.add_input(depNode.id);
      }
    }
  }

  void BuildGraph::mark_all_dirty()
  {
    for (auto &kv : nodes_)
      kv.second.mark_dirty();
  }

  void BuildGraph::mark_clean_from_previous(const BuildGraph &previous)
  {
    for (auto &kv : nodes_)
    {
      BuildNode &node = kv.second;
      const BuildNode *old = previous.find_node(node.id);

      if (!old)
      {
        node.mark_dirty();
        continue;
      }

      if (old->kind != node.kind ||
          old->path != node.path ||
          old->size != node.size ||
          old->mtime != node.mtime ||
          old->hash != node.hash)
      {
        node.mark_dirty();
        continue;
      }

      if (node.missing())
        continue;

      node.mark_clean();
    }
  }

  void BuildGraph::propagate_dirty()
  {
    bool changed = true;

    while (changed)
    {
      changed = false;

      for (auto &kv : nodes_)
      {
        BuildNode &node = kv.second;

        if (node.dirty() || node.missing())
          continue;

        for (const auto &depId : node.deps)
        {
          const BuildNode *dep = find_node(depId);

          if (!dep)
            continue;

          if (dep->dirty() || dep->missing())
          {
            node.mark_dirty();
            changed = true;
            break;
          }
        }
      }
    }
  }

  std::vector<BuildTask> BuildGraph::compile_tasks() const
  {
    std::vector<BuildTask> out;

    for (const auto &kv : tasks_)
    {
      if (kv.second.kind == BuildTaskKind::Compile)
        out.push_back(kv.second);
    }

    std::sort(
        out.begin(),
        out.end(),
        [](const BuildTask &a, const BuildTask &b)
        {
          return a.id < b.id;
        });

    return out;
  }

  std::vector<BuildTask> BuildGraph::dirty_compile_tasks() const
  {
    std::vector<BuildTask> out;

    for (const auto &kv : tasks_)
    {
      const BuildTask &task = kv.second;

      if (task.kind != BuildTaskKind::Compile)
        continue;

      if (task_is_dirty(task))
        out.push_back(task);
    }

    std::sort(
        out.begin(),
        out.end(),
        [](const BuildTask &a, const BuildTask &b)
        {
          return a.id < b.id;
        });

    return out;
  }

  bool BuildGraph::task_is_dirty(const BuildTask &task) const
  {
    if (task.kind == BuildTaskKind::Compile)
    {
      for (const auto &inputId : task.inputs)
      {
        const BuildNode *node = find_node(inputId);

        if (!node)
          return true;

        if (node->dirty() || node->missing())
          return true;
      }

      for (const auto &outputId : task.outputs)
      {
        const BuildNode *node = find_node(outputId);

        if (!node)
          return true;

        if (node->missing())
          return true;
      }

      return false;
    }

    for (const auto &inputId : task.inputs)
    {
      const BuildNode *node = find_node(inputId);

      if (!node)
        return true;

      if (node->dirty() || node->missing())
        return true;
    }

    for (const auto &outputId : task.outputs)
    {
      const BuildNode *node = find_node(outputId);

      if (!node)
        return true;

      if (node->dirty() || node->missing())
        return true;
    }

    return false;
  }

  std::string BuildGraph::fingerprint() const
  {
    std::uint64_t h = FNV_OFFSET;

    for (const auto &id : sorted_node_ids())
    {
      const BuildNode *node = find_node(id);
      if (!node)
        continue;

      h = fnv_mix_string(h, node->id);
      h = fnv_mix_string(h, to_string(node->kind));
      h = fnv_mix_string(h, to_string(node->state));
      h = fnv_mix_string(h, normalize_path_string(node->path));
      h = fnv_mix_string(h, node->hash);

      h = fnv_mix(
          h,
          &node->size,
          sizeof(node->size));

      h = fnv_mix(
          h,
          &node->mtime,
          sizeof(node->mtime));

      std::vector<std::string> deps = node->deps;
      std::sort(deps.begin(), deps.end());

      for (const auto &dep : deps)
        h = fnv_mix_string(h, dep);
    }

    for (const auto &id : sorted_task_ids())
    {
      const BuildTask *task = find_task(id);
      if (!task)
        continue;

      h = fnv_mix_string(h, task->id);
      h = fnv_mix_string(h, to_string(task->kind));
      h = fnv_mix_string(h, task->commandHash);

      for (const auto &input : task->inputs)
        h = fnv_mix_string(h, input);

      for (const auto &output : task->outputs)
        h = fnv_mix_string(h, output);

      for (const auto &dep : task->deps)
        h = fnv_mix_string(h, dep);
    }

    return hex64(h);
  }

  bool BuildGraph::save(const fs::path &path) const
  {
    std::ostringstream out;
    out << BUILD_GRAPH_MAGIC << "\n";
    out << "project=" << escape_field(config_.projectDir.string()) << "\n";
    out << "build=" << escape_field(config_.buildDir.string()) << "\n";
    out << "object=" << escape_field(config_.objectDir.string()) << "\n";
    out << "compiler=" << escape_field(config_.compiler) << "\n";
    out << "fingerprint=" << escape_field(config_.buildFingerprint) << "\n";

    for (const auto &id : sorted_node_ids())
    {
      const BuildNode *node = find_node(id);
      if (!node)
        continue;

      out << "node|"
          << escape_field(node->id) << "|"
          << escape_field(to_string(node->kind)) << "|"
          << escape_field(to_string(node->state)) << "|"
          << escape_field(node->path.string()) << "|"
          << escape_field(node->hash) << "|"
          << node->size << "|"
          << node->mtime << "|"
          << escape_field(join_list(node->deps)) << "\n";
    }

    for (const auto &id : sorted_task_ids())
    {
      const BuildTask *task = find_task(id);
      if (!task)
        continue;

      out << "task|"
          << escape_field(task->id) << "|"
          << escape_field(to_string(task->kind)) << "|"
          << escape_field(to_string(task->state)) << "|"
          << escape_field(join_list(task->inputs)) << "|"
          << escape_field(join_list(task->outputs)) << "|"
          << escape_field(join_list(task->deps)) << "|"
          << escape_field(join_list(task->command)) << "|"
          << escape_field(task->commandHash) << "|"
          << escape_field(task->workingDirectory.string()) << "|"
          << escape_field(task->logFile.string()) << "|"
          << task->exitCode << "\n";
    }

    return write_text_file_atomic(path, out.str());
  }

  std::optional<BuildGraph> BuildGraph::load(const fs::path &path)
  {
    std::ifstream in(path, std::ios::binary);
    if (!in)
      return std::nullopt;

    std::string magic;
    if (!std::getline(in, magic))
      return std::nullopt;

    if (magic != BUILD_GRAPH_MAGIC)
      return std::nullopt;

    BuildGraph graph;
    std::string line;

    while (std::getline(in, line))
    {
      if (line.empty())
        continue;

      if (line.rfind("project=", 0) == 0)
      {
        graph.config_.projectDir = unescape_field(line.substr(8));
        continue;
      }

      if (line.rfind("build=", 0) == 0)
      {
        graph.config_.buildDir = unescape_field(line.substr(6));
        continue;
      }

      if (line.rfind("object=", 0) == 0)
      {
        graph.config_.objectDir = unescape_field(line.substr(7));
        continue;
      }

      if (line.rfind("compiler=", 0) == 0)
      {
        graph.config_.compiler = unescape_field(line.substr(9));
        continue;
      }

      if (line.rfind("fingerprint=", 0) == 0)
      {
        graph.config_.buildFingerprint = unescape_field(line.substr(12));
        continue;
      }

      if (line.rfind("node|", 0) == 0)
      {
        const auto fields = split_fields(line);

        if (fields.size() != 9)
          continue;

        BuildNode node;
        node.id = fields[1];
        node.kind = build_node_kind_from_string(fields[2]);
        node.state = build_node_state_from_string(fields[3]);
        node.path = fields[4];
        node.hash = fields[5];
        node.size = parse_u64_or_zero(fields[6]);
        node.mtime = parse_u64_or_zero(fields[7]);
        node.deps = split_list(fields[8]);

        graph.add_node(node);
        continue;
      }

      if (line.rfind("task|", 0) == 0)
      {
        const auto fields = split_fields(line);

        if (fields.size() != 12)
          continue;

        BuildTask task;
        task.id = fields[1];
        task.kind = build_task_kind_from_string(fields[2]);
        task.state = build_task_state_from_string(fields[3]);
        task.inputs = split_list(fields[4]);
        task.outputs = split_list(fields[5]);
        task.deps = split_list(fields[6]);
        task.command = split_list(fields[7]);
        task.commandHash = fields[8];
        task.workingDirectory = fields[9];
        task.logFile = fields[10];
        task.exitCode = static_cast<int>(parse_u64_or_zero(fields[11]));

        graph.add_task(task);
        continue;
      }
    }

    if (!graph.config_.valid())
      return std::nullopt;

    return graph;
  }

  fs::path BuildGraph::default_graph_path(const fs::path &buildDir)
  {
    return buildDir / ".vix" / "build-graph.vix";
  }

  fs::path BuildGraph::object_path_for_source(const fs::path &source) const
  {
    fs::path relative;

    std::error_code ec;
    relative = fs::relative(source, config_.projectDir, ec);

    if (ec)
      relative = source.filename();

    std::string name = sanitize_object_component(relative.generic_string());
    name += ".o";

    return (config_.objectDir / name).lexically_normal();
  }

  fs::path BuildGraph::dependency_path_for_source(const fs::path &source) const
  {
    fs::path out = object_path_for_source(source);
    out.replace_extension(".d");
    return out;
  }

  std::vector<std::string> BuildGraph::compile_command_for(
      const fs::path &source,
      const fs::path &object,
      const fs::path &dependencyFile) const
  {
    std::vector<std::string> cmd;

    cmd.push_back(config_.compiler.empty() ? "c++" : config_.compiler);
    cmd.push_back("-std=c++20");
    cmd.push_back("-MMD");
    cmd.push_back("-MP");
    cmd.push_back("-MF");
    cmd.push_back(dependencyFile.string());

    for (const auto &includeDir : config_.includeDirs)
      cmd.push_back("-I" + includeDir);

    for (const auto &define : config_.defines)
      cmd.push_back("-D" + define);

    for (const auto &flag : config_.flags)
      cmd.push_back(flag);

    cmd.push_back("-c");
    cmd.push_back(source.string());
    cmd.push_back("-o");
    cmd.push_back(object.string());

    return cmd;
  }

} // namespace vix::cli::build
