// Periodic compute workload with timing and deadline tracking.
#include "tasks/compute_task.h"
#include "config/config.h"
#include "core/metrics.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>

// Static matrices for a deterministic compute kernel.
static int matrix_a[MATRIX_SIZE][MATRIX_SIZE];
static int matrix_b[MATRIX_SIZE][MATRIX_SIZE];
static int matrix_c[MATRIX_SIZE][MATRIX_SIZE];
static volatile int kernel_sink = 0;
static bool kernel_initialized = false;

// Initialize matrices once to keep the kernel deterministic.
static void init_matrices(void)
{
    if (kernel_initialized) {
        return;
    }

    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            matrix_a[i][j] = (i + 1) * (j + 3);
            matrix_b[i][j] = (i + 2) * (j + 1);
            matrix_c[i][j] = 0;
        }
    }

    kernel_initialized = true;
}

// O(n^3) matrix multiply to create a heavy, repeatable workload.
static void compute_kernel(void)
{
    int local_sink = 0;

    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            int sum = 0;
            for (int k = 0; k < MATRIX_SIZE; k++) {
                sum += matrix_a[i][k] * matrix_b[k][j];
            }
            matrix_c[i][j] = sum;
            local_sink += sum;
        }
    }

    kernel_sink += local_sink;
}

void compute_task(void *param)
{
    system_context_t *ctx = (system_context_t *)param;
    TickType_t last_wake = xTaskGetTickCount();

    init_matrices();

    while (1) {
        TickType_t start = xTaskGetTickCount();

        // Scale compute load: load_factor maps across PROCESSING_BLOCKS, active_blocks caps it.
        uint32_t blocks = 0;
        if (ctx != NULL && DEFAULT_LOAD_FACTOR > 0) {
            if (!ctx->cpu_baseline_ready) {
                blocks = 0;
            } else {
            uint64_t scaled = ((uint64_t)ctx->load_factor * (uint64_t)PROCESSING_BLOCKS) /
                              (uint64_t)DEFAULT_LOAD_FACTOR;
            if (scaled > PROCESSING_BLOCKS) {
                scaled = PROCESSING_BLOCKS;
            }
            blocks = (uint32_t)scaled;

            uint32_t max_blocks = ctx->active_blocks;
            if (max_blocks > PROCESSING_BLOCKS) {
                max_blocks = PROCESSING_BLOCKS;
            }
            if (blocks > max_blocks) {
                blocks = max_blocks;
            }
            }
        }

        if (ctx != NULL) {
            ctx->effective_blocks = blocks;
        }

        for (uint32_t i = 0; i < blocks; i++) {
            compute_kernel();
        }

        TickType_t end = xTaskGetTickCount();
        TickType_t exec_ticks = end - start;

        if (ctx != NULL) {
            // Store last execution time.
            ctx->processing_exec_ticks = exec_ticks;

            // Update max execution time.
            if (exec_ticks > ctx->processing_exec_max) {
                ctx->processing_exec_max = exec_ticks;
            }

            // Update sum and count for average calculation.
            ctx->processing_exec_sum += exec_ticks;
            ctx->processing_exec_count++;

            // Deadline check based on the scheduled wake time.
            TickType_t expected_deadline = last_wake + pdMS_TO_TICKS(COMPUTE_PERIOD_MS);
            if (end > expected_deadline) {
                ctx->deadline_miss_processing++;
            }

            // Windowed stats: publish-ready snapshots for the manager task.
            if (ctx->processing_exec_count >= PROCESSING_WINDOW_CYCLES) {
                ctx->processing_window_avg =
                    ctx->processing_exec_sum / ctx->processing_exec_count;
                ctx->processing_window_max =
                    ctx->processing_exec_max;
                ctx->processing_window_miss =
                    ctx->deadline_miss_processing;
                ctx->processing_window_ready = 1;

                // Reset window accumulators.
                ctx->processing_exec_sum = 0;
                ctx->processing_exec_count = 0;
                ctx->processing_exec_max = 0;
                ctx->deadline_miss_processing = 0;
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(COMPUTE_PERIOD_MS));
    }
}
