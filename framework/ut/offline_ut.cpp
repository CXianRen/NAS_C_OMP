#include "../offline/offline.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

static std::string config_file(const std::string &contents)
{
  char path[] = "/tmp/offline-ut-XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  FILE *file = fdopen(fd, "w");
  assert(file);
  assert(std::fwrite(contents.data(), 1, contents.size(), file) == contents.size());
  assert(std::fclose(file) == 0);
  return path;
}

static void check_cfg(const hams_binding_cfg *cfg, const int *cpus, int count)
{
  assert(cfg && cfg->thread_number == count);
  assert(cfg->mask.count() == static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    assert(cfg->mask[cpus[i]] && cfg->tid_to_cpu[i] == cpus[i]);
  }
}

static void check_full(const hams_binding_cfg *cfg, int count)
{
  int cpus[HAMS_CPU_COUNT];
  for (int i = 0; i < count; ++i) cpus[i] = i;
  check_cfg(cfg, cpus, count);
}

/* 所有 metadata 在挂载前已齐全，配置可引用尚未执行的 parallel region。 */
static void exercise(const char *path, int full, bool configured,
                     bool second_configured = false, int high_cpu = -1)
{
  region_info regions[] = {{"A", -1, 0, "a.c", 10, 0},
                           {"B", -1, 0, "b.c", 20, 0},
                           {"unused", -1, 0, "unused.c", 30, 0},
                           {"inner", 0, 0, "a.c", 40, 0}};
  offline *policy = offline_create(regions, 4, full, path);
  assert(policy);
  const int selected[] = {0, 2, 4};
  const int second[] = {3};
  for (int visit = 0; visit < 3; ++visit) {
    const hams_binding_cfg *a = offline_select_cfg(policy, 0);
    if (high_cpu >= 0) check_cfg(a, &high_cpu, 1);
    else if (configured) check_cfg(a, selected, 3);
    else check_full(a, full);
    offline_observe(policy, 0, visit * 100.0);
    const hams_binding_cfg *b = offline_select_cfg(policy, 1);
    if (second_configured) check_cfg(b, second, 1);
    else check_full(b, full);
    offline_observe(policy, 1, 0.0001);
  }
  offline_destroy(policy);
}

/* 单独进程捕获错误退出和日志，不把解析器的 exit 转成测试专用接口。 */
template <typename Function>
static std::string run_child(Function function, bool success)
{
  FILE *capture = std::tmpfile();
  assert(capture);
  std::fflush(nullptr);
  pid_t pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    assert(dup2(fileno(capture), STDOUT_FILENO) >= 0);
    assert(dup2(fileno(capture), STDERR_FILENO) >= 0);
    function();
    std::fflush(nullptr);
    _exit(0);
  }
  int status = 0;
  assert(waitpid(pid, &status, 0) == pid);
  assert(WIFEXITED(status));
  std::rewind(capture);
  std::string output;
  char buffer[512];
  while (std::size_t count = std::fread(buffer, 1, sizeof(buffer), capture))
    output.append(buffer, count);
  assert(std::fclose(capture) == 0);
  if ((WEXITSTATUS(status) == 0) != success) {
    std::fprintf(stderr, "Unexpected child status %d:\n%s", WEXITSTATUS(status), output.c_str());
    assert(false);
  }
  return output;
}

static std::size_t occurrences(const std::string &text, const std::string &part)
{
  std::size_t count = 0, position = 0;
  while ((position = text.find(part, position)) != std::string::npos) {
    ++count;
    position += part.size();
  }
  return count;
}

static void must_fail(const std::string &contents, int full = 8)
{
  std::string path = config_file(contents);
  std::string output = run_child([&] { exercise(path.c_str(), full, false); }, false);
  assert(!output.empty());
  assert(output.find("Offline loaded") == std::string::npos);
  assert(output.find("Offline config") == std::string::npos);
  assert(unlink(path.c_str()) == 0);
}

static std::string single_bit_hex(int bit)
{
  static const char digits[] = "1248";
  return std::string(1, digits[bit % 4]) + std::string(bit / 4, '0');
}

int main()
{
  std::string path = config_file(" # comment\r\n\n A:10 ; 3 ; 0X00015 \r\n"
                                 "B:20;1;8\nunused:30;2;0x3\n");
  std::string output = run_child([&] { exercise(path.c_str(), 8, true, true); }, true);
  assert(occurrences(output, "Offline loaded") == 1);
  assert(occurrences(output, "Offline config region=") == 3);
  assert(output.find("entries=3") != std::string::npos);
  assert(output.find("Offline missing") == std::string::npos);
  assert(unlink(path.c_str()) == 0);

  path = config_file("A:10;3;15\n");
  output = run_child([&] { exercise(path.c_str(), 3, true); }, true);
  assert(occurrences(output, "Offline missing region=B:20") == 1);
  assert(output.find("Offline missing region=A:10") == std::string::npos);
  assert(unlink(path.c_str()) == 0);

  path = config_file("");
  output = run_child([&] { exercise(path.c_str(), 3, false); }, true);
  assert(output.find("Offline loaded") != std::string::npos);
  assert(output.find("entries=0") != std::string::npos);
  assert(occurrences(output, "Offline missing region=") == 2);
  assert(unlink(path.c_str()) == 0);

  for (const char *missing : {static_cast<const char *>(nullptr), "", path.c_str(), "/tmp"}) {
    output = run_child([&] { exercise(missing, HAMS_CPU_COUNT, false); }, true);
    assert(occurrences(output, "Offline fallback") == 1);
    assert(output.find("Offline loaded") == std::string::npos);
    assert(output.find("Offline missing") == std::string::npos);
  }

  /* mask 表示系统 CPU ID；高位可超过线程数，不检查机器当前允许的 CPU。 */
  const int high_cpu = HAMS_CPU_COUNT - 1;
  path = config_file("A:10;1;" + single_bit_hex(high_cpu) + "\n");
  output = run_child([&] { exercise(path.c_str(), 3, false, false, high_cpu); }, true);
  assert(output.find("Offline loaded") != std::string::npos);
  assert(unlink(path.c_str()) == 0);

  for (const char *invalid : {
         "unknown:30;1;1\n", "A:11;1;1\n", "A;1;1\n",
         "inner:40;1;1\n",
         "A:10;1;1\nA:10;1;2\n", "A:10;0;0\n", "A:10;-1;1\n",
         "A:10;9;1ff\n", "A:10;2147483648;1\n", "A:10;2x;3\n",
         "A:10;;1\n", "A:10;1;\n", "A:10;1;0x\n", "A:10;1;0\n",
         "A:10;2;1\n", "A:10;1;3\n", "A:10;1;xyz\n", "A:10;1;-1\n",
         "A:10;1;1;extra\n", "A:10;1\n", ";1;1\n",
         "A:10;1;1\nunknown:30;1;2\n"})
    must_fail(invalid);
  must_fail("A:10;4;f\n", 3);
  must_fail("A:10;1;" + single_bit_hex(HAMS_CPU_COUNT) + "\n");
  std::puts("offline PASS (strict parsing, shared metadata, unused regions, fixed masks, fallback, log once)");
}
