#include "dcnow_net_init.h"
#include "dcnow_vmu.h"
#include <stdio.h>
#include <string.h>

#ifdef _arch_dreamcast
#include <kos.h>
#include <kos/net.h>
#include <ppp/ppp.h>
#include <dc/modem/modem.h>
#include <dc/scif.h>
#include <kos/dbgio.h>
#include <arch/timer.h>
#include <dc/pvr.h>
#endif

/* Serial coders cable connection state */
static int serial_connection_active = 0;

#ifdef _arch_dreamcast
/* DreamPi can take ~23s to fully settle PPP hangup/rules on disconnect.
 * Track the last PPP disconnect and auto-wait before reconnect attempts so
 * users don't need to manually count timing between sessions. */
static uint64_t last_ppp_disconnect_ms = 0;
static const uint64_t PPP_RECONNECT_COOLDOWN_MS = 23000;
#endif

/* When non-zero, SCIF is being used (or was used) for serial data.
 * ALL printf() calls must be suppressed because KOS routes printf through
 * SCIF, which would inject debug text into the serial data stream and
 * corrupt communication with DreamPi.
 *
 * This flag is set before SCIF baud rate changes on first serial connection
 * and intentionally NEVER cleared back to 0 afterward. This prevents printf
 * output from leaking through SCIF during modem sessions (which confuses
 * DreamPi's serial handler). The DC Now preamble needed for serial reconnect
 * is sent explicitly via scif_write_string() in try_serial_coders_cable(). */
static int scif_in_use_for_data = 0;

#ifdef _arch_dreamcast
/* Helper to write a string to SCIF (serial port) */
static void scif_write_string(const char* str) {
    while (*str) {
        scif_write(*str++);
    }
}
#endif

int dcnow_is_serial_scif_active(void) {
    return scif_in_use_for_data;
}

/**
 * Log a message to the RAM disk log file only (no printf).
 * Safe to call at any time, even when SCIF is in use for serial data.
 */
static void serial_log(const char* msg) {
    FILE* logfile = fopen("/ram/DCNOW_LOG.TXT", "a");
    if (logfile) {
        fprintf(logfile, "%s\n", msg);
        fclose(logfile);
    }
}

/* Status callback for visual feedback */
static dcnow_status_callback_t status_callback = NULL;

/* When false, update_status() skips the 500ms sleep after callback.
 * The async worker thread disables this so it doesn't block between steps. */
static bool status_sleep_enabled = true;

void dcnow_set_status_callback(dcnow_status_callback_t callback) {
    status_callback = callback;
}

void dcnow_set_status_sleep_enabled(bool enabled) {
    status_sleep_enabled = enabled;
}

#ifdef _arch_dreamcast
/* Force PPP to fully release SCIF ownership/callbacks.
 * Some reconnect failures happen when PPP shutdown appears complete but SCIF RX
 * callbacks still consume incoming bytes before scif_read() polling sees them.
 * A quick init->shutdown cycle mirrors the deeper reset effect seen after a
 * successful modem connect/disconnect path. */
static void ppp_force_release_scif(void) {
    ppp_shutdown();
    timer_spin_sleep(120);

    if (ppp_init() >= 0) {
        timer_spin_sleep(40);
        ppp_shutdown();
    }

    timer_spin_sleep(240);
}
#endif

static void update_status(const char* message) {
    /* Only printf when SCIF is NOT in use for serial data.
     * When SCIF is active, printf would send debug text through the serial
     * port and corrupt AT commands / PPP data going to DreamPi. */
    if (!scif_in_use_for_data) {
        printf("DC Now STATUS: %s\n", message);
    }

    /* Always log to RAM disk - this is safe regardless of SCIF state */
    FILE* logfile = fopen("/ram/DCNOW_LOG.TXT", "a");
    if (logfile) {
        fprintf(logfile, "STATUS: %s\n", message);
        fclose(logfile);
    } else if (!scif_in_use_for_data) {
        printf("DC Now: WARNING - Failed to open log file\n");
    }

    if (status_callback) {
        /* Call callback which will draw the message (sync) or update buffer (async) */
        status_callback(message);
        /* Give user time to see the message (skipped in async worker mode) */
        if (status_sleep_enabled) {
            timer_spin_sleep(700);  /* 500ms delay so messages are visible */
        }
    } else {
        if (!scif_in_use_for_data) {
            printf("DC Now: WARNING - No status callback set!\n");
        }
        logfile = fopen("/ram/DCNOW_LOG.TXT", "a");
        if (logfile) {
            fprintf(logfile, "ERROR: No status callback!\n");
            fclose(logfile);
        }
    }
}

