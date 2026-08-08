/**
 *
 *  @file BuildLiveProcess.hpp
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
#include <functional>
#include <iosfwd>
#include <mutex>
#include <string>
#include <string_view>

#include <vix/cli/build/BuildLiveSession.hpp>

namespace vix::cli::build
{
  /**
   * @brief Thread-safe facade around BuildLiveSession for build executors.
   *
   * BuildLiveProcess provides the small public API that command and process
   * execution layers use to report live build activity.
   *
   * It deliberately hides BuildEvent construction, BuildOutputParser,
   * BuildLiveRenderer, and the internal session state from callers.
   *
   * The class supports two complementary sources of build information:
   *
   * - semantic events emitted directly by structured Vix build engines;
   * - raw process output delivered through observer().
   *
   * Semantic events should be preferred whenever the build engine already
   * knows what operation is taking place. Raw process parsing remains useful
   * for external tools such as CMake and Ninja.
   *
   * Calls that mutate the session are serialized internally because build
   * schedulers can report compilation events from multiple worker threads.
   *
   * Example semantic usage:
   *
   * @code
   * BuildLiveProcess live(std::cout);
   *
   * live.begin("orelunza");
   *
   * live.compile_progress(
   *     18,
   *     48,
   *     "src/game/World.cpp",
   *     "orelunza");
   *
   * live.link_started("orelunza");
   * live.link_finished("orelunza");
   *
   * live.finish(0);
   * @endcode
   *
   * Example raw-output usage:
   *
   * @code
   * BuildLiveProcess live(std::cout);
   * live.begin("app");
   *
   * auto outputObserver = live.observer();
   *
   * outputObserver(chunk);
   *
   * live.finish(exitCode);
   * @endcode
   */
  class BuildLiveProcess
  {
  public:
    /**
     * @brief Callback type used to receive arbitrary process-output chunks.
     *
     * The callback can be passed to a process executor capable of streaming
     * stdout/stderr while the process is running.
     */
    using OutputObserver =
        std::function<void(std::string_view)>;

    /**
     * @brief Construct a live build process facade.
     *
     * @param output Stream used for user-facing live build presentation.
     */
    explicit BuildLiveProcess(
        std::ostream &output);

    /**
     * @brief Reset the complete live build process state.
     *
     * The underlying BuildLiveSession is reset and the stored target,
     * started state, and finished state are cleared.
     *
     * This operation is thread-safe.
     */
    void reset();

    /**
     * @brief Begin a build session.
     *
     * Emits project-started and project-finished events once. Calling this
     * method repeatedly during the same build has no effect after the first
     * successful call.
     *
     * @param target Optional primary build target.
     */
    void begin(
        std::string_view target = {});

    /**
     * @brief Report that configuration has started.
     *
     * If begin() has not been called yet, the build session is initialized
     * automatically before entering the configuration phase.
     */
    void begin_configure();

    /**
     * @brief Report that dependency resolution has started.
     *
     * If begin() has not been called yet, the build session is initialized
     * automatically.
     */
    void begin_dependencies();

    /**
     * @brief Report that dependency resolution has completed.
     *
     * If begin() has not been called yet, the build session is initialized
     * automatically.
     *
     * @param message Optional human-readable dependency summary.
     */
    void end_dependencies(
        std::string_view message = {});

    /**
     * @brief Report semantic compilation progress.
     *
     * This method is suitable for BuildGraphExecutor and BuildScheduler
     * integrations where the source file and progress counters are already
     * known without parsing compiler output.
     *
     * Calls are serialized internally so several scheduler workers can
     * safely report progress.
     *
     * @param current Current compile operation index.
     * @param total Total compile operations, when known.
     * @param file Source file currently being compiled.
     * @param target Related build target. When empty, the primary target
     * stored by begin() is used.
     */
    void compile_progress(
        std::size_t current,
        std::size_t total,
        std::string_view file = {},
        std::string_view target = {});

    /**
     * @brief Report that compilation has completed.
     *
     * Duplicate completion events are ignored by BuildLiveSession.
     *
     * @param target Related build target. When empty, the stored primary
     * target is used.
     */
    void compile_finished(
        std::string_view target = {});

    /**
     * @brief Report that linking has started.
     *
     * BuildLiveSession automatically closes an active compilation phase
     * before entering the linker phase.
     *
     * @param target Executable or library being linked. When empty, the
     * stored primary target is used.
     */
    void link_started(
        std::string_view target = {});

    /**
     * @brief Report that linking has completed.
     *
     * @param target Executable or library that was linked. When empty, the
     * stored primary target is used.
     */
    void link_finished(
        std::string_view target = {});

    /**
     * @brief Create a thread-safe raw process-output observer.
     *
     * The returned callback forwards arbitrary stdout/stderr chunks to
     * BuildLiveSession::consume_output_chunk().
     *
     * The callback captures this object and must therefore not outlive the
     * BuildLiveProcess instance that created it.
     *
     * @return Callback suitable for a streaming process executor.
     */
    [[nodiscard]]
    OutputObserver observer();

    /**
     * @brief Finalize the build.
     *
     * The real normalized process or build result code must be supplied.
     * Success is never inferred from textual build output.
     *
     * Duplicate calls are ignored.
     *
     * @param exitCode Final normalized build exit code.
     */
    void finish(
        int exitCode);

    /**
     * @brief Return the complete captured raw build output.
     *
     * The returned reference belongs to the underlying BuildLiveSession.
     *
     * This accessor is intended to be used once asynchronous build activity
     * has stopped, typically after finish().
     *
     * @return Complete captured build log.
     */
    [[nodiscard]]
    const std::string &log() const noexcept;

    /**
     * @brief Return the primary build target.
     *
     * This accessor is intended for use outside concurrent mutation of the
     * build session.
     *
     * @return Stored primary build target.
     */
    [[nodiscard]]
    const std::string &target() const noexcept;

    /**
     * @brief Return the final normalized exit code.
     *
     * This accessor is normally used after finish().
     *
     * @return Final build exit code.
     */
    [[nodiscard]]
    int exit_code() const noexcept;

    /**
     * @brief Return whether the finalized build failed.
     *
     * This accessor is normally used after finish().
     *
     * @return true when the finalized build has a non-zero exit code.
     */
    [[nodiscard]]
    bool failed() const noexcept;

  private:
    /**
     * @brief Initialize the build session while mutex_ is already held.
     *
     * This private helper prevents nested locking when methods such as
     * begin_configure() or begin_dependencies() need to initialize the
     * session automatically.
     *
     * @param target Optional primary build target.
     */
    void begin_unlocked(
        std::string_view target = {});

    /**
     * @brief Resolve an optional event target against the stored target.
     *
     * @param target Explicit event target, possibly empty.
     * @return Explicit target when present, otherwise the stored target.
     */
    [[nodiscard]]
    std::string_view resolve_target_unlocked(
        std::string_view target) const noexcept;

    /**
     * @brief Stateful semantic build session.
     */
    BuildLiveSession session_;

    /**
     * @brief Primary build target associated with this session.
     */
    std::string target_;

    /**
     * @brief Serializes all mutations of session_ and process state.
     *
     * BuildGraphExecutor compilation events can originate from multiple
     * scheduler workers, therefore live rendering and session mutation must
     * not occur concurrently.
     */
    mutable std::mutex mutex_;

    /**
     * @brief Whether the build session has been initialized.
     */
    bool started_{false};

    /**
     * @brief Whether finish() has finalized this build.
     */
    bool finished_{false};
  };

} // namespace vix::cli::build
