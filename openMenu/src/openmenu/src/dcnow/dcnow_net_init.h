#ifndef DCNOW_NET_INIT_H
#define DCNOW_NET_INIT_H

/**
 * Initialize network for DreamPi or BBA with automatic modem dialing
 *
 * This function:
 * - Auto-detects BBA (Broadband Adapter) or modem hardware
 * - For BBA: Configures and returns immediately
 * - For DreamPi/modem: Automatically dials and establishes PPP connection using:
 *   - ppp_init() - Initialize PPP subsystem
 *   - ppp_modem_init("555-5555", 1, NULL) - Dial DreamPi
 *   - ppp_set_login("dreamcast", "dreamcast") - Set auth credentials
 *   - ppp_connect() - Establish connection
 *   - Waits up to 30 seconds for link up
 *
 * This should be called early in main() before any network operations
 *
 * @return 0 on success, negative on error:
 *         -1: No network hardware detected
 *         -2: PPP detected but not connected (legacy message)
 *         -3: PPP subsystem init failed
 *         -4: ppp_modem_init failed (dial failed)
 *         -5: ppp_set_login failed
 *         -6: ppp_connect failed
 *         -7: PPP connection timeout (30 seconds)
 */
int dcnow_net_early_init(void);

#endif /* DCNOW_NET_INIT_H */
