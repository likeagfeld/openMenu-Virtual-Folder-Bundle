#ifdef DCNOW_ASYNC

#include "dcnow_worker.h"
#include "dcnow_net_init.h"
#include "dcnow_api.h"
#include <stdio.h>
#include <string.h>

#ifdef _arch_dreamcast
#include <kos/thread.h>
#include <kos/mutex.h>
#include <arch/timer.h>
#endif

#ifdef _arch_dreamcast
/* Worker thread handle - kept for thd_join() */
static kthread_t* worker_thread = NULL;

/* Mutex for thread-safe access to shared state */
static mutex_t worker_mutex = MUTEX_INITIALIZER;
#endif

/* Active context pointer (set when worker is running) */
static volatile dcnow_worker_context_t* active_ctx = NULL;

/* Parameters passed to worker thread */
static dcnow_connection_method_t worker_conn_method;
static uint32_t worker_fetch_timeout_ms = 5000;

/* Forward declarations */
static void* connect_worker_func(void* arg);
static void* fetch_worker_func(void* arg);
static void worker_status_callback(const char* message);

void dcnow_worker_init(void) {
#ifdef _arch_dreamcast
    mutex_init(&worker_mutex, MUTEX_TYPE_NORMAL);
    worker_thread = NULL;
#endif
    active_ctx = NULL;
    DCNOW_DPRINTF("DC Now Worker: Initialized\n");
}

void dcnow_worker_shutdown(void) {
#ifdef _arch_dreamcast
    mutex_lock(&worker_mutex);
    if (active_ctx) {
        ((dcnow_worker_context_t*)active_ctx)->cancel_requested = true;
    }
    mutex_unlock(&worker_mutex);

    /* Join worker thread if it's running */
    if (worker_thread) {
        DCNOW_DPRINTF("DC Now Worker: Joining worker thread for shutdown...\n");
        thd_join(worker_thread, NULL);
        worker_thread = NULL;
    }

    mutex_destroy(&worker_mutex);
#endif
    active_ctx = NULL;
    DCNOW_DPRINTF("DC Now Worker: Shutdown complete\n");
}

/**
 * Status callback that updates shared context from worker thread.
 * Only writes to the status_message buffer - NO rendering, NO sleeps.
 */
static void worker_status_callback(const char* message) {
#ifdef _arch_dreamcast
    mutex_lock(&worker_mutex);
    if (active_ctx) {
        strncpy((char*)active_ctx->status_message, message, 127);
        ((char*)active_ctx->status_message)[127] = '\0';
    }
    mutex_unlock(&worker_mutex);
#endif
}

/**
 * Worker thread entry point for PPP/modem connection.
 */
static void* connect_worker_func(void* arg) {
#ifdef _arch_dreamcast
    dcnow_worker_context_t* ctx = (dcnow_worker_context_t*)arg;

    DCNOW_DPRINTF("DC Now Worker: Connect thread started (method=%d)\n", worker_conn_method);

    mutex_lock(&worker_mutex);
    ctx->state = DCNOW_WORKER_CONNECTING;
    strncpy((char*)ctx->status_message, "Starting connection...", 127);
    active_ctx = ctx;
    mutex_unlock(&worker_mutex);

    /* Disable status sleeps - main thread renders at 60fps */
    dcnow_set_status_sleep_enabled(false);

    /* Set up status callback so we get progress updates */
    dcnow_set_status_callback(worker_status_callback);

    /* This is the blocking call - runs in worker thread */
    int result = dcnow_net_init_with_method(worker_conn_method);

    /* Clear status callback and restore sleep behavior */
    dcnow_set_status_callback(NULL);
    dcnow_set_status_sleep_enabled(true);

    mutex_lock(&worker_mutex);
    if (result < 0) {
        ctx->state = DCNOW_WORKER_ERROR;
        ctx->error_code = result;
        snprintf((char*)ctx->status_message, 127, "Connection failed (error %d)", result);
        DCNOW_DPRINTF("DC Now Worker: Connection failed with error %d\n", result);
    } else {
        ctx->state = DCNOW_WORKER_DONE;
        ctx->error_code = 0;
        strncpy((char*)ctx->status_message, "Connected!", 127);
        DCNOW_DPRINTF("DC Now Worker: Connection successful\n");
    }
    active_ctx = NULL;
    mutex_unlock(&worker_mutex);
#endif
    return NULL;
}

/**
 * Worker thread entry point for HTTP data fetch.
 */
static void* fetch_worker_func(void* arg) {
#ifdef _arch_dreamcast
    dcnow_worker_context_t* ctx = (dcnow_worker_context_t*)arg;

    DCNOW_DPRINTF("DC Now Worker: Fetch thread started\n");

    mutex_lock(&worker_mutex);
    ctx->state = DCNOW_WORKER_FETCHING;
    strncpy((char*)ctx->status_message, "Fetching data...", 127);
    active_ctx = ctx;
    mutex_unlock(&worker_mutex);

    /* This is the blocking call - runs in worker thread */
    int result = dcnow_fetch_data(&ctx->result_data, worker_fetch_timeout_ms);

    mutex_lock(&worker_mutex);
    if (result < 0) {
        ctx->state = DCNOW_WORKER_ERROR;
        ctx->error_code = result;
        if (ctx->result_data.error_message[0] != '\0') {
            strncpy((char*)ctx->status_message, ctx->result_data.error_message, 127);
        } else {
            snprintf((char*)ctx->status_message, 127, "Fetch failed (error %d)", result);
        }
        DCNOW_DPRINTF("DC Now Worker: Fetch failed with error %d\n", result);
    } else {
        ctx->state = DCNOW_WORKER_DONE;
        ctx->error_code = 0;
        snprintf((char*)ctx->status_message, 127, "Loaded %d games", ctx->result_data.game_count);
        DCNOW_DPRINTF("DC Now Worker: Fetch successful, %d games\n", ctx->result_data.game_count);
    }
    active_ctx = NULL;
    mutex_unlock(&worker_mutex);
#endif
    return NULL;
}

