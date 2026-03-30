/**
 *
 *  @file MakeCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/MakeCommand.hpp>

#include <vix/cli/Style.hpp>
#include <vix/cli/util/Ui.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vix::commands
{
  namespace fs = std::filesystem;
  namespace ui = vix::cli::util;
  using namespace vix::cli::style;

  namespace
  {
    struct FileSpec
    {
      fs::path path;
      std::string content;
    };

    struct GenerateResult
    {
      bool ok = false;
      std::vector<FileSpec> files;
      std::vector<std::string> notes;
      std::string preview;
    };

    struct Options
    {
      std::string kind;
      std::string name;
      std::string dir;
      std::string in;
      std::string name_space;
      bool force = false;
      bool dry_run = false;
      bool print_only = false;
      bool header_only = false;
      bool show_help = false;
    };

    static bool starts_with(std::string_view s, std::string_view pfx)
    {
      return s.size() >= pfx.size() && s.substr(0, pfx.size()) == pfx;
    }

    static bool ends_with(std::string_view s, std::string_view suf)
    {
      return s.size() >= suf.size() && s.substr(s.size() - suf.size()) == suf;
    }

    static std::string trim(std::string s)
    {
      auto is_space = [](unsigned char c)
      { return std::isspace(c) != 0; };

      while (!s.empty() && is_space(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());

      while (!s.empty() && is_space(static_cast<unsigned char>(s.back())))
        s.pop_back();

      return s;
    }

    static std::string to_lower(std::string s)
    {
      for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return s;
    }

    static std::string to_upper(std::string s)
    {
      for (char &c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      return s;
    }

    static fs::path resolve_root(const std::string &dir_opt)
    {
      std::error_code ec{};
      fs::path root = dir_opt.empty() ? fs::current_path(ec) : fs::path(dir_opt);
      if (ec)
        return fs::current_path();

      root = fs::absolute(root, ec);
      if (ec)
        return fs::current_path();

      return root;
    }

    static bool exists_file(const fs::path &p)
    {
      std::error_code ec{};
      return fs::exists(p, ec) && fs::is_regular_file(p, ec) && !ec;
    }

    static bool exists_dir(const fs::path &p)
    {
      std::error_code ec{};
      return fs::exists(p, ec) && fs::is_directory(p, ec) && !ec;
    }

    static bool ensure_dir(const fs::path &p)
    {
      std::error_code ec{};
      if (fs::exists(p, ec))
        return !ec;
      return fs::create_directories(p, ec) && !ec;
    }

    static std::optional<std::string> read_file(const fs::path &p)
    {
      std::ifstream in(p.string(), std::ios::in | std::ios::binary);
      if (!in)
        return std::nullopt;

      std::ostringstream ss;
      ss << in.rdbuf();
      return ss.str();
    }

    static bool write_file_overwrite(const fs::path &p, const std::string &content)
    {
      std::ofstream out(p.string(), std::ios::out | std::ios::binary | std::ios::trunc);
      if (!out)
        return false;
      out << content;
      return static_cast<bool>(out);
    }

    static std::string detect_project_name_from_cmake(const fs::path &root)
    {
      const fs::path cm = root / "CMakeLists.txt";
      auto content = read_file(cm);
      if (!content)
        return "app";

      std::istringstream in(*content);
      std::string line;

      while (std::getline(in, line))
      {
        std::string s = trim(line);
        if (s.empty())
          continue;

        auto pos = to_lower(s).find("project(");
        if (pos == std::string::npos)
          continue;

        auto open = s.find('(', pos);
        auto close = s.find(')', open == std::string::npos ? 0 : open + 1);
        if (open == std::string::npos || close == std::string::npos || close <= open + 1)
          continue;

        std::string inside = trim(s.substr(open + 1, close - (open + 1)));
        if (inside.empty())
          continue;

        const auto sp = inside.find_first_of(" \t\r\n");
        std::string name = sp == std::string::npos ? inside : inside.substr(0, sp);

        if (name.size() >= 2 &&
            ((name.front() == '"' && name.back() == '"') ||
             (name.front() == '\'' && name.back() == '\'')))
        {
          name = name.substr(1, name.size() - 2);
        }

        if (!name.empty())
          return name;
      }

      return "app";
    }

    static bool is_identifier_start(char c)
    {
      return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    static bool is_identifier_char(char c)
    {
      return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    static bool is_valid_cpp_identifier(const std::string &s)
    {
      if (s.empty())
        return false;
      if (!is_identifier_start(s.front()))
        return false;

      for (char c : s)
      {
        if (!is_identifier_char(c))
          return false;
      }
      return true;
    }

    static bool is_reserved_cpp_keyword(const std::string &s)
    {
      static const std::unordered_set<std::string> kw = {
          "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
          "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
          "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
          "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
          "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit",
          "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline",
          "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
          "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
          "register", "reinterpret_cast", "requires", "return", "short", "signed",
          "sizeof", "static", "static_assert", "static_cast", "struct", "switch",
          "template", "this", "thread_local", "throw", "true", "try", "typedef",
          "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
          "volatile", "wchar_t", "while", "xor", "xor_eq"};
      return kw.find(s) != kw.end();
    }

    static bool is_valid_namespace_token(const std::string &s)
    {
      return is_valid_cpp_identifier(s) && !is_reserved_cpp_keyword(s);
    }

    static bool is_valid_namespace_string(const std::string &ns)
    {
      if (ns.empty())
        return true;

      std::size_t start = 0;
      while (start < ns.size())
      {
        const std::size_t pos = ns.find("::", start);
        const std::string token =
            pos == std::string::npos ? ns.substr(start) : ns.substr(start, pos - start);

        if (token.empty() || !is_valid_namespace_token(token))
          return false;

        if (pos == std::string::npos)
          break;

        start = pos + 2;
      }

      return true;
    }

    static std::vector<std::string> split_namespace(const std::string &ns)
    {
      std::vector<std::string> out;
      if (ns.empty())
        return out;

      std::size_t start = 0;
      while (start < ns.size())
      {
        std::size_t pos = ns.find("::", start);
        if (pos == std::string::npos)
        {
          out.push_back(ns.substr(start));
          break;
        }

        out.push_back(ns.substr(start, pos - start));
        start = pos + 2;
      }

      return out;
    }

    static std::string join_path_guard_parts(const std::vector<std::string> &parts)
    {
      std::string out;
      for (const auto &part : parts)
      {
        if (!out.empty())
          out += "_";

        for (char c : part)
        {
          if (std::isalnum(static_cast<unsigned char>(c)) != 0)
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
          else
            out.push_back('_');
        }
      }
      return out;
    }

    static std::string make_include_guard(const fs::path &file)
    {
      std::vector<std::string> parts;
      for (const auto &p : file)
      {
        std::string s = p.string();
        if (!s.empty())
          parts.push_back(s);
      }

      std::string out = join_path_guard_parts(parts);
      if (out.empty())
        out = "VIX_GENERATED_HPP";
      return out;
    }

    static std::string snake_case(std::string s)
    {
      std::string out;
      out.reserve(s.size() * 2);

      for (std::size_t i = 0; i < s.size(); ++i)
      {
        const char c = s[i];

        if (std::isalnum(static_cast<unsigned char>(c)) == 0)
        {
          if (!out.empty() && out.back() != '_')
            out.push_back('_');
          continue;
        }

        if (std::isupper(static_cast<unsigned char>(c)) != 0)
        {
          if (!out.empty() && out.back() != '_' &&
              std::islower(static_cast<unsigned char>(out.back())) != 0)
          {
            out.push_back('_');
          }
          out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        else
        {
          out.push_back(c);
        }
      }

      while (!out.empty() && out.front() == '_')
        out.erase(out.begin());
      while (!out.empty() && out.back() == '_')
        out.pop_back();

      return out.empty() ? "item" : out;
    }

    static std::string namespace_open(const std::string &ns)
    {
      if (ns.empty())
        return {};

      std::ostringstream o;
      const auto parts = split_namespace(ns);
      for (const auto &p : parts)
        o << "namespace " << p << "\n{\n";
      return o.str();
    }

    static std::string namespace_close(const std::string &ns)
    {
      if (ns.empty())
        return {};

      std::ostringstream o;
      const auto parts = split_namespace(ns);
      for (std::size_t i = 0; i < parts.size(); ++i)
        o << "}\n";
      return o.str();
    }

    static std::string qualified_name(const std::string &ns, const std::string &name)
    {
      return ns.empty() ? name : (ns + "::" + name);
    }

    struct Layout
    {
      fs::path root;
      fs::path base;
      fs::path include_dir;
      fs::path src_dir;
      fs::path tests_dir;
      std::string project;
      std::string default_namespace;
      bool in_module = false;
      std::string module_name;
    };

    static std::string guess_default_namespace(const std::string &project)
    {
      std::string s = snake_case(project);
      s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
      if (s.empty())
        return "app";
      if (!is_identifier_start(s.front()))
        s.insert(s.begin(), 'n');
      if (is_reserved_cpp_keyword(s))
        s += "_ns";
      return s;
    }

    static Layout resolve_layout(const fs::path &root, const Options &opt)
    {
      Layout layout;
      layout.root = root;
      layout.project = detect_project_name_from_cmake(root);
      layout.default_namespace = guess_default_namespace(layout.project);

      layout.base = opt.in.empty() ? root : fs::absolute(root / opt.in);
      layout.tests_dir = root / "tests";

      const fs::path relative = fs::relative(layout.base, root);
      const std::string rel = relative.generic_string();

      if (starts_with(rel, "modules/"))
      {
        const auto first_slash = rel.find('/');
        const auto second_slash = rel.find('/', first_slash + 1);
        std::string module = rel.substr(first_slash + 1,
                                        second_slash == std::string::npos
                                            ? std::string::npos
                                            : second_slash - (first_slash + 1));

        if (!module.empty())
        {
          layout.in_module = true;
          layout.module_name = module;
          layout.include_dir = root / "modules" / module / "include" / module;
          layout.src_dir = root / "modules" / module / "src";
          layout.default_namespace += "::" + module;
          return layout;
        }
      }

      layout.include_dir = root / "include";
      layout.src_dir = root / "src";
      return layout;
    }

    static std::string doxygen_file_header(const std::string &filename)
    {
      std::ostringstream o;
      o << "/**\n";
      o << " *\n";
      o << " *  @file " << filename << "\n";
      o << " *\n";
      o << " */\n";
      return o.str();
    }

    static std::string header_preamble(const fs::path &file)
    {
      const std::string guard = make_include_guard(file);

      std::ostringstream o;
      o << doxygen_file_header(file.filename().string());
      o << "#ifndef " << guard << "\n";
      o << "#define " << guard << "\n\n";
      return o.str();
    }

    static std::string header_postamble()
    {
      return "\n#endif\n";
    }

    static std::string source_preamble(const fs::path &file)
    {
      return doxygen_file_header(file.filename().string());
    }

    static std::string class_header_content(const std::string &ns,
                                            const std::string &name,
                                            const fs::path &file)
    {
      std::ostringstream o;
      o << header_preamble(file);
      o << "#include <string>\n";
      o << "#include <utility>\n\n";
      o << namespace_open(ns);
      o << "class " << name << "\n";
      o << "{\n";
      o << "public:\n";
      o << "  " << name << "();\n";
      o << "  explicit " << name << "(std::string id);\n";
      o << "  virtual ~" << name << "() = default;\n\n";
      o << "  " << name << "(const " << name << " &) = default;\n";
      o << "  " << name << "(" << name << " &&) noexcept = default;\n";
      o << "  " << name << " &operator=(const " << name << " &) = default;\n";
      o << "  " << name << " &operator=(" << name << " &&) noexcept = default;\n\n";
      o << "  [[nodiscard]] const std::string &id() const noexcept;\n";
      o << "  void set_id(std::string value);\n\n";
      o << "private:\n";
      o << "  std::string id_;\n";
      o << "};\n";
      o << namespace_close(ns);
      o << header_postamble();
      return o.str();
    }

    static std::string class_source_content(const std::string &ns,
                                            const std::string &name,
                                            const fs::path &header_path)
    {
      std::ostringstream o;
      o << source_preamble(header_path.filename().replace_extension(".cpp"));
      o << "#include <" << header_path.generic_string() << ">\n\n";
      o << qualified_name(ns, name) << "::" << name << "() = default;\n\n";
      o << qualified_name(ns, name) << "::" << name << "(std::string id)\n";
      o << "    : id_(std::move(id))\n";
      o << "{\n";
      o << "}\n\n";
      o << "const std::string &" << qualified_name(ns, name) << "::id() const noexcept\n";
      o << "{\n";
      o << "  return id_;\n";
      o << "}\n\n";
      o << "void " << qualified_name(ns, name) << "::set_id(std::string value)\n";
      o << "{\n";
      o << "  id_ = std::move(value);\n";
      o << "}\n";
      return o.str();
    }

    static std::string struct_header_content(const std::string &ns,
                                             const std::string &name,
                                             const fs::path &file)
    {
      std::ostringstream o;
      o << header_preamble(file);
      o << "#include <string>\n\n";
      o << namespace_open(ns);
      o << "struct " << name << "\n";
      o << "{\n";
      o << "  std::string id;\n";
      o << "  std::string label;\n";
      o << "  bool valid = false;\n";
      o << "};\n";
      o << namespace_close(ns);
      o << header_postamble();
      return o.str();
    }

    static std::string enum_header_content(const std::string &ns,
                                           const std::string &name,
                                           const fs::path &file)
    {
      std::ostringstream o;
      o << header_preamble(file);
      o << "#include <string_view>\n\n";
      o << namespace_open(ns);
      o << "enum class " << name << "\n";
      o << "{\n";
      o << "  unknown,\n";
      o << "  active,\n";
      o << "  disabled,\n";
      o << "};\n\n";
      o << "[[nodiscard]] constexpr std::string_view to_string(" << name << " value) noexcept\n";
      o << "{\n";
      o << "  switch (value)\n";
      o << "  {\n";
      o << "    case " << name << "::unknown:\n";
      o << "      return \"unknown\";\n";
      o << "    case " << name << "::active:\n";
      o << "      return \"active\";\n";
      o << "    case " << name << "::disabled:\n";
      o << "      return \"disabled\";\n";
      o << "  }\n";
      o << "  return \"unknown\";\n";
      o << "}\n";
      o << namespace_close(ns);
      o << header_postamble();
      return o.str();
    }

    static std::string function_header_content(const std::string &ns,
                                               const std::string &name,
                                               const fs::path &file)
    {
      std::ostringstream o;
      o << header_preamble(file);
      o << "#include <string_view>\n\n";
      o << namespace_open(ns);
      o << "[[nodiscard]] bool " << name << "(std::string_view value);\n";
      o << namespace_close(ns);
      o << header_postamble();
      return o.str();
    }

    static std::string function_source_content(const std::string &ns,
                                               const std::string &name,
                                               const fs::path &header_path)
    {
      std::ostringstream o;
      o << source_preamble(header_path.filename().replace_extension(".cpp"));
      o << "#include <" << header_path.generic_string() << ">\n\n";
      o << "bool " << qualified_name(ns, name) << "(std::string_view value)\n";
      o << "{\n";
      o << "  return !value.empty();\n";
      o << "}\n";
      return o.str();
    }

    static std::string lambda_snippet_content(const std::string &name)
    {
      std::ostringstream o;
      o << "auto " << name << " = []<typename T>(const T &value)\n";
      o << "{\n";
      o << "  return value;\n";
      o << "};\n";
      return o.str();
    }

    static std::string concept_header_content(const std::string &ns,
                                              const std::string &name,
                                              const fs::path &file)
    {
      std::ostringstream o;
      o << header_preamble(file);
      o << "#include <concepts>\n";
      o << "#include <type_traits>\n\n";
      o << namespace_open(ns);
      o << "template <typename T>\n";
      o << "concept " << name << " = requires(T value)\n";
      o << "{\n";
      o << "  { value == value } -> std::convertible_to<bool>;\n";
      o << "} && !std::is_reference_v<T>;\n";
      o << namespace_close(ns);
      o << header_postamble();
      return o.str();
    }

    static std::string exception_header_content(const std::string &ns,
                                                const std::string &name,
                                                const fs::path &file)
    {
      std::ostringstream o;
      o << header_preamble(file);
      o << "#include <exception>\n";
      o << "#include <string>\n";
      o << "#include <utility>\n\n";
      o << namespace_open(ns);
      o << "class " << name << " final : public std::exception\n";
      o << "{\n";
      o << "public:\n";
      o << "  explicit " << name << "(std::string message);\n";
      o << "  [[nodiscard]] const char *what() const noexcept override;\n\n";
      o << "private:\n";
      o << "  std::string message_;\n";
      o << "};\n";
      o << namespace_close(ns);
      o << header_postamble();
      return o.str();
    }

    static std::string exception_source_content(const std::string &ns,
                                                const std::string &name,
                                                const fs::path &header_path)
    {
      std::ostringstream o;
      o << source_preamble(header_path.filename().replace_extension(".cpp"));
      o << "#include <" << header_path.generic_string() << ">\n\n";
      o << qualified_name(ns, name) << "::" << name << "(std::string message)\n";
      o << "    : message_(std::move(message))\n";
      o << "{\n";
      o << "}\n\n";
      o << "const char *" << qualified_name(ns, name) << "::what() const noexcept\n";
      o << "{\n";
      o << "  return message_.c_str();\n";
      o << "}\n";
      return o.str();
    }

    static std::string test_source_content(const std::string &ns,
                                           const std::string &name)
    {
      const std::string suite = name;
      const std::string test_name = "DefaultConstruction";

      std::ostringstream o;
      o << source_preamble(fs::path("test_" + snake_case(name) + ".cpp"));
      o << "#include <gtest/gtest.h>\n\n";
      if (!ns.empty())
        o << "namespace " << ns << "\n{\n}\n\n";
      o << "TEST(" << suite << ", " << test_name << ")\n";
      o << "{\n";
      o << "  EXPECT_TRUE(true);\n";
      o << "}\n";
      return o.str();
    }

    static bool collect_write_targets(const std::vector<FileSpec> &files,
                                      bool force,
                                      std::string &error_message)
    {
      for (const auto &f : files)
      {
        if (exists_file(f.path) && !force)
        {
          error_message = "File already exists: " + f.path.string() + " (use --force)";
          return false;
        }
      }
      return true;
    }

    static bool write_generated_files(const std::vector<FileSpec> &files,
                                      std::string &error_message)
    {
      for (const auto &f : files)
      {
        const fs::path parent = f.path.parent_path();
        if (!parent.empty() && !ensure_dir(parent))
        {
          error_message = "Failed to create directory: " + parent.string();
          return false;
        }

        if (!write_file_overwrite(f.path, f.content))
        {
          error_message = "Failed to write file: " + f.path.string();
          return false;
        }
      }
      return true;
    }

    static std::optional<std::string> parse_name_from_user(const std::string &raw)
    {
      std::string name = trim(raw);
      if (name.empty())
        return std::nullopt;
      if (!is_valid_cpp_identifier(name))
        return std::nullopt;
      if (is_reserved_cpp_keyword(name))
        return std::nullopt;
      return name;
    }

    static GenerateResult generate_class(const Layout &layout,
                                         const Options &opt,
                                         const std::string &ns,
                                         const std::string &name)
    {
      GenerateResult r;
      const fs::path header = layout.include_dir / (name + ".hpp");
      const fs::path source = layout.src_dir / (name + ".cpp");

      r.files.push_back({header, class_header_content(ns, name, fs::relative(header, layout.root))});
      if (!opt.header_only)
      {
        r.files.push_back({source,
                           class_source_content(ns, name, fs::relative(header, layout.root))});
      }

      r.notes.push_back("Generated modern class skeleton with Rule of 5 friendly defaults.");
      r.notes.push_back("Header path: " + header.string());
      if (!opt.header_only)
        r.notes.push_back("Source path: " + source.string());

      r.ok = true;
      return r;
    }

    static GenerateResult generate_struct(const Layout &layout,
                                          const Options &opt,
                                          const std::string &ns,
                                          const std::string &name)
    {
      GenerateResult r;
      (void)opt;

      const fs::path header = layout.include_dir / (name + ".hpp");
      r.files.push_back({header, struct_header_content(ns, name, fs::relative(header, layout.root))});
      r.notes.push_back("Generated plain data struct.");
      r.ok = true;
      return r;
    }

    static GenerateResult generate_enum(const Layout &layout,
                                        const Options &opt,
                                        const std::string &ns,
                                        const std::string &name)
    {
      GenerateResult r;
      (void)opt;

      const fs::path header = layout.include_dir / (name + ".hpp");
      r.files.push_back({header, enum_header_content(ns, name, fs::relative(header, layout.root))});
      r.notes.push_back("Generated enum class with constexpr to_string.");
      r.ok = true;
      return r;
    }

    static GenerateResult generate_function(const Layout &layout,
                                            const Options &opt,
                                            const std::string &ns,
                                            const std::string &name)
    {
      GenerateResult r;
      const fs::path header = layout.include_dir / (name + ".hpp");
      const fs::path source = layout.src_dir / (name + ".cpp");

      r.files.push_back({header,
                         function_header_content(ns, name, fs::relative(header, layout.root))});
      if (!opt.header_only)
      {
        r.files.push_back({source,
                           function_source_content(ns, name, fs::relative(header, layout.root))});
      }

      r.notes.push_back("Generated free function skeleton.");
      r.ok = true;
      return r;
    }

    static GenerateResult generate_lambda(const Layout &layout,
                                          const Options &opt,
                                          const std::string &ns,
                                          const std::string &name)
    {
      GenerateResult r;
      (void)layout;
      (void)ns;

      r.preview = lambda_snippet_content(name);
      r.notes.push_back("Lambda generation is snippet-first.");
      r.notes.push_back("Use --print or --dry-run to copy the snippet.");
      r.ok = true;

      if (!opt.print_only && !opt.dry_run)
      {
        const fs::path header = layout.include_dir / (name + ".hpp");
        std::ostringstream o;
        o << header_preamble(fs::relative(header, layout.root));
        if (!ns.empty())
          o << namespace_open(ns);
        o << r.preview;
        if (!ns.empty())
          o << namespace_close(ns);
        o << header_postamble();

        r.files.push_back({header, o.str()});
      }

      return r;
    }

    static GenerateResult generate_concept(const Layout &layout,
                                           const Options &opt,
                                           const std::string &ns,
                                           const std::string &name)
    {
      GenerateResult r;
      (void)opt;

      const fs::path header = layout.include_dir / (name + ".hpp");
      r.files.push_back({header, concept_header_content(ns, name, fs::relative(header, layout.root))});
      r.notes.push_back("Generated C++20 concept.");
      r.ok = true;
      return r;
    }

    static GenerateResult generate_exception(const Layout &layout,
                                             const Options &opt,
                                             const std::string &ns,
                                             const std::string &name)
    {
      GenerateResult r;
      const fs::path header = layout.include_dir / (name + ".hpp");
      const fs::path source = layout.src_dir / (name + ".cpp");

      r.files.push_back({header,
                         exception_header_content(ns, name, fs::relative(header, layout.root))});
      if (!opt.header_only)
      {
        r.files.push_back({source,
                           exception_source_content(ns, name, fs::relative(header, layout.root))});
      }

      r.notes.push_back("Generated std::exception derived type.");
      r.ok = true;
      return r;
    }

    static GenerateResult generate_test(const Layout &layout,
                                        const Options &opt,
                                        const std::string &ns,
                                        const std::string &name)
    {
      GenerateResult r;
      (void)opt;

      const fs::path file = layout.tests_dir / ("test_" + snake_case(name) + ".cpp");
      r.files.push_back({file, test_source_content(ns, name)});
      r.notes.push_back("Generated GoogleTest skeleton.");
      r.ok = true;
      return r;
    }

    static GenerateResult generate_module_stub(const Layout &layout,
                                               const Options &opt,
                                               const std::string &ns,
                                               const std::string &name)
    {
      GenerateResult r;
      (void)layout;
      (void)opt;
      (void)ns;

      r.notes.push_back("For module generation, prefer: vix modules add " + snake_case(name));
      r.ok = true;
      return r;
    }

    static Options parse_args(const std::vector<std::string> &args)
    {
      Options opt;

      auto is_opt = [](const std::string &s)
      { return !s.empty() && s[0] == '-'; };

      if (args.empty())
      {
        opt.show_help = true;
        return opt;
      }

      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const auto &a = args[i];

        if (a == "-h" || a == "--help")
        {
          opt.show_help = true;
        }
        else if (a == "--force")
        {
          opt.force = true;
        }
        else if (a == "--dry-run")
        {
          opt.dry_run = true;
        }
        else if (a == "--print")
        {
          opt.print_only = true;
        }
        else if (a == "--header-only")
        {
          opt.header_only = true;
        }
        else if (a == "-d" || a == "--dir")
        {
          if (i + 1 < args.size() && !is_opt(args[i + 1]))
            opt.dir = args[++i];
        }
        else if (starts_with(a, "--dir="))
        {
          opt.dir = a.substr(std::string("--dir=").size());
        }
        else if (a == "--in")
        {
          if (i + 1 < args.size() && !is_opt(args[i + 1]))
            opt.in = args[++i];
        }
        else if (starts_with(a, "--in="))
        {
          opt.in = a.substr(std::string("--in=").size());
        }
        else if (a == "--namespace")
        {
          if (i + 1 < args.size() && !is_opt(args[i + 1]))
            opt.name_space = args[++i];
        }
        else if (starts_with(a, "--namespace="))
        {
          opt.name_space = a.substr(std::string("--namespace=").size());
        }
        else if (opt.kind.empty() && !is_opt(a))
        {
          opt.kind = a;
        }
        else if (opt.name.empty() && !is_opt(a))
        {
          opt.name = a;
        }
      }

      return opt;
    }

    static bool validate_kind(const std::string &kind)
    {
      static const std::set<std::string> allowed = {
          "class", "struct", "enum", "function", "lambda",
          "concept", "exception", "test", "module"};
      return allowed.find(kind) != allowed.end();
    }

    static void print_summary(const Layout &layout,
                              const Options &opt,
                              const std::string &ns,
                              const std::string &name,
                              const GenerateResult &result)
    {
      ui::section(std::cout, "Make");
      ui::kv(std::cout, "kind", opt.kind, 12);
      ui::kv(std::cout, "name", name, 12);
      ui::kv(std::cout, "namespace", ns.empty() ? "(none)" : ns, 12);
      ui::kv(std::cout, "project", layout.project, 12);
      if (layout.in_module)
        ui::kv(std::cout, "module", layout.module_name, 12);

      if (opt.dry_run)
        ui::kv(std::cout, "mode", "dry-run", 12);
      else if (opt.print_only)
        ui::kv(std::cout, "mode", "print", 12);
      else
        ui::kv(std::cout, "mode", "write", 12);

      if (!result.files.empty())
      {
        for (const auto &f : result.files)
          ui::kv(std::cout, "file", f.path.string(), 12);
      }

      for (const auto &note : result.notes)
        ui::warn_line(std::cout, note);
    }

    static GenerateResult generate_dispatch(const Layout &layout,
                                            const Options &opt,
                                            const std::string &ns,
                                            const std::string &name)
    {
      const std::string kind = to_lower(opt.kind);

      if (kind == "class")
        return generate_class(layout, opt, ns, name);
      if (kind == "struct")
        return generate_struct(layout, opt, ns, name);
      if (kind == "enum")
        return generate_enum(layout, opt, ns, name);
      if (kind == "function")
        return generate_function(layout, opt, ns, name);
      if (kind == "lambda")
        return generate_lambda(layout, opt, ns, name);
      if (kind == "concept")
        return generate_concept(layout, opt, ns, name);
      if (kind == "exception")
        return generate_exception(layout, opt, ns, name);
      if (kind == "test")
        return generate_test(layout, opt, ns, name);
      if (kind == "module")
        return generate_module_stub(layout, opt, ns, name);

      GenerateResult r;
      r.ok = false;
      return r;
    }
  } // namespace

  int MakeCommand::run(const std::vector<std::string> &args)
  {
    const Options opt = parse_args(args);

    if (opt.show_help)
      return help();

    if (opt.kind.empty())
    {
      ui::err_line(std::cout, "Missing make kind.");
      ui::warn_line(std::cout, "Example: vix make class User");
      return 1;
    }

    if (!validate_kind(to_lower(opt.kind)))
    {
      ui::err_line(std::cout, "Unknown make kind: " + opt.kind);
      ui::warn_line(std::cout, "Run: vix make --help");
      return 1;
    }

    if (to_lower(opt.kind) != "module" && opt.name.empty())
    {
      ui::err_line(std::cout, "Missing name.");
      ui::warn_line(std::cout, "Example: vix make class User");
      return 1;
    }

    const fs::path root = resolve_root(opt.dir);
    const Layout layout = resolve_layout(root, opt);

    const std::string ns = opt.name_space.empty() ? layout.default_namespace : opt.name_space;
    if (!is_valid_namespace_string(ns))
    {
      ui::err_line(std::cout, "Invalid namespace: " + ns);
      return 1;
    }

    std::string name = opt.name;
    if (to_lower(opt.kind) == "module" && name.empty())
      name = "module";

    const auto parsed_name = parse_name_from_user(name);
    if (!parsed_name)
    {
      ui::err_line(std::cout, "Invalid C++ name: " + name);
      ui::warn_line(std::cout, "Use a valid identifier that is not a reserved C++ keyword.");
      return 1;
    }

    const GenerateResult result = generate_dispatch(layout, opt, ns, *parsed_name);
    if (!result.ok)
    {
      ui::err_line(std::cout, "Generation failed.");
      return 1;
    }

    print_summary(layout, opt, ns, *parsed_name, result);

    if (!result.preview.empty())
    {
      std::cout << "\n"
                << BOLD << "Preview" << RESET << "\n\n"
                << result.preview << "\n";
    }

    if (opt.print_only || opt.dry_run)
    {
      ui::ok_line(std::cout, opt.print_only ? "Printed" : "Dry-run complete");
      return 0;
    }

    std::string error_message;
    if (!collect_write_targets(result.files, opt.force, error_message))
    {
      ui::err_line(std::cout, error_message);
      return 1;
    }

    if (!write_generated_files(result.files, error_message))
    {
      ui::err_line(std::cout, error_message);
      return 1;
    }

    ui::ok_line(std::cout, "Generated");
    ui::kv(std::cout, "count", std::to_string(result.files.size()), 12);

    if (to_lower(opt.kind) == "module")
      ui::warn_line(std::cout, "Tip: run vix modules add " + snake_case(*parsed_name));

    return 0;
  }

  int MakeCommand::help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix make <kind> <name> [options]\n\n";

    out << "Goal:\n";
    out << "  Generate C++ code faster with safe, predictable scaffolding.\n";
    out << "  This command is snippet-friendly for small constructs and file-oriented for real project code.\n\n";

    out << "Kinds:\n";
    out << "  class       Generate a class (.hpp + .cpp by default)\n";
    out << "  struct      Generate a plain data struct (.hpp)\n";
    out << "  enum        Generate an enum class with to_string (.hpp)\n";
    out << "  function    Generate a free function (.hpp + .cpp by default)\n";
    out << "  lambda      Generate a modern generic lambda snippet\n";
    out << "  concept     Generate a C++20 concept (.hpp)\n";
    out << "  exception   Generate a std::exception derived type\n";
    out << "  test        Generate a GoogleTest skeleton\n";
    out << "  module      Hint for module creation workflow\n\n";

    out << "Options:\n";
    out << "  -d, --dir <path>          Project root (default: current directory)\n";
    out << "  --in <path>               Target area inside the project (example: modules/auth)\n";
    out << "  --namespace <ns>          Override namespace\n";
    out << "  --header-only             Generate only header files when supported\n";
    out << "  --print                   Print snippet/preview without writing files\n";
    out << "  --dry-run                 Show what would be generated without writing files\n";
    out << "  --force                   Overwrite existing files\n";
    out << "  -h, --help                Show help\n\n";

    out << "Examples:\n";
    out << "  vix make class User\n";
    out << "  vix make struct Claims --namespace auth\n";
    out << "  vix make enum Status --in modules/auth\n";
    out << "  vix make function parse_token --in modules/auth\n";
    out << "  vix make lambda visit_all --print\n";
    out << "  vix make concept Hashable\n";
    out << "  vix make exception InvalidToken --in modules/auth\n";
    out << "  vix make test AuthService\n\n";

    out << "Behavior:\n";
    out << "  - In a regular project, generated headers go to include/ and sources to src/.\n";
    out << "  - In a module path like modules/auth, headers go to modules/auth/include/auth/ and sources to modules/auth/src/.\n";
    out << "  - Existing files are never overwritten unless --force is set.\n\n";

    return 0;
  }

} // namespace vix::commands
