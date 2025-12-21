#include <vix/cli/commands/repl/ReplLineEditor.hpp>
#include <vix/cli/commands/repl/ReplConsole.hpp>

#ifndef _WIN32
#include <unistd.h>
#include <iostream>
#endif

namespace vix::cli::repl
{
#ifndef _WIN32
    constexpr unsigned char KEY_CTRL_C = 0x03;
    constexpr unsigned char KEY_CTRL_D = 0x04;
    constexpr unsigned char KEY_CTRL_L = 0x0C;
    constexpr unsigned char KEY_BACKSPACE_1 = 0x08;
    constexpr unsigned char KEY_BACKSPACE_2 = 0x7F;
    constexpr unsigned char KEY_ENTER_1 = '\n';
    constexpr unsigned char KEY_ENTER_2 = '\r';
    constexpr unsigned char KEY_TAB = '\t';
    constexpr unsigned char KEY_ESC = 0x1B;

    static bool read_byte(unsigned char &out)
    {
        const ssize_t n = ::read(STDIN_FILENO, &out, 1);
        return n == 1;
    }

    static void redraw(const std::string &prompt, const std::string &line)
    {
        // \r -> begin line ; \033[2K -> clear line
        std::cout << "\r\033[2K" << prompt << line << std::flush;
    }

    struct RedrawCtx
    {
        const std::string *prompt = nullptr;
        const std::string *line = nullptr;
    };

    static void redraw_trampoline(void *p)
    {
        auto *ctx = static_cast<RedrawCtx *>(p);
        if (!ctx || !ctx->prompt || !ctx->line)
            return;
        redraw(*ctx->prompt, *ctx->line);
    }

    static inline void end_editing()
    {
        vix::cli::repl::Console::set_redraw_callback(nullptr, nullptr);
        vix::cli::repl::Console::set_editing(false);
    }

    static void print_suggestions_and_redraw(
        const std::vector<std::string> &suggestions,
        const std::string &prompt,
        const std::string &line)
    {
        // We are about to print extra output while editing.
        // Ensure we're on a clean line, then print, then redraw current prompt+buffer.
        std::cout << "\n";
        for (const auto &s : suggestions)
            std::cout << s << "  ";
        std::cout << "\n";
        redraw(prompt, line);
    }

    // Parse arrow keys: ESC [ A/B/C/D
    static bool read_escape_sequence(unsigned char &seq2, unsigned char &seq3)
    {
        if (!read_byte(seq2))
            return false;
        if (!read_byte(seq3))
            return false;
        return true;
    }

    ReadStatus read_line_edit(
        const std::string &prompt,
        std::string &outLine,
        const CompletionFn &completer,
        const HistoryNavFn &onHistoryUp,
        const HistoryNavFn &onHistoryDown)
    {
        outLine.clear();
        std::cout << prompt << std::flush;

        // Inform Console that we're currently editing a line in raw mode.
        vix::cli::repl::Console::set_editing(true);
        RedrawCtx ctx{&prompt, &outLine};
        vix::cli::repl::Console::set_redraw_callback(&redraw_trampoline, &ctx);

        while (true)
        {
            unsigned char ch = 0;
            if (!read_byte(ch))
            {
                end_editing();
                std::cout << "\n";
                return ReadStatus::Eof;
            }

            if (ch == KEY_CTRL_C)
            {
                end_editing();
                std::cout << "^C\n";
                outLine.clear();
                return ReadStatus::Interrupted;
            }

            if (ch == KEY_CTRL_D)
            {
                if (outLine.empty())
                {
                    end_editing();
                    std::cout << "\n";
                    return ReadStatus::Eof;
                }
                continue;
            }

            if (ch == KEY_CTRL_L)
            {
                end_editing();
                outLine.clear();
                std::cout << "\n";
                return ReadStatus::Clear;
            }

            if (ch == KEY_ENTER_1 || ch == KEY_ENTER_2)
            {
                end_editing();
                std::cout << "\n";
                return ReadStatus::Ok;
            }

            if (ch == KEY_BACKSPACE_1 || ch == KEY_BACKSPACE_2)
            {
                if (!outLine.empty())
                {
                    outLine.pop_back();
                    redraw(prompt, outLine);
                }
                continue;
            }

            if (ch == KEY_TAB)
            {
                if (completer)
                {
                    auto res = completer(outLine);

                    if (res.changed)
                    {
                        outLine = res.newLine;
                        redraw(prompt, outLine);
                        continue;
                    }

                    if (!res.suggestions.empty())
                    {
                        print_suggestions_and_redraw(res.suggestions, prompt, outLine);
                        continue;
                    }
                }
                continue;
            }

            if (ch == KEY_ESC)
            {
                unsigned char a = 0, b = 0;
                if (!read_escape_sequence(a, b))
                    continue;

                // Expect ESC [ X
                if (a == '[')
                {
                    // Up: A, Down: B, Right: C, Left: D
                    if (b == 'A')
                    {
                        if (onHistoryUp && onHistoryUp(outLine))
                            redraw(prompt, outLine);
                        continue;
                    }
                    if (b == 'B')
                    {
                        if (onHistoryDown && onHistoryDown(outLine))
                            redraw(prompt, outLine);
                        continue;
                    }

                    // Left/Right not implemented (cursor support later)
                    continue;
                }

                continue;
            }

            // Ignore other control chars
            if (ch < 0x20)
                continue;

            outLine.push_back(static_cast<char>(ch));
            std::cout << static_cast<char>(ch) << std::flush;
        }
    }
#else
    ReadStatus read_line_edit(
        const std::string &prompt,
        std::string &outLine,
        const CompletionFn &,
        const HistoryNavFn &,
        const HistoryNavFn &)
    {
        std::cout << prompt << std::flush;
        if (!std::getline(std::cin, outLine))
        {
            std::cout << "\n";
            return ReadStatus::Eof;
        }
        return ReadStatus::Ok;
    }
#endif
}