/**
 * Try to connect via serial coders cable to DreamPi 2
 * Uses SCIF at 115200 baud with AT command handshake
 *
 * Protocol:
 * 1. Send "AT\r\n" and wait for "OK\r\n" (DreamPi detection)
 * 2. Send "ATDT\r\n" (dial command)
 * 3. Wait for "CONNECT 115200\r\n"
 * 4. Sleep 5 seconds for DreamPi to start pppd
 * 5. Initialize PPP over serial
 *
 * @return 0 on success, negative on error
 */
static int try_serial_coders_cable(void) {
#ifdef _arch_dreamcast
    char buf[64];
    int bytes_read;
    uint64 start_time;
    const uint64 TIMEOUT_MS = 4000;  /* 4 second timeout for responses */

    update_status("Checking for serial cable...");

    /* CRITICAL: Mark SCIF as in use for serial data BEFORE changing baud rate.
     * From this point on, ALL printf() calls must be suppressed because KOS
     * routes printf through SCIF. Any debug text sent through SCIF at 115200
     * baud will be received by DreamPi as corrupted AT commands or PPP data.
     * This was the root cause of:
     * - "AT" being followed by "DC Now STATUS: Waiting for OK response..."
     * - Baud rate appearing to "switch" (actually debug text at wrong baud)
     * - FTDI cables failing (more timing-sensitive to mixed data)
     * - Idle disconnects (printf during PPP corrupts data stream) */
    scif_in_use_for_data = 1;

    /* Detach KOS debug I/O from SCIF so that printf() calls inside KOS
     * libraries (PPP negotiation, PVR errors, modem driver, etc.) can
     * never reach the serial port.  DCNOW_DPRINTF and update_status()
     * already guard dcnow's own printf calls, but KOS-internal code
     * bypasses those guards.  dbgio_disable() makes ALL printf output
     * a no-op, while direct scif_write() for the AT/PPP protocol and
     * fprintf()-to-file logging remain unaffected. */
    dbgio_disable();

    /* Run the serial AT handshake in two passes.
     * Some reconnect failures leave stale PPP/SCIF state around long enough
     * that DreamPi never answers the first pass. A second full SCIF re-init
     * pass (with extra settle time) recovers without requiring a console reboot. */
    const int HANDSHAKE_PASSES = 2;
    const int AT_MAX_RETRIES = 5;
    int got_ok = 0;
    int pass;
    int at_attempt;

    for (pass = 0; pass < HANDSHAKE_PASSES && !got_ok; pass++) {
        if (pass > 0) {
            update_status("No OK - resetting serial and retrying...");
            serial_log("AT handshake pass 1 failed - resetting SCIF for pass 2");
        }

        serial_log(pass == 0 ? "AT handshake pass 1 start" : "AT handshake pass 2 start");

        /* Ensure PPP fully releases SCIF before we try to use it.
         * ppp_scif_init() takes ownership of SCIF's receive path. On reconnect,
         * the previous ppp_shutdown() in dcnow_net_disconnect() terminates PPP
         * but may not fully deregister its SCIF I/O hooks. DreamPi then sends
         * "OK" but PPP's lingering receive handler consumes the bytes before
         * our scif_read() polling can see them. Calling ppp_shutdown() again
         * is safe (no-op if already shut down) and ensures SCIF is released.
         * A full DC reboot works because it power-cycles SCIF hardware and
         * clears all PPP state from memory — this replicates that cleanup. */
        ppp_force_release_scif();
        timer_spin_sleep(pass == 0 ? 140 : 500);

        /* Initialize SCIF hardware - required before setting parameters */
        scif_init();
        scif_set_irq_usage(0);  /* Disable IRQ mode for polling */
        scif_set_parameters(115200, 1);  /* 115200 baud, FIFO enabled */

        /* Flush any pending data - extra thorough drain */
        timer_spin_sleep(200);
        while (scif_read() != -1) { /* drain buffer */ }
        timer_spin_sleep(100);
        while (scif_read() != -1) { /* drain again after settling */ }

        /* Small delay after flush */
        timer_spin_sleep(100);

        /* Send an explicit preamble at 115200 before AT detection.
         * DreamPi's AT handler may ignore standalone AT probes until it sees
         * normal serial text traffic first. We cannot rely on printf() for this
         * because printf baud/state can vary between reconnects; write directly
         * to SCIF after configuring it to 115200 so the preamble is always clean. */
        scif_write_string("DC Now: serial link check\r\n");
        scif_flush();
        timer_spin_sleep(100);

        /* Explicit line wakeup: some adapters/hosts only answer AT after
         * receiving an extra CR/LF boundary post-reset. */
        scif_write_string("\r\n");
        scif_flush();
        timer_spin_sleep(120);

        /* Normalize modem command profile before probing for OK.
         * After a previous PPP session DreamPi can stay in a state where
         * result codes are suppressed, so AT commands are accepted but no
         * visible "OK" is returned. Force verbose result codes on (Q0/V1)
         * and enable echo once so subsequent AT probing is observable. */
        scif_write_string("ATE1Q0V1\r\n");
        scif_flush();
        timer_spin_sleep(180);
        while (scif_read() != -1) { /* drain profile response noise */ }
        timer_spin_sleep(60);

        /* Send AT command with retry logic.
         * USB-to-serial adapters (especially FTDI) can have line noise or need
         * time to stabilize after SCIF initialization. Retrying the AT command
         * across multiple retries significantly improves reliability across cable types. */
        for (at_attempt = 0; at_attempt < AT_MAX_RETRIES; at_attempt++) {
            if (at_attempt > 0) {
                /* Between retries: drain any garbage and wait for line to settle */
                char retry_msg[48];
                snprintf(retry_msg, sizeof(retry_msg), "AT retry %d of %d...", at_attempt + 1, AT_MAX_RETRIES);
                update_status(retry_msg);
                serial_log(retry_msg);
                timer_spin_sleep(300);
                while (scif_read() != -1) { /* drain buffer */ }
                timer_spin_sleep(180);
            } else {
                update_status("Sending AT command...");
            }

            /* Keep command simple and deterministic per DreamPi protocol. */
            scif_write_string("AT\r\n");
            scif_flush();  /* Ensure data is transmitted */

            /* Wait for DreamPi to process before reading response */
            timer_spin_sleep(700);

            /* Wait for OK response with timeout */
            memset(buf, 0, sizeof(buf));
            bytes_read = 0;
            start_time = timer_ms_gettime64();

            while ((timer_ms_gettime64() - start_time) < TIMEOUT_MS) {
                int c = scif_read();
                if (c != -1 && bytes_read < (int)sizeof(buf) - 1) {
                    buf[bytes_read++] = (char)c;
                    buf[bytes_read] = '\0';

                    /* Check for OK response */
                    if (strstr(buf, "OK") != NULL) {
                        if (pass > 0) {
                            serial_log("Serial - Got OK response from DreamPi on pass 2");
                        } else {
                            serial_log("Serial - Got OK response from DreamPi");
                        }
                        got_ok = 1;
                        break;
                    }
                }
                timer_spin_sleep(10);  /* Small delay to avoid busy loop */
            }

            if (got_ok) break;

            /* Log what we got on this attempt */
            char log_msg[96];
            snprintf(log_msg, sizeof(log_msg), "AT pass %d attempt %d/%d: no OK in %llums - got %d bytes: %.20s",
                     pass + 1, at_attempt + 1, AT_MAX_RETRIES, (unsigned long long)TIMEOUT_MS, bytes_read, buf);
            serial_log(log_msg);
        }
    }

    if (!got_ok) {
        /* DreamPi serial protocol requires an explicit OK response before dial.
         * If no OK is observed after both handshake passes, fail fast so the
         * caller can report a clean serial-detection failure and retry later. */
        serial_log("No OK after AT handshake passes - aborting dial");

        /* Show status on screen (via callback, printf suppressed) */
        char status_msg[96];
        snprintf(status_msg, sizeof(status_msg), "No OK after %d tries x %d passes - got: %.20s",
                 AT_MAX_RETRIES, HANDSHAKE_PASSES, buf);
        update_status(status_msg);
        timer_spin_sleep(2000);
        return -1;  /* No DreamPi detected on serial */
    }

    serial_log("AT handshake complete, proceeding to dial");
    update_status("DreamPi found! Dialing...");

    /* Send dial command */
    timer_spin_sleep(100);  /* Small delay before dial */

    /* Flush buffer before dial */
    while (scif_read() != -1) { /* drain buffer */ }

    scif_write_string("ATDT\r\n");
    scif_flush();  /* Ensure data is transmitted */

    /* Small delay for DreamPi to process */
    timer_spin_sleep(100);

    /* Wait for CONNECT response */
    memset(buf, 0, sizeof(buf));
    bytes_read = 0;
    start_time = timer_ms_gettime64();
    const uint64 CONNECT_TIMEOUT_MS = 5000;  /* 5 second timeout for CONNECT */

    while ((timer_ms_gettime64() - start_time) < CONNECT_TIMEOUT_MS) {
        int c = scif_read();
        if (c != -1 && bytes_read < (int)sizeof(buf) - 1) {
            buf[bytes_read++] = (char)c;
            buf[bytes_read] = '\0';

            /* Check for CONNECT response */
            if (strstr(buf, "CONNECT") != NULL) {
                serial_log("Serial - Got CONNECT response");
                break;
            }
        }
        timer_spin_sleep(10);
    }

    if (strstr(buf, "CONNECT") == NULL) {
        /* Log what we received for debugging */
        char log_msg[96];
        snprintf(log_msg, sizeof(log_msg), "No CONNECT - got: %.30s", buf);
        serial_log(log_msg);

        /* Leave SCIF at 115200 for retry - show status via callback */
        char status_msg[80];
        snprintf(status_msg, sizeof(status_msg), "No CONNECT - got: %.30s", buf);
        update_status(status_msg);
        timer_spin_sleep(2000);
        return -2;  /* Dial failed */
    }

    /* Connection established - wait for DreamPi to start pppd */
    update_status("Connected! Waiting for PPP...");
    timer_spin_sleep(6000);  /* 6 seconds to ensure pppd is ready */

    /* Flush any remaining data in buffer before PPP takes over */
    while (scif_read() != -1) { /* drain buffer */ }

    /* Initialize PPP subsystem */
    if (ppp_init() < 0) {
        update_status("PPP init failed!");
        return -3;
    }

    /* Initialize PPP over SCIF (serial) */
    update_status("Starting PPP (serial)...");
    int err = ppp_scif_init(115200);  /* Use 115200 baud for PPP */
    if (err < 0) {
        char log_msg[80];
        snprintf(log_msg, sizeof(log_msg), "ppp_scif_init failed: %d", err);
        serial_log(log_msg);
        ppp_shutdown();

        char status_msg[80];
        snprintf(status_msg, sizeof(status_msg), "ppp_scif_init failed: %d", err);
        update_status(status_msg);
        timer_spin_sleep(2000);
        return -4;
    }

    /* Set login credentials */
    if (ppp_set_login("dream", "dreamcast") < 0) {
        update_status("Login setup failed!");
        ppp_shutdown();
        return -5;
    }

    /* Establish PPP connection */
    update_status("Connecting PPP...");
    err = ppp_connect();
    if (err) {
        char log_msg[80];
        snprintf(log_msg, sizeof(log_msg), "ppp_connect failed: %d", err);
        serial_log(log_msg);
        ppp_shutdown();

        char status_msg[80];
        snprintf(status_msg, sizeof(status_msg), "ppp_connect failed: %d", err);
        update_status(status_msg);
        timer_spin_sleep(2000);
        return -6;
    }

    update_status("Connected via serial!");
    serial_connection_active = 1;
    /* scif_in_use_for_data remains 1 — SCIF is now owned by PPP.
     * It stays 1 even after disconnect to prevent printf at wrong baud rate. */
    serial_log("Serial coders cable connection established!");
    return 0;

#else
    return -1;
#endif
}

