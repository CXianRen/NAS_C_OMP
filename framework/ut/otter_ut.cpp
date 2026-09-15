/* Original 14 mock cases: otter branch 42e091c152420c3c9ce1e8fadcb43555507dbeb7,
 * otter/tests/otter_tuner_search_correctness.cpp. This white-box test includes
 * the pure production policy: do not additionally link otter.cpp. No OpenMP,
 * CPU discovery, binding, environment variables or wall-clock samples. */
#include "../otter/otter.cpp"

typedef struct {
  const char *name;
  int max_threads;
  int optimum;                 /* Zero selects a constant mock curve. */
  double base;
  double curvature;
  double threshold;
  int expect_golden;
  int expected_threads;
  int placement_supported;     /* Logical capability, no actual binding. */
  double contiguous_metric;
  double scatter_metric;
  otter_placement expected_placement;
} search_case;

static double mock_metric(const search_case *test, const otter_search_state *tuner)
{
  double distance = tuner->current_threads - test->optimum;

  switch (tuner->state) {
    case OTTER_WARMUP_FIRST:
    case OTTER_WARMUP_SECOND:
    case OTTER_CONTIGUOUS_WARMUP:
    case OTTER_SCATTER_WARMUP:
      return 1.0e6; /* Warmup outliers must not affect selection. */
    case OTTER_CONTIGUOUS_MEASURE: return test->contiguous_metric;
    case OTTER_SCATTER_MEASURE: return test->scatter_metric;
    default:
      return test->base + test->curvature * distance * distance;
  }
}

static int run_case(const search_case *test)
{
  std::vector<int> cpus(test->max_threads);
  for (int cpu = 0; cpu < test->max_threads; ++cpu) cpus[cpu] = cpu;
  otter_options options;
  options.threshold_fraction = test->threshold;
  options.golden_distance = 1;
  options.placement_supported = test->placement_supported;
  otter *policy = otter_create(test->max_threads, cpus.data(), cpus.size(), options);
  auto &tuner = policy->search;
  int steps;
  int saw_golden = 0;
  int contiguous_samples = 0;
  int scatter_samples = 0;
  int failures = 0;

  for (steps = 0; steps < 128 && tuner.state != OTTER_TUNING_DONE; steps++) {
    if (tuner.current_threads < 1 ||
        tuner.current_threads > test->max_threads) {
      fprintf(stderr, "FAIL: %s requested invalid T=%d\n",
              test->name, tuner.current_threads);
      failures++;
      break;
    }
    if (tuner.state == OTTER_GOLDEN_SEARCH) saw_golden = 1;
    if (tuner.state == OTTER_CONTIGUOUS_MEASURE) contiguous_samples++;
    if (tuner.state == OTTER_SCATTER_MEASURE) scatter_samples++;
    if ((tuner.state == OTTER_CONTIGUOUS_WARMUP ||
         tuner.state == OTTER_CONTIGUOUS_MEASURE ||
         tuner.state == OTTER_SCATTER_WARMUP ||
         tuner.state == OTTER_SCATTER_MEASURE) &&
        tuner.current_threads != test->expected_threads) {
      fprintf(stderr, "FAIL: %s placement comparison used T=%d, expected %d\n",
              test->name, tuner.current_threads, test->expected_threads);
      failures++;
    }
    const hams_binding_cfg before = *otter_select_cfg(policy);
    assert(before.thread_number == tuner.current_threads);
    assert(before.mask.count() == static_cast<unsigned>(tuner.current_threads));
    for (int tid = 0; tid < before.thread_number; ++tid) {
      assert(before.mask[before.tid_to_cpu[tid]]);
      assert(tid == 0 || before.tid_to_cpu[tid - 1] < before.tid_to_cpu[tid]);
    }
    otter_observe(policy, mock_metric(test, &tuner));
    // Advancing the search cannot rewrite the configuration that just ran.
    assert(tuner.cfg.thread_number == before.thread_number);
    assert(tuner.cfg.mask == before.mask);
  }

  if (tuner.state != OTTER_TUNING_DONE ||
      tuner.current_threads != test->expected_threads ||
      tuner.best_threads != test->expected_threads ||
      tuner.placement != test->expected_placement ||
      tuner.best_placement != test->expected_placement ||
      saw_golden != test->expect_golden ||
      contiguous_samples != test->placement_supported ||
      scatter_samples != test->placement_supported) {
    fprintf(stderr,
            "FAIL: %s state=%s T_best=%d expected=%d P_best=%s expected=%s "
            "golden=%d expected=%d placement_samples=%d/%d\n",
            test->name, otter_state_name(tuner.state), tuner.best_threads,
            test->expected_threads, otter_placement_name(tuner.best_placement),
            otter_placement_name(test->expected_placement), saw_golden,
            test->expect_golden, contiguous_samples, scatter_samples);
    failures++;
  }
  /* Terminal state must remain stable even if another metric is supplied. */
  if (tuner.state == OTTER_TUNING_DONE) {
    const hams_binding_cfg final = *otter_select_cfg(policy);
    assert(final.thread_number == test->expected_threads);
    assert(tuner.selected_placement == test->expected_placement);
    otter_observe(policy, 1.0e9);
    assert(otter_select_cfg(policy)->mask == final.mask);
    otter_observe(policy, 1.0e-9);
    assert(otter_select_cfg(policy)->mask == final.mask);
    if (tuner.state != OTTER_TUNING_DONE ||
        tuner.current_threads != test->expected_threads ||
        tuner.placement != test->expected_placement) {
      fprintf(stderr, "FAIL: %s terminal state changed\n", test->name);
      failures++;
    }
  }
  printf("case=%s check=%s path=%s T_best=%d P_best=%s steps=%d\n",
         test->name, failures == 0 ? "PASS" : "FAIL",
         saw_golden ? "GOLDEN_SEARCH" : "NEWTON",
         tuner.best_threads,
         test->placement_supported ? otter_placement_name(tuner.best_placement)
                                   : "UNCONTROLLED", steps);
  otter_destroy(policy, false);
  return failures;
}

