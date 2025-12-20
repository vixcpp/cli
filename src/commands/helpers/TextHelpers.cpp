#include "vix/cli/commands/helpers/TextHelpers.hpp"

#include <fstream>
#include <sstream>

namespace vix::cli::commands::helpers
{
    std::string bool01(bool v)
    {
        return v ? "1" : "0";
    }

    std::string read_text_file_or_empty(const fs::path &p)
    {
        std::ifstream ifs(p);
        if (!ifs)
            return {};

        std::ostringstream oss;
        oss << ifs.rdbuf();
        return oss.str();
    }

    bool write_text_file(const fs::path &p, const std::string &text)
    {
        std::ofstream ofs(p, std::ios::trunc);
        if (!ofs)
            return false;

        ofs << text;
        return static_cast<bool>(ofs);
    }

} // namespace vix::cli::commands::helpers