/**
 * Connect using modem dial-up only (no serial attempt)
 */
static int try_modem_dialup(void) {
#ifdef _arch_dreamcast
    /* Initialize modem hardware */
    update_status("Initializing modem...");
    if (!modem_init()) {
        update_status("Modem init failed!");
        return -1;
    }

    /* Force 14400 baud for faster connection */
    update_status("Setting modem speed to 14400...");
    modem_set_mode(0 /* MODEM_MODE_REMOTE */, 0x86 /* MODEM_SPEED_V8_14400 */);

    /* Initialize PPP subsystem */
    if (ppp_init() < 0) {
        update_status("PPP init failed!");
        modem_shutdown();
        timer_spin_sleep(200);
        return -2;
    }

    /* Dial modem (using DreamPi dummy number) */
    update_status("Dialing...");
    int err = ppp_modem_init("111-1111", 1, NULL);
    if (err) {
        update_status("Dial failed!");
        ppp_shutdown();
        modem_shutdown();
        timer_spin_sleep(200);
        return -3;
    }

    /* Set login credentials */
    if (ppp_set_login("dream", "dreamcast") < 0) {
        update_status("Login setup failed!");
        ppp_shutdown();
        modem_shutdown();
        timer_spin_sleep(200);
        return -4;
    }

    /* Establish PPP connection */
    update_status("Connecting...");
    err = ppp_connect();
    if (err) {
        update_status("Connection failed!");
        ppp_shutdown();
        modem_shutdown();
        timer_spin_sleep(200);
        return -5;
    }

    update_status("Connected!");
    DCNOW_DPRINTF("DC Now: Modem connection established!\n");
    return 0;

#else
    return -1;
#endif
}

