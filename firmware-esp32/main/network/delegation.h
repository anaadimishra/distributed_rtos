#ifndef DELEGATION_H
#define DELEGATION_H

#include "core/system_context.h"

/*
 * Called every manager cycle (~1 s).
 * - Expires REQUESTING state after DELEGATION_TIMEOUT_MS.
 * - Clears ACTIVE/HOSTING when the remote peer disappears from the peer table.
 */
void delegation_tick(system_context_t *ctx);

/*
 * Called when self is STRESS_HIGH and DELEGATION_IDLE.
 * Finds a LOW-stress peer, publishes a delegate_request, and transitions to
 * DELEGATION_REQUESTING.
 */
void delegation_try_offload(system_context_t *ctx);

/*
 * Called by mqtt.c when a delegate_request arrives on our topic.
 * Accepts (transitions to DELEGATION_HOSTING) or rejects based on CPU headroom
 * and current delegation state.
 */
void delegation_handle_request(system_context_t *ctx, const char *data, int data_len);

/*
 * Called by mqtt.c when a delegate_reply arrives on our topic.
 * Transitions REQUESTING → ACTIVE on accept, or REQUESTING → IDLE on reject.
 */
void delegation_handle_reply(system_context_t *ctx, const char *data, int data_len);

/*
 * Called by compute_task each cycle when delegation_state == DELEGATION_ACTIVE.
 * Serialises both input matrices and dispatches them to a STRESS_LOW peer via
 * cluster/{peer}/work_item. Uses round-robin across available peers.
 * matrix_a and matrix_b are passed as flat row-major arrays of MATRIX_SIZE*MATRIX_SIZE ints.
 */
typedef enum {
    DISPATCH_OK = 0,
    DISPATCH_BUSY,   /* channels at cap and/or no pending slot available */
    DISPATCH_ERROR,  /* malformed state or publish failure */
} delegation_dispatch_result_t;

delegation_dispatch_result_t delegation_dispatch_work_item(system_context_t *ctx,
                                                           int block_id,
                                                           const int *matrix_a,
                                                           const int *matrix_b);

/*
 * Called by mqtt.c when a work_item arrives on cluster/{node_id}/work_item.
 * Parses matrix_a and matrix_b, computes C = A × B, and publishes the result
 * to cluster/{from}/work_result. Only executes if delegation_state == HOSTING.
 */
void delegation_handle_work_item(system_context_t *ctx,
                                 const char *data, int data_len);

/*
 * Called by mqtt.c when a work_result arrives on cluster/{node_id}/work_result.
 * Matches the result to a pending_work entry and increments deleg_blocks_returned.
 */
void delegation_handle_work_result(system_context_t *ctx,
                                   const char *data, int data_len);

/* Query helpers — replace direct field reads in compute_task and manager_task. */

/* Number of channels currently in CHAN_ACTIVE state. */
int delegation_active_channel_count(const system_context_t *ctx);

/* Sum of blocks across all CHAN_ACTIVE channels. */
int delegation_total_delegated_blocks(const system_context_t *ctx);

/* Dominant role string: "ACTIVE" > "REQUESTING" > "HOSTING" > "IDLE". */
const char *delegation_node_role_str(const system_context_t *ctx);

/* Node ID of first non-IDLE channel peer, or "" if all IDLE. */
const char *delegation_primary_peer(const system_context_t *ctx);

/*
 * Called by work_recv_task when a binary result frame arrives over TCP.
 * channel_idx identifies which ACTIVE channel owns this result.
 * result is the 900-int32 output matrix (not stored — count only).
 */
void delegation_handle_work_result_tcp(system_context_t *ctx,
                                       uint32_t cycle_id, uint8_t block_id,
                                       int channel_idx,
                                       const int32_t *result);

#endif /* DELEGATION_H */
