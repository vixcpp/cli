/**
 *
 *  @file ReplayCapture.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/replay/ReplayCapture.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace vix::commands::replay
{

  namespace
  {

    /**
     * @brief Trim whitespace around a string.
     *
     * @param value Input string.
     * @return Trimmed string.
     */
    std::string trim_copy(std::string value)
    {
      auto is_space = [](unsigned char c)
      {
        return std::isspace(c) != 0;
      };

      while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
        value.pop_back();

      std::size_t start = 0;
      while (start < value.size() && is_space(static_cast<unsigned char>(value[start])))
        ++start;

      if (start > 0)
        value.erase(0, start);

      return value;
    }

    /**
     * @brief Lowercase a string copy.
     *
     * @param value Input string.
     * @return Lowercase string.
     */
    std::string lower_copy(std::string value)
    {
      for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      return value;
    }

    /**
     * @brief Split a text into lines.
     *
     * @param text Input text.
     * @return Lines.
     */
    std::vector<std::string> split_lines(const std::string &text)
    {
      std::vector<std::string> lines;
      std::istringstream in(text);
      std::string line;

      while (std::getline(in, line))
        lines.push_back(line);

      return lines;
    }

    /**
     * @brief Return true when a line looks like a compiler or runtime error.
     *
     * @param line Input line.
     * @return true when the line is useful as a short error message.
     */
    bool line_looks_like_error(const std::string &line)
    {
      const std::string lower = lower_copy(line);

      return lower.find("error:") != std::string::npos ||
             lower.find("fatal error") != std::string::npos ||
             lower.find("undefined reference") != std::string::npos ||
             lower.find("segmentation fault") != std::string::npos ||
             lower.find("terminate called") != std::string::npos ||
             lower.find("runtime error:") != std::string::npos ||
             lower.find("addresssanitizer") != std::string::npos ||
             lower.find("undefinedbehaviorsanitizer") != std::string::npos;
    }

  } // namespace

  ReplayCapture::ReplayCapture(
      ReplayRecorder *recorder,
      ReplayCaptureOptions options)
  {
    attach(recorder, options);
  }

  void ReplayCapture::attach(
      ReplayRecorder *recorder,
      ReplayCaptureOptions options)
  {
    recorder_ = recorder;
    options_ = options;

    stdout_buffer_.clear();
    stderr_buffer_.clear();
    combined_buffer_.clear();
    last_error_.clear();
  }

  bool ReplayCapture::capture_stdout(const std::string &text, std::string &err)
  {
    append_bounded(stdout_buffer_, text, options_.max_stdout_bytes);
    append_bounded(combined_buffer_, text, options_.max_combined_bytes);

    if (!recorder_ || !recorder_->active() || !options_.write_stdout)
      return true;

    if (!recorder_->append_stdout(text, err))
    {
      last_error_ = err;
      return options_.ignore_recorder_errors;
    }

    return true;
  }

  bool ReplayCapture::capture_stderr(const std::string &text, std::string &err)
  {
    append_bounded(stderr_buffer_, text, options_.max_stderr_bytes);
    append_bounded(combined_buffer_, text, options_.max_combined_bytes);

    if (!recorder_ || !recorder_->active() || !options_.write_stderr)
      return true;

    if (!recorder_->append_stderr(text, err))
    {
      last_error_ = err;
      return options_.ignore_recorder_errors;
    }

    return true;
  }

  void ReplayCapture::capture_stdout_noexcept(const std::string &text)
  {
    std::string err;
    (void)capture_stdout(text, err);
  }

  void ReplayCapture::capture_stderr_noexcept(const std::string &text)
  {
    std::string err;
    (void)capture_stderr(text, err);
  }

  ReplayCapturedOutput ReplayCapture::output() const
  {
    ReplayCapturedOutput out{};
    out.stdout_text = stdout_buffer_;
    out.stderr_text = stderr_buffer_;
    out.combined_text = combined_buffer_;
    return out;
  }

  bool ReplayCapture::active() const
  {
    return recorder_ != nullptr && recorder_->active();
  }

  const std::string &ReplayCapture::last_error() const
  {
    return last_error_;
  }

  void ReplayCapture::append_bounded(
      std::string &buffer,
      const std::string &text,
      std::size_t maxBytes)
  {
    if (maxBytes == 0 || text.empty())
      return;

    if (text.size() >= maxBytes)
    {
      buffer = text.substr(text.size() - maxBytes);
      return;
    }

    buffer += text;

    if (buffer.size() > maxBytes)
      buffer.erase(0, buffer.size() - maxBytes);
  }

  ReplayCapturedResult make_replay_captured_result(
      const ReplayCapturedOutput &output,
      const ReplayProcessResult &process)
  {
    ReplayCapturedResult result{};

    result.output = output;
    result.process = process;
    result.status = infer_replay_status_from_process(process);
    result.error_kind = infer_replay_error_kind_from_process(process);
    result.error_message = replay_error_message_from_output(output);
    result.hint = replay_hint_from_output(output, process);

    return result;
  }

  ReplayRecorderFinish make_replay_finish_from_capture(
      const ReplayCapturedResult &result)
  {
    ReplayRecorderFinish finish{};

    finish.status = result.status;
    finish.error_kind = result.error_kind;
    finish.process = result.process;
    finish.error_message = result.error_message;
    finish.hint = result.hint;

    return finish;
  }

  std::string replay_error_message_from_output(const ReplayCapturedOutput &output)
  {
    const std::vector<std::string> stderr_lines = split_lines(output.stderr_text);

    for (const auto &line : stderr_lines)
    {
      const std::string trimmed = trim_copy(line);
      if (!trimmed.empty() && line_looks_like_error(trimmed))
        return trimmed;
    }

    const std::string last_stderr = last_non_empty_line(output.stderr_text);
    if (!last_stderr.empty())
      return last_stderr;

    return last_non_empty_line(output.combined_text);
  }

  std::string replay_hint_from_output(
      const ReplayCapturedOutput &output,
      const ReplayProcessResult &process)
  {
    const std::string combined = lower_copy(output.combined_text);

    if (process.exit_code == 130 || process.signal == 2)
      return "Execution was interrupted by the user.";

    if (combined.find("undefined reference") != std::string::npos)
      return "This looks like a linker error. Check missing libraries or target links.";

    if (combined.find("fatal error") != std::string::npos &&
        combined.find("no such file") != std::string::npos)
    {
      return "This looks like a missing include or source file.";
    }

    if (combined.find("segmentation fault") != std::string::npos)
      return "The process crashed with a segmentation fault.";

    if (combined.find("addresssanitizer") != std::string::npos)
      return "AddressSanitizer reported a memory error.";

    if (combined.find("undefinedbehaviorsanitizer") != std::string::npos ||
        combined.find("runtime error:") != std::string::npos)
    {
      return "UndefinedBehaviorSanitizer reported undefined behavior.";
    }

    return {};
  }

  std::string first_non_empty_line(const std::string &text)
  {
    for (const auto &line : split_lines(text))
    {
      const std::string trimmed = trim_copy(line);
      if (!trimmed.empty())
        return trimmed;
    }

    return {};
  }

  std::string last_non_empty_line(const std::string &text)
  {
    const std::vector<std::string> lines = split_lines(text);

    for (auto it = lines.rbegin(); it != lines.rend(); ++it)
    {
      const std::string trimmed = trim_copy(*it);
      if (!trimmed.empty())
        return trimmed;
    }

    return {};
  }

} // namespace vix::commands::replay
