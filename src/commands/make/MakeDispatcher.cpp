/**
 *
 *  @file MakeDispatcher.cpp
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
#include <vix/cli/commands/make/MakeDispatcher.hpp>
#include <vix/cli/commands/make/MakeUtils.hpp>
#include <vix/cli/commands/make/generators/EnumGenerator.hpp>
#include <vix/cli/commands/make/generators/StructGenerator.hpp>
#include <vix/cli/commands/make/generators/ClassGenerator.hpp>
#include <vix/cli/commands/make/generators/FunctionGenerator.hpp>
#include <vix/cli/commands/make/generators/LambdaGenerator.hpp>
#include <vix/cli/commands/make/generators/ConceptGenerator.hpp>
#include <vix/cli/commands/make/generators/ExceptionGenerator.hpp>
#include <vix/cli/commands/make/generators/TestGenerator.hpp>

#include <utility>

namespace vix::cli::make
{
  namespace
  {
    [[nodiscard]] MakeResult make_error(std::string message)
    {
      MakeResult result;
      result.ok = false;
      result.error = std::move(message);
      return result;
    }

    [[nodiscard]] MakeResult make_not_implemented_result(
        const MakeContext &ctx,
        const std::string &what)
    {
      MakeResult result;
      result.ok = false;
      result.error = what + " generator is not implemented yet.";
      result.notes.push_back("kind: " + std::string(to_string(ctx.kind)));
      result.notes.push_back("name: " + ctx.name);
      result.notes.push_back("namespace: " +
                             (ctx.name_space.empty() ? std::string("(none)")
                                                     : ctx.name_space));
      return result;
    }

    [[nodiscard]] bool requires_name(MakeKind kind) noexcept
    {
      switch (kind)
      {
      case MakeKind::Unknown:
        return false;
      case MakeKind::Class:
      case MakeKind::Struct:
      case MakeKind::Enum:
      case MakeKind::Function:
      case MakeKind::Lambda:
      case MakeKind::Concept:
      case MakeKind::Exception:
      case MakeKind::Test:
      case MakeKind::Module:
        return true;
      }

      return true;
    }

    [[nodiscard]] MakeResult validate_options(const MakeOptions &options,
                                              MakeKind kind)
    {
      if (!is_valid_make_kind(kind))
        return make_error("Unknown make kind: " + options.kind);

      if (requires_name(kind))
      {
        const std::string trimmed_name = trim(options.name);
        if (trimmed_name.empty())
          return make_error("Missing name.");

        if (!is_valid_cpp_identifier(trimmed_name))
          return make_error("Invalid C++ name: " + trimmed_name);

        if (is_reserved_cpp_keyword(trimmed_name))
          return make_error("Reserved C++ keyword is not allowed: " +
                            trimmed_name);
      }

      if (!options.name_space.empty() &&
          !is_valid_namespace_string(options.name_space))
      {
        return make_error("Invalid namespace: " + options.name_space);
      }

      return MakeResult{true, {}, {}, {}, {}};
    }

    [[nodiscard]] MakeContext build_context(const MakeOptions &options,
                                            MakeKind kind)
    {
      MakeContext ctx;
      ctx.options = options;
      ctx.kind = kind;

      const auto root = resolve_root(options.dir);
      ctx.layout = resolve_layout(root, options.in);

      ctx.name = trim(options.name);
      ctx.name_space =
          options.name_space.empty() ? ctx.layout.default_namespace
                                     : options.name_space;

      return ctx;
    }

    [[nodiscard]] MakeResult dispatch_class(const MakeContext &ctx)
    {
      return generators::generate_class(ctx);
    }

    [[nodiscard]] MakeResult dispatch_struct(const MakeContext &ctx)
    {
      return generators::generate_struct(ctx);
    }

    [[nodiscard]] MakeResult dispatch_enum(const MakeContext &ctx)
    {
      return generators::generate_enum(ctx);
    }

    [[nodiscard]] MakeResult dispatch_function(const MakeContext &ctx)
    {
      return generators::generate_function(ctx);
    }

    [[nodiscard]] MakeResult dispatch_lambda(const MakeContext &ctx)
    {
      return generators::generate_lambda(ctx);
    }

    [[nodiscard]] MakeResult dispatch_concept(const MakeContext &ctx)
    {
      return generators::generate_concept(ctx);
    }

    [[nodiscard]] MakeResult dispatch_exception(const MakeContext &ctx)
    {
      return generators::generate_exception(ctx);
    }

    [[nodiscard]] MakeResult dispatch_test(const MakeContext &ctx)
    {
      return generators::generate_test(ctx);
    }

    [[nodiscard]] MakeResult dispatch_module(const MakeContext &ctx)
    {
      MakeResult result;
      result.ok = false;
      result.error = "module generation belongs to 'vix modules'.";
      result.notes.push_back("Use: vix modules add " + snake_case(ctx.name));
      return result;
    }
  } // namespace

  MakeKind parse_make_kind(const std::string &kind)
  {
    const std::string k = to_lower(trim(kind));

    if (k == "class")
      return MakeKind::Class;
    if (k == "struct")
      return MakeKind::Struct;
    if (k == "enum")
      return MakeKind::Enum;
    if (k == "function")
      return MakeKind::Function;
    if (k == "lambda")
      return MakeKind::Lambda;
    if (k == "concept")
      return MakeKind::Concept;
    if (k == "exception")
      return MakeKind::Exception;
    if (k == "test")
      return MakeKind::Test;
    if (k == "module")
      return MakeKind::Module;

    return MakeKind::Unknown;
  }

  bool is_valid_make_kind(MakeKind kind) noexcept
  {
    return kind != MakeKind::Unknown;
  }

  MakeResult dispatch_make(const MakeOptions &options)
  {
    const MakeKind kind = parse_make_kind(options.kind);

    const MakeResult validation = validate_options(options, kind);
    if (!validation.ok)
      return validation;

    const MakeContext ctx = build_context(options, kind);

    switch (kind)
    {
    case MakeKind::Class:
      return dispatch_class(ctx);

    case MakeKind::Struct:
      return dispatch_struct(ctx);

    case MakeKind::Enum:
      return dispatch_enum(ctx);

    case MakeKind::Function:
      return dispatch_function(ctx);

    case MakeKind::Lambda:
      return dispatch_lambda(ctx);

    case MakeKind::Concept:
      return dispatch_concept(ctx);

    case MakeKind::Exception:
      return dispatch_exception(ctx);

    case MakeKind::Test:
      return dispatch_test(ctx);

    case MakeKind::Module:
      return dispatch_module(ctx);

    case MakeKind::Unknown:
    default:
      return make_error("Unknown make kind.");
    }
  }

} // namespace vix::cli::make
