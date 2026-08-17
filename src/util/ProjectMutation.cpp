#include <vix/cli/util/ProjectMutation.hpp>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace vix::cli::util
{
  namespace fs = std::filesystem;
  namespace {
    std::atomic<unsigned long> sequence{0};
    std::optional<std::string> read_bytes(const fs::path &p) { std::ifstream in(p, std::ios::binary); if (!in) return std::nullopt; return std::string((std::istreambuf_iterator<char>(in)), {}); }
    bool write_bytes(const fs::path &p, const std::string &bytes) { std::ofstream out(p, std::ios::binary | std::ios::trunc); return out && static_cast<bool>(out << bytes); }
    bool atomic_replace(const fs::path &from, const fs::path &to, std::string &error) { std::error_code ec; fs::rename(from,to,ec); if (!ec) return true; error="commit failed: "+ec.message(); return false; }
    bool under_root(const fs::path &root, const fs::path &path) { const auto rel=path.lexically_relative(root); return !rel.empty() && rel.native().find("..") != 0; }
    bool recover_transactions(const fs::path &root, std::string &error)
    {
      const fs::path base=root/".vix"/"transactions"; std::error_code ec;
      if (!fs::exists(base, ec)) return true;
      for (const auto &dir : fs::directory_iterator(base, ec)) {
        if (ec || !dir.is_directory()) { error="cannot inspect transaction journal"; return false; }
        const fs::path journal=dir.path(); const auto state=read_bytes(journal/"state");
        if (state && *state=="committed\n") { fs::remove_all(journal, ec); if (ec) { error="cannot clean completed transaction journal: "+ec.message(); return false; } continue; }
        const auto meta=read_bytes(journal/"meta");
        if (!state || !meta) { error="ambiguous unfinished project transaction: "+journal.string(); return false; }
        std::istringstream in(*meta); char kind; int existed; std::string relative; unsigned index=0;
        while (in >> kind >> existed >> index >> std::quoted(relative)) {
          const fs::path target=root/fs::path(relative);
          if (!under_root(root, target)) { error="unsafe transaction journal path"; return false; }
          if (existed) { const auto bytes=read_bytes(journal/("original-"+std::to_string(index))); if (!bytes || !write_bytes(target, *bytes)) { error="cannot restore unfinished transaction"; return false; } }
          else { fs::remove(target, ec); if (ec) { error="cannot remove unfinished transaction output: "+ec.message(); return false; } }
        }
        if (!in.eof()) { error="invalid transaction journal"; return false; }
        fs::remove_all(journal, ec); if (ec) { error="cannot remove recovered transaction journal: "+ec.message(); return false; }
      }
      return true;
    }
  }
  ProjectMutationLock::ProjectMutationLock(const fs::path &root)
  {
#ifdef _WIN32
    error_ = "Project mutation lock is not implemented on this platform.";
#else
    std::error_code ec; fs::create_directories(root / ".vix" / "locks", ec); if (ec) { error_="cannot create mutation lock directory: "+ec.message(); return; }
    const auto path = root / ".vix" / "locks" / "project-mutation.lock";
    fd_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0600); if (fd_ < 0) { error_="cannot open project mutation lock"; return; }
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) { ::close(fd_); fd_=-1; error_="another Vix project mutation is active"; return; }
    if (!recover_transactions(root, error_)) { ::flock(fd_, LOCK_UN); ::close(fd_); fd_=-1; }