/* Virtual sparse IDs exercise word boundaries, the highest valid bit,
 * rounding in SCATTER, and max_threads smaller than the candidate pool. */
static int check_binding_masks(void)
{
  const int boundary = HAMS_CPU_COUNT > 66 ? 64 : 4;
  const int last = HAMS_CPU_COUNT - 1;
  const int cpus[] = {0, 1, 2, boundary - 1, boundary, boundary + 1, last};
  const int expected[5][7] = {
      {0, 1, 2}, {0, boundary - 1, last}, {0},
      {0, 1, 2, boundary - 1, boundary, boundary + 1, last},
      {0, 2, boundary, last}
  };
  const int threads[] = {3, 3, 1, 7, 4};
  otter *policy = otter_create(7, cpus, 7);
  auto &state = policy->search;
  int failures = 0;
  for (int test = 0; test < 5; ++test) {
    state.current_threads = threads[test];
    state.placement = test == 0 ? OTTER_CONTIGUOUS : OTTER_SCATTER;
    const auto *cfg = otter_select_cfg(policy);
    std::bitset<HAMS_CPU_COUNT> mask;
    for (int tid = 0; tid < threads[test]; ++tid) {
      mask[expected[test][tid]] = true;
      if (cfg->tid_to_cpu[tid] != expected[test][tid]) ++failures;
    }
    if (cfg->thread_number != threads[test] || cfg->mask != mask) ++failures;
  }
  otter_destroy(policy, false);

  policy = otter_create(3, cpus, 7);
  policy->search.placement = OTTER_SCATTER;
  const auto *cfg = otter_select_cfg(policy);
  for (int tid = 0; tid < 3; ++tid)
    if (cfg->tid_to_cpu[tid] != expected[1][tid]) ++failures;
  otter_destroy(policy, false);
  printf("configuration_masks=%s cases=6 failures=%d\n",
         failures == 0 ? "PASS" : "FAIL", failures);
  return failures;
}

