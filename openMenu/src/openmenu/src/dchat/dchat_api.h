#ifndef DCHAT_API_H
#define DCHAT_API_H

#include <stdint.h>
#include <stdbool.h>

/* Discord chat message limits */
#define DCHAT_MAX_MESSAGES      20
#define DCHAT_MAX_USERNAME_LEN  32
#define DCHAT_MAX_CONTENT_LEN   200
#define DCHAT_MAX_MSG_ID_LEN    24
#define DCHAT_MAX_TIMESTAMP_LEN 32

/* Discord bot token and channel config limits */
#define DCHAT_MAX_TOKEN_LEN     128
#define DCHAT_MAX_CHANNEL_LEN   24

/* Keyboard text input buffer */
#define DCHAT_INPUT_BUF_LEN     140

/**
 * A single Discord chat message
 */
typedef struct {
    char id[DCHAT_MAX_MSG_ID_LEN];
    char username[DCHAT_MAX_USERNAME_LEN];
    char content[DCHAT_MAX_CONTENT_LEN];
    char timestamp[DCHAT_MAX_TIMESTAMP_LEN];
} dchat_message_t;

/**
 * Discord chat state - holds fetched messages and config
 */
typedef struct {
    dchat_message_t messages[DCHAT_MAX_MESSAGES];
    int message_count;
    bool data_valid;
    char error_message[128];
    uint32_t last_update_time;

    /* Configuration (loaded from OPENMENU.INI or SD card) */
    char bot_token[DCHAT_MAX_TOKEN_LEN];
    char channel_id[DCHAT_MAX_CHANNEL_LEN];
    bool config_loaded;
} dchat_data_t;

/**
 * Initialize the Discord chat subsystem.
 * Loads bot token and channel ID from /cd/DISCORD.CFG on the SD card.
 *
 * Config file format (plain text, one per line):
 *   TOKEN=Bot <your_bot_token>
 *   CHANNEL=<channel_id>
 *
 * @return 0 on success, negative on error
 */
int dchat_init(void);

/**
 * Shutdown Discord chat subsystem and free resources.
 */
void dchat_shutdown(void);

/**
 * Fetch recent messages from the configured Discord channel.
 * Uses Discord Bot HTTP API: GET /api/v10/channels/{id}/messages?limit=20
 *
 * Requires network to already be initialized (via dcnow_net_init or BBA).
 *
 * @param data      Pointer to dchat_data_t to fill with results
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, negative on error
 */
int dchat_fetch_messages(dchat_data_t *data, uint32_t timeout_ms);

/**
 * Send a message to the configured Discord channel.
 * Uses Discord Bot HTTP API: POST /api/v10/channels/{id}/messages
 *
 * @param data      Pointer to dchat_data_t (for config)
 * @param message   The message text to send
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, negative on error
 */
int dchat_send_message(dchat_data_t *data, const char *message, uint32_t timeout_ms);

/**
 * Get cached message data without making a network request.
 *
 * @param data Pointer to dchat_data_t to fill
 * @return true if cached data available, false otherwise
 */
bool dchat_get_cached_data(dchat_data_t *data);

/**
 * Clear cached message data.
 */
void dchat_clear_cache(void);

/**
 * Check if network is available for Discord chat.
 * @return true if a network device is active, false otherwise
 */
bool dchat_network_available(void);

#endif /* DCHAT_API_H */
