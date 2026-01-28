#include "dcnow_net_init.h"
#include "dcnow_flash_isp.h"
#include <stdio.h>
#include <string.h>

#ifdef _arch_dreamcast
#include <kos.h>
#include <kos/net.h>
#include <ppp/ppp.h>
#include <dc/modem/modem.h>
#include <arch/timer.h>
#include <dc/pvr.h>
#endif

/* Status callback for visual feedback */
static dcnow_status_callback_t status_callback = NULL;

void dcnow_set_status_callback(dcnow_status_callback_t callback) {
    status_callback = callback;
}

static void update_status(const char* message) {
    printf("DC Now STATUS: %s\n", message);

    /* Log to RAM disk for debugging without serial cable */
    /* /ram/ is writable, unlike /cd/ which is read-only */
    FILE* logfile = fopen("/ram/DCNOW_LOG.TXT", "a");
    if (logfile) {
        fprintf(logfile, "STATUS: %s\n", message);
        fclose(logfile);
    } else {
        printf("DC Now: WARNING - Failed to open log file\n");
    }

    if (status_callback) {
        printf("DC Now: Calling status callback...\n");
        /* Call callback which will draw the message */
        /* Callback is responsible for full scene rendering */
        status_callback(message);
        printf("DC Now: Status callback returned\n");
        /* Give user time to see the message */
        timer_spin_sleep(500);  /* 500ms delay so messages are visible */
    } else {
        printf("DC Now: WARNING - No status callback set!\n");
        logfile = fopen("/ram/DCNOW_LOG.TXT", "a");
        if (logfile) {
            fprintf(logfile, "ERROR: No status callback!\n");
            fclose(logfile);
        }
    }
}

int dcnow_net_init_with_isp(const isp_config_t* isp_config) {
#ifdef _arch_dreamcast
    if (!isp_config || !isp_config->valid) {
        printf("DC Now: ERROR - Invalid ISP configuration\n");
        update_status("Invalid ISP config");
        return -1;
    }

    printf("DC Now: Using ISP: %s\n", isp_config->name);
    printf("DC Now: Phone: %s\n", isp_config->phone);
    printf("DC Now: Username: %s\n", isp_config->username);

    /* Write ISP details to log file */
    FILE* logfile = fopen("/ram/DCNOW_LOG.TXT", "w");
    if (logfile) {
        fprintf(logfile, "=== DC Now Connection Attempt ===\n");
        fprintf(logfile, "ISP Name: %s\n", isp_config->name);
        fprintf(logfile, "Phone: %s\n", isp_config->phone);
        fprintf(logfile, "Username: %s\n", isp_config->username);
        fprintf(logfile, "Phone length: %d\n", (int)strlen(isp_config->phone));
        fclose(logfile);
    }

    /* Clean up any existing PPP connection first */
    printf("DC Now: Cleaning up any existing PPP connection...\n");
    ppp_shutdown();
    thd_sleep(500);  /* Wait 500ms for cleanup */

    /* Initialize modem (DreamPi dial-up) */
    char status_msg[64];
    snprintf(status_msg, sizeof(status_msg), "Connecting to %s...", isp_config->name);
    update_status(status_msg);

    /* Initialize modem hardware */
    update_status("Initializing modem...");
    if (!modem_init()) {
        update_status("Modem init failed!");
        printf("DC Now: modem_init() failed\n");
        logfile = fopen("/ram/DCNOW_LOG.TXT", "a");
        if (logfile) {
            fprintf(logfile, "ERROR: modem_init() failed\n");
            fclose(logfile);
        }
        return -1;
    }

    /* Initialize PPP subsystem */
    if (ppp_init() < 0) {
        update_status("PPP init failed!");
        printf("DC Now: ppp_init() failed\n");
        logfile = fopen("/ram/DCNOW_LOG.TXT", "a");
        if (logfile) {
            fprintf(logfile, "ERROR: ppp_init() failed\n");
            fclose(logfile);
        }
        return -2;
    }

    /* Dial modem using ISP phone number */
    snprintf(status_msg, sizeof(status_msg), "Dialing %s...", isp_config->phone);
    update_status(status_msg);
    printf("DC Now: Dialing %s\n", isp_config->phone);

    logfile = fopen("/ram/DCNOW_LOG.TXT", "a");
    if (logfile) {
        fprintf(logfile, "About to dial: '%s'\n", isp_config->phone);
        fclose(logfile);
    }

    int err = ppp_modem_init(isp_config->phone, 1, NULL);
    if (err) {
        update_status("Dial failed!");
        printf("DC Now: ppp_modem_init() failed: %d\n", err);
        logfile = fopen("/ram/DCNOW_LOG.TXT", "a");
        if (logfile) {
            fprintf(logfile, "ERROR: ppp_modem_init() returned %d\n", err);
            fclose(logfile);
        }
        ppp_shutdown();
        return -3;
    }

    /* Set login credentials from ISP config */
    printf("DC Now: Setting credentials for %s\n", isp_config->username);
    if (ppp_set_login(isp_config->username, isp_config->password) < 0) {
        update_status("Login setup failed!");
        printf("DC Now: ppp_set_login() failed\n");
        ppp_shutdown();
        return -4;
    }

    /* Establish PPP connection */
    update_status("Connecting...");
    err = ppp_connect();
    if (err) {
        update_status("Connection failed!");
        printf("DC Now: ppp_connect() failed: %d\n", err);
        ppp_shutdown();
        return -5;
    }

    /* ppp_connect() is BLOCKING - returns when connection is established */
    update_status("Connected!");
    printf("DC Now: Successfully connected via %s\n", isp_config->name);

    return 0;

#else
    /* Non-Dreamcast - no network */
    printf("DC Now: Not running on Dreamcast hardware\n");
    return -1;
#endif
}

int dcnow_net_early_init(void) {
    /* Use default DreamPi configuration */
    isp_config_t default_isp;
    strcpy(default_isp.name, "DreamPi (default)");
    strcpy(default_isp.phone, "111-1111");
    strcpy(default_isp.username, "dream");
    strcpy(default_isp.password, "dreamcast");
    default_isp.valid = true;

    return dcnow_net_init_with_isp(&default_isp);
}
