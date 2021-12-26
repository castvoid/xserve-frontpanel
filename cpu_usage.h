#pragma once

#import <stdlib.h>

int cpu_usage_setup(void);
void cpu_update_usage_loop(volatile float *usage_breakdowns, unsigned int num_breakdowns);

