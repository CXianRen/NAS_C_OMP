# OpenMP tuning framework

`region_control` 用 Clang AST 在编译前自动插入 OpenMP region 桩，并生成完整元数据；`tuner` 选择配置，`HAMS` 应用线程数及 CPU 绑定。应用的总计时窗口、step 和其他业务边界仍手工标记。

| `TUNER` | 搜索状态 | 配置生效位置 | 反馈耗时 |
| --- | --- | --- | --- |
| `none`（默认） | 无 | 不修改配置 | 仅保留可选计时报告 |
| `dummy` | 固定初始满线程配置 | `PARALLEL_START` | 单个 region，接收后忽略 |
| `offline` | 按配置文件固定每个 region | `PARALLEL_START` | 单个 region，接收后忽略 |
| `j2025` | 每个 region 独立 | `PARALLEL_START` | 单个 region |
| `j2025_b` | 每个 region 独立 | `PARALLEL_START` | 单个 region |
| `otter` | 全局一份 | `step_start` | 整个 step，包含串行部分 |

## 1. Framework

```text
build:
    Clang AST 识别应用源码的 parallel / parallel for / for
    按现有 nowait 规则静态合并循环，计算一对 START / END 的位置
    在构建目录生成插桩源码副本、region_auto.h / .c 和 instrumentation.json
    编译源码副本和完整 region_info 表，再链接共享 framework

region_control_init(control, region 表, region 数量)
// framework 读取 REGION_TIME_REPORT，决定是否累计并输出 region 报告
// 此时所有 region 的元数据已完整；任意 tuner 均可读取 control.regions[R]
tuner = tuner_attach(control)                 // 按环境变量 TUNER 挂载 callback
    if tuner 使用 HAMS 绑定:
        apply_config(最大线程数, close)         // 先创建并绑定完整 hot team
        apply_config(固定辅助配置)              // 配置初始化阶段，不 select/observe
应用初始化 / warmup                           // 使用固定辅助配置
iteration_start()                             // 正式总计时窗口

for 每个 step:
    step_start():
        if tuner == Otter:
            反馈尚未结束的上一 step 样本
            apply_config(Otter.select_cfg())
            开始整步计时

    for 本 step 实际执行的每个 parallel region R:
        PARALLEL_START(R):
            if tuner in {J2025, J2025_B, Dummy, Offline}:
                apply_config(tuner.select_cfg(R))
            按需开始 region 计时

        执行 R 一次                           // 内部 for 仅计时

        PARALLEL_END(R):
            按需结束 region 计时
            if tuner in {J2025, J2025_B, Dummy, Offline}: tuner.observe(R, region 耗时)

    step_end()  // 可省略；Otter 结束整步计时并反馈，不切换配置

iteration_end()  // 结算 Otter 最后一个未结束的 step，再结束总窗口
region_report()
tuner_detach()   // 注销并释放；J2025/J2025_B 逐 region 输出，Otter 仅输出一条配置

apply_config(cfg):
    允许绑定 → HAMS.apply(cfg)；否则 → 仅设置线程数

HAMS.apply(cfg):
    if cfg 与当前配置相同: return
    设置默认线程数；按 cfg 的 tid → CPU 映射绑定线程
```

源码、头文件或编译参数变化时重新生成源码副本和元数据；无需先执行 region，`TUNER=none` 和 `INSTRUMENT=0` 也保留完整信息。原始源码只保留 OpenMP 指令和手工应用边界，不维护 OpenMP region ID 或表。

自动生成器复用 `ompt_v` 的 Clang AST 源码改写设计，事件仍进入现有 `region_control`，没有使用 OMPT 回调。`parallel` 桩在构造外执行，`for` 桩只由 master 计时。连续 nowait 循环与后续普通 for 合成一个 region，END 放在该普通 for 的隐式 barrier 后；尾部 nowait 的 END 放在 enclosing parallel join 后。合并区间只生成一个 ID，不增加 barrier，规则见 [for_region_merge.md](for_region_merge.md)。

生成范围仅包含应用源码，不包含 tuner 或 HAMS 内部的 OpenMP 构造。`iteration_start/end`、`step_start/end`、初始化、挂载、报告等调用仍由应用显式保留。`framework/region_control/generate_regions.py` 继续服务手工桩的独立测试；应用构建使用 `instrument_regions.py`。

当前自动范围是 C/C++ 自由函数中的字面量 `parallel`、`parallel for` 和 `for`；相关源文件须一起传给生成器。条件块和直接调用 helper 内的 nowait 可延伸到调用者已有的 barrier 或 parallel join。编译期确定组成员、唯一 ID 和 END 位置；条件分支或重复调用仅用 master 执行标记保证首次执行时 START、固定终点处 END，全部跳过时不计时。跨文件成员及终点记录在 `instrumentation.json` 的 `loop_sites` / `end_file` 中。

嵌套 parallel、宏生成的 pragma、头文件内的 OpenMP 实现、namespace/类方法/lambda、不带花括号的条件 nowait，以及无法证明唯一静态终点的控制流会报错；这类程序可继续使用手工桩接口。每个可执行文件最多 256 个 region。

