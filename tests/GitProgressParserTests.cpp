#include <vix/cli/util/GitProgress.hpp>

#include <cassert>
#include <vector>

int main()
{
  std::vector<vix::cli::util::GitProgressEvent> events;
  vix::cli::util::GitProgressParser parser([&](const auto &event) { events.push_back(event); });
  parser.push("Receiving obj");
  parser.push("ects:  37% (123/331), 3.80 MiB | 1.40 MiB/s\rResolving deltas:  81% (250/308)\n");
  parser.push("unknown Git message\rReceiving objects: 100% (331/331), 8.24 MiB | 2.10 MiB/s, done.\r");
  parser.push("Counting objects: malformed\n");
  parser.push("Counting objects: 999999999999999999999999% (1/2)\n");
  parser.finish();

  assert(events.size() == 3);
  assert(events[0].phase == "receiving objects" && events[0].percent == 37);
  assert(events[0].current == 123 && events[0].total == 331);
  assert(events[0].transferred == "3.80 MiB" && events[0].speed == "1.40 MiB/s");
  assert(events[1].phase == "resolving deltas" && events[1].percent == 81);
  assert(events[2].percent == 100 && events[2].transferred == "8.24 MiB");
}
