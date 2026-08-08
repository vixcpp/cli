/**
 *
 *  @file BuildLiveSession.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */

#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>

#include <vix/cli/build/BuildLiveRenderer.hpp>
#include <vix/cli/build/BuildOutputParser.hpp>

namespace vix::cli::build
{
  /**
   * @brief Coordinates semantic build events, raw process output,
   * progress tracking, rendering, and diagnostic log capture.
   *
   * BuildLiveSession is the stateful center of the live build
   * presentation pipeline.
   *
   * It supports two complementary sources of build information:
   *
   * - raw CMake/Ninja process output, interpreted by BuildOutputParser;
   * - direct semantic events emitted by Vix build engines such as the
   *   target-aware BuildGraphExecutor.
   *
   * Both sources are normalized into BuildEvent objects and forwarded to
   * BuildLiveRenderer.
   *
   * The session also keeps a complete normalized copy of raw process
   * output so existing Vix diagnostics can inspect the original build log
   * after a failure.
   *
   * Process execution itself intentionally remains outside this class.
   *
   * Example using raw process output:
   *
   * @code
   * BuildLiveSession session(std::cout);
   *
   * session.project_started("app");
   * session.project_finished("app");
   * session.configure_started();
   *
   * session.consume_output_chunk(chunk);
   *
   * session.finish(exitCode, "app");
   * @endcode
   *
   * Example using semantic graph events:
   *
   * @code
   * session.compile_progress(
   *     18,
   *     48,
   *     "src/game/World.cpp",
   *     "orelunza");
   *
   * session.link_started("orelunza");
   * session.finish(0, "orelunza");
   * @endcode
   */
  class BuildLiveSession
  {
  public:
    /**
     * @brief High-level stage currently active in the build.
     *
     * The stage is deliberately independent from CMake, Ninja, GCC,
     * Clang, or any specific build backend.
     *
     * It allows Vix to produce beginner-friendly failure messages such as
     * "Compilation failed" or "Linking failed" without exposing low-level
     * process details unnecessarily.
     */
    enum class Stage
    {
      /**
       * @brief No build stage has been entered yet.
       */
      None,

      /**
       * @brief Vix is loading or preparing the project.
       */
      Project,

      /**
       * @brief Build-system configuration is running.
       */
      Configure,

      /**
       * @brief Project dependencies are being resolved.
       */
      Dependencies,

      /**
       * @brief Source files are being compiled.
       */
      Compile,

      /**
       * @brief Objects or libraries are being linked.
       */
      Link
    };

    /**
     * @brief Construct a live build session.
     *
     * @param output Stream used for user-facing build presentation.
     */
    explicit BuildLiveSession(
        std::ostream &output);

    /**
     * @brief Reset the complete session state.
     *
     * Resets the output parser, renderer, captured log, pending process
     * fragment, current stage, target, process result, and compilation/
     * linking state.
     *
     * Call this before reusing the same session for another independent
     * build.
     */
    void reset();

    /**
     * @brief Report that project preparation has started.
     *
     * Changes the active stage to Stage::Project and emits
     * BuildEventKind::ProjectLoadStarted.
     *
     * @param target Optional project or target name.
     */
    void project_started(
        std::string_view target = {});

    /**
     * @brief Report that project preparation has completed.
     *
     * Emits BuildEventKind::ProjectLoadFinished.
     *
     * @param target Optional project or target name.
     */
    void project_finished(
        std::string_view target = {});

    /**
     * @brief Report that build-system configuration has started.
     *
     * Changes the active stage to Stage::Configure and emits
     * BuildEventKind::ConfigureStarted.
     */
    void configure_started();

    /**
     * @brief Report that dependency resolution has started.
     *
     * Changes the active stage to Stage::Dependencies and emits
     * BuildEventKind::DependencyResolutionStarted.
     */
    void dependencies_started();

    /**
     * @brief Report that dependency resolution has completed.
     *
     * @param message Optional human-readable summary of the resolution.
     */
    void dependencies_finished(
        std::string_view message = {});

    /**
     * @brief Report that compilation has started.
     *
     * This semantic entry point is useful when the build engine already
     * knows that a compile task is starting and therefore does not need
     * BuildOutputParser to infer the phase from textual output.
     *
     * Duplicate compilation-start events are ignored.
     *
     * @param current Current compile operation index.
     * @param total Total number of compile operations, when known.
     * @param file Source file currently being compiled.
     * @param target Related build target.
     */
    void compile_started(
        std::size_t current = 0,
        std::size_t total = 0,
        std::string_view file = {},
        std::string_view target = {});

    /**
     * @brief Report direct semantic compilation progress.
     *
     * If compilation has not started yet, the first progress update is
     * converted into the initial CompileStarted event automatically.
     *
     * Subsequent calls emit CompileProgress events.
     *
     * @param current Current compile operation index.
     * @param total Total number of compile operations.
     * @param file Source file currently being compiled.
     * @param target Related build target.
     */
    void compile_progress(
        std::size_t current,
        std::size_t total,
        std::string_view file = {},
        std::string_view target = {});

    /**
     * @brief Report that compilation has completed.
     *
     * The most recently observed compile progress is retained in the
     * completion event so the renderer can display the final count.
     *
     * Duplicate completion calls are ignored.
     *
     * @param target Related build target.
     */
    void compile_finished(
        std::string_view target = {});

