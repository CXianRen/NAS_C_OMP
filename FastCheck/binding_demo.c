#define _GNU_SOURCE
#include <omp.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

static const char *env_or_default(const char *name)
{
    const char *value = getenv(name);
    return value ? value : "(runtime default)";
}

static void print_place_cpus(int place)
{
    if (place < 0) {
        printf("-");
        return;
    }
    int count = omp_get_place_num_procs(place);
    int *cpus = malloc((size_t)count * sizeof(*cpus));
    if (!cpus) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    omp_get_place_proc_ids(place, cpus);
    printf("{");
    for (int i = 0; i < count; ++i)
        printf("%s%d", i ? "," : "", cpus[i]);
    printf("}");
    free(cpus);
}

/* Compact contiguous Linux CPU IDs into ranges. */
static void print_affinity(const cpu_set_t *mask)
{
    int first = 1;
    printf("{");
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, mask))
            continue;
        int end = cpu;
        while (end + 1 < CPU_SETSIZE && CPU_ISSET(end + 1, mask))
            ++end;
        printf("%s%d", first ? "" : ",", cpu);
        if (end != cpu)
            printf("-%d", end);
        first = 0;
        cpu = end;
    }
    printf("}");
}

int main(void)
{
    omp_set_dynamic(0);
    printf("OMP_NUM_THREADS=%s  OMP_PLACES=%s  OMP_PROC_BIND=%s\n",
           env_or_default("OMP_NUM_THREADS"), env_or_default("OMP_PLACES"),
           env_or_default("OMP_PROC_BIND"));

    int failed = 0;
#pragma omp parallel reduction(| : failed)
    {
        int tid = omp_get_thread_num();
        int team = omp_get_num_threads();
        int place = omp_get_place_num();
        int cpu = sched_getcpu();
        cpu_set_t mask;
        CPU_ZERO(&mask);
        int affinity_ok = sched_getaffinity(0, sizeof(mask), &mask) == 0;
        if (!affinity_ok || cpu < 0)
            failed = 1;

#pragma omp single
        {
            printf("team_size=%d  num_places=%d  proc_bind_enabled=%s\n",
                   team, omp_get_num_places(),
                   omp_get_proc_bind() == omp_proc_bind_false ? "no" : "yes");
        }
        /* Serialize printing only; each worker took its own snapshot above. */
        const char *labels[] = {"tid", "cpu", "place", "place_cpus", "allowed_cpus"};
        for (int row = 0; row < 5; ++row) {
            for (int turn = 0; turn < team; ++turn) {
#pragma omp barrier
                if (tid == turn) {
                    if (turn == 0)
                        printf("%-16s", labels[row]);
                    switch (row) {
                    case 0: printf("%d", tid); break;
                    case 1: printf("%d", cpu); break;
                    case 2: printf("%d", place); break;
                    case 3: print_place_cpus(place); break;
                    case 4:
                        if (affinity_ok)
                            print_affinity(&mask);
                        else
                            printf("ERROR(sched_getaffinity)");
                        break;
                    }
                    printf(turn == team - 1 ? "\n" : "\t");
                }
            }
        }
    }
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