#endif
  }
  ProjectMutationLock::~ProjectMutationLock()
  {
#ifndef _WIN32
    if (fd_ >= 0) { ::flock(fd_, LOCK_UN); ::close(fd_); }
#endif
  }
  ProjectMutationTransaction::ProjectMutationTransaction(const fs::path &root) : root_(root) {}
  ProjectMutationTransaction::~ProjectMutationTransaction() { if (!finished_) rollback(); }
  bool ProjectMutationTransaction::stage_write(const fs::path &path, const std::string &bytes, std::string &error)
  {
    if (finished_) { error="transaction is already finished"; return false; }
    std::error_code ec; fs::create_directories(path.parent_path(),ec); if (ec) { error="staging failed: "+ec.message(); return false; }
    Item item; item.path=path; item.original=read_bytes(path); item.temporary=path.string()+".vix-txn-"+std::to_string(++sequence);
    { std::ofstream out(item.temporary,std::ios::binary|std::ios::trunc); if (!out || !(out<<bytes)) { std::error_code ignored; fs::remove(item.temporary,ignored); error="staging failed"; return false; } }
    items_.push_back(std::move(item)); return true;
  }
  bool ProjectMutationTransaction::stage_delete(const fs::path &path, std::string &error) { if (finished_) { error="transaction is already finished"; return false; } Item item; item.path=path; item.original=read_bytes(path); item.deletion=true; items_.push_back(std::move(item)); return true; }
  bool ProjectMutationTransaction::prepare_journal(std::string &error)
  {
    if (items_.empty()) return true;
    journal_=root_/".vix"/"transactions"/("txn-"+std::to_string(++sequence)); std::error_code ec;
    fs::create_directories(journal_, ec); if (ec) { error="cannot create transaction journal: "+ec.message(); return false; }
    std::ofstream meta(journal_/"meta", std::ios::binary|std::ios::trunc); if (!meta) { error="cannot write transaction journal"; return false; }
    for (std::size_t i=0;i<items_.size();++i) { const auto &item=items_[i]; if (!under_root(root_, item.path)) { error="transaction path escapes project"; return false; } const auto rel=item.path.lexically_relative(root_).generic_string(); meta << (item.deletion ? 'D' : 'W') << ' ' << (item.original ? 1 : 0) << ' ' << i << ' ' << std::quoted(rel) << '\n'; if (item.original && !write_bytes(journal_/("original-"+std::to_string(i)), *item.original)) { error="cannot save transaction recovery data"; return false; } }
    meta.flush(); if (!meta || !write_bytes(journal_/"state", "prepared\n")) { error="cannot finalize transaction journal"; return false; }
    journalPrepared_=true; return true;
  }
  void ProjectMutationTransaction::cleanup_journal() { if (!journal_.empty()) { std::error_code ec; fs::remove_all(journal_,ec); } }
  bool ProjectMutationTransaction::commit(std::string &error)
  {
    if (finished_) { error="transaction is already finished"; return false; }
    if (!prepare_journal(error)) { rollback(); return false; }
    if (!items_.empty() && !write_bytes(journal_/"state", "publishing\n")) { error="cannot mark transaction publishing"; rollback(); return false; }
    for (std::size_t i=0;i<items_.size();++i) { auto &item=items_[i]; if (failPublishAt_ && i == *failPublishAt_) { error="commit failed (injected)"; rollback(); return false; } if (abandonPublishAt_ && i == *abandonPublishAt_) { error="commit interrupted (injected)"; finished_=true; return false; } std::error_code ec; if (item.deletion) fs::remove(item.path,ec); else if (!atomic_replace(item.temporary,item.path,error)) { rollback(); return false; } if (ec) { error="commit failed: "+ec.message(); rollback(); return false; } }
    if (!items_.empty() && !write_bytes(journal_/"state", "committed\n")) { error="transaction committed but journal cannot be finalized"; finished_=true; return false; }
    finished_=true; cleanup_journal(); return true;
  }
  void ProjectMutationTransaction::rollback()
  {
    for (auto it=items_.rbegin();it!=items_.rend();++it) { std::error_code ec; if (it->original) { const fs::path tmp=it->path.string()+".vix-rollback-"+std::to_string(++sequence); { std::ofstream out(tmp,std::ios::binary|std::ios::trunc); out<<*it->original; } fs::rename(tmp,it->path,ec); } else fs::remove(it->path,ec); if (!it->temporary.empty()) fs::remove(it->temporary,ec); }
    finished_=true; cleanup_journal();
  }
}
