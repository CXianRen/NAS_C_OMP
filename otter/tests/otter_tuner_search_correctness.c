#define _GNU_SOURCE

/* White-box test: feed mock metrics into the production state machine.
 * Do not call create/begin/end/destroy: those discover CPUs, create teams,
 * bind workers or read wall time. Do not link otter_tuner.o separately. */
#include "../otter_tuner.c"

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

static double mock_metric(const search_case *test, const otter_tuner *tuner)
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
  otter_tuner tuner = {0};
  int steps;
  int saw_golden = 0;
  int contiguous_samples = 0;
  int scatter_samples = 0;
  int failures = 0;

  /* Virtual topology and policy parameters, independent of host/environment. */
  tuner.benchmark_name = test->name;
  tuner.enabled = 1;
  tuner.max_threads = test->max_threads;
  tuner.current_threads = tuner.best_threads = test->max_threads;
  tuner.half_threads = test->max_threads / 2;
  if (tuner.half_threads < 1) tuner.half_threads = 1;
  tuner.three_quarter_threads = 3 * test->max_threads / 4;
  if (tuner.three_quarter_threads < 1) tuner.three_quarter_threads = 1;
  tuner.threshold_fraction = test->threshold;
  /* Unit tests request integer resolution for exact golden-search answers. */
  tuner.golden_distance = 1;
  tuner.placement_supported = test->placement_supported;
  tuner.placement = tuner.best_placement = OTTER_CONTIGUOUS;
  tuner.state = OTTER_WARMUP_FIRST;
  tuner.samples = calloc((size_t)test->max_threads + 1, sizeof(*tuner.samples));
  if (tuner.samples == NULL) {
    fprintf(stderr, "out of memory\n");
    exit(2);
  }

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
    otter_advance_state(&tuner, mock_metric(test, &tuner));
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
    otter_advance_state(&tuner, 1.0e9);
    otter_advance_state(&tuner, 1.0e-9);
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
  free(tuner.samples);
  return failures;
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
  int failures = 0;
  size_t test;

  for (test = 0; test < sizeof(cases) / sizeof(cases[0]); test++) {
    failures += run_case(&cases[test]);
  }
  printf("search_correctness=%s cases=%zu failures=%d\n",
         failures == 0 ? "PASS" : "FAIL",
         sizeof(cases) / sizeof(cases[0]), failures);
  return failures == 0 ? 0 : 1;
}
