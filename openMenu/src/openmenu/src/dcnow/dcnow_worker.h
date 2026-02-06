#ifndef DCNOW_WORKER_H
#define DCNOW_WORKER_H

#ifdef DCNOW_ASYNC

#include <stdbool.h>
#include <stdint.h>
#include "dcnow_api.h"
#include "dcnow_net_init.h"

/**
 * Worker thread states for non-blocking network operations
 */
typedef enum {
    DCNOW_WORKER_IDLE,          /* No operation in progress */
    DCNOW_WORKER_CONNECTING,    /* PPP/modem connection in progress */
    DCNOW_WORKER_FETCHING,      /* HTTP data fetch in progress */
    DCNOW_WORKER_DONE,          /* Operation completed successfully */
    DCNOW_WORKER_ERROR          /* Operation failed */
} dcnow_worker_state_t;

/**
 * Worker thread context - shared between main and worker threads.
 * Volatile fields are written by the worker and read by the main thread.
 */
typedef struct {
    volatile dcnow_worker_state_t state;
    volatile char status_message[128];  /* Current status for UI display */
    dcnow_data_t result_data;           /* Fetch result (only valid when state == DONE after fetch) */
    volatile int error_code;            /* Error code if state == ERROR */
    volatile bool cancel_requested;     /* Set by main thread to request cancellation */
} dcnow_worker_context_t;

/**
 * Initialize the worker thread system.
 * Must be called once before using any worker functions.
 */
void dcnow_worker_init(void);

/**
 * Shutdown the worker thread system.
 * Cancels any pending operation and joins the worker thread.
 */
void dcnow_worker_shutdown(void);

/**
 * Start async network connection in worker thread.
 *
 * @param ctx - Context structure to receive status updates and results
 * @param method - Connection method (serial or modem)
 * @return 0 on success (worker started), negative on error:
 *         -1: Worker already busy
 *         -2: Thread creation failed
 */
int dcnow_worker_start_connect(dcnow_worker_context_t* ctx, dcnow_connection_method_t method);

/**
 * Start async data fetch in worker thread.
 * Network must already be connected.
 *
 * @param ctx - Context structure to receive status updates and results
 * @param timeout_ms - HTTP timeout in milliseconds
 * @return 0 on success (worker started), negative on error:
 *         -1: Worker already busy
 *         -2: Thread creation failed
 */
int dcnow_worker_start_fetch(dcnow_worker_context_t* ctx, uint32_t timeout_ms);

/**
 * Poll worker thread status (call from main loop each frame).
 * When DONE or ERROR is returned, the worker thread is joined and
 * the worker slot is freed for reuse.
 *
 * @param ctx - Context structure
 * @return Current worker state
 */
dcnow_worker_state_t dcnow_worker_poll(dcnow_worker_context_t* ctx);

/**
 * Get current status message for UI display.
 *
 * @param ctx - Context structure
 * @return Pointer to status message string
 */
const char* dcnow_worker_get_status(dcnow_worker_context_t* ctx);

/**
 * Request cancellation of current operation.
 * Note: Some blocking operations (like ppp_connect) cannot be interrupted.
 *
 * @param ctx - Context structure
 */
void dcnow_worker_cancel(dcnow_worker_context_t* ctx);

/**
 * Check if worker is currently busy.
 *
 * @return true if a worker operation is in progress
 */
bool dcnow_worker_is_busy(void);

#endif /* DCNOW_ASYNC */
#endif /* DCNOW_WORKER_H */
