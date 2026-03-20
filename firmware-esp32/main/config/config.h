
#ifndef CONFIG_H
#define CONFIG_H

// Firmware identity tag (displayed in telemetry).
#define FIRMWARE_VERSION "fw-0.1.2-debug"
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
#define MQTT_TELEMETRY_TOPIC "cluster/{node_id}/telemetry"
#define MQTT_CONTROL_TOPIC "cluster/{node_id}/control"

// Task periods (ms).
#define SENSOR_PERIOD_MS 100
#define CONTROL_PERIOD_MS 100
#define COMPUTE_PERIOD_MS 100
#define MANAGER_PERIOD_MS 1000

// Queue sizing.
#define SENSOR_QUEUE_LENGTH 16

// Stack sizes.
#define SENSOR_TASK_STACK_SIZE 2048
#define CONTROL_TASK_STACK_SIZE 2048
#define COMPUTE_TASK_STACK_SIZE 2048
#define MANAGER_TASK_STACK_SIZE 3072

// Task priorities.
#define SENSOR_TASK_PRIORITY 5
#define CONTROL_TASK_PRIORITY 5
#define COMPUTE_TASK_PRIORITY 2
#define MANAGER_TASK_PRIORITY 3

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

#endif