    /**
     * @brief Report that linking has started.
     *
     * If compilation previously started but was not explicitly completed,
     * compilation is finalized before the LinkStarted event is emitted.
     * This guarantees correctly ordered visible phases.
     *
     * Duplicate link-start events are ignored.
     *
     * @param target Executable or library being linked.
     */
    void link_started(
        std::string_view target = {});

    /**
     * @brief Report that linking has completed.
     *
     * Duplicate completion calls are ignored.
     *
     * @param target Executable or library that was linked.
     */
    void link_finished(
        std::string_view target = {});

    /**
     * @brief Consume one complete stdout line from a build process.
     *
     * The line is retained in captured_log() and passed through
     * BuildOutputParser.
     *
     * Prefer consume_output_chunk() when process reads are not guaranteed
     * to correspond to complete lines.
     *
     * @param line Complete stdout line.
     */
    void consume_stdout_line(
        std::string_view line);

    /**
     * @brief Consume one complete stderr line from a build process.
     *
     * stdout and stderr currently share the same semantic parser because
     * useful build information may be emitted to either stream.
     *
     * Separate public entry points preserve the possibility of treating
     * the streams differently in a future process executor.
     *
     * @param line Complete stderr line.
     */
    void consume_stderr_line(
        std::string_view line);

    /**
     * @brief Consume an arbitrary raw process-output chunk.
     *
     * Process reads can contain several lines or only part of one line.
     * This method buffers incomplete fragments and reconstructs complete
     * lines before sending them to BuildOutputParser.
     *
     * Any final unterminated fragment is automatically processed by
     * finish().
     *
     * @param chunk Raw process output.
     */
    void consume_output_chunk(
        std::string_view chunk);

    /**
     * @brief Finalize the build session using the real process exit code.
     *
     * Success and failure are determined exclusively from the supplied
     * exit code. Textual CMake, Ninja, compiler, or linker output is never
     * used as the authoritative success condition.
     *
     * Before finalization:
     *
     * - pending raw output is flushed;
     * - an active compilation phase is completed on success;
     * - an active linking phase is completed on success.
     *
     * A successful build emits BuildSucceeded.
     *
     * A failed build emits BuildFailed with a user-facing message derived
     * from the stage in which the failure occurred.
     *
     * @param exitCode Normalized process/build exit code.
     * @param target Optional final target name.
     */
    void finish(
        int exitCode,
        std::string_view target = {});

    /**
     * @brief Return the complete captured raw build output.
     *
     * This log can be passed directly to the existing Vix diagnostic
     * subsystem after a failed build.
     *
     * @return Complete captured build log.
     */
    [[nodiscard]]
    const std::string &captured_log() const noexcept;

    /**
     * @brief Return whether the finalized build failed.
     *
     * A session is only considered failed after finish() has finalized it
     * with a non-zero exit code.
     *
     * @return true when the finalized build failed.
     */
    [[nodiscard]]
    bool failed() const noexcept;

    /**
     * @brief Return the final normalized build exit code.
     *
     * @return Exit code supplied to finish().
     */
    [[nodiscard]]
    int exit_code() const noexcept;

    /**
     * @brief Return the current or final high-level build stage.
     *
     * @return Current Stage.
     */
    [[nodiscard]]
    Stage stage() const noexcept;

  private:
    /**
     * @brief Dispatch one semantic event.
     *
     * Updates session state and progress tracking before forwarding the
     * event to BuildLiveRenderer.
     *
     * @param event Event to dispatch.
     */
    void dispatch(
        const BuildEvent &event);

    /**
     * @brief Process one complete reconstructed process-output line.
     *
     * The line is appended to capturedLog_, parsed by BuildOutputParser,
     * and all resulting semantic events are dispatched.
     *
     * @param line Complete normalized process-output line.
     */
    void consume_process_line(
        std::string_view line);

    /**
     * @brief Process a final buffered output fragment.
     *
     * Used by finish() when the process terminates without writing a
     * trailing newline.
     */
    void flush_pending_output();

    /**
     * @brief Parser used for raw CMake/Ninja build output.
     */
    BuildOutputParser parser_;

    /**
     * @brief Terminal renderer used for semantic build events.
     */
    BuildLiveRenderer renderer_;

    /**
     * @brief Complete normalized raw output retained for diagnostics.
     */
    std::string capturedLog_;

    /**
     * @brief Incomplete raw process fragment waiting for a newline.
     */
    std::string pendingOutput_;

    /**
     * @brief Most recently known build target.
     */
    std::string lastTarget_;

    /**
     * @brief Current high-level build stage.
     */
    Stage stage_{Stage::None};

    /**
     * @brief Final normalized build exit code.
     */
    int exitCode_{0};

    /**
     * @brief Whether finish() has finalized this session.
     */
    bool finished_{false};

    /**
     * @brief Whether compilation has started.
     */
    bool compileStarted_{false};

    /**
     * @brief Whether compilation has completed.
     */
    bool compileFinished_{false};

    /**
     * @brief Most recently observed compile operation index.
     */
    std::size_t compileCurrent_{0};

    /**
     * @brief Most recently known total number of compile operations.
     */
    std::size_t compileTotal_{0};

    /**
     * @brief Whether linking has started.
     */
    bool linkStarted_{false};

    /**
     * @brief Whether linking has completed.
     */
    bool linkFinished_{false};
  };

} // namespace vix::cli::build
