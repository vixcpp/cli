/**
 *
 *  @file ConfigGenerator.cpp
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
#include <vix/cli/commands/make/generators/ConfigGenerator.hpp>
#include <vix/cli/commands/make/MakeUtils.hpp>

#include <filesystem>
#include <sstream>
#include <string>

namespace vix::cli::make::generators
{
  namespace fs = std::filesystem;

  namespace
  {
    [[nodiscard]] ConfigSpec make_default_spec_from_context(const MakeContext &ctx)
    {
      (void)ctx;

      ConfigSpec spec;
      spec.with_server = true;
      spec.with_logging = true;
      spec.with_waf = true;
      spec.with_websocket = false;
      spec.with_database = false;
      return spec;
    }

    [[nodiscard]] MakeResult validate_spec(const MakeContext &ctx,
                                           const ConfigSpec &spec)
    {
      MakeResult result;

      if (trim(ctx.name).empty())
      {
        result.ok = false;
        result.error = "Config name is required.";
        return result;
      }

      if (!is_valid_cpp_identifier(ctx.name))
      {
        result.ok = false;
        result.error = "Invalid config name: " + ctx.name;
        return result;
      }

      if (is_reserved_cpp_keyword(ctx.name))
      {
        result.ok = false;
        result.error = "Reserved C++ keyword is not allowed: " + ctx.name;
        return result;
      }

      if (!spec.with_server &&
          !spec.with_logging &&
          !spec.with_waf &&
          !spec.with_websocket &&
          !spec.with_database)
      {
        result.ok = false;
        result.error = "At least one config section must be enabled.";
        return result;
      }

      result.ok = true;
      return result;
    }

    [[nodiscard]] void append_server(std::ostringstream &out,
                                     bool add_comma)
    {
      out << "  \"server\": {\n";
      out << "    \"port\": 8080,\n";
      out << "    \"request_timeout\": 2000,\n";
      out << "    \"io_threads\": 0,\n";
      out << "    \"session_timeout_sec\": 300,\n";
      out << "    \"bench_mode\": true\n";
      out << "  }";

      if (add_comma)
        out << ",";

      out << "\n";
    }

    [[nodiscard]] void append_logging(std::ostringstream &out,
                                      bool add_comma)
    {
      out << "  \"logging\": {\n";
      out << "    \"async\": true,\n";
      out << "    \"queue_max\": 20000,\n";
      out << "    \"drop_on_overflow\": true\n";
      out << "  }";

      if (add_comma)
        out << ",";

      out << "\n";
    }

    [[nodiscard]] void append_waf(std::ostringstream &out,
                                  bool add_comma)
    {
      out << "  \"waf\": {\n";
      out << "    \"mode\": \"off\",\n";
      out << "    \"max_target_len\": 4096,\n";
      out << "    \"max_body_bytes\": 1048576\n";
      out << "  }";

      if (add_comma)
        out << ",";

      out << "\n";
    }

    [[nodiscard]] void append_websocket(std::ostringstream &out,
                                        bool add_comma)
    {
      out << "  \"websocket\": {\n";
      out << "    \"port\": 9090,\n";
      out << "    \"max_message_size\": 65536,\n";
      out << "    \"idle_timeout\": 60,\n";
      out << "    \"ping_interval\": 30,\n";
      out << "    \"enable_deflate\": true,\n";
      out << "    \"auto_ping_pong\": true\n";
      out << "  }";

      if (add_comma)
        out << ",";

      out << "\n";
    }

    [[nodiscard]] void append_database(std::ostringstream &out,
                                       bool add_comma)
    {
      out << "  \"database\": {\n";
      out << "    \"default\": {\n";
      out << "      \"host\": \"localhost\",\n";
      out << "      \"port\": 3306,\n";
      out << "      \"name\": \"mydb\",\n";
      out << "      \"user\": \"myuser\",\n";
      out << "      \"password\": \"\"\n";
      out << "    }\n";
      out << "  }";

      if (add_comma)
        out << ",";

      out << "\n";
    }

    [[nodiscard]] std::string build_config_content(const MakeContext &ctx,
                                                   const ConfigSpec &spec)
    {
      std::ostringstream out;
      std::vector<std::string> sections;

      if (spec.with_server)
        sections.push_back("server");

      if (spec.with_logging)
        sections.push_back("logging");

      if (spec.with_waf)
        sections.push_back("waf");

      if (spec.with_websocket)
        sections.push_back("websocket");

      if (spec.with_database)
        sections.push_back("database");

      out << "{\n";

      for (std::size_t i = 0; i < sections.size(); ++i)
      {
        const bool add_comma = (i + 1) < sections.size();
        const std::string &section = sections[i];

        if (section == "server")
          append_server(out, add_comma);
        else if (section == "logging")
          append_logging(out, add_comma);
        else if (section == "waf")
          append_waf(out, add_comma);
        else if (section == "websocket")
          append_websocket(out, add_comma);
        else if (section == "database")
          append_database(out, add_comma);
      }

      out << "}\n";

      (void)ctx;
      return out.str();
    }

    [[nodiscard]] std::string build_preview(const MakeContext &ctx,
                                            const ConfigSpec &spec,
                                            const fs::path &file_path)
    {
      std::ostringstream out;

      out << "kind: config\n";
      out << "name: " << ctx.name << "\n";
      out << "file: " << file_path.string() << "\n";
      out << "sections:";

      if (spec.with_server)
        out << " server";

      if (spec.with_logging)
        out << " logging";

      if (spec.with_waf)
        out << " waf";

      if (spec.with_websocket)
        out << " websocket";

      if (spec.with_database)
        out << " database";

      out << "\n";
      return out.str();
    }
  } // namespace

  MakeResult generate_config(const MakeContext &ctx)
  {
    return generate_config(ctx, make_default_spec_from_context(ctx));
  }

  MakeResult generate_config(const MakeContext &ctx,
                             const ConfigSpec &spec)
  {
    const MakeResult validation = validate_spec(ctx, spec);
    if (!validation.ok)
      return validation;

    MakeResult result;

    const fs::path file_path = ctx.layout.base / (snake_case(ctx.name) + ".json");

    result.files.push_back(
        MakeFile{
            file_path,
            build_config_content(ctx, spec)});

    result.preview = build_preview(ctx, spec, file_path);
    result.notes.push_back("Generated JSON runtime configuration.");

    if (spec.with_server)
      result.notes.push_back("Includes server section.");

    if (spec.with_logging)
      result.notes.push_back("Includes logging section.");

    if (spec.with_waf)
      result.notes.push_back("Includes waf section.");

    if (spec.with_websocket)
      result.notes.push_back("Includes websocket section.");

    if (spec.with_database)
      result.notes.push_back("Includes database section.");

    result.ok = true;
    return result;
  }

} // namespace vix::cli::make::generators
