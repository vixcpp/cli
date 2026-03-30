/**
 *
 *  @file ClassGenerator.hpp
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
#ifndef VIX_CLASS_GENERATOR_HPP
#define VIX_CLASS_GENERATOR_HPP

#include <vix/cli/commands/make/MakeDispatcher.hpp>
#include <vix/cli/commands/make/MakeResult.hpp>

#include <string>
#include <vector>

namespace vix::cli::make::generators
{
  struct ClassField
  {
    std::string name;
    std::string type;
  };

  struct ClassSpec
  {
    std::string name;
    std::string name_space;
    std::vector<ClassField> fields;

    bool header_only{false};
    bool with_default_ctor{true};
    bool with_value_ctor{true};
    bool with_getters_setters{true};
    bool with_copy_move{true};
    bool with_virtual_destructor{true};
  };

  [[nodiscard]] MakeResult generate_class(const MakeContext &ctx);

  [[nodiscard]] MakeResult generate_class(const MakeContext &ctx,
                                          const ClassSpec &spec);
} // namespace vix::cli::make::generators

#endif