unsigned int dcnow_net_get_ppp_cooldown_remaining_ms(void) {
#ifdef _arch_dreamcast
    if (!last_ppp_disconnect_ms) {
        return 0;
    }

    uint64_t now = timer_ms_gettime64();
    uint64_t elapsed = now - last_ppp_disconnect_ms;
    if (elapsed >= PPP_RECONNECT_COOLDOWN_MS) {
        return 0;
    }

    return (unsigned int)(PPP_RECONNECT_COOLDOWN_MS - elapsed);
#else
    return 0;
#endif
}

int dcnow_net_init_with_method(dcnow_connection_method_t method) {
#ifdef _arch_dreamcast
    update_status("Initializing network...");

    /* Check if BBA is already active */
    if (net_default_dev) {
        update_status("Network ready (BBA detected)");
        return 0;  /* BBA already active, we're done */
    }

    /* Use the specified connection method */
    if (method == DCNOW_CONN_SERIAL) {
        return try_serial_coders_cable();
    } else {
        return try_modem_dialup();
    }

#else
    return -1;
#endif
}

int dcnow_net_early_init(void) {
#ifdef _arch_dreamcast
    update_status("Initializing network...");

    /* Check if BBA is already active (like ClassiCube does) */
    if (net_default_dev) {
        update_status("Network ready (BBA detected)");
        return 0;  /* BBA already active, we're done */
    }

    /* Try serial coders cable first (faster than modem dial-up) */
    int serial_result = try_serial_coders_cable();
    if (serial_result == 0) {
        return 0;  /* Serial connection successful */
    }
    DCNOW_DPRINTF("DC Now: Serial cable not detected, trying modem...\n");

    /* Give system time to settle after serial detection before modem init */
    timer_spin_sleep(500);

    /* No BBA or serial - try modem */
    return try_modem_dialup();

#else
    /* Non-Dreamcast - no network */
    return -1;
#endif
}