/* Separate application tuner instances own separate whole-iteration searches. */
static void check_instances(void)
{
  int cpus[16];
  for (int cpu = 0; cpu < 16; ++cpu) cpus[cpu] = cpu;
  otter *first = otter_create(16, cpus, 16);
  otter *second = otter_create(16, cpus, 16);
  for (int step = 0; step < 3; ++step) {
    assert(otter_select_cfg(first)->thread_number == 16);
    otter_observe(first, 10.0);
    assert(second->search.state == OTTER_WARMUP_FIRST);
    assert(second->search.cfg.thread_number == 0);
  }
  assert(first->search.state == OTTER_SAMPLE_HALF);
  assert(first->search.cfg.thread_number == 16);
  assert(otter_select_cfg(first)->thread_number == 8);
  assert(otter_select_cfg(second)->thread_number == 16);
  otter_destroy(first, false);
  otter_destroy(second, false);
  puts("otter_independent_instances=PASS otter_last_selected_cfg=PASS");
}

static void check_options(void)
{
  int cpus[17];
  for (int cpu = 0; cpu < 17; ++cpu) cpus[cpu] = cpu;
  otter *policy = otter_create(17, cpus, 17);
  assert(policy->search.golden_distance == 3);
  assert(policy->search.threshold_fraction == .1);
  assert(policy->search.placement_supported);
  otter_destroy(policy, false);
  puts("otter_default_options=PASS");
}

int main(void)
{
  /* Newton: full and 3/4 samples match, so use the saturation branch.
   * For 10 + 0.2*(T-14)^2, the minimum is 10 at T=14. With 10%
   * tolerance the first acceptable integer is T=12 (10.8 <= 11).
   * Golden: convex curves have independently known exact minima. */
  static const search_case cases[] = {
      {"newton-flat/scatter", 16, 0, 10, 0, .1, 0, 8,
       1, 10, 3, OTTER_SCATTER},
      {"newton-tolerance/contiguous", 16, 14, 10, .2, .1, 0, 12,
       1, 3, 10, OTTER_CONTIGUOUS},
      {"newton-zero-threshold", 16, 14, 10, .2, 0, 0, 14,
       1, 10, 3, OTTER_SCATTER},
      {"placement-tie", 16, 0, 10, 0, .1, 0, 8,
       1, 3, 3, OTTER_SCATTER},
      {"golden-interior-low", 16, 5, 1, 1, .1, 1, 5,
       1, 10, 3, OTTER_SCATTER},
      {"golden-interior-high", 16, 11, 1, 1, .1, 1, 11,
       1, 3, 10, OTTER_CONTIGUOUS},
      {"golden-lower-bound", 16, 1, 1, 1, .1, 1, 1,
       1, 10, 3, OTTER_SCATTER},
      {"golden-upper-bound", 16, 16, 1, 1, .1, 1, 16,
       1, 3, 10, OTTER_CONTIGUOUS},
      {"golden-odd-size", 7, 3, 1, 1, .1, 1, 3,
       1, 10, 3, OTTER_SCATTER},
      {"newton-no-placement", 16, 0, 10, 0, .1, 0, 8,
       0, 0, 0, OTTER_CONTIGUOUS},
      {"golden-no-placement", 16, 5, 1, 1, .1, 1, 5,
       0, 0, 0, OTTER_CONTIGUOUS},
      {"single-thread", 1, 0, 10, 0, .1, 0, 1,
       0, 0, 0, OTTER_CONTIGUOUS},
      {"two-threads/duplicate-samples", 2, 0, 10, 0, .1, 0, 1,
       0, 0, 0, OTTER_CONTIGUOUS},
      {"three-threads", 3, 0, 10, 0, .1, 0, 1,
       0, 0, 0, OTTER_CONTIGUOUS},
  };
  check_options();
  check_instances();
  int failures = check_binding_masks();
  size_t test;

  for (test = 0; test < sizeof(cases) / sizeof(cases[0]); test++) {
    failures += run_case(&cases[test]);
  }
  printf("search_correctness=%s cases=%zu failures=%d\n",
         failures == 0 ? "PASS" : "FAIL",
         sizeof(cases) / sizeof(cases[0]), failures);
  return failures == 0 ? 0 : 1;
}
