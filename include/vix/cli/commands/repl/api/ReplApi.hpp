#ifndef RELP_API_HPP
#define RELP_API_HPP

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <optional>

namespace vix::cli::repl::api
{
    void print(std::string_view s);
    void println(std::string_view s = {});
    void eprint(std::string_view s);
    void eprintln(std::string_view s = {});
    void print_int(long long v);
    void println_int(long long v);
    std::optional<std::string> readln(); // returns nullopt on EOF (Ctrl+D)
    void clear();
    std::filesystem::path pwd();
    bool cd(const std::filesystem::path &p, std::string *err = nullptr);
}

#endif
