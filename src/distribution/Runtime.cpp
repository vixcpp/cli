#include <vix/cli/distribution/Runtime.hpp>
#include <vix/env/Legacy.hpp>

#include <toml.hpp>
#include <vix/requests/Client.hpp>
#include <vix/requests/Url.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <system_error>
#ifndef _WIN32
#include <cstdlib>
#endif

#include <zlib.h>

namespace vix::cli::distribution
{
  namespace
  {
    namespace fs = std::filesystem;

    Error fail(ErrorCode code, std::string message)
    {
      return {code, std::move(message)};
    }

    bool https_url(const std::string &text)
    {
      try
      {
        return vix::requests::Url::parse(text).is_https();
      }
      catch (const std::exception &)
      {
        return false;
      }
    }

    std::optional<std::string> string_field(const toml::table &table, std::string_view name)
    {
      const auto *node = table.get(name);
      if (!node)
        return std::nullopt;
      const auto value = node->value<std::string>();
      return value ? std::optional<std::string>{*value} : std::nullopt;
    }

    Result<std::string> get_metadata(const std::string &url, ErrorCode code)
    {
      if (!https_url(url))
        return {{}, fail(code, "release metadata location must use HTTPS")};
      std::string current = url;
      for (std::size_t redirects = 0; redirects <= 5; ++redirects)
      {
        try
        {
          vix::requests::RequestOptions options;
          options.follow_redirects = false;
          options.max_redirects = 0;
          options.verify_tls = true;
          options.set_timeout(std::chrono::seconds(30));
          std::string body;
          int status = 0;
          bool exceeded = false;
          std::optional<std::string> redirect;
          options.response_head_sink = [&](int responseStatus, const vix::requests::Headers &headers)
          {
            status = responseStatus;
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
              redirect = headers.get("Location");
          };
          options.body_sink = [&](std::span<const std::byte> chunk)
          {
            if (status != 200)
              return;
            constexpr std::size_t maximumMetadataBytes = 4 * 1024 * 1024;
            if (body.size() > maximumMetadataBytes || chunk.size() > maximumMetadataBytes - body.size())
            {
              exceeded = true;
              throw std::runtime_error("release metadata exceeds the configured bound");
            }
            body.append(reinterpret_cast<const char *>(chunk.data()), chunk.size());
          };
          vix::requests::Client client;
          const auto response = client.get(current, std::move(options));
          status = response.status_code();
          if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
          {
            if (!redirect || redirects == 5)
              break;
            const auto target = vix::requests::resolve_url_reference(vix::requests::Url::parse(current), *redirect);
            if (!https_url(target))
              break;
            current = target;
            continue;
          }
          if (status != 200 || exceeded || !https_url(current))
            return {{}, fail(code, "release metadata request failed")};
          return {std::move(body), {}};
        }
        catch (const std::exception &)
        {
          break;
        }
      }
      return {{}, fail(code, "release metadata request failed or left HTTPS")};
    }

    Result<std::filesystem::path> path_error(ErrorCode code, std::string message)
    {
      return {{}, fail(code, std::move(message))};
    }

    bool valid_version_path_component(const std::string &value)
    {
      return !value.empty() && value != "." && value != ".." &&
             value.find('/') == std::string::npos && value.find('\\') == std::string::npos;
    }

    bool qualified_target_sdk(std::string_view target)
    {
      return target == "android-arm64-v8a-api21" || target == "android-x86_64-api21" ||
             target == "wasm32-wasip2";
    }

    std::string version_string(const vix::cli::project::native::Version &version)
    {
      std::string result = std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
                           std::to_string(version.patch);
      if (version.prerelease)
        result += "-" + *version.prerelease;
      return result;
    }

    std::optional<vix::cli::project::native::Version> parse_path_version(const std::string &value)
    {
      if (!valid_version_path_component(value) ||
          !std::all_of(value.begin(), value.end(), [](unsigned char c)
                       { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                                (c >= 'a' && c <= 'z') || c == '.' || c == '-'; }))
        return std::nullopt;
      const std::string text = "format = 1\ngeneration = \"current\"\nversion = \"" + value +
                               "\"\nchannel = \"stable\"\nsequence = 0\n\n[[artifact]]\nid = \"x\"\nhost = \"x86_64-linux-gnu\"\nlocation = \"https://invalid\"\narchive = \"tar.gz\"\ndigest = \"sha256:0000000000000000000000000000000000000000000000000000000000000000\"\n";
      const auto manifest = parse_release_manifest(text);
      return manifest.valid() ? std::optional{manifest.value->version} : std::nullopt;
    }
  }

  Result<DistributionSourceConfig> parse_distribution_source_config(const std::string &text)
  {
    try
    {
      const auto root = toml::parse(text);
      for (const auto &entry : root)
        if (entry.first.str() != "format" && entry.first.str() != "channels")
          return {{}, fail(ErrorCode::release_source_missing, "distribution source config contains an unsupported field")};
      const auto format = root["format"].value<std::int64_t>();
      const auto *channels = root.get_as<toml::table>("channels");
      if (!format || *format != 1 || !channels || channels->empty())
        return {{}, fail(ErrorCode::release_source_missing, "distribution source config must be format 1 with at least one channel")};
      DistributionSourceConfig result;
      for (const auto &entry : *channels)
      {
        const auto name = entry.first.str();
        const auto *table = entry.second.as_table();
        if (name.empty() || !table || table->size() != 2)
          return {{}, fail(ErrorCode::release_source_missing, "distribution channel source is malformed")};
        const auto manifest = string_field(*table, "manifest");
        const auto signature = string_field(*table, "signature");
        if (!manifest || !signature || !https_url(*manifest) || !https_url(*signature))
          return {{}, fail(ErrorCode::release_source_missing, "manifest and signature locations must use HTTPS")};
        result.channels.emplace(name, ChannelSource{*manifest, *signature});
      }
      return {std::move(result), {}};
    }
    catch (const std::exception &e)
    {
      return {{}, fail(ErrorCode::release_source_missing, e.what())};
    }
  }

