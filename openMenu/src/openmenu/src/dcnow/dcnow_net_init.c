#include "dcnow_net_init.h"
#include <stdio.h>

#ifdef _arch_dreamcast
#include <kos.h>
#include <kos/net.h>
#include <ppp/ppp.h>
#include <arch/timer.h>
#endif

int dcnow_net_early_init(void) {
#ifdef _arch_dreamcast
    printf("DC Now: Early network initialization...\n");

    /* Try to initialize network hardware */
    /* This will auto-detect BBA or modem */
    int net_result = net_init();

    if (net_result < 0) {
        printf("DC Now: No network hardware detected\n");
        printf("DC Now: DC Now feature will be unavailable\n");
        return -1;
    }

    printf("DC Now: Network hardware initialized\n");

    /* Check what device we got */
    if (net_default_dev) {
        printf("DC Now: Device: %s\n", net_default_dev->name);

        /* For BBA - nothing more needed, it's auto-configured */
        if (strncmp(net_default_dev->name, "bba", 3) == 0) {
            printf("DC Now: BBA detected - ready to use\n");
            return 0;
        }

        /* For PPP/modem - attempt to establish connection if not already up */
        if (strncmp(net_default_dev->name, "ppp", 3) == 0) {
            printf("DC Now: PPP/Modem device detected\n");

            /* Check if PPP link is already up */
            if (net_default_dev->if_flags & NETIF_FLAG_LINK_UP) {
                printf("DC Now: PPP already connected - ready to use\n");
                return 0;
            }

            printf("DC Now: PPP not connected, attempting to dial DreamPi...\n");

            /* Initialize PPP subsystem */
            printf("DC Now: Initializing PPP subsystem...\n");
            if (ppp_init() < 0) {
                printf("DC Now: Failed to initialize PPP subsystem\n");
                return -3;
            }

            /* Initialize modem with phone number and speed */
            /* DreamPi uses 555-5555 (or similar dummy number) */
            /* Second parameter: 1 = speed flag (V.90 56k) */
            /* Third parameter: NULL = no callback */
            printf("DC Now: Initializing modem (dialing 555-5555)...\n");
            if (ppp_modem_init("555-5555", 1, NULL) < 0) {
                printf("DC Now: Failed to initialize modem\n");
                ppp_shutdown();
                return -4;
            }

            /* Set login credentials for PPP authentication */
            /* DreamPi typically uses dreamcast/dreamcast */
            printf("DC Now: Setting PPP login credentials...\n");
            if (ppp_set_login("dreamcast", "dreamcast") < 0) {
                printf("DC Now: Failed to set login credentials\n");
                ppp_shutdown();
                return -5;
            }

            /* Establish PPP connection */
            printf("DC Now: Connecting PPP...\n");
            if (ppp_connect() < 0) {
                printf("DC Now: Failed to initiate PPP connection\n");
                ppp_shutdown();
                return -6;
            }

            printf("DC Now: PPP connection initiated, waiting for link up...\n");

            /* Wait for PPP connection to be established (max 30 seconds) */
            int wait_count = 0;
            int max_wait = 300; /* 30 seconds at 100ms intervals */

            while (wait_count < max_wait) {
                /* Check if link is up */
                if (net_default_dev && (net_default_dev->if_flags & NETIF_FLAG_LINK_UP)) {
                    printf("DC Now: PPP connection established!\n");

                    /* Display connection info */
                    if (net_default_dev->ip_addr[0] != 0) {
                        printf("DC Now: IP Address: %d.%d.%d.%d\n",
                               net_default_dev->ip_addr[0],
                               net_default_dev->ip_addr[1],
                               net_default_dev->ip_addr[2],
                               net_default_dev->ip_addr[3]);
                    }

                    return 0;  /* Success! */
                }

                /* Wait 100ms before checking again */
                timer_spin_sleep(100);
                wait_count++;

                /* Print progress every 5 seconds */
                if (wait_count % 50 == 0) {
                    printf("DC Now: Still waiting for connection (%d/%d seconds)...\n",
                           wait_count / 10, max_wait / 10);
                }
            }

            /* Timeout - connection failed */
            printf("DC Now: PPP connection timeout after 30 seconds\n");
            printf("DC Now: Please check:\n");
            printf("DC Now:   - DreamPi is running and configured\n");
            printf("DC Now:   - Modem cable is properly connected\n");
            printf("DC Now:   - DreamPi network settings are correct\n");
            printf("DC Now:   - Phone line cable connected between DC and DreamPi\n");

            ppp_shutdown();
            return -7;  /* Connection timeout */
        }
    }

    printf("DC Now: Network initialized successfully\n");
    return 0;

#else
    /* Non-Dreamcast - no network */
    return -1;
#endif
}
