#ifndef VIX_CLI_UTIL_PROJECT_MUTATION_HPP
#define VIX_CLI_UTIL_PROJECT_MUTATION_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vix::cli::util
{
  class ProjectMutationLock
  {
  public:
    explicit ProjectMutationLock(const std::filesystem::path &projectRoot);
    ~ProjectMutationLock();
    ProjectMutationLock(const ProjectMutationLock &) = delete;
    bool acquired() const { return fd_ >= 0; }
    const std::string &error() const { return error_; }
  private: int fd_{-1}; std::string error_;
  };

  class ProjectMutationTransaction
  {
  public:
    explicit ProjectMutationTransaction(const std::filesystem::path &projectRoot);
    ~ProjectMutationTransaction();
    bool stage_write(const std::filesystem::path &path, const std::string &bytes, std::string &error);
    bool stage_delete(const std::filesystem::path &path, std::string &error);
    bool commit(std::string &error);
    void rollback();
    // Test seam: fail publication before this zero-based item.
    void fail_publish_at_for_test(std::size_t index) { failPublishAt_ = index; }
    // Test seam: emulate abrupt termination before this item; recovery is
    // deliberately deferred to the next lock holder.
    void abandon_publish_at_for_test(std::size_t index) { abandonPublishAt_ = index; }
  private:
    struct Item { std::filesystem::path path, temporary; std::optional<std::string> original; bool deletion{false}; };
    bool prepare_journal(std::string &error);
    void cleanup_journal();
    std::filesystem::path root_, journal_;
    std::vector<Item> items_; bool finished_{false}; bool journalPrepared_{false}; std::optional<std::size_t> failPublishAt_, abandonPublishAt_;
  };
}
#endif