  Result<DistributionSourceConfig> load_distribution_source_config(const fs::path &path)
  {
    std::ifstream input(path, std::ios::binary);
    if (!input)
      return {{}, fail(ErrorCode::release_source_missing, "distribution source configuration is unavailable")};
    const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input.good() && !input.eof())
      return {{}, fail(ErrorCode::release_source_missing, "distribution source configuration could not be read")};
    return parse_distribution_source_config(text);
  }

  Result<fs::path> distribution_source_config_path(const fs::path &versionRoot)
  {
    if (versionRoot.empty())
      return {{}, fail(ErrorCode::release_source_missing, "distribution version root is empty")};
    return {versionRoot / "share" / "vix" / "distribution" / "source.toml", {}};
  }

  Result<fs::path> default_managed_vix_home()
  {
#ifdef _WIN32
    return {{}, fail(ErrorCode::unsupported_activation_platform, "managed bootstrap is not supported on Windows yet")};
#else
    if (const char *overrideRoot = vix::env::legacy::getenv("VIX_HOME"); overrideRoot && *overrideRoot)
    {
      const fs::path path{overrideRoot};
      if (!path.is_absolute())
        return {{}, fail(ErrorCode::invalid_value, "VIX_HOME must be an absolute path")};
      return {path, {}};
    }
    if (const char *xdg = vix::env::legacy::getenv("XDG_DATA_HOME"); xdg && *xdg)
    {
      const fs::path path{xdg};
      if (!path.is_absolute())
        return {{}, fail(ErrorCode::invalid_value, "XDG_DATA_HOME must be an absolute path")};
      return {path / "vix", {}};
    }
    if (const char *home = vix::env::legacy::getenv("HOME"); home && *home)
    {
      const fs::path path{home};
      if (!path.is_absolute())
        return {{}, fail(ErrorCode::invalid_value, "HOME must be an absolute path for managed installation")};
      return {path / ".local" / "share" / "vix", {}};
    }
    return {{}, fail(ErrorCode::invalid_value, "HOME or XDG_DATA_HOME is required for managed installation")};
#endif
  }

  Result<fs::path> default_vix_executable_path()
  {
#ifdef _WIN32
    return {{}, fail(ErrorCode::unsupported_activation_platform, "managed bootstrap is not supported on Windows yet")};
#else
    const char *home = vix::env::legacy::getenv("HOME");
    if (!home || !*home)
      return {{}, fail(ErrorCode::invalid_value, "HOME is required to expose vix on PATH")};
    const fs::path path{home};
    if (!path.is_absolute())
      return {{}, fail(ErrorCode::invalid_value, "HOME must be an absolute path to expose vix on PATH")};
    return {path / ".local" / "bin" / "vix", {}};
#endif
  }

#ifndef _WIN32
  Result<Unit> PosixExecutableExposure::expose(const fs::path &installationRoot, const fs::path &activeExecutable)
  {
    if (path_.empty() || installationRoot.empty() || activeExecutable.empty())
      return {{}, fail(ErrorCode::executable_exposure_failed, "stable executable path is invalid")};
    const auto expectedTarget = (installationRoot / "current" / "bin" / "vix").lexically_normal();
    if (activeExecutable.lexically_normal() != expectedTarget)
      return {{}, fail(ErrorCode::executable_exposure_failed, "PATH exposure must resolve through the managed current pointer")};
    std::error_code ec;
    fs::create_directories(path_.parent_path(), ec);
    if (ec)
      return {{}, fail(ErrorCode::executable_exposure_failed, "user executable directory could not be created")};
    const auto desired = fs::absolute(activeExecutable, ec);
    if (ec)
      return {{}, fail(ErrorCode::executable_exposure_failed, "active executable path could not be resolved")};
    const auto status = fs::symlink_status(path_, ec);
    if (!ec && fs::exists(status))
    {
      if (!fs::is_symlink(status))
        return {{}, fail(ErrorCode::executable_exposure_failed, "refusing to replace a non-symlink PATH entry")};
      const auto existing = fs::read_symlink(path_, ec);
      if (!ec && (path_.parent_path() / existing).lexically_normal() == desired.lexically_normal())
        return {Unit{}, {}};
      return {{}, fail(ErrorCode::executable_exposure_failed, "refusing to replace an unrelated vix PATH symlink")};
    }
    if (ec != std::errc::no_such_file_or_directory && ec)
      return {{}, fail(ErrorCode::executable_exposure_failed, "PATH entry could not be inspected")};
    ec.clear();
    fs::create_symlink(desired, path_, ec);
    if (ec)
      return {{}, fail(ErrorCode::executable_exposure_failed, "stable PATH symlink could not be created")};
    return {Unit{}, {}};
  }
