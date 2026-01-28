#ifndef DCNOW_NET_INIT_H
#define DCNOW_NET_INIT_H

#include "dcnow_flash_isp.h"

/**
 * Status callback for network initialization
 * Called during network init to provide visual feedback
 * @param message - Status message to display (e.g., "Dialing modem...")
 */
typedef void (*dcnow_status_callback_t)(const char* message);

/**
 * Set status callback for visual feedback during network initialization
 * @param callback - Function to call with status updates (NULL to disable)
 */
void dcnow_set_status_callback(dcnow_status_callback_t callback);

/**
 * Initialize network using a specific ISP configuration from flash
 *
 * This function:
 * - Checks if BBA is already active (net_default_dev exists)
 * - For BBA: Returns immediately (already initialized)
 * - For DreamPi/modem: Uses the provided ISP config to dial and connect:
 *   1. modem_init() - Initialize modem hardware
 *   2. ppp_init() - Initialize PPP subsystem
 *   3. ppp_modem_init(isp->phone, ...) - Dial using ISP phone number
 *   4. ppp_set_login(isp->username, isp->password) - Use ISP credentials
 *   5. ppp_connect() - Establish connection
 *
 * @param isp_config - ISP configuration to use (from flash or user selection)
 * @return 0 on success, negative on error:
 *         -1: Modem hardware initialization failed
 *         -2: PPP subsystem init failed
 *         -3: ppp_modem_init failed (dial failed)
 *         -4: ppp_set_login failed
 *         -5: ppp_connect failed
 */
int dcnow_net_init_with_isp(const isp_config_t* isp_config);

/**
 * Initialize network using default/hardcoded settings
 * (Legacy function - prefer dcnow_net_init_with_isp for flexibility)
 *
 * @return 0 on success, negative on error (same codes as above)
 */
int dcnow_net_early_init(void);

#endif /* DCNOW_NET_INIT_H */
