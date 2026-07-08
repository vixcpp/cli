/**
 * @file ModulesTypes.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 *
 * Shared types for the `vix modules` command.
 */

#ifndef VIX_CLI_MODULES_TYPES_HPP
#define VIX_CLI_MODULES_TYPES_HPP

#include <string>

namespace vix::commands::modules_cmd
{
  /**
   * @brief Describes the template family used when generating a module.
   *
   * The default template creates the normal Vix module layout. The WebSocket
   * template creates a module that is still part of the application module
   * system, but whose generated files are prepared for a WebSocket runtime
   * workflow.
   */
  enum class ModuleTemplate
  {
    /**
     * @brief Generate the standard module layout.
     */
    Default,

    /**
     * @brief Generate a backend or service module layout.
     */
    Backend,

    /**
     * @brief Generate a WebSocket-oriented module layout.
     */
    WebSocket
  };

  /**
   * @brief Describes how a generated WebSocket module should be started.
   *
   * WebSocket modules need an explicit workflow because they may run as a
   * standalone server, attach to an HTTP application, expose HTTP bridge
   * endpoints, or behave as a client. Keeping this value explicit prevents the
   * CLI from hiding long-lived runtime behavior behind normal route
   * registration.
   */
  enum class WebSocketWorkflow
  {
    /**
     * @brief No WebSocket workflow was requested.
     */
    None,

    /**
     * @brief Run HTTP and WebSocket together in the same process with separate listeners.
     */
    Attached,

    /**
     * @brief Run the WebSocket server without an attached HTTP application.
     */
    Standalone,

    /**
     * @brief Generate HTTP bridge endpoints for WebSocket-compatible traffic.
     */
    Bridge,

    /**
     * @brief Generate a module prepared for outbound WebSocket client usage.
     */
    Client
  };

  /**
   * @brief Parsed CLI arguments for `vix modules`.
   */
  struct Options
  {
    /**
     * @brief Requested subcommand.
     *
     * Expected values include:
     * - init
     * - add
     * - check
     * - list
     * - enable
     * - disable
     * - help
     */
    std::string subcmd;

    /**
     * @brief Project root passed through `--dir`.
     *
     * An empty value means the current working directory is used.
     */
    std::string dir;

    /**
     * @brief Project name override passed through `--project`.
     *
     * An empty value means the project name is detected from the current
     * project files.
     */
    std::string project;

    /**
     * @brief Module name used by commands that target one module.
     */
    std::string module;

    /**
     * @brief Whether `vix modules init` should patch the root CMake project.
     */
    bool patchRoot{true};

    /**
     * @brief Whether `vix modules add` should link or register the new module.
     */
    bool patchLink{true};

    /**
     * @brief Whether the help screen was requested.
     */
    bool showHelp{false};

    /**
     * @brief Whether `vix modules add` should generate a WebSocket module.
     */
    bool websocket{false};

    /**
     * @brief WebSocket workflow selected by the CLI.
     */
    WebSocketWorkflow websocketWorkflow{WebSocketWorkflow::None};
  };

  /**
   * @brief Options used by the `vix modules add` command.
   *
   * This structure keeps module generation explicit. It avoids growing
   * `cmd_add` with many positional booleans and gives future workflows a stable
   * place to add configuration without changing the command signature again.
   */
  struct AddModuleOptions
  {
    /**
     * @brief Template family selected for the generated module.
     */
    ModuleTemplate moduleTemplate{ModuleTemplate::Default};

    /**
     * @brief WebSocket workflow selected when moduleTemplate is WebSocket.
     */
    WebSocketWorkflow websocketWorkflow{WebSocketWorkflow::None};

    /**
     * @brief Whether the command should patch the root CMake project or vix.app.
     */
    bool patchRootLink{true};
  };

  /**
   * @brief Convert a WebSocket workflow to its CLI and manifest text value.
   *
   * @param workflow Workflow enum value.
   * @return Stable lowercase workflow name.
   */
  [[nodiscard]] constexpr const char *websocket_workflow_name(
      WebSocketWorkflow workflow) noexcept
  {
    switch (workflow)
    {
    case WebSocketWorkflow::Attached:
      return "attached";

    case WebSocketWorkflow::Standalone:
      return "standalone";

    case WebSocketWorkflow::Bridge:
      return "bridge";

    case WebSocketWorkflow::Client:
      return "client";

    case WebSocketWorkflow::None:
      return "none";
    }

    return "none";
  }

  /**
   * @brief Convert a WebSocket workflow to the value stored in vix.module.
   *
   * @param workflow Workflow enum value.
   * @return Stable manifest workflow identifier.
   */
  [[nodiscard]] constexpr const char *websocket_manifest_workflow_name(
      WebSocketWorkflow workflow) noexcept
  {
    switch (workflow)
    {
    case WebSocketWorkflow::Attached:
      return "websocket.attached";

    case WebSocketWorkflow::Standalone:
      return "websocket.standalone";

    case WebSocketWorkflow::Bridge:
      return "websocket.bridge";

    case WebSocketWorkflow::Client:
      return "websocket.client";

    case WebSocketWorkflow::None:
      return "";
    }

    return "";
  }

} // namespace vix::commands::modules_cmd

#endif // VIX_CLI_MODULES_TYPES_HPP