#endif

  Result<BootstrapOutcome> ManagedInstallerRuntime::install(const fs::path &installationRoot,
                                                            const ReleaseTrustStore &trustStore,
                                                            const fs::path &exposedExecutable,
                                                            const HostPlatform &host,
                                                            const std::string &channel)
  {
#ifdef _WIN32
    (void)installationRoot;
    (void)trustStore;
    (void)exposedExecutable;
    (void)host;
    (void)channel;
    return {{}, fail(ErrorCode::unsupported_activation_platform, "managed bootstrap is not supported on Windows yet")};
#else
    const auto canonicalHost = parse_host_platform(host.canonical());
    if (installationRoot.empty() || exposedExecutable.empty() || trustStore.keys.empty() ||
        !canonicalHost.valid() || *canonicalHost.value != host || channel.empty())
      return {{}, fail(ErrorCode::invalid_value, "managed bootstrap inputs are invalid")};
    auto current = activator_.current_version();
    if (!current.valid())
      return {{}, current.error};
    if (current.value->has_value())
      return {{}, fail(ErrorCode::already_installed, "a managed Vix installation is already active")};
    std::error_code ec;
    const auto rootStatus = fs::symlink_status(installationRoot, ec);
    if (!ec && fs::is_symlink(rootStatus))
      return {{}, fail(ErrorCode::staging_failed, "managed installation root must not be a symlink")};
    if (ec != std::errc::no_such_file_or_directory && ec)
      return {{}, fail(ErrorCode::staging_failed, "managed installation root could not be inspected")};
    ec.clear();
    if (fs::exists(installationRoot / "install.toml", ec) || ec)
      return {{}, fail(ErrorCode::already_installed, "an installation receipt already exists; bootstrap will not overwrite it")};

    auto signedManifest = source_.fetch(channel);
    if (!signedManifest.valid())
      return {{}, signedManifest.error};
    auto verified = verify_release_manifest(signedManifest.value->manifestText, signedManifest.value->signature, trustStore, "current");
    if (!verified.valid())
      return {{}, verified.error};
    const auto &manifest = verified.value->manifest();
    if (manifest.channel != channel)
      return {{}, fail(ErrorCode::channel_mismatch, "signed release channel differs from requested channel")};
    // Host bootstrap installs exactly one host artifact. Signed optional
    // components are not part of that transaction and are deliberately left
    // untouched. Component kinds without an explicitly separate installation
    // contract remain fail-closed.
    if (std::any_of(manifest.components.begin(), manifest.components.end(), [](const ReleaseComponent &component)
                    { return component.kind != ComponentKind::target_sdk; }))
      return {{}, fail(ErrorCode::unsupported_components, "bootstrap cannot install non-target-SDK release components")};
    const auto artifactIt = std::find_if(manifest.artifacts.begin(), manifest.artifacts.end(),
                                         [&](const ReleaseArtifact &artifact) { return artifact.host == host; });
    if (artifactIt == manifest.artifacts.end())
      return {{}, fail(ErrorCode::unsupported_host_artifact, "release has no exact artifact for this host")};
    if (artifactIt->archive != ArchiveFormat::tar_gz)
      return {{}, fail(ErrorCode::unsupported_archive, "managed bootstrap currently supports tar.gz artifacts only")};
    const auto version = version_string(manifest.version);
    const auto layout = installation_layout(installationRoot, version);
    if (!layout.valid())
      return {{}, layout.error};
    if (fs::exists(layout.value->versionRoot, ec) || ec)
      return {{}, fail(ErrorCode::already_installed, "the release version directory already exists; bootstrap will not overwrite it")};

    const auto staging = installationRoot / "staging" / version;
    const auto stagingParent = fs::symlink_status(installationRoot / "staging", ec);
    if (!ec && fs::is_symlink(stagingParent))
      return {{}, fail(ErrorCode::staging_failed, "staging parent must not be a symlink")};
    if (ec != std::errc::no_such_file_or_directory && ec)
      return {{}, fail(ErrorCode::staging_failed, "staging parent could not be inspected")};
    ec.clear();
    if (fs::exists(staging, ec) || ec)
      return {{}, fail(ErrorCode::staging_failed, "version staging path already exists")};
    fs::create_directories(staging, ec);
    if (ec)
      return {{}, fail(ErrorCode::staging_failed, "version staging directory could not be created")};
    const auto archive = fetcher_.fetch(artifactIt->location, staging / "release.archive");
    if (!archive.valid())
    {
      fs::remove_all(staging, ec);
      return {{}, archive.error};
    }
    const auto archiveSize = fs::file_size(*archive.value, ec);
    if (ec || archiveSize > maximum_distribution_archive_bytes || (artifactIt->size && archiveSize != *artifactIt->size))
    {
      fs::remove_all(staging, ec);
      return {{}, fail(ErrorCode::artifact_digest_mismatch, "downloaded artifact size is invalid or differs from signed metadata")};
    }
    std::ifstream archiveInput(*archive.value, std::ios::binary);
    if (!archiveInput)
    {
      fs::remove_all(staging, ec);
      return {{}, fail(ErrorCode::artifact_fetch_failed, "downloaded release artifact cannot be read")};
    }
    const std::string bytes{std::istreambuf_iterator<char>{archiveInput}, std::istreambuf_iterator<char>{}};
    if (bytes.size() != archiveSize || !verify_distribution_artifact(bytes, artifactIt->digest).valid())
    {
      fs::remove_all(staging, ec);
      return {{}, fail(ErrorCode::artifact_digest_mismatch, "downloaded artifact differs from signed size or digest")};
    }
    const auto extracted = extractor_.extract(artifactIt->archive, *archive.value, staging / "unpacked");
    if (!extracted.valid())
    {
      fs::remove_all(staging, ec);
      return {{}, extracted.error};
    }
    if (!fs::is_regular_file(*extracted.value / "bin" / "vix", ec) || ec ||
        !fs::is_directory(*extracted.value / "share" / "vix", ec) || ec)
    {
      fs::remove_all(staging, ec);
      return {{}, fail(ErrorCode::invalid_distribution, "release is missing bin/vix or share/vix")};
    }
    fs::create_directories(layout.value->versions, ec);
    if (ec)
    {
      fs::remove_all(staging, ec);
      return {{}, fail(ErrorCode::staging_failed, "version directory could not be created")};
    }
    if (fs::is_symlink(fs::symlink_status(layout.value->versions, ec)) || ec)
    {
      fs::remove_all(staging, ec);
      return {{}, fail(ErrorCode::staging_failed, "versions directory is invalid or symlinked")};
    }
    fs::rename(*extracted.value, layout.value->versionRoot, ec);
    if (ec)
    {
      fs::remove_all(staging, ec);
      return {{}, fail(ErrorCode::staging_failed, "validated version could not be promoted")};
    }
    const InstallReceipt receipt{1, "current", manifest.version, host, channel, InstallKind::managed, {}, installationRoot.string()};
    const auto receiptBytes = serialize_install_receipt(receipt);
    if (!receiptBytes.valid())
    {
      fs::remove_all(layout.value->versionRoot, ec);
      fs::remove_all(staging, ec);
      return {{}, receiptBytes.error};
    }
    const auto receiptTemp = installationRoot / "install.toml.next";
    if (fs::exists(receiptTemp, ec) || ec)
    {
      fs::remove_all(layout.value->versionRoot, ec);
      fs::remove_all(staging, ec);
      return {{}, fail(ErrorCode::staging_failed, "temporary installation receipt already exists")};
    }
    {
      std::ofstream output(receiptTemp, std::ios::binary | std::ios::trunc);
      if (!output)
      {
        fs::remove_all(layout.value->versionRoot, ec);
        fs::remove_all(staging, ec);
        return {{}, fail(ErrorCode::staging_failed, "managed installation receipt could not be staged")};
      }
      output.write(receiptBytes.value->data(), static_cast<std::streamsize>(receiptBytes.value->size()));
      output.flush();
      if (!output)
      {
        output.close();
        fs::remove(receiptTemp, ec);
        fs::remove_all(layout.value->versionRoot, ec);
        fs::remove_all(staging, ec);
        return {{}, fail(ErrorCode::staging_failed, "managed installation receipt could not be written")};
      }
    }
    fs::rename(receiptTemp, layout.value->receipt, ec);
    if (ec)
    {
      fs::remove(receiptTemp, ec);
      fs::remove_all(layout.value->versionRoot, ec);
      fs::remove_all(staging, ec);
      return {{}, fail(ErrorCode::staging_failed, "managed installation receipt could not be committed")};
    }
    auto activated = activator_.activate(version);
    if (!activated.valid())
    {
      fs::remove(layout.value->receipt, ec);
      fs::remove_all(layout.value->versionRoot, ec);
      fs::remove_all(staging, ec);
      return {{}, activated.error};
    }
    const auto exposed = exposure_.expose(installationRoot, layout.value->current / "bin" / "vix");
    if (!exposed.valid())
    {
      auto restored = activator_.rollback(std::nullopt);
      fs::remove(layout.value->receipt, ec);
      if (!restored.valid())
        return {{}, fail(ErrorCode::rollback_failed, "PATH exposure failed and active pointer could not be cleared")};
      return {{}, exposed.error};
    }
    fs::remove_all(staging, ec);
    return {BootstrapOutcome{version, installationRoot, exposedExecutable}, {}};
#endif
  }

  Result<SignedReleaseManifest> HttpsReleaseManifestSource::fetch(const std::string &channel)
  {
    const auto it = config_.channels.find(channel);
    if (it == config_.channels.end())
      return {{}, fail(ErrorCode::release_source_missing, "no release source is configured for the installation channel")};
    auto manifest = get_metadata(it->second.manifest, ErrorCode::manifest_fetch_failed);
    if (!manifest.valid())
      return {{}, manifest.error};
    auto signatureText = get_metadata(it->second.signature, ErrorCode::signature_fetch_failed);
    if (!signatureText.valid())
      return {{}, signatureText.error};
    auto signature = parse_signature_envelope(*signatureText.value);
    if (!signature.valid())
      return {{}, fail(ErrorCode::signature_fetch_failed, "release signature envelope is malformed")};
    return {SignedReleaseManifest{std::move(*manifest.value), std::move(*signature.value)}, {}};
  }

  Result<fs::path> HttpsDistributionArtifactFetcher::fetch(const std::string &location, const fs::path &stagingPath)
  {
    if (!https_url(location) || stagingPath.empty() || maximumBytes_ == 0)
      return path_error(ErrorCode::artifact_fetch_failed, "artifact location or staging path is invalid");
    std::error_code ec;
    fs::create_directories(stagingPath.parent_path(), ec);
    if (ec)
      return path_error(ErrorCode::artifact_fetch_failed, "artifact staging directory could not be created");
    const auto partial = fs::path{stagingPath.string() + ".part"};
    std::string current = location;
    for (std::size_t redirects = 0; redirects <= 5; ++redirects)
    {
      try
      {
        vix::requests::RequestOptions options;
        options.follow_redirects = false;
        options.max_redirects = 0;
        options.verify_tls = true;
        options.set_timeout(std::chrono::seconds(120));
        options.headers.set("Accept-Encoding", "identity");
        std::optional<std::string> redirect;
        int status = 0;
        std::uint64_t amount = 0;
        bool tooLarge = false;
        std::ofstream output;
        options.response_head_sink = [&](int responseStatus, const vix::requests::Headers &headers)
        {
          status = responseStatus;
          if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
            redirect = headers.get("Location");
          if (status == 200)
          {
            output.open(partial, std::ios::binary | std::ios::trunc);
            if (!output)
              tooLarge = true;
          }
          if (const auto encoding = headers.get("Content-Encoding"); encoding && *encoding != "identity")
            tooLarge = true;
          if (const auto length = headers.get("Content-Length"))
          {
            try
            {
              if (std::stoull(*length) > maximumBytes_)
                tooLarge = true;
            }
            catch (const std::exception &)
            {
              tooLarge = true;
            }
          }
        };
        options.body_sink = [&](std::span<const std::byte> chunk)
        {
          if (status != 200 || tooLarge)
            return;
          const auto size = static_cast<std::uint64_t>(chunk.size());
          if (amount > maximumBytes_ || size > maximumBytes_ - amount)
          {
            tooLarge = true;
            throw std::runtime_error("artifact exceeds configured download bound");
          }
          output.write(reinterpret_cast<const char *>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
          if (!output)
            throw std::runtime_error("artifact staging write failed");
          amount += size;
        };
        vix::requests::Client client;
        const auto response = client.get(current, std::move(options));
        output.close();
        status = response.status_code();
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
        {
          if (!redirect || redirects == 5)
            break;
          try
          {
            const auto target = vix::requests::resolve_url_reference(vix::requests::Url::parse(current), *redirect);
            if (!https_url(target))
              break;
            current = target;
            continue;
          }
          catch (const std::exception &)
          {
            break;
          }
        }
        if (status != 200 || tooLarge || amount == 0 || !output)
          break;
        fs::rename(partial, stagingPath, ec);
        if (ec)
          break;
        return {stagingPath, {}};
      }
      catch (const std::exception &)
      {
        break;
      }
    }
    fs::remove(partial, ec);
    return path_error(ErrorCode::artifact_fetch_failed, "artifact download failed, exceeded its bound, or left HTTPS");
  }

  Result<std::optional<std::string>> PosixInstallationActivator::current_version()
  {
#if defined(_WIN32)
    return {{}, fail(ErrorCode::activation_failed, "Windows version activation is not implemented")};
#else
    std::error_code ec;
    const auto pointer = root_ / "current";
    const auto status = fs::symlink_status(pointer, ec);
    if ((ec == std::errc::no_such_file_or_directory) || status.type() == fs::file_type::not_found)
      return {std::optional<std::string>{}, {}};
    if (ec || !fs::is_symlink(status))
      return {{}, fail(ErrorCode::activation_failed, "managed current pointer is missing or invalid")};
    const auto target = fs::read_symlink(pointer, ec).lexically_normal();
    if (ec || target.parent_path() != fs::path{"versions"} || !parse_path_version(target.filename().string()))
      return {{}, fail(ErrorCode::activation_failed, "managed current pointer does not reference a version directory")};
    const auto versionStatus = fs::symlink_status(root_ / target, ec);
    if (ec || !fs::is_directory(versionStatus) || fs::is_symlink(versionStatus))
      return {{}, fail(ErrorCode::activation_failed, "managed current pointer does not reference a version directory")};
    return {std::optional<std::string>{target.filename().string()}, {}};
#endif
  }

  Result<Unit> PosixInstallationActivator::activate(const std::string &version)
  {
#if defined(_WIN32)
    static_cast<void>(version);
    return {{}, fail(ErrorCode::activation_failed, "Windows version activation is not implemented")};
#else
    if (!parse_path_version(version))
      return {{}, fail(ErrorCode::activation_failed, "activation version is invalid")};
    std::error_code ec;
    const auto targetStatus = fs::symlink_status(root_ / "versions" / version, ec);
    if (ec || !fs::is_directory(targetStatus) || fs::is_symlink(targetStatus))
      return {{}, fail(ErrorCode::activation_failed, "activation target version is not installed")};
    fs::create_directories(root_, ec);
    if (ec)
      return {{}, fail(ErrorCode::activation_failed, "managed installation root is unavailable")};
    const auto next = root_ / "current.next";
    fs::remove(next, ec);
    ec.clear();
    fs::create_directory_symlink(fs::path{"versions"} / version, next, ec);
    if (ec)
      return {{}, fail(ErrorCode::activation_failed, "temporary activation pointer could not be created")};
    fs::rename(next, root_ / "current", ec);
    if (ec)
    {
      fs::remove(next, ec);
      return {{}, fail(ErrorCode::activation_failed, "atomic activation pointer replacement failed")};
    }
    return {Unit{}, {}};
#endif
  }

  Result<Unit> PosixInstallationActivator::rollback(const std::optional<std::string> &previousVersion)
  {
#if defined(_WIN32)
    static_cast<void>(previousVersion);
    return {{}, fail(ErrorCode::rollback_failed, "Windows version rollback is not implemented")};
#else
    if (!previousVersion)
    {
      std::error_code ec;
      const auto pointer = root_ / "current";
      const auto status = fs::symlink_status(pointer, ec);
      if (ec == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found)
        return {Unit{}, {}};
      if (ec || !fs::is_symlink(status))
        return {{}, fail(ErrorCode::rollback_failed, "initial activation pointer could not be safely removed")};
      fs::remove(pointer, ec);
      if (ec)
        return {{}, fail(ErrorCode::rollback_failed, "initial activation pointer could not be removed")};
      return {Unit{}, {}};
    }
    auto restored = activate(*previousVersion);
    if (!restored.valid())
      return {{}, fail(ErrorCode::rollback_failed, "previous managed version could not be reactivated")};
    return restored;
#endif
  }

  Result<fs::path> SafeTarGzExtractor::extract(ArchiveFormat format, const fs::path &verifiedArchive,
                                               const fs::path &stagingDirectory)
  {
    if (format != ArchiveFormat::tar_gz)
      return path_error(ErrorCode::unsupported_archive, "only tar.gz distribution archives are supported safely");
    gzFile input = gzopen(verifiedArchive.string().c_str(), "rb");
    if (!input)
      return path_error(ErrorCode::unsafe_archive, "verified tar.gz archive could not be opened");
    std::string tar;
    constexpr std::size_t maximumExpandedBytes = 512ull * 1024ull * 1024ull;
    std::array<char, 64 * 1024> buffer{};
    int amount = 0;
    while ((amount = gzread(input, buffer.data(), static_cast<unsigned>(buffer.size()))) > 0)
    {
      const auto count = static_cast<std::size_t>(amount);
      if (tar.size() > maximumExpandedBytes || count > maximumExpandedBytes - tar.size())
      {
        gzclose(input);
        return path_error(ErrorCode::unsafe_archive, "expanded release archive exceeds the supported bound");
      }
      tar.append(buffer.data(), count);
    }
    const int closeStatus = gzclose(input);
    if (amount < 0 || closeStatus != Z_OK)
      return path_error(ErrorCode::unsafe_archive, "tar.gz stream is malformed or failed integrity checks");

    const auto octal = [](std::string_view field) -> std::optional<std::uint64_t>
    {
      std::size_t begin = 0;
      while (begin < field.size() && (field[begin] == ' ' || field[begin] == '\0'))
        ++begin;
      std::uint64_t result = 0;
      bool sawDigit = false;
      for (std::size_t i = begin; i < field.size() && field[i] != '\0' && field[i] != ' '; ++i)
      {
        if (field[i] < '0' || field[i] > '7' || result > (UINT64_MAX - 7) / 8)
          return std::nullopt;
        result = result * 8 + static_cast<unsigned>(field[i] - '0');
        sawDigit = true;
      }
      return sawDigit ? std::optional<std::uint64_t>{result} : std::optional<std::uint64_t>{0};
    };
    const auto safeRelative = [](std::string_view text)
    {
      if (text.empty() || text.front() == '/' || text.find('\\') != std::string_view::npos ||
          text.find(':') != std::string_view::npos)
        return false;
      fs::path path{std::string{text}};
      if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
        return false;
      for (const auto &part : path)
        if (part.empty() || part == "." || part == "..")
          return false;
      return true;
    };
    std::error_code ec;
    fs::create_directories(stagingDirectory, ec);
    if (ec)
      return path_error(ErrorCode::staging_failed, "archive staging directory is unavailable");
    if (fs::is_symlink(fs::symlink_status(stagingDirectory, ec)) || ec || !fs::is_empty(stagingDirectory, ec) || ec)
      return path_error(ErrorCode::unsafe_archive, "archive extraction destination must be an empty real directory");
    const auto root = fs::weakly_canonical(stagingDirectory, ec);
    if (ec)
      return path_error(ErrorCode::staging_failed, "archive staging root could not be canonicalized");
    std::set<std::string> seen;
    std::size_t offset = 0;
    std::size_t entries = 0;
    std::uint64_t totalFileBytes = 0;
    while (offset + 512 <= tar.size())
    {
      const auto *header = reinterpret_cast<const unsigned char *>(tar.data() + offset);
      bool allZero = true;
      for (std::size_t i = 0; i < 512; ++i)
        allZero = allZero && header[i] == 0;
      if (allZero)
        break;
      if (++entries > 100000)
        return path_error(ErrorCode::unsafe_archive, "release archive contains too many entries");
      const auto fieldText = [&](std::size_t start, std::size_t length)
      {
        const auto *begin = tar.data() + offset + start;
        const auto *end = static_cast<const char *>(std::memchr(begin, 0, length));
        return std::string{begin, end ? end : begin + length};
      };
      const auto checksum = octal(std::string_view{tar}.substr(offset + 148, 8));
      const auto size = octal(std::string_view{tar}.substr(offset + 124, 12));
      const auto mode = octal(std::string_view{tar}.substr(offset + 100, 8));
      if (!checksum || !size || !mode || *size > tar.size() || offset + 512 > tar.size() ||
          *size > static_cast<std::uint64_t>(tar.size() - offset - 512))
        return path_error(ErrorCode::unsafe_archive, "release archive header or entry size is invalid");
      std::uint64_t checksumSum = 0;
      for (std::size_t i = 0; i < 512; ++i)
        checksumSum += (i >= 148 && i < 156) ? static_cast<unsigned char>(' ') : header[i];
      if (checksumSum != *checksum)
        return path_error(ErrorCode::unsafe_archive, "release archive header checksum is invalid");
      std::string name = fieldText(0, 100);
      const std::string prefix = fieldText(345, 155);
      if (!prefix.empty())
        name = prefix + "/" + name;
      const char type = static_cast<char>(header[156]);
      if (type == '5' && !name.empty() && name.back() == '/')
        name.pop_back();
      else if (!name.empty() && name.back() == '/')
        return path_error(ErrorCode::unsafe_archive, "non-directory archive entry has a directory path suffix");
      if (!safeRelative(name))
        return path_error(ErrorCode::unsafe_archive, "release archive contains an unsafe relative path");
      const fs::path relative{name};
      const std::string key = relative.generic_string();
      if (!seen.insert(key).second)
        return path_error(ErrorCode::unsafe_archive, "release archive contains duplicate paths");
      const fs::path destination = root / relative;
      const auto normalized = destination.lexically_normal();
      const auto relativeToRoot = normalized.lexically_relative(root);
      if (relativeToRoot.empty() || relativeToRoot.is_absolute() || *relativeToRoot.begin() == "..")
        return path_error(ErrorCode::unsafe_archive, "release archive entry escapes staging");
      fs::path parent = destination.parent_path();
      for (auto cursor = parent; cursor != root && cursor.has_relative_path(); cursor = cursor.parent_path())
      {
        const auto status = fs::symlink_status(cursor, ec);
        if (!ec && fs::is_symlink(status))
          return path_error(ErrorCode::unsafe_archive, "release archive entry traverses a symlink");
        ec.clear();
      }
      const auto dataOffset = offset + 512;
      if (type == '\0' || type == '0')
      {
        if (*size > 256ull * 1024ull * 1024ull || totalFileBytes > maximumExpandedBytes - *size)
          return path_error(ErrorCode::unsafe_archive, "release archive file exceeds the supported bound");
        totalFileBytes += *size;
        fs::create_directories(parent, ec);
        if (ec)
          return path_error(ErrorCode::unsafe_archive, "release archive parent directory could not be created");
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (!output)
          return path_error(ErrorCode::unsafe_archive, "release archive file could not be created");
        output.write(tar.data() + dataOffset, static_cast<std::streamsize>(*size));
        output.close();
        if (!output)
          return path_error(ErrorCode::unsafe_archive, "release archive file could not be written");
        auto permissions = fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read;
        if ((*mode & 0111u) != 0)
          permissions |= fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
        fs::permissions(destination, permissions, fs::perm_options::replace, ec);
        if (ec)
          return path_error(ErrorCode::unsafe_archive, "release archive file permissions could not be set");
      }
      else if (type == '5')
      {
        if (*size != 0)
          return path_error(ErrorCode::unsafe_archive, "directory archive entry has a body");
        fs::create_directories(destination, ec);
        if (ec)
          return path_error(ErrorCode::unsafe_archive, "release archive directory could not be created");
      }
      else if (type == '2')
      {
        const std::string target = fieldText(157, 100);
        const fs::path targetPath{target};
        if (*size != 0 || target.empty() || target.front() == '/' ||
            target.find('\\') != std::string::npos || target.find(':') != std::string::npos ||
            targetPath.is_absolute() || targetPath.has_root_name() || targetPath.has_root_directory())
          return path_error(ErrorCode::unsafe_archive, "release archive symlink is unsafe");
        const auto resolved = (relative.parent_path() / targetPath).lexically_normal();
        const auto linkRelative = resolved.lexically_relative(fs::path{"."});
        if (linkRelative.empty() || linkRelative.is_absolute() || *linkRelative.begin() == "..")
          return path_error(ErrorCode::unsafe_archive, "release archive symlink escapes staging");
        fs::create_directories(parent, ec);
        if (ec)
          return path_error(ErrorCode::unsafe_archive, "release archive symlink parent could not be created");
        fs::create_symlink(target, destination, ec);
        if (ec)
          return path_error(ErrorCode::unsafe_archive, "release archive symlink could not be created");
      }
      else
        return path_error(ErrorCode::unsafe_archive, "release archive contains a hardlink or special filesystem entry");
      const auto padded = ((*size + 511) / 512) * 512;
      if (padded > tar.size() - offset - 512)
        return path_error(ErrorCode::unsafe_archive, "release archive entry is truncated");
      offset += 512 + static_cast<std::size_t>(padded);
    }
    if (entries == 0)
      return path_error(ErrorCode::unsafe_archive, "release archive is empty or malformed");
    return {root, {}};
  }

  Result<RuntimeOutcome> UpgradeRuntime::run(const fs::path &receiptPath, const fs::path &trustStorePath, const HostPlatform &host)
  {
    std::ifstream input(receiptPath, std::ios::binary);
    if (!input)
      return {{}, fail(ErrorCode::invalid_receipt, "installation receipt is unavailable")};
    const std::string receiptText{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    auto parsedReceipt = parse_install_receipt(receiptText);
    if (!parsedReceipt.valid())
      return {{}, parsedReceipt.error};
    auto receipt = std::move(*parsedReceipt.value);
    if (receipt.installKind == InstallKind::external)
      return {RuntimeOutcome{RuntimeOutcomeKind::externally_managed, receipt.externalOwner, {}}, {}};
    if (receipt.installKind != InstallKind::managed)
      return {RuntimeOutcome{RuntimeOutcomeKind::unmanaged_install, {}, {}}, {}};
    const fs::path root = receipt.installationRoot;
    auto active = activator_.current_version();
    if (!active.valid())
      return {{}, active.error};
    if (!active.value->has_value())
      return {{}, fail(ErrorCode::invalid_receipt, "managed installation has no active version pointer")};
    const std::string oldVersion = **active.value;
    // The atomically switched current pointer is the active-version authority.
    const auto activeVersion = parse_path_version(oldVersion);
    if (!activeVersion)
      return {{}, fail(ErrorCode::invalid_receipt, "active version pointer is invalid")};
    receipt.version = *activeVersion;

    auto signedManifest = source_.fetch(receipt.channel);
    if (!signedManifest.valid())
      return {{}, signedManifest.error};
    auto trust = load_release_trust_store(trustStorePath);
    if (!trust.valid())
      return {{}, trust.error};
    auto verified = verify_release_manifest(signedManifest.value->manifestText, signedManifest.value->signature, *trust.value, "current");
    if (!verified.valid())
      return {{}, verified.error};
    // `vix upgrade` is intentionally a host-installation operation. Optional
    // target SDK components in the same signed release are not selected or
    // installed by this transaction. Unknown component kinds remain rejected.
    if (std::any_of(verified.value->manifest().components.begin(), verified.value->manifest().components.end(),
                    [](const ReleaseComponent &component)
                    { return component.kind != ComponentKind::target_sdk; }))
      return {{}, fail(ErrorCode::unsupported_components, "host upgrade cannot install non-target-SDK release components")};
    auto plan = plan_upgrade(receipt, *verified.value, host);
    if (!plan.valid())
      return {{}, plan.error};
    if (plan.value->kind == PlanKind::already_current)
      return {RuntimeOutcome{RuntimeOutcomeKind::already_current, {}, oldVersion}, {}};
    if (plan.value->kind == PlanKind::externally_managed)
      return {RuntimeOutcome{RuntimeOutcomeKind::externally_managed, plan.value->externalOwner, {}}, {}};
    if (plan.value->kind == PlanKind::archive_installation)
      return {RuntimeOutcome{RuntimeOutcomeKind::unmanaged_install, {}, {}}, {}};
    if (plan.value->kind != PlanKind::upgrade_available || !plan.value->artifact)
    {
      const auto code = plan.value->kind == PlanKind::unsupported_host   ? ErrorCode::unsupported_host_artifact
                        : plan.value->kind == PlanKind::channel_mismatch ? ErrorCode::channel_mismatch
                                                                         : ErrorCode::no_newer_release;
      return {{}, fail(code, "no applicable newer release artifact is available")};
    }

    const auto newVersion = version_string(plan.value->to);
    const auto layout = installation_layout(root, newVersion);
    if (!layout.valid())
      return {{}, layout.error};
    std::error_code ec;
    const auto staging = root / "staging" / newVersion;
    fs::create_directories(staging.parent_path(), ec);
    if (ec || fs::is_symlink(fs::symlink_status(staging.parent_path(), ec)) || ec)
      return {{}, fail(ErrorCode::staging_failed, "distribution staging parent is invalid or symlinked")};
    const auto stageStatus = fs::symlink_status(staging, ec);
    if (!ec && fs::is_symlink(stageStatus))
      return {{}, fail(ErrorCode::staging_failed, "version staging path must not be a symlink")};
    ec.clear();
    fs::remove_all(staging, ec);
    if (ec)
      return {{}, fail(ErrorCode::staging_failed, "stale version staging data could not be cleared")};
    fs::create_directories(staging, ec);
    if (ec)
      return {{}, fail(ErrorCode::staging_failed, "version staging directory could not be created")};
    const auto archive = fetcher_.fetch(plan.value->artifact->location, staging / "release.archive");
    if (!archive.valid())
      return {{}, archive.error};
    const auto archiveSize = fs::file_size(*archive.value, ec);
    if (ec || archiveSize > maximum_distribution_archive_bytes ||
        (plan.value->artifact->size && archiveSize != *plan.value->artifact->size))
      return {{}, fail(ErrorCode::artifact_digest_mismatch, "downloaded artifact size is invalid or differs from the signed manifest")};
    std::ifstream archiveInput(*archive.value, std::ios::binary);
    if (!archiveInput)
      return {{}, fail(ErrorCode::artifact_fetch_failed, "downloaded release artifact cannot be read")};
    const std::string bytes{std::istreambuf_iterator<char>{archiveInput}, std::istreambuf_iterator<char>{}};
    if (bytes.size() != archiveSize || !verify_distribution_artifact(bytes, plan.value->artifact->digest).valid())
      return {{}, fail(ErrorCode::artifact_digest_mismatch, "downloaded artifact differs from its signed size or digest")};
    const auto extractedDir = staging / "root";
    fs::create_directories(extractedDir, ec);
    if (ec)
      return {{}, fail(ErrorCode::staging_failed, "extraction staging directory could not be created")};
    const auto extracted = extractor_.extract(plan.value->artifact->archive, *archive.value, extractedDir);
    if (!extracted.valid())
      return {{}, extracted.error};
    if (!fs::is_regular_file(*extracted.value / "bin" / "vix", ec) || ec ||
        !fs::is_directory(*extracted.value / "share" / "vix", ec) || ec)
      return {{}, fail(ErrorCode::invalid_distribution, "release is missing bin/vix or share/vix")};
    fs::create_directories(layout.value->versions, ec);
    if (ec)
      return {{}, fail(ErrorCode::staging_failed, "version directory could not be created")};
    const bool versionExists = fs::exists(layout.value->versionRoot, ec);
    if (ec || versionExists)
      return {{}, fail(ErrorCode::staging_failed, "version destination exists or cannot be created")};
    fs::rename(*extracted.value, layout.value->versionRoot, ec);
    if (ec)
      return {{}, fail(ErrorCode::staging_failed, "validated version could not be promoted")};
    auto activated = activator_.activate(newVersion);
    if (!activated.valid())
    {
      auto rollback = activator_.rollback(oldVersion);
      if (!rollback.valid())
        return {{}, fail(ErrorCode::rollback_failed, "activation failed and previous version restoration failed")};
      return {{}, fail(ErrorCode::activation_failed, "activation failed; previous version restored")};
    }
    receipt.version = plan.value->to;
    const auto receiptBytes = serialize_install_receipt(receipt);
    if (!receiptBytes.valid())
    {
      auto rollback = activator_.rollback(oldVersion);
      if (!rollback.valid())
        return {{}, fail(ErrorCode::rollback_failed, "receipt serialization failed and previous version restoration failed")};
      return {{}, receiptBytes.error};
    }
    const auto tempReceipt = fs::path{receiptPath.string() + ".next"};
    {
      std::ofstream output(tempReceipt, std::ios::binary | std::ios::trunc);
      if (!output)
      {
        auto rollback = activator_.rollback(oldVersion);
        if (!rollback.valid())
          return {{}, fail(ErrorCode::rollback_failed, "receipt staging failed and previous version restoration failed")};
        return {{}, fail(ErrorCode::activation_failed, "updated installation receipt could not be staged")};
      }
      output.write(receiptBytes.value->data(), static_cast<std::streamsize>(receiptBytes.value->size()));
      output.flush();
      if (!output)
      {
        output.close();
        fs::remove(tempReceipt, ec);
        auto rollback = activator_.rollback(oldVersion);
        if (!rollback.valid())
          return {{}, fail(ErrorCode::rollback_failed, "receipt write failed and previous version restoration failed")};
        return {{}, fail(ErrorCode::activation_failed, "updated installation receipt could not be written")};
      }
    }
    fs::rename(tempReceipt, receiptPath, ec);
    if (ec)
    {
      auto rollback = activator_.rollback(oldVersion);
      if (!rollback.valid())
        return {{}, fail(ErrorCode::rollback_failed, "receipt replacement failed and rollback failed")};
      return {{}, fail(ErrorCode::activation_failed, "receipt replacement failed; previous version restored")};
    }
    return {RuntimeOutcome{RuntimeOutcomeKind::upgraded, {}, newVersion}, {}};
  }
  Result<TargetSdkInstallOutcome> TargetSdkInstallRuntime::install(
      const fs::path &componentRoot, const fs::path &trustStorePath,
      const TargetPlatform &target, const std::string &channel)
  {
    const auto canonicalTarget = parse_target_platform(target.triple);
    if (componentRoot.empty() || !componentRoot.is_absolute() ||
        componentRoot != componentRoot.lexically_normal() || !canonicalTarget.valid() ||
        !qualified_target_sdk(target.triple) ||
        *canonicalTarget.value != target || channel.empty())
      return {{}, fail(ErrorCode::invalid_value, "target SDK installation inputs are invalid")};

    const auto signedManifest = source_.fetch(channel);
    if (!signedManifest.valid()) return {{}, signedManifest.error};
    const auto trust = load_release_trust_store(trustStorePath);
    if (!trust.valid()) return {{}, trust.error};
    const auto verified = verify_release_manifest(signedManifest.value->manifestText,
                                                  signedManifest.value->signature, *trust.value, "current");
    if (!verified.valid()) return {{}, verified.error};
    const auto &manifest = verified.value->manifest();
    if (manifest.channel != channel)
      return {{}, fail(ErrorCode::channel_mismatch, "signed release channel differs from requested channel")};

    const ReleaseComponent *selected = nullptr;
    for (const auto &component : manifest.components)
    {
      if (component.kind != ComponentKind::target_sdk || !component.target || *component.target != target)
        continue;
      if (selected)
        return {{}, fail(ErrorCode::malformed_manifest, "release contains multiple target SDK components for the requested target")};
      selected = &component;
    }
    if (!selected)
      return {{}, fail(ErrorCode::unsupported_host_artifact, "release has no target SDK component for the requested target")};
    if (selected->archive != ArchiveFormat::tar_gz)
      return {{}, fail(ErrorCode::unsupported_archive, "target SDK installation currently supports tar.gz artifacts only")};

    const auto version = version_string(manifest.version);
    if (selected->installPath && *selected->installPath != "target-sdks")
      return {{}, fail(ErrorCode::unsupported_components, "target SDK installation is restricted to the owned target-sdks root")};
    const fs::path namespacePath{"target-sdks"};

    std::error_code ec;
    const auto componentRootStatus = fs::symlink_status(componentRoot, ec);
    if (!ec && fs::is_symlink(componentRootStatus))
      return {{}, fail(ErrorCode::staging_failed, "target SDK component root must not be a symlink")};
    if (ec != std::errc::no_such_file_or_directory && ec)
      return {{}, fail(ErrorCode::staging_failed, "target SDK component root could not be inspected")};
    ec.clear();
    fs::path checked = componentRoot;
    for (const auto &part : namespacePath / target.triple)
    {
      checked /= part;
      const auto status = fs::symlink_status(checked, ec);
      if (!ec && fs::is_symlink(status))
        return {{}, fail(ErrorCode::staging_failed, "target SDK component path contains a symlink")};
      if (ec != std::errc::no_such_file_or_directory && ec)
        return {{}, fail(ErrorCode::staging_failed, "target SDK component path could not be inspected")};
      ec.clear();
    }
    const fs::path targetRoot = componentRoot / namespacePath / target.triple;
    const fs::path destination = targetRoot / version;
    const auto destinationStatus = fs::symlink_status(destination, ec);
    if (!ec && fs::is_symlink(destinationStatus))
      return {{}, fail(ErrorCode::staging_failed, "target SDK destination must not be a symlink")};
    if (!ec && fs::exists(destinationStatus))
    {
      const auto receiptPath = destination / "share" / "vix" / "target-sdk-install.toml";
      std::ifstream receiptInput(receiptPath, std::ios::binary);
      const std::string receiptText{std::istreambuf_iterator<char>{receiptInput}, std::istreambuf_iterator<char>{}};
      const auto expected = std::string{"format = 1\nversion = \""} + version + "\"\ntarget = \"" +
                            target.triple + "\"\ncomponent = \"" + selected->id + "\"\ndigest = \"" +
                            selected->digest.value + "\"\n";
      if (receiptInput.is_open() && receiptText == expected)
        return {TargetSdkInstallOutcome{selected->id, target, destination, true}, {}};
      return {{}, fail(ErrorCode::already_installed, "a different target SDK already occupies this versioned path")};
    }
    if (ec != std::errc::no_such_file_or_directory && ec)
      return {{}, fail(ErrorCode::staging_failed, "target SDK destination could not be inspected")};
    ec.clear();
    fs::create_directories(targetRoot, ec);
    if (ec || fs::is_symlink(fs::symlink_status(targetRoot, ec)) || ec)
      return {{}, fail(ErrorCode::staging_failed, "target SDK installation root is invalid or symlinked")};

    static std::atomic<std::uint64_t> stagingSequence{0};
    const auto staging = targetRoot / (".staging-" + version + "-" +
                                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                                       std::to_string(stagingSequence.fetch_add(1, std::memory_order_relaxed)));
    if (!fs::create_directory(staging, ec) || ec)
      return {{}, fail(ErrorCode::staging_failed, "unique target SDK staging directory could not be created")};
    const auto cleanup = [&]
    {
      std::error_code ignored;
      fs::remove_all(staging, ignored);
    };
    const auto archive = fetcher_.fetch(selected->location, staging / "component.archive");
    if (!archive.valid()) { cleanup(); return {{}, archive.error}; }
    const auto archiveSize = fs::file_size(*archive.value, ec);
    if (ec || archiveSize > maximum_distribution_archive_bytes ||
        (selected->size && archiveSize != *selected->size))
    {
      cleanup();
      return {{}, fail(ErrorCode::artifact_digest_mismatch, "target SDK archive size differs from signed metadata or exceeds the bound")};
    }
    std::ifstream archiveInput(*archive.value, std::ios::binary);
    if (!archiveInput) { cleanup(); return {{}, fail(ErrorCode::artifact_fetch_failed, "target SDK archive cannot be read")}; }
    const std::string bytes{std::istreambuf_iterator<char>{archiveInput}, std::istreambuf_iterator<char>{}};
    if (bytes.size() != archiveSize || !verify_distribution_artifact(bytes, selected->digest).valid())
    {
      cleanup();
      return {{}, fail(ErrorCode::artifact_digest_mismatch, "target SDK archive differs from signed digest")};
    }
    const auto extracted = extractor_.extract(selected->archive, *archive.value, staging / "unpacked");
    if (!extracted.valid()) { cleanup(); return {{}, extracted.error}; }
    if (!fs::is_directory(*extracted.value / "include", ec) || ec ||
        !fs::is_regular_file(*extracted.value / "lib" / "cmake" / "Vix" / "VixConfig.cmake", ec) || ec)
    {
      cleanup();
      return {{}, fail(ErrorCode::invalid_distribution, "target SDK is missing public headers or its Vix CMake package")};
    }
    const auto metadataDirectory = *extracted.value / "share" / "vix";
    const auto metadataStatus = fs::symlink_status(metadataDirectory, ec);
    if (ec || !fs::is_directory(metadataStatus) || fs::is_symlink(metadataStatus))
    {
      cleanup();
      return {{}, fail(ErrorCode::invalid_distribution, "target SDK metadata directory must be a real directory")};
    }
    const auto receiptPath = metadataDirectory / "target-sdk-install.toml";
    (void)fs::symlink_status(receiptPath, ec);
    if (!ec || (ec != std::errc::no_such_file_or_directory && ec))
    {
      cleanup();
      return {{}, fail(ErrorCode::invalid_distribution, "target SDK archive contains a conflicting ownership record")};
    }
    ec.clear();
    std::ofstream receiptOutput(receiptPath,
                                std::ios::binary | std::ios::trunc);
    receiptOutput << "format = 1\nversion = \"" << version << "\"\ntarget = \"" << target.triple
                  << "\"\ncomponent = \"" << selected->id << "\"\ndigest = \""
                  << selected->digest.value << "\"\n";
    receiptOutput.close();
    if (!receiptOutput) { cleanup(); return {{}, fail(ErrorCode::staging_failed, "target SDK ownership metadata could not be written")}; }
    fs::rename(*extracted.value, destination, ec);
    if (ec) { cleanup(); return {{}, fail(ErrorCode::staging_failed, "validated target SDK could not be atomically promoted")}; }
    cleanup();
    return {TargetSdkInstallOutcome{selected->id, target, destination, false}, {}};
  }
} // namespace vix::cli::distribution
