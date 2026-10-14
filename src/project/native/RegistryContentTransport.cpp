#include <vix/cli/project/native/RegistryContentTransport.hpp>

#include <vix/cli/project/native/RegistryContentLayout.hpp>

#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#include <vix/requests/Client.hpp>
#include <vix/requests/Url.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vix::cli::project::native
{
  namespace
  {
    namespace fs = std::filesystem;

    [[nodiscard]] std::optional<fs::path> path_from_utf8_bytes(std::string_view value)
    {
#ifdef _WIN32
      if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return std::nullopt;
      const int inputSize = static_cast<int>(value.size());
      const int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), inputSize, nullptr, 0);
      if (wideSize <= 0)
        return std::nullopt;
      std::wstring native(static_cast<std::size_t>(wideSize), L'\0');
      if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), inputSize,
          native.data(), wideSize) != wideSize)
        return std::nullopt;
      return fs::path{std::move(native)};
#else
      return fs::path{std::string{value}};
#endif
    }

    [[nodiscard]] bool valid_utf8(std::string_view value) noexcept
    {
      std::size_t index = 0;
      while (index < value.size())
      {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7fU)
        {
          ++index;
          continue;
        }

        std::size_t count = 0;
        std::uint32_t codepoint = 0;
        if ((first & 0xe0U) == 0xc0U)
        {
          count = 2;
          codepoint = first & 0x1fU;
        }
        else if ((first & 0xf0U) == 0xe0U)
        {
          count = 3;
          codepoint = first & 0x0fU;
        }
        else if ((first & 0xf8U) == 0xf0U)
        {
          count = 4;
          codepoint = first & 0x07U;
        }
        else
        {
          return false;
        }

        if (value.size() - index < count)
          return false;
        for (std::size_t offset = 1; offset < count; ++offset)
        {
          const auto next = static_cast<unsigned char>(value[index + offset]);
          if ((next & 0xc0U) != 0x80U)
            return false;
          codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if ((count == 2 && codepoint < 0x80U) ||
            (count == 3 && codepoint < 0x800U) ||
            (count == 4 && codepoint < 0x10000U) ||
            codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU))
          return false;
        index += count;
      }
      return true;
    }

    [[nodiscard]] bool valid_object_path(std::string_view path) noexcept
    {
      constexpr std::string_view prefix = "objects/sha256/";
      if (path.size() != 80 || !path.starts_with(prefix) || path[17] != '/')
        return false;
      for (std::size_t index = 15; index < path.size(); ++index)
      {
        if (index == 17)
          continue;
        const char value = path[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
          return false;
      }
      return true;
    }

    [[nodiscard]] RegistryContentTransportResult configuration_error(std::string message)
    {
      return {RegistryContentTransportStatus::configuration_error, std::nullopt, std::move(message)};
    }

    [[nodiscard]] RegistryContentTransportResult transport_failure(std::string message)
    {
      return {RegistryContentTransportStatus::transport_failure, std::nullopt, std::move(message)};
    }

    [[nodiscard]] RegistryContentTransportResult filesystem_error(
      const std::error_code &error, std::string operation)
    {
      if (error == std::errc::permission_denied)
        return {RegistryContentTransportStatus::permission_denied, std::nullopt,
          std::move(operation) + " failed: permission denied"};
      if (error == std::errc::no_such_file_or_directory)
        return {RegistryContentTransportStatus::not_found, std::nullopt,
          std::move(operation) + " failed: mirror object not found"};
      return transport_failure(std::move(operation) + " failed while accessing the file mirror");
    }

    [[nodiscard]] RegistryContentTransportResult filesystem_error_from_errno(
      int error, std::string operation)
    {
      if (error == EACCES || error == EPERM)
        return {RegistryContentTransportStatus::permission_denied, std::nullopt,
          std::move(operation) + " failed: permission denied"};
      if (error == ENOENT)
        return {RegistryContentTransportStatus::not_found, std::nullopt,
          std::move(operation) + " failed: mirror object not found"};
      return transport_failure(std::move(operation) + " failed while accessing the file mirror");
    }

    [[nodiscard]] std::optional<fs::path> parse_file_mirror(
      const RegistryContentLocation &location)
    {
      constexpr std::string_view scheme = "file://";
      const std::string_view descriptor = location.descriptor;
      if (!descriptor.starts_with(scheme))
        return std::nullopt;

      std::string pathBytes{descriptor.substr(scheme.size())};
      if (pathBytes.empty() || pathBytes.front() != '/' ||
          pathBytes.starts_with("//") || pathBytes.find('\0') != std::string::npos ||
          pathBytes.find('?') != std::string::npos || pathBytes.find('#') != std::string::npos ||
          !valid_utf8(pathBytes))
        return std::nullopt;

#ifdef _WIN32
      if (pathBytes.size() < 4 || pathBytes[0] != '/' ||
          !((pathBytes[1] >= 'A' && pathBytes[1] <= 'Z') ||
            (pathBytes[1] >= 'a' && pathBytes[1] <= 'z')) ||
          pathBytes[2] != ':' || pathBytes[3] != '/')
        return std::nullopt;
      pathBytes.erase(pathBytes.begin());
#endif

      try
      {
        const auto converted = path_from_utf8_bytes(pathBytes);
        if (!converted)
          return std::nullopt;
        fs::path path = *converted;
        if (!path.is_absolute())
          return std::nullopt;
        return path;
      }
      catch (const std::exception &)
      {
        return std::nullopt;
      }
    }

    [[nodiscard]] RegistryContentTransportResult inspect_directory(
      const fs::path &path, std::string operation)
    {
      std::error_code error;
      const fs::file_status status = fs::symlink_status(path, error);
      if (error)
        return filesystem_error(error, std::move(operation));
      if (status.type() == fs::file_type::not_found)
        return {RegistryContentTransportStatus::not_found, std::nullopt,
          std::move(operation) + " not found"};
      if (fs::is_symlink(status) || !fs::is_directory(status))
        return configuration_error(std::move(operation) + " is not a non-symlink directory");
      return {RegistryContentTransportStatus::available, std::string{}, {}};
    }

    struct PayloadLimitExceeded final {};

    [[nodiscard]] bool redirect_status(int status) noexcept
    {
      return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    }

    [[nodiscard]] bool absolute_uri_reference(std::string_view value) noexcept
    {
      const auto colon = value.find(':');
      const auto delimiter = value.find_first_of("/?#");
      if (colon == std::string_view::npos ||
          (delimiter != std::string_view::npos && delimiter < colon) || colon == 0)
        return false;
      for (std::size_t index = 0; index < colon; ++index)
      {
        const unsigned char ch = static_cast<unsigned char>(value[index]);
        const bool valid = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
          (index > 0 && ((ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == '.'));
        if (!valid)
          return false;
      }
      return true;
    }

    [[nodiscard]] std::string lower_ascii(std::string_view value)
    {
      std::string result;
      result.reserve(value.size());
      for (const char raw : value)
      {
        const auto ch = static_cast<unsigned char>(raw);
        result.push_back(static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch));
      }
      return result;
    }

    [[nodiscard]] std::optional<std::string> https_object_url(
      std::string_view descriptor, std::string_view objectPath)
    {
      if (descriptor.find_first_of("?#") != std::string_view::npos)
        return std::nullopt;
      try
      {
        const auto base = vix::requests::Url::parse(descriptor);
        if (!base.is_https() || !base.query().empty() || !base.fragment().empty())
          return std::nullopt;
        std::string basePath = base.path();
        while (!basePath.empty() && basePath.back() == '/')
          basePath.pop_back();
        return base.origin() + basePath + "/" + std::string{objectPath};
      }
      catch (const std::exception &)
      {
        return std::nullopt;
      }
    }
  } // namespace

  RegistryContentTransportResult FileRegistryContentTransport::fetch(
    const RegistryContentLocation &location,
    std::string_view relativeObjectPath,
    std::uint64_t maximumBytes)
  {
    const auto mirror = parse_file_mirror(location);
    if (!mirror)
      return configuration_error("file Registry content location is malformed or not a local absolute path");
    if (!valid_object_path(relativeObjectPath))
      return configuration_error("Registry content object path is not a format-1 digest path");

    try
    {
      auto checked = inspect_directory(*mirror, "file Registry mirror base");
      if (checked.status != RegistryContentTransportStatus::available)
        return checked;

      fs::path current = *mirror;
      constexpr std::string_view directories[] = {"objects", "sha256"};
      for (const auto component : directories)
      {
        current /= fs::path{std::string{component}};
        checked = inspect_directory(current, "file Registry mirror directory");
        if (checked.status != RegistryContentTransportStatus::available)
          return checked;
      }

      const std::string shard{relativeObjectPath.substr(15, 2)};
      current /= fs::path{shard};
      checked = inspect_directory(current, "file Registry mirror shard");
      if (checked.status != RegistryContentTransportStatus::available)
        return checked;

      const std::string objectName{relativeObjectPath.substr(18)};
      const fs::path objectPath = current / fs::path{objectName};
      std::error_code error;
      const fs::file_status objectStatus = fs::symlink_status(objectPath, error);
      if (error)
        return filesystem_error(error, "file Registry object status");
      if (objectStatus.type() == fs::file_type::not_found)
        return {RegistryContentTransportStatus::not_found, std::nullopt,
          "digest-addressed Registry object not found"};
      if (fs::is_symlink(objectStatus) || !fs::is_regular_file(objectStatus))
        return configuration_error("digest-addressed Registry object is not a regular non-symlink file");

      const auto fileSize = fs::file_size(objectPath, error);
      if (error)
        return filesystem_error(error, "file Registry object size");
      if (fileSize > maximumBytes || fileSize > std::numeric_limits<std::size_t>::max())
        return {RegistryContentTransportStatus::payload_too_large, std::nullopt,
          "digest-addressed Registry object exceeds the configured byte limit"};

      errno = 0;
      std::ifstream input(objectPath, std::ios::binary);
      if (!input)
        return filesystem_error_from_errno(errno, "file Registry object open");

      std::string bytes;
      bytes.reserve(static_cast<std::size_t>(fileSize));
      std::array<char, 64U * 1024U> buffer{};
      while (true)
      {
        errno = 0;
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize amount = input.gcount();
        if (amount > 0)
        {
          const auto count = static_cast<std::uint64_t>(amount);
          const auto currentSize = static_cast<std::uint64_t>(bytes.size());
          if (currentSize > maximumBytes || count > maximumBytes - currentSize)
            return {RegistryContentTransportStatus::payload_too_large, std::nullopt,
              "digest-addressed Registry object grew beyond the configured byte limit"};
          bytes.append(buffer.data(), static_cast<std::size_t>(amount));
        }
        if (input.bad())
          return filesystem_error_from_errno(errno, "file Registry object read");
        if (input.eof())
          break;
        if (input.fail())
          return transport_failure("file Registry object read was incomplete");
      }

      if (bytes.size() != fileSize)
        return transport_failure("file Registry object changed during bounded read");
      return {RegistryContentTransportStatus::available, std::move(bytes), {}};
    }
    catch (const fs::filesystem_error &)
    {
      return transport_failure("file Registry mirror operation failed");
    }
  }

  RegistryPackageContentSourceResult TransportRegistryPackageContentSource::acquire(
    const RegistryContentLocation &location,
    const RegistryPackageContentRequest &request)
  {
    const std::string objectPath = registry_format1_content_object_path(request.expectedContent);
    auto result = transport_.fetch(location, objectPath, maximumBytes_);
    return {result.status, std::move(result.bytes), std::move(result.diagnostic)};
  }

  RegistryHttpsExecutionResult RequestsRegistryHttpsRequestExecutor::get(
    std::string_view url, const HeadSink &headSink, const BodySink &bodySink)
  {
    try
    {
      vix::requests::RequestOptions options;
      options.follow_redirects = false;
      options.max_redirects = 0;
      options.verify_tls = true;
      options.set_timeout(std::chrono::seconds(30));
      options.headers.set("Accept-Encoding", "identity");
      options.response_head_sink = [&](int status, const vix::requests::Headers &headers)
      {
        RegistryHttpsResponseHead head;
        head.statusCode = status;
        if (const auto length = headers.get("Content-Length"))
        {
          std::uint64_t parsed = 0;
          const auto conversion = std::from_chars(length->data(),
            length->data() + length->size(), parsed);
          if (conversion.ec == std::errc{} && conversion.ptr == length->data() + length->size())
            head.contentLength = parsed;
        }
        head.location = headers.get("Location");
        head.contentEncoding = headers.get("Content-Encoding");
        headSink(head);
      };
      options.body_sink = [&](std::span<const std::byte> bytes) { bodySink(bytes); };
      vix::requests::Client client;
      static_cast<void>(client.get(url, std::move(options)));
      return {true, {}};
    }
    catch (const PayloadLimitExceeded &)
    {
      throw;
    }
    catch (const std::exception &)
    {
      return {false, "HTTPS Registry request failed"};
    }
  }

  RegistryContentTransportResult HttpsRegistryContentTransport::fetch(
    const RegistryContentLocation &location,
    std::string_view relativeObjectPath,
    std::uint64_t maximumBytes)
  {
    if (!valid_object_path(relativeObjectPath))
      return configuration_error("Registry content object path is not a format-1 digest path");
    const auto initialUrl = https_object_url(location.descriptor, relativeObjectPath);
    if (!initialUrl)
      return configuration_error("HTTPS Registry content location is malformed or unsupported");

    std::string currentUrl = *initialUrl;
    std::string bytes;
    for (std::size_t redirects = 0; ; )
    {
      std::optional<RegistryHttpsResponseHead> responseHead;
      bytes.clear();
      const auto headSink = [&](const RegistryHttpsResponseHead &head)
      {
        responseHead = head;
        if (head.statusCode == 200 && head.contentLength && *head.contentLength > maximumBytes)
          throw PayloadLimitExceeded{};
        if (head.statusCode == 200 && head.contentEncoding &&
            lower_ascii(*head.contentEncoding) != "identity")
          throw std::runtime_error("unsupported HTTP content encoding");
      };
      const auto bodySink = [&](std::span<const std::byte> chunk)
      {
        if (!responseHead || responseHead->statusCode != 200)
          return;
        const auto currentSize = static_cast<std::uint64_t>(bytes.size());
        const auto chunkSize = static_cast<std::uint64_t>(chunk.size());
        if (currentSize > maximumBytes || chunkSize > maximumBytes - currentSize ||
            chunk.size() > bytes.max_size() - bytes.size())
          throw PayloadLimitExceeded{};
        bytes.append(reinterpret_cast<const char *>(chunk.data()), chunk.size());
      };

      RegistryHttpsExecutionResult execution;
      try
      {
        execution = executor_.get(currentUrl, headSink, bodySink);
      }
      catch (const PayloadLimitExceeded &)
      {
        return {RegistryContentTransportStatus::payload_too_large, std::nullopt,
          "HTTPS Registry object exceeds the configured byte limit"};
      }
      catch (const std::exception &)
      {
        return transport_failure("HTTPS Registry response could not be processed safely");
      }
      if (!execution.completed || !responseHead)
        return transport_failure(execution.diagnostic.empty() ?
          "HTTPS Registry request failed" : std::move(execution.diagnostic));

      const int status = responseHead->statusCode;
      if (redirect_status(status))
      {
        if (!responseHead->location || responseHead->location->empty())
          return transport_failure("HTTPS Registry redirect omitted Location");
        if (redirects >= 5)
          return transport_failure("HTTPS Registry redirect limit exceeded");
        try
        {
          const auto base = vix::requests::Url::parse(currentUrl);
          if (absolute_uri_reference(*responseHead->location))
          {
            const auto absoluteTarget = vix::requests::Url::parse(*responseHead->location);
            if (!absoluteTarget.is_https())
              return transport_failure("HTTPS Registry redirect was not HTTPS");
            currentUrl = absoluteTarget.without_fragment();
          }
          else
          {
            const std::string target = vix::requests::resolve_url_reference(base, *responseHead->location);
            const auto parsedTarget = vix::requests::Url::parse(target);
            if (!parsedTarget.is_https())
              return transport_failure("HTTPS Registry redirect was not HTTPS");
            currentUrl = parsedTarget.without_fragment();
          }
        }
        catch (const std::exception &)
        {
          return transport_failure("HTTPS Registry redirect target is invalid");
        }
        ++redirects;
        continue;
      }
      if (status == 404 || status == 410)
        return {RegistryContentTransportStatus::not_found, std::nullopt,
          "HTTPS Registry object was not found"};
      if (status == 401 || status == 403)
        return {RegistryContentTransportStatus::permission_denied, std::nullopt,
          "HTTPS Registry denied access to the content object"};
      if (status != 200)
        return transport_failure("HTTPS Registry returned an unsuccessful response");
      return {RegistryContentTransportStatus::available, std::move(bytes), {}};
    }
  }

  RegistryContentTransportResult SchemeRegistryContentTransport::fetch(
    const RegistryContentLocation &location,
    std::string_view relativeObjectPath,
    std::uint64_t maximumBytes)
  {
    if (location.descriptor.starts_with("file://"))
      return file_.fetch(location, relativeObjectPath, maximumBytes);
    try
    {
      const auto parsed = vix::requests::Url::parse(location.descriptor);
      if (parsed.is_https())
        return https_.fetch(location, relativeObjectPath, maximumBytes);
    }
    catch (const std::exception &) {}
    return configuration_error("unsupported Registry content location scheme");
  }
} // namespace vix::cli::project::native