void dcnow_net_disconnect(void) {
#ifdef _arch_dreamcast
    /* Use serial_log since SCIF may still be in use for PPP data */
    serial_log("Disconnecting network...");

    /* Restore VMU to OpenMenu logo when disconnecting */
    dcnow_vmu_restore_logo();

    /* Check if we have a network device */
    if (!net_default_dev) {
        /* Leave SCIF state as-is (don't restore to 57600) so reconnect works.
         * Just clear connection flag if needed. */
        serial_log("No network device to disconnect");
        if (scif_in_use_for_data) {
            serial_connection_active = 0;
            /* scif_in_use_for_data stays 1 — preamble is sent explicitly
             * via scif_write_string() in try_serial_coders_cable(). */
        }
        return;
    }

    /* Check if it's a PPP connection (modem or serial) */
    if (strncmp(net_default_dev->name, "ppp", 3) == 0) {
        serial_log("Shutting down PPP connection...");
        ppp_force_release_scif();

        if (serial_connection_active) {
            /* Serial coders cable - do NOT restore SCIF to 57600 debug mode.
             * After PPP at 115200, DreamPi is listening at 115200. If we
             * switched SCIF to 57600 here, any printf output between disconnect
             * and reconnect would be sent at 57600, which DreamPi receives as
             * garbage at 115200. This fills DreamPi's serial buffer with noise,
             * preventing it from detecting the next "AT" command on reconnect.
             *
             * Instead, leave SCIF at 115200 with IRQs disabled (as PPP left it)
             * and keep scif_in_use_for_data = 1. This prevents printf output from
             * leaking through SCIF during subsequent modem sessions, which would
             * confuse DreamPi's serial handler (it sees "DC Now STATUS:" messages
             * and enters AT mode prematurely). The DC Now preamble needed for
             * serial reconnect is sent explicitly via scif_write_string() in
             * try_serial_coders_cable() — no need to re-enable printf here. */
            serial_log("Serial PPP disconnected");

            /* Extra PPP/SCIF detach to mirror modem-cycle reset behavior. */
            ppp_force_release_scif();

            /* Drain any leftover PPP data from SCIF buffers */
            timer_spin_sleep(700);
            while (scif_read() != -1) { /* drain buffer */ }
            timer_spin_sleep(200);
            while (scif_read() != -1) { /* drain again */ }

            serial_connection_active = 0;
            /* scif_in_use_for_data stays 1 — prevents printf from leaking
             * through SCIF during modem sessions. Preamble is sent explicitly
             * via scif_write_string() in try_serial_coders_cable(). */

            serial_log("Serial disconnected, SCIF left at 115200 for reconnect");
        } else {
            /* Modem connection - shutdown modem hardware */
            serial_log("Shutting down modem hardware...");
            modem_shutdown();

            /* Give modem hardware time to reset */
            timer_spin_sleep(700);

            DCNOW_DPRINTF("DC Now: Modem and PPP disconnected\n");
            serial_log("PPP and modem disconnected successfully");
        }

        /* Record disconnect time so next connect can auto-wait for DreamPi. */
        last_ppp_disconnect_ms = timer_ms_gettime64();

        /* Reset network state to NULL so future init knows to reinitialize */
        net_default_dev = NULL;
        serial_log("Network state reset to NULL");
    } else {
        /* BBA doesn't need special disconnect handling */
        DCNOW_DPRINTF("DC Now: Network device is not modem (BBA), no disconnect needed\n");
    }
#else
    /* Non-Dreamcast - nothing to do */
#endif
}
