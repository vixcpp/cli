#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <cstddef>

namespace vix::cli::repl
{
    class History
    {
    public:
        explicit History(std::size_t maxItems);

        void add(const std::string &line);
        void clear();

        const std::vector<std::string> &items() const noexcept;

        bool loadFromFile(const std::filesystem::path &file, std::string *err = nullptr);
        bool saveToFile(const std::filesystem::path &file, std::string *err = nullptr) const;

    private:
        std::size_t max_;
        std::vector<std::string> items_;
    };
}