tuner 样本排除配置选择、绑定和反馈开销，总窗口包含这些开销；关闭报告仍向 tuner 提供样本。

`REGION_TIME_REPORT` 由 framework 在每次 `region_control_init` 时读取：`1`、`true`、`yes`、`on` 开启，未设置或其他值关闭。各 benchmark 不读取此变量、不传递报告开关；结束时由 framework 决定是否输出。`INSTRUMENT=0` 始终关闭 region 报告。

### 辅助阶段线程配置

使用 HAMS 手动绑定的 tuner 在 attach 时先创建并绑定启动线程上限大小的 OpenMP team，再为正式循环前的初始化和 warmup 应用一份固定配置。`PHAMS_AUX_NUM_THREADS` 指定辅助线程数，默认是启动线程上限；合法范围为 `[1, 启动线程上限]`。`PHAMS_AUX_PROC_BIND` 可取 `close`（默认）或 `spread`：`close` 选择前 N 个 CPU，`spread` 在启动 CPU 范围内等距选择 N 个 CPU。两步都使用普通 `hams_binding_cfg` 和 HAMS；配置相同时第二次 apply 由缓存直接返回。整个准备过程位于正式计时前，且不会调用 tuner 的 `select/observe` 或推进搜索状态。

这两个变量只控制辅助阶段；进入正式 step 后仍完全由所选 tuner 决定配置。`TUNER=none` 以及不使用 HAMS 手动绑定的路径保持原行为。正式窗口后的验证沿用 tuner 最后应用的配置。

当前最小实现沿用既有 Dummy/J2025 的 CPU 编号约定，要求可绑定 CPU 是从 0 开始的连续区间；它面向当前 LLVM/libomp 运行环境，不解析完整的 OpenMP places 语法。

Dummy 保留上述挂载、选择、绑定、计时和反馈流程：

```text
attach: 初始配置 = (启动线程上限 N, tid → CPU tid)
select_cfg(region): return 初始配置
observe(region, 耗时): return
```

`N` 取启动时 OpenMP 线程上限（受线程限制和 HAMS 容量限制），不受 Otter 参数影响；每个 region 始终使用这一个配置。

## 2. Offline：按文件配置每个 region

`OFFLINE_CONFIG` 指定文件，默认空；每行 `函数名:原始源码 pragma 行号;线程数;十六进制 mask`，例如 `compute_rhs:43;2;0x5`（CPU 0、2；行号以当前生成结果为准）。允许空行和 `#` 注释行。

配置名称直接使用自动生成的 `function:line`，可从构建目录的 `instrumentation.json` 中读取 `parent=-1` 的条目，或查看运行报告。没有旧手工桩行号的兼容映射；源码位置变化后须更新配置。region ID 是当前构建内部的索引，不作为外部配置键。同名同位置键的歧义在生成阶段报错。

```text
attach:
    从 control.regions 读取 parallel region 的 function:line
    路径为空或文件无法读取 → 提示，所有 region 使用 Dummy 的初始满线程配置
    否则先校验整个文件：
        名称不在 region_info 表中 / 重复名称 / 格式错误 → 报错退出
        线程数不在 [1, N] / mask 越界 / 置位数不等于线程数 → 报错退出
    输出加载结果和各项配置；名称合法但未执行的 region 允许存在
select_cfg(R):
    有配置 → 返回配置，tid 按置位 CPU 编号升序绑定
    缺配置 → 每个 region 提示一次，返回初始满线程配置
observe(R, 耗时): return
```

## 3. J2025：先选绑定，再搜线程数

`N` 为初始线程上限（至少为 2 的偶数）。每个 region 独立推进下列状态；每次调用只执行一个配置。

```text
WARMUP:
    用 (N, SCATTER) 执行一次，忽略耗时
    → MAPPING

MAPPING:
    后续两次调用分别测量 (N, SCATTER)、(N, CLOSE)
    best_mapping = 耗时较小者
    → SEARCH

SEARCH:
    固定 best_mapping，在 {2, 4, ..., N} 上做 Fibonacci 搜索
    选择下一个未测线程数，执行一次，缓存耗时并缩小区间
    没有待测候选 → STABLE

STABLE:
    每次进入该 region，恢复其已测最快配置
```

## 4. J2025_B：先搜线程数，再选绑定

独立实现在 `framework/j2025_b`，有自己的状态和 API；按 region 调优。`CLOSE` 即连续 CPU 绑定（CONTIGUOUS）。

```text
WARMUP:
    用 (N, CLOSE) 执行一次，忽略耗时
    → SEARCH

SEARCH:
    固定 CLOSE，在 {2, 4, ..., N} 上做 Fibonacci 搜索
    每次执行一个未测线程数，缓存耗时并缩小区间
    没有待测候选 → 固定已测最快线程数 best_NT → MAPPING

MAPPING:
    后续两次调用分别测量 (best_NT, SCATTER)、(best_NT, CLOSE)
    使用这两个新样本选择较快绑定；相同耗时选 SCATTER
    → STABLE

STABLE:
    每次进入该 region，恢复 (best_NT, best_mapping)
```

