
#ifndef CONFIG_H
#define CONFIG_H

// Firmware identity tag (displayed in telemetry).
#define FIRMWARE_VERSION "fw-0.3.0-deleg"
// Enable lightweight debug logging.
#define DEBUG_LOGS 1
// Profiling controls: disable to measure baseline compute behavior.
#ifndef ENABLE_METRICS
#define ENABLE_METRICS 1
#endif
#define ENABLE_MANAGER_TASK 1

// MQTT broker connection.
#define MQTT_BROKER_HOST "192.168.0.220"
#define MQTT_BROKER_PORT 1883
// #define MQTT_CLIENT_ID "node-XXXXXX"

// Topic templates. Use "{node_id}" as placeholder for runtime replacement.
#define MQTT_TELEMETRY_TOPIC    "cluster/{node_id}/telemetry"
#define MQTT_CONTROL_TOPIC      "cluster/{node_id}/control"
#define MQTT_WORK_ITEM_TOPIC    "cluster/{node_id}/work_item"
#define MQTT_WORK_RESULT_TOPIC  "cluster/{node_id}/work_result"

// Task periods (ms).
// Distinct periods enable Rate Monotonic priority assignment (shorter period => higher priority).
#define SENSOR_PERIOD_MS 20
#define CONTROL_PERIOD_MS 50
#define COMPUTE_PERIOD_MS 100
#define MANAGER_PERIOD_MS 1000

// Queue sizing.
#define SENSOR_QUEUE_LENGTH 16

// Stack sizes.
#define SENSOR_TASK_STACK_SIZE 2048
#define CONTROL_TASK_STACK_SIZE 2048
// Compute also handles high-rate delegation dispatch/hosting JSON paths.
#define COMPUTE_TASK_STACK_SIZE 8192
// Manager also runs delegation selection and telemetry formatting.
#define MANAGER_TASK_STACK_SIZE 6144

// Task priorities — Rate Monotonic ordering with one intentional deviation.
// FreeRTOS: higher number = higher priority.
// sensor (20ms) > control (50ms) follow RM strictly.
// manager (1000ms) is elevated above compute (100ms) so telemetry remains
// deliverable under compute saturation — deliberate observability design choice.
#define SENSOR_TASK_PRIORITY 5
#define CONTROL_TASK_PRIORITY 4
#define MANAGER_TASK_PRIORITY 3
#define COMPUTE_TASK_PRIORITY 2

// Default load scaling factor; compute load scales relative to this value.
#define DEFAULT_LOAD_FACTOR 1000
// Boot-time load factor (start low; can be raised via control).
#define DEFAULT_LOAD_START 100
// Clamp bounds for SET_LOAD updates.
#define LOAD_FACTOR_MIN 100
#define LOAD_FACTOR_MAX 1000
// Number of samples to establish idle baseline for CPU usage.
#define CPU_BASELINE_SAMPLES 20

// Compute kernel parameters.
// Max compute blocks used for scaling.
#define PROCESSING_BLOCKS 20
// Matrix size for the compute kernel; smaller reduces CPU load.
#define MATRIX_SIZE 30
// Default number of active blocks (base compute load).
#define ACTIVE_BLOCKS 20

// Windowed stats: number of cycles per reporting window.
#define PROCESSING_WINDOW_CYCLES 20   // 20 cycles = 2s (100ms period)

// Distributed adaptation thresholds and limits.
#define MAX_PEERS 8
#define MAX_PENDING_WORK 20
#define MAX_DELEGATION_CHANNELS 4
#define DELEGATION_MAX_INFLIGHT_PER_CHANNEL 4
#define DELEGATION_PENDING_TIMEOUT_MS 2000
#define STRESS_EXEC_THRESHOLD_TICKS 8
#define STRESS_CPU_THRESHOLD_PCT 85
#define PEER_TIMEOUT_MS 5000
#define ADAPT_LOAD_STEP 100
#define ADAPT_LOW_WINDOWS_TO_INCREASE 9999  /* disabled during delegation validation */
#define ADAPT_DECREASE_ENABLED 0            /* disabled during delegation validation */

#endif
