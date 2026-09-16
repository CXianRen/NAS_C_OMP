#include "offline.h"

#include <cassert>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

struct offline {
  const region_info *regions;
  hams_binding_cfg initial{};
  std::unordered_map<std::string, hams_binding_cfg> configs;
  std::vector<const hams_binding_cfg *> selected;
  bool loaded = false;
};

[[noreturn]] static void fail(const char *path, std::size_t line,
                             const std::string &message)
{
  std::fprintf(stderr, "Offline error path=%s line=%zu: %s\n",
               path ? path : "", line, message.c_str());
  std::exit(EXIT_FAILURE);
}

static std::string trim(const std::string &value)
{
  auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

static std::string hex_mask(const hams_binding_cfg &cfg)
{
  std::string text;
  for (int base = ((HAMS_CPU_COUNT - 1) / 4) * 4; base >= 0; base -= 4) {
    unsigned digit = 0;
    for (int bit = 0; bit < 4 && base + bit < HAMS_CPU_COUNT; ++bit)
      if (cfg.mask[base + bit]) digit |= 1u << bit;
    if (digit || !text.empty()) text += "0123456789abcdef"[digit];
  }
  return "0x" + (text.empty() ? "0" : text);
}

static hams_binding_cfg parse_cfg(const std::string &number,
                                  const std::string &mask, int maximum,
                                  const char *path, std::size_t line)
{
  hams_binding_cfg cfg{};
  if (number.empty() || number.find_first_not_of("0123456789") != std::string::npos)
    fail(path, line, "thread_number must be a positive decimal integer");
  auto parsed = std::from_chars(number.data(), number.data() + number.size(), cfg.thread_number);
  if (parsed.ec == std::errc::result_out_of_range || cfg.thread_number > maximum)
    fail(path, line, "thread_number exceeds max_threads=" + std::to_string(maximum));
  if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() ||
      cfg.thread_number < 1)
    fail(path, line, "thread_number must be a positive decimal integer");

  std::size_t begin = mask.size() >= 2 && mask[0] == '0' &&
                      (mask[1] == 'x' || mask[1] == 'X') ? 2 : 0;
  if (begin == mask.size()) fail(path, line, "mask must be a hexadecimal integer");
  for (std::size_t index = begin; index < mask.size(); ++index) {
    char ch = mask[index];
    int digit = ch >= '0' && ch <= '9' ? ch - '0' :
                ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 :
                ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
    if (digit < 0) fail(path, line, "mask must be a hexadecimal integer");
    std::size_t nibble = mask.size() - 1 - index;
    if (digit && nibble > static_cast<std::size_t>((HAMS_CPU_COUNT - 1) / 4))
      fail(path, line, "mask exceeds HAMS_CPU_COUNT=" + std::to_string(HAMS_CPU_COUNT));
    if (!digit) continue;
    for (int bit = 0; bit < 4; ++bit) {
      if (!(digit & (1 << bit))) continue;
      std::size_t cpu = nibble * 4 + bit;
      if (cpu >= HAMS_CPU_COUNT)
        fail(path, line, "mask exceeds HAMS_CPU_COUNT=" + std::to_string(HAMS_CPU_COUNT));
      cfg.mask[cpu] = true;
    }
  }
  if (cfg.mask.count() != static_cast<std::size_t>(cfg.thread_number))
    fail(path, line, "mask bit count does not match thread_number");
  int tid = 0;
  for (int cpu = 0; cpu < HAMS_CPU_COUNT; ++cpu)
    if (cfg.mask[cpu]) cfg.tid_to_cpu[tid++] = cpu;
  return cfg;
}

static void fallback(const offline *tuner, const char *path, const char *reason)
{
  std::printf("Offline fallback path=%s reason=%s threads=%d mask=%s\n",
              path ? path : "", reason, tuner->initial.thread_number,
              hex_mask(tuner->initial).c_str());
  std::fflush(stdout);
}

static std::string region_key(const region_info &region)
{
  return std::string(region.name) + ":" + std::to_string(region.line);
}

offline *offline_create(const region_info *regions, int region_count,
                        int max_threads, const char *path)
{
  assert(regions && region_count > 0 && max_threads >= 1 && max_threads <= HAMS_CPU_COUNT);
  auto *tuner = new offline{};
  tuner->regions = regions;
  tuner->selected.resize(region_count, nullptr);
  tuner->initial.thread_number = max_threads;
  for (int tid = 0; tid < max_threads; ++tid) {
    tuner->initial.mask[tid] = true;
    tuner->initial.tid_to_cpu[tid] = tid;
  }
  if (!path || !*path) {
    fallback(tuner, path, "empty-path");
    return tuner;
  }
  std::ifstream file(path);
  if (!file.is_open()) {
    fallback(tuner, path, "cannot-open");
    return tuner;
  }

  // 先完整读取，读取失败时不能保留部分配置。
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) lines.push_back(line);
  if (file.bad() || !file.eof()) {
    fallback(tuner, path, "cannot-read");
    return tuner;
  }
  std::vector<std::string> order;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    line = trim(lines[index]);
    if (line.empty() || line[0] == '#') continue;
    auto first = line.find(';');
    auto second = first == std::string::npos ? first : line.find(';', first + 1);
    if (second == std::string::npos || line.find(';', second + 1) != std::string::npos)
      fail(path, index + 1, "expected region_name:line;thread_number;hex_mask");
    std::string key = trim(line.substr(0, first));
    bool known = false;
    for (int id = 0; id < region_count; ++id) {
      // 内部 for 只计时，不单独调整线程配置。
      if (regions[id].parent == -1 && region_key(regions[id]) == key) {
        known = true;
        break;
      }
    }
    if (!known) fail(path, index + 1, "unknown region=" + key);
    if (tuner->configs.count(key)) fail(path, index + 1, "duplicate region=" + key);
    auto cfg = parse_cfg(trim(line.substr(first + 1, second - first - 1)),
                         trim(line.substr(second + 1)), max_threads, path, index + 1);
    tuner->configs.emplace(key, cfg);
    order.push_back(key);
  }
  tuner->loaded = true;
  std::printf("Offline loaded path=%s entries=%zu\n", path, order.size());
  for (const auto &key : order) {
    const auto &cfg = tuner->configs.at(key);
    std::printf("Offline config region=%s threads=%d mask=%s\n",
                key.c_str(), cfg.thread_number, hex_mask(cfg).c_str());
  }
  std::fflush(stdout);
  return tuner;
}

const hams_binding_cfg *offline_select_cfg(offline *tuner, int id)
{
  assert(tuner && id >= 0 && id < static_cast<int>(tuner->selected.size()));
  auto *&selected = tuner->selected[id];
  if (selected) return selected;
  selected = &tuner->initial;
  if (!tuner->loaded) return selected;
  const auto &region = tuner->regions[id];
  std::string key = region_key(region);
  auto config = tuner->configs.find(key);
  if (config != tuner->configs.end()) {
    selected = &config->second;
  } else {
    std::printf("Offline missing region=%s threads=%d mask=%s (using default)\n",
                key.c_str(), selected->thread_number, hex_mask(*selected).c_str());
    std::fflush(stdout);
  }
  return selected;
}

void offline_observe(offline *, int, double) {}

void offline_destroy(offline *tuner)
{
  delete tuner;
}