int dcnow_worker_start_connect(dcnow_worker_context_t* ctx, dcnow_connection_method_t method) {
#ifdef _arch_dreamcast
    mutex_lock(&worker_mutex);

    /* Check if already busy */
    if (worker_thread != NULL) {
        mutex_unlock(&worker_mutex);
        DCNOW_DPRINTF("DC Now Worker: Cannot start connect - worker busy\n");
        return -1;
    }

    /* Store connection method for worker thread */
    worker_conn_method = method;

    /* Initialize context */
    memset(ctx, 0, sizeof(dcnow_worker_context_t));
    ctx->state = DCNOW_WORKER_CONNECTING;
    ctx->cancel_requested = false;
    strncpy((char*)ctx->status_message, "Initializing...", 127);

    mutex_unlock(&worker_mutex);

    /* Create worker thread */
    worker_thread = thd_create(0, connect_worker_func, ctx);
    if (!worker_thread) {
        DCNOW_DPRINTF("DC Now Worker: Failed to create connect thread\n");
        ctx->state = DCNOW_WORKER_ERROR;
        ctx->error_code = -2;
        return -2;
    }

    DCNOW_DPRINTF("DC Now Worker: Connect thread created\n");
    return 0;
#else
    ctx->state = DCNOW_WORKER_ERROR;
    ctx->error_code = -100;
    return -100;
#endif
}

int dcnow_worker_start_fetch(dcnow_worker_context_t* ctx, uint32_t timeout_ms) {
#ifdef _arch_dreamcast
    mutex_lock(&worker_mutex);

    /* Check if already busy */
    if (worker_thread != NULL) {
        mutex_unlock(&worker_mutex);
        DCNOW_DPRINTF("DC Now Worker: Cannot start fetch - worker busy\n");
        return -1;
    }

    /* Store timeout for worker thread */
    worker_fetch_timeout_ms = timeout_ms;

    /* Initialize context */
    ctx->state = DCNOW_WORKER_FETCHING;
    ctx->error_code = 0;
    ctx->cancel_requested = false;
    strncpy((char*)ctx->status_message, "Starting fetch...", 127);

    mutex_unlock(&worker_mutex);

    /* Create worker thread */
    worker_thread = thd_create(0, fetch_worker_func, ctx);
    if (!worker_thread) {
        DCNOW_DPRINTF("DC Now Worker: Failed to create fetch thread\n");
        ctx->state = DCNOW_WORKER_ERROR;
        ctx->error_code = -2;
        return -2;
    }

    DCNOW_DPRINTF("DC Now Worker: Fetch thread created\n");
    return 0;
#else
    ctx->state = DCNOW_WORKER_ERROR;
    ctx->error_code = -100;
    return -100;
#endif
}

dcnow_worker_state_t dcnow_worker_poll(dcnow_worker_context_t* ctx) {
    dcnow_worker_state_t state;

#ifdef _arch_dreamcast
    mutex_lock(&worker_mutex);
    state = ctx->state;

    /* If operation completed and worker has cleared active_ctx, join the thread */
    if ((state == DCNOW_WORKER_DONE || state == DCNOW_WORKER_ERROR) &&
        active_ctx == NULL && worker_thread != NULL) {
        kthread_t* thread_to_join = worker_thread;
        worker_thread = NULL;
        mutex_unlock(&worker_mutex);

        /* Join outside mutex to avoid potential deadlock */
        DCNOW_DPRINTF("DC Now Worker: Joining completed thread\n");
        thd_join(thread_to_join, NULL);
        DCNOW_DPRINTF("DC Now Worker: Thread joined successfully\n");
        return state;
    }

    mutex_unlock(&worker_mutex);
#else
    state = ctx->state;
#endif

    return state;
}

const char* dcnow_worker_get_status(dcnow_worker_context_t* ctx) {
    return (const char*)ctx->status_message;
}

void dcnow_worker_cancel(dcnow_worker_context_t* ctx) {
#ifdef _arch_dreamcast
    mutex_lock(&worker_mutex);
    ctx->cancel_requested = true;
    DCNOW_DPRINTF("DC Now Worker: Cancellation requested\n");
    mutex_unlock(&worker_mutex);
#else
    ctx->cancel_requested = true;
#endif
}

bool dcnow_worker_is_busy(void) {
#ifdef _arch_dreamcast
    bool busy;
    mutex_lock(&worker_mutex);
    busy = (worker_thread != NULL);
    mutex_unlock(&worker_mutex);
    return busy;
#else
    return false;
#endif
}

#endif /* DCNOW_ASYNC */
