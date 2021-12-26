#include "cpu_usage.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mach/mach_host.h>
#include <mach/vm_map.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#define CPU_UPDATE_INTERVAL ((useconds_t)(1e6 / 5))

static int get_num_packages(void) {
    int num_packages;
    size_t sizeof_num_packages = sizeof(num_packages);

    if (sysctlbyname("hw.packages", &num_packages, &sizeof_num_packages, NULL, 0) != 0) {
        printf("ERROR fetching hw.packages: %s", strerror(errno));
        return -1;
    }

    if (num_packages <= 0) {
        printf("WARNING: hw.packages gave invalid number %d, using 1", num_packages);
        num_packages = 1;
    }

    return num_packages;
}

int fetch_ticks_by_core(unsigned int **ticks_by_core_busy_ptr, unsigned int **ticks_by_core_total_ptr, unsigned int *num_cores_ptr) {
    // Fetch load info
    natural_t num_cores;
    processor_cpu_load_info_data_t* load_info;
    mach_msg_type_number_t load_info_len;

    kern_return_t err = host_processor_info(mach_host_self(),
                                            PROCESSOR_CPU_LOAD_INFO,
                                            &num_cores,
                                            (processor_info_array_t *)&load_info,
                                            &load_info_len);
    if (err != KERN_SUCCESS) {
        printf("ERROR fetching CPU load info: %d\n", err);
        return -1;
    }

    // Aggregate per-core ticks into busy & total counts
    unsigned int *ticks_by_core_busy = calloc(num_cores, sizeof(unsigned int));
    unsigned int *ticks_by_core_total = calloc(num_cores, sizeof(unsigned int));

    if (ticks_by_core_busy == NULL || ticks_by_core_total == NULL) {
        printf("Failed to alloc\n");
        return -1;
    }

    for (size_t core = 0; core < num_cores; core++) {
        unsigned int *ticks_by_state = load_info[core].cpu_ticks;
        for (int state = 0; state < CPU_STATE_MAX; state++) {
            ticks_by_core_total[core] += ticks_by_state[state];
            if (state != CPU_STATE_IDLE) {
                ticks_by_core_busy[core] += ticks_by_state[state];
            }

        }
    }

    // Deallocate memory from the host_processor_info call
    vm_deallocate(mach_task_self(), (vm_address_t)load_info, load_info_len);

    // Output aggregated data
    *ticks_by_core_busy_ptr = ticks_by_core_busy;
    *ticks_by_core_total_ptr = ticks_by_core_total;
    *num_cores_ptr = num_cores;

    return 0;
}

static int num_packages;
static unsigned int *prev_ticks_by_core_busy = NULL;
static unsigned int *prev_ticks_by_core_total = NULL;
static unsigned int prev_num_cores = 0;

int get_usage_by_core(float **usage_by_core_ptr, unsigned int *num_cores_ptr) {
    int ret = 0;

    unsigned int *ticks_by_core_busy;
    unsigned int *ticks_by_core_total;
    unsigned int num_cores;

    // Fetch current # of ticks by core
    int err = fetch_ticks_by_core(&ticks_by_core_busy, &ticks_by_core_total, &num_cores);
    if (err != 0) {
        printf("Failed to get CPU load data\n");
        return -1;
    }

    // If the # of cores has changed, we can't compare the number of ticks across runs
    if (num_cores != prev_num_cores) {
        ret = -2;
        printf("# of cores has changed!");
        goto always;
    }

    float *usage_by_core = calloc(num_cores, sizeof(float));
    if (usage_by_core == NULL) {
        printf("Failed to alloc\n");
        ret = -3;
        goto always;
    }

    // Actually calculate the usage by core
    for (size_t core = 0; core < num_cores; core++) {
        unsigned int delta_busy = ticks_by_core_busy[core] - prev_ticks_by_core_busy[core];
        unsigned int delta_total = ticks_by_core_total[core] - prev_ticks_by_core_total[core];

        float usage;
        if (delta_total > 0) {
            usage = ((float)delta_busy) / delta_total;
        } else{
            usage = -1.0f;
        }

        usage_by_core[core] = usage;
    }

    // Update the output pointers
    *usage_by_core_ptr = usage_by_core;
    *num_cores_ptr = num_cores;

    // Final work:
    always:
    // Free old prev values
    free(prev_ticks_by_core_busy);
    free(prev_ticks_by_core_total);
    // Update prev values
    prev_ticks_by_core_busy = ticks_by_core_busy;
    prev_ticks_by_core_total = ticks_by_core_total;
    prev_num_cores = num_cores;
    // Return our return code
    return ret;
}

