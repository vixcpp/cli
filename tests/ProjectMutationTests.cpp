#include <vix/cli/util/ProjectMutation.hpp>
#include <cassert>
#include <filesystem>
#include <fstream>
namespace fs=std::filesystem;
static std::string read(const fs::path&p){std::ifstream in(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(in)),{});}
int main(){const fs::path root=fs::temp_directory_path()/"vix mutation test";fs::remove_all(root);fs::create_directories(root);std::ofstream(root/"a")<<"old\n";std::string e;
 {vix::cli::util::ProjectMutationTransaction t(root);assert(t.stage_write(root/"a","new",e));assert(t.stage_write(root/"b","two",e));assert(t.commit(e));assert(read(root/"a")=="new"&&read(root/"b")=="two");}
 {vix::cli::util::ProjectMutationTransaction t(root);assert(t.stage_write(root/"a","bad",e));assert(t.stage_write(root/"c","three",e));t.fail_publish_at_for_test(1);assert(!t.commit(e));assert(read(root/"a")=="new"&&!fs::exists(root/"c"));}
 {vix::cli::util::ProjectMutationTransaction t(root);assert(t.stage_delete(root/"b",e));t.rollback();assert(read(root/"b")=="two");}
 {std::ofstream(root/"unrelated",std::ios::binary)<<"keep";vix::cli::util::ProjectMutationTransaction t(root);assert(t.stage_write(root/"a","interrupted",e));assert(t.stage_write(root/"missing","created",e));t.abandon_publish_at_for_test(1);assert(!t.commit(e));assert(read(root/"a")=="interrupted");assert(!fs::exists(root/"missing"));}
 {vix::cli::util::ProjectMutationLock recovery(root);assert(recovery.acquired());assert(read(root/"a")=="new");assert(!fs::exists(root/"missing"));assert(read(root/"unrelated")=="keep");}
 {vix::cli::util::ProjectMutationLock a(root);assert(a.acquired());vix::cli::util::ProjectMutationLock b(root);assert(!b.acquired());}
 {vix::cli::util::ProjectMutationLock b(root);assert(b.acquired());}
 fs::remove_all(root);}
