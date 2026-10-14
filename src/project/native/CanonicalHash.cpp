#include <vix/cli/project/native/CanonicalHash.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace vix::cli::project::native
{
  namespace
  {
    constexpr std::array<std::uint32_t, 64> round_constants{
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
      0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
      0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
      0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

    constexpr std::array<std::uint32_t, 8> initial_state{
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned int amount) noexcept
    {
      return (value >> amount) | (value << (32u - amount));
    }

    std::array<std::uint8_t, 32> sha256_bytes(std::string_view input)
    {
      std::vector<std::uint8_t> message;
      message.reserve(((input.size() + 9u + 63u) / 64u) * 64u);
      if (!input.empty())
      {
        const auto *begin = reinterpret_cast<const std::uint8_t *>(input.data());
        message.insert(message.end(), begin, begin + input.size());
      }
      message.push_back(0x80u);
      while (message.size() % 64u != 56u) message.push_back(0u);

      const auto bit_length = static_cast<std::uint64_t>(input.size()) * 8u;
      for (int shift = 56; shift >= 0; shift -= 8)
        message.push_back(static_cast<std::uint8_t>((bit_length >> static_cast<unsigned int>(shift)) & 0xffu));

      auto state = initial_state;
      for (std::size_t offset = 0; offset < message.size(); offset += 64u)
      {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16u; ++i)
        {
          const auto base = offset + i * 4u;
          words[i] = (static_cast<std::uint32_t>(message[base]) << 24u) |
            (static_cast<std::uint32_t>(message[base + 1u]) << 16u) |
            (static_cast<std::uint32_t>(message[base + 2u]) << 8u) |
            static_cast<std::uint32_t>(message[base + 3u]);
        }
        for (std::size_t i = 16u; i < words.size(); ++i)
        {
          const auto s0 = rotate_right(words[i - 15u], 7u) ^ rotate_right(words[i - 15u], 18u) ^ (words[i - 15u] >> 3u);
          const auto s1 = rotate_right(words[i - 2u], 17u) ^ rotate_right(words[i - 2u], 19u) ^ (words[i - 2u] >> 10u);
          words[i] = words[i - 16u] + s0 + words[i - 7u] + s1;
        }

        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];
        for (std::size_t i = 0; i < words.size(); ++i)
        {
          const auto sum1 = rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
          const auto choice = (e & f) ^ (~e & g);
          const auto temp1 = h + sum1 + choice + round_constants[i] + words[i];
          const auto sum0 = rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
          const auto majority = (a & b) ^ (a & c) ^ (b & c);
          const auto temp2 = sum0 + majority;
          h = g;
          g = f;
          f = e;
          e = d + temp1;
          d = c;
          c = b;
          b = a;
          a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
      }

      std::array<std::uint8_t, 32> digest{};
      for (std::size_t i = 0; i < state.size(); ++i)
        for (std::size_t j = 0; j < 4u; ++j)
          digest[i * 4u + j] = static_cast<std::uint8_t>((state[i] >> static_cast<unsigned int>(24u - j * 8u)) & 0xffu);
      return digest;
    }

    void append_field(std::string &output, std::string_view name, std::string_view value)
    {
      output.append(name);
      output.push_back('=');
      output += std::to_string(value.size());
      output.push_back(':');
      if (!value.empty()) output.append(value.data(), value.size());
      output.push_back('\n');
    }

    std::string version_text(const Version &version)
    {
      std::string result = std::to_string(version.major) + "." +
        std::to_string(version.minor) + "." + std::to_string(version.patch);
      if (version.prerelease)
      {
        result.push_back('-');
        result += *version.prerelease;
      }
      return result;
    }

    template <typename Revision>
    std::pair<std::string_view, std::string_view> revision_mode(const Revision &revision)
    {
      return std::visit([](const auto &value) -> std::pair<std::string_view, std::string_view>
      {
        using Type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, CommitRevision>) return {"commit", value.value};
        else if constexpr (std::is_same_v<Type, TagRevision>) return {"tag", value.value};
        else return {"branch", value.value};
      }, revision);
    }

    bool ascii_alias_less(std::string_view left, std::string_view right) noexcept
    {
      const auto common = std::min(left.size(), right.size());
      for (std::size_t i = 0; i < common; ++i)
      {
        const auto lhs = static_cast<unsigned char>(left[i]);
        const auto rhs = static_cast<unsigned char>(right[i]);
        if (lhs != rhs) return lhs < rhs;
      }
      return left.size() < right.size();
    }

    void append_root_entry(std::string &output, const RootRequirementDigestEntry &entry)
    {
      output += "entry\n";
      append_field(output, "alias", entry.alias);
      std::visit([&](const auto &declared)
      {
        using Type = std::decay_t<decltype(declared)>;
        if constexpr (std::is_same_v<Type, NormalizedDeclaredRegistry>)
        {
          append_field(output, "source", "registry");
          append_field(output, "registry", declared.identity.registry);
          append_field(output, "publisher", declared.identity.publisher);
          append_field(output, "name", declared.identity.name);
          std::visit([&](const auto &requirement)
          {
            using Mode = std::decay_t<decltype(requirement)>;
            if constexpr (std::is_same_v<Mode, ExactVersion>)
              append_field(output, "mode", "exact");
            else
              append_field(output, "mode", "compatible");
            append_field(output, "version", version_text(requirement.version));
          }, declared.requirement);
        }
        else
        {
          append_field(output, "source", "git");
          append_field(output, "repository", declared.normalizedRepository);
          append_field(output, "package-root", ".");
          const auto [mode, revision] = revision_mode(declared.revision);
          append_field(output, "mode", mode);
          append_field(output, "revision", revision);
        }
      }, entry.declared);
    }
  } // namespace

  std::string canonical_registry_node_bytes(const RegistryNodeIdentity &identity)
  {
    std::string output{"VIX-LOCK-NODE-v1\n"};
    append_field(output, "source", "registry");
    append_field(output, "registry", identity.package.registry);
    append_field(output, "publisher", identity.package.publisher);
    append_field(output, "name", identity.package.name);
    append_field(output, "version", version_text(identity.version));
    return output;
  }

  std::string canonical_git_node_bytes(const GitNodeIdentity &identity)
  {
    std::string output{"VIX-LOCK-NODE-v1\n"};
    append_field(output, "source", "git");
    append_field(output, "repository", identity.normalizedRepository);
    append_field(output, "package-root", ".");
    append_field(output, "commit", identity.commit);
    return output;
  }

  std::string sha256_hex(std::string_view bytes)
  {
    constexpr char hex[] = "0123456789abcdef";
    const auto digest = sha256_bytes(bytes);
    std::string output;
    output.reserve(64u);
    for (const auto byte : digest)
    {
      output.push_back(hex[byte >> 4u]);
      output.push_back(hex[byte & 0x0fu]);
    }
    return output;
  }

  std::string sha256_digest(std::string_view bytes)
  { return "sha256:" + sha256_hex(bytes); }

  std::string registry_node_id(const RegistryNodeIdentity &identity)
  { return sha256_digest(canonical_registry_node_bytes(identity)); }

  std::string git_node_id(const GitNodeIdentity &identity)
  { return sha256_digest(canonical_git_node_bytes(identity)); }

  std::string canonical_root_requirements_bytes(const std::vector<RootRequirementDigestEntry> &requirements)
  {
    std::vector<const RootRequirementDigestEntry *> ordered;
    ordered.reserve(requirements.size());
    for (const auto &requirement : requirements) ordered.push_back(&requirement);
    std::sort(ordered.begin(), ordered.end(), [](const auto *left, const auto *right)
    { return ascii_alias_less(left->alias, right->alias); });

    std::string output{"VIX-ROOT-REQUIREMENTS-v1\n"};
    output += "count=" + std::to_string(ordered.size()) + "\n";
    for (const auto *entry : ordered) append_root_entry(output, *entry);
    return output;
  }

  std::string root_requirements_digest(const std::vector<RootRequirementDigestEntry> &requirements)
  { return sha256_digest(canonical_root_requirements_bytes(requirements)); }
} // namespace vix::cli::project::native
