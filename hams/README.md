# HAMS 线程绑定 API

HAMS 接收 `hams_binding_cfg { thread_number, mask, tid_to_cpu }`，设置后续 OpenMP 并行区的线程数和 CPU 绑定。使用 C++17、标准库、Linux 和 OpenMP。

`mask` 直接使用 `<bitset>` 中的 `std::bitset<HAMS_CPU_COUNT>`：`cfg.mask[cpu] = true` 置位，`cfg.mask[cpu]` 查询，`==` 比较配置。无需第三方 bitset 库。

bit 下标是系统 CPU ID（hwloc PU 的 `os_index`）。调用方在生成配置的同一次遍历中填写 mask 和 `tid_to_cpu[]`：`tid x` 对应从低到高第 x 个置位 bit，每线程绑定一个 CPU。例如 `{2, 6, 9}` 对应 `tid_to_cpu = {2, 6, 9}`。contiguous、spread 等策略由调用方完成转换，绑定层直接读取映射。

绑定时，将 `tid_to_cpu[tid]` 用 `CPU_SET` 写入单 CPU 的 `cpu_set_t`，再调用 `sched_setaffinity`；CPU ID 须小于 `CPU_SETSIZE`。

`HAMS_CPU_COUNT` 默认是 **128**，可修改头文件或用 `-DHAMS_CPU_COUNT=N` 指定；所有编译单元须使用相同值。假定 `[0, HAMS_CPU_COUNT)` 内所有 CPU 都支持绑定，不读取或检查 allowed CPU 掩码。

## 使用示例

在 `hams/` 目录将以下代码保存为 `example.cpp`。示例从 CPU 0 开始绑定最多 3 个线程：

```cpp
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hams_binding.h"
#include <assert.h>
#include <omp.h>
#include <sched.h>
#include <stdio.h>

int main(void)
{
    hams_binding *binding = hams_binding_create();
    hams_binding_status status;
    hams_binding_cfg cfg = {};
    hams_binding_get_status(binding, &status);
    assert(status.supported);

    cfg.thread_number = status.max_threads < 3 ? status.max_threads : 3;
    for (int tid = 0; tid < cfg.thread_number; tid++) {
        int cpu = tid;
        cfg.mask[cpu] = true;
        cfg.tid_to_cpu[tid] = cpu;
    }
    hams_binding_apply(binding, &cfg);

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        printf("tid=%d target_cpu=%d actual_cpu=%d\n", tid,
               hams_binding_get_target_cpu(&cfg, tid), sched_getcpu());
    }
    hams_binding_destroy(binding);
    return 0;
}
```

编译运行（也可将 `clang++-18` 换成 `g++`）：

```sh
clang++-18 -std=c++17 -O2 -fopenmp example.cpp hams_binding.cpp -o example
env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
    OMP_PROC_BIND=false OMP_NUM_THREADS=3 ./example
```

## 简单 UT

[hams_binding_ut.cpp](hams_binding_ut.cpp) 独立测试三次连续切换，不依赖 Otter：

| 阶段（满线程数为 8） | 线程数 | CPU ID |
| --- | --- | --- |
| full | 8 | 0、1、2、3、4、5、6、7 |
| half close | 4 | 0、1、2、3 |
| half spread | 4 | 0、2、4、6 |

```sh
make -C hams test THREADS=8
```

从 benchmark 根目录执行。`THREADS` 取本机支持的偶数，且不超过 `HAMS_CPU_COUNT`。
UT 仅用 `assert` 检查后续普通并行区的线程数、单 CPU 亲和性和实际 CPU ID。
每个阶段输出 `PASS`，并按 tid 顺序打印 `sched_getaffinity` 读取到的实际 bitmask；三个阶段均使用初始满线程数作为显示宽度，例如 `THREADS=8` 时显示 8 位，最右边是 CPU 0。

## 接口与约定

| 接口 | 功能 |
| --- | --- |
| `hams_binding_create()` | 按编译期 CPU 数量和 OpenMP 限制建立上下文 |
| `hams_binding_get_status()` | 查询线程上限和绑定支持状态 |
| `hams_binding_apply(binding, &cfg)` | 直接按映射绑定，相同配置直接返回 |
| `hams_binding_get_target_cpu(&cfg, tid)` | 仅用于测试/示例诊断，查询 tid 对应的 CPU ID |
| `hams_binding_destroy(binding)` | 仅释放上下文内存，保留线程配置 |

- 要求 `1 <= thread_number <= status.max_threads`，mask 置位数等于线程数；`tid_to_cpu[tid]` 必须是第 tid 个置位 bit 的 CPU ID，严格升序且小于 `HAMS_CPU_COUNT`。未使用的 mask bit 必须为 0，用 `hams_binding_cfg cfg = {}` 初始化。
- 在同一协调线程、并行区外调用创建、应用和销毁；一个线程池同时只使用一个上下文。切换配置时修改 cfg，再调用 `apply`。
- `get_status`、`apply`、`destroy` 返回 `void`；错误仅使用 `assert` 终止程序，不返回错误码、不尝试失败回退。查询和应用的参数必须非空；`destroy(NULL)` 也可调用。
- 相同线程数和 mask 会跳过线程组创建和重复绑定；期间不要另行修改默认线程数、dynamic 设置或工作线程亲和性。上下文只缓存线程数和 mask，可以复用并修改传入的 cfg 变量。
- 性能构建可增加 `-DNDEBUG` 关闭断言检查；有效配置下的绑定仍会执行。
- `destroy` 只调用 `delete` 释放上下文；最后应用的线程数、dynamic 设置和 CPU 绑定继续保持。

完整参数与返回值说明见 [hams_binding.h](hams_binding.h)。
