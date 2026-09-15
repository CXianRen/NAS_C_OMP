XObject：一个完整 iteration（即 step）的运行时间，包括所有 region 和串行部分。

```c
State *S = global_state();       // 所有 region 共用一份状态和配置
N = MAX_THREADS;
H = max(1, floor(N / 2));
Q = max(1, floor(3 * N / 4));
epsilon = 0.10;                  // 可通过环境变量修改
S->phase = WARMUP;
S->T = N;
S->P = CONTIGUOUS;
S->warmup_count = 0;
S->sample_index = 0;

for each iteration I {

    /* 只在 step_start 应用配置；整步内不再改变线程数或绑定。 */
    step_start(I) {
        apply_configuration(S->T, S->P);  // 不支持绑定时仅设置线程数
    }

    /* 配置生效后开始计时；本次正常执行一次，不重复运行 region。 */
    XObject = run_and_measure_whole_iteration(I);

    /* step_sample：只更新搜索状态，下一配置到下个 step_start 才生效。 */

    /* ==============================================
     * 1. Warm-up
     * ============================================== */

    if (S->phase == WARMUP) {
        // 满线程预热两步，不保存这两步的耗时。
        if (++S->warmup_count == 2)
            S->phase = INITIAL_SAMPLE;
        continue;
    }

    /* ==============================================
     * 2. Initial thread-count samples
     * ============================================== */

    if (S->phase == INITIAL_SAMPLE) {
        samples[S->T] = XObject;
        sequence = {N, H, Q};

        if (++S->sample_index < 3) {
            S->T = sequence[S->sample_index];
            continue;
        }

        denominator = min(samples[N], samples[Q]);
        saturated = denominator > 0 &&
            abs(samples[N] - samples[Q]) / denominator <= epsilon;

        /* ==========================================
         * 3. Newton prediction or golden search
         * ========================================== */

        if (saturated) {
            prediction = newton_quadratic({H, Q, N}, samples);
            // 在整数区间 [H, N] 内，取满足下式的最小线程数：
            // 0 < prediction[T] <= min_valid_prediction * (1 + epsilon)
            T = smallest_T_within_tolerance(prediction, epsilon);
            if (duplicate_nodes_or_invalid_prediction)
                T = best_measured_threads(samples);
            finish_thread_search(S, T);
        } else {
            S->interval = [1, N];
            S->phase = GOLDEN_SEARCH;
            prepare_golden_step(S);
        }
        continue;
    }

    if (S->phase == GOLDEN_SEARCH) {
        samples[S->T] = XObject;
        prepare_golden_step(S);
        continue;
    }

    /* ==============================================
     * 4. Thread mapping search at fixed thread count
     * ============================================== */

    if (S->phase == CONTIGUOUS_WARMUP) {
        S->phase = CONTIGUOUS_MEASURE;  // 忽略一次预热
        continue;
    }
    if (S->phase == CONTIGUOUS_MEASURE) {
        S->contiguous_time = XObject;
        S->P = SCATTER;
        S->phase = SCATTER_WARMUP;
        continue;
    }
    if (S->phase == SCATTER_WARMUP) {
        S->phase = SCATTER_MEASURE;     // 忽略一次预热
        continue;
    }
    if (S->phase == SCATTER_MEASURE) {
        S->P = S->contiguous_time < XObject ? CONTIGUOUS : SCATTER;
        S->phase = STABLE;
        continue;
    }

    /* ==============================================
     * 5. Stable execution
     * ============================================== */

    if (S->phase == STABLE) {
        // 本步已使用收敛配置执行；后续所有 step 继续复用，不重新搜索。
        continue;
    }
}

prepare_golden_step(S) {
    // 复用已测点，根据两个黄金分割点的耗时缩小整数区间。
    // 区间足够小时补测两端；每次只返回一个未测线程数。
    T = golden_next_unmeasured(S->interval, samples);
    if (T exists)
        S->T = T;
    else
        finish_thread_search(S, best_measured_threads(samples));
}

finish_thread_search(S, T) {
    S->T = T;                      // 固定线程数，仅准备后续 step
    S->P = CONTIGUOUS;
    S->phase = placement_supported ? CONTIGUOUS_WARMUP : STABLE;
}
```

`step_sample` 在 `step_end` 反馈；省略时由下一次 `step_start` 或最后的 `iteration_end` 反馈一次，配置应用和反馈开销不计入样本。