int get_aggregated_usage(volatile float *usage_breakdowns, unsigned int num_breakdowns) {
    if (num_breakdowns < 1) {
        printf("ERROR: get_aggregated_usage was told to use 0 breakdowns");
        return -1;
    }

    float *usage_by_core;
    unsigned int num_cores;
    if (get_usage_by_core(&usage_by_core, &num_cores) != 0) {
        printf("Aggregate usage failed: failed to get usage by core\n");
        return -1;
    }


    int cores_per_package = num_cores / num_packages;
    int cores_per_breakdown;
    unsigned int breakdown_mul;

    if (num_cores % num_packages != 0) {
        // If # cores not divisible by # packages, there isn't really a fair way to do per-package breakdowns
        // Just show the same utilisation for each breakdown by having all cores on all breakdowns.
        cores_per_breakdown = -1;
        breakdown_mul = num_breakdowns;
    } else if (num_packages < num_breakdowns) {
        // If there are fewer packages then breakdowns
        if (num_breakdowns % num_packages == 0) {
            // and we can put each package on an equal number of breakdowns, do that
            cores_per_breakdown = cores_per_package; // 1 package per set of breakdowns
            breakdown_mul = num_breakdowns / num_packages; // 'double up' on the breakdowns
        } else {
            // otherwise, there is no good way to distribute so just show same util. on all breakdowns
            cores_per_breakdown = -1;
            breakdown_mul = num_breakdowns;
        }
    } else {
        // We can do it (relatively) fairly with round-robin!
        // So with 3 packages on 2 breakdowns, put packages 1+3 on breakdown 1, and package 2 on breakdown 2.
        cores_per_breakdown = cores_per_package; // 1 package per breakdown
        breakdown_mul = 1;
    }

    unsigned int num_cores_in_breakdown[num_breakdowns];
    float sum_usage_in_breakdown[num_breakdowns];
    memset(num_cores_in_breakdown, 0, num_breakdowns * sizeof(num_cores_in_breakdown[0]));
    memset(sum_usage_in_breakdown, 0, num_breakdowns * sizeof(sum_usage_in_breakdown[0]));

    // Calculate sum_usage_in_breakdown + num_cores_in_breakdown
    {
        unsigned int breakdown_index = 0;
        unsigned int cores_in_this_breakdown = 0;
        for (size_t core = 0; core < num_cores; core++) {
            float usage = usage_by_core[core];
            if (usage > -0.1f) {
                for (size_t i = 0; i < breakdown_mul; i++) {
                    size_t index = (breakdown_index + i) % num_breakdowns;
                    sum_usage_in_breakdown[index] += usage;
                    num_cores_in_breakdown[index]++;
                }
            }

            cores_in_this_breakdown++;
            if (cores_in_this_breakdown >= cores_per_breakdown) {
                cores_in_this_breakdown = 0;
                breakdown_index = (breakdown_index + breakdown_mul) % num_breakdowns;
            }
        }
    }

    // Calculate values for usage_breakdowns
    {
        for (size_t breakdown = 0; breakdown < num_breakdowns; breakdown++) {
            unsigned int cores_in_this_breakdown = num_cores_in_breakdown[breakdown];
            float usage = sum_usage_in_breakdown[breakdown] / cores_in_this_breakdown;
            if (cores_in_this_breakdown == 0) usage = 0;
            usage_breakdowns[breakdown] = usage;
        }
    }

    free(usage_by_core);

    return 0;
}

int cpu_usage_setup(void) {
    num_packages = get_num_packages();
    if (num_packages < 0) {
        printf("Invalid # packages, exiting...\n");
        exit(1);
    }

    int err = fetch_ticks_by_core(&prev_ticks_by_core_busy, &prev_ticks_by_core_total, &prev_num_cores);
    if (err != 0) {
        printf("Failed to get initial CPU load data\n");
        exit(1);
    }

    return 0;
}

void cpu_update_usage_loop(volatile float *usage_breakdowns, unsigned int num_breakdowns) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
    while (1) {
        get_aggregated_usage(usage_breakdowns, num_breakdowns);
        usleep(CPU_UPDATE_INTERVAL);
    }
#pragma clang diagnostic pop
}