## 5. Otter：先搜线程数，再选绑定

`N` 为线程上限，`ε` 为阈值（默认 10%）。全局状态跨 step 推进，**仅在 `step_start` 应用配置，同一步所有 region 共用它**。

```text
用 (N, CONTIGUOUS) 预热两个 step，忽略耗时
固定 CONTIGUOUS，在后续三个 step 分别测量 N、N/2、3N/4
    // 线程数向下取整，至少为 1；每个样本是完整 step 的耗时

if |time[N] - time[3N/4]| / min(time[N], time[3N/4]) ≤ ε:
    用三个样本做 Newton 二次插值
    在 [N/2, N] 中，选预测时间 ≤ 最佳预测时间 × (1+ε) 的最小线程数
    插值无效时回退到已测最快线程数
else:
    在 [1, N] 中做整数黄金分割搜索，复用已有样本
    区间足够小时补测两端，选已测最快线程数

固定所选线程数：
    若支持绑定，比较 CONTIGUOUS 和 SCATTER
    每种绑定先预热一个 step，再测量一个 step，保留较快者

搜索结束：后续 step 复用统一配置
```

源码：[挂载](framework/tuner/tuner.cpp) · [计时](framework/region_control/region_control.c) · [绑定](framework/hams/hams_binding.cpp) · [Dummy](framework/dummy/dummy.cpp) · [Offline](framework/offline/offline.cpp) · [自动插桩](framework/region_control/instrument_regions.py) · [J2025](framework/j2025/j2025.cpp) · [J2025_B](framework/j2025_b/j2025_b.cpp) · [Otter](framework/otter/otter.cpp)。

分阶段伪代码：[J2025](framework/j2025/J-2025.md) · [J2025_B](framework/j2025_b/J-2025_B.md) · [Otter](framework/otter/Otter.md)。

## 运行与检查

已同步 `ompt_v` 的全部 benchmark：

| 目录 | 程序 |
| --- | --- |
| [NPB3.3-OMP-C](NPB3.3-OMP-C/README.md) | BT、CG、DC、EP、FT、IS、LU、MG、SP、UA，保留各自合法 Class |
| [lulesh](lulesh/README) | LULESH 2.0，默认 OpenMP / 非 MPI |
| [rodina](rodina/README.md) | Hotspot、Streamcluster、ParticleFilter、CFD 的 float / double 与预计算版本，共 7 个可执行文件 |

这些程序复用 `framework/benchmark/benchmark.h`、`benchmark.cpp` 和 `benchmark.mk` 初始化、挂载 tuner、报告与链接公共实现。应用手工设置正式窗口和真实迭代的 step；DC、EP 等批处理程序只设置一个工作 step。各目录 README 给出输入、边界和独立测试命令。

需要 OpenMP C/C++17 编译器、hwloc、Clang、Python 3 和 GNU Make 4.3+。`CLANG` / `PYTHON` 可指定插桩生成工具；使用 GCC 编译时也需要 Clang 解析。绑定所选的 CPU 须可用。

```sh
make -C NPB3.3-OMP-C CLASS=S CC=clang-18
make -C lulesh CC=clang-18 CXX=clang++-18
make -C rodina CC=clang-18 CXX=clang++-18
env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
    OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=8 \
    PHAMS_AUX_NUM_THREADS=8 PHAMS_AUX_PROC_BIND=close \
    TUNER=otter REGION_TIME_REPORT=1 ./NPB3.3-OMP-C/bin/SP.S

make -C example run TUNER=otter
OFFLINE_CONFIG=/path/to/regions.conf make -C example run TUNER=offline
make -C framework/ut test
python3 NPB3.3-OMP-C/tests/test_build.py --cc clang-18
python3 NPB3.3-OMP-C/tests/test_suite.py --cc clang-18
python3 lulesh/tests/test_automatic_build.py --cc clang-18 --cxx clang++-18
python3 rodina/tests/test_instrumented_build.py --cc clang-18 --cxx clang++-18
python3 example/test_build.py --cc clang-18 --cxx clang++-18
```

切换 `TUNER=none|dummy|offline|j2025|j2025_b|otter` 或 `OFFLINE_CONFIG` 无需重编译。Dummy、Offline、J2025 与 J2025_B 需要 `OMP_PROC_BIND=false`。`INSTRUMENT=0` 关闭 region hook，此构建仅允许 `TUNER=none`。
Otter 默认打印搜索过程（step、配置、耗时 `time_us`、预热标记、搜索分支和状态转换），收敛后停止逐步打印；`OTTER_VERBOSE=0` 仅保留最终配置。过程日志不受 `REGION_TIME_REPORT` 控制，打印不计入 tuner 样本。
独立绑定工具见 [FastCheck](FastCheck/README.md)。
