#ifndef DCHAT_API_H
#define DCHAT_API_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Discross Chat Client for openMenu
 *
 * Discross (https://discross.net) is a web-based Discord relay that works
 * over plain HTTP - no HTTPS/TLS needed. This makes it perfect for the
 * Dreamcast which has no native TLS support.
 *
 * Protocol overview:
 *   POST /login          - Login with username/password, get sessionID cookie
 *   GET  /server/        - List Discord servers (HTML, parse for IDs)
 *   GET  /server/{id}    - List channels in a server (HTML, parse for IDs)
 *   GET  /channels/{id}  - View messages in a channel (HTML, parse messages)
 *   GET  /send?message=..&channel=.. - Send a message (returns 302)
 *   GET  /longpoll-xhr?{lastID}&uid={ts} - Long-poll for new messages
 */

/* Limits */
#define DCHAT_MAX_MESSAGES      16
#define DCHAT_MAX_SERVERS       12
#define DCHAT_MAX_CHANNELS      20
#define DCHAT_MAX_NAME_LEN      40
#define DCHAT_MAX_CONTENT_LEN   200
#define DCHAT_MAX_ID_LEN        24
#define DCHAT_MAX_HOST_LEN      64
#define DCHAT_MAX_SESSION_LEN   48
#define DCHAT_MAX_CRED_LEN      48

/* Keyboard text input buffer */
#define DCHAT_INPUT_BUF_LEN     140

/* Discross server default port */
#define DCHAT_DEFAULT_PORT      4000

/**
 * A single chat message parsed from Discross HTML
 */
typedef struct {
    char username[DCHAT_MAX_NAME_LEN];
    char content[DCHAT_MAX_CONTENT_LEN];
} dchat_message_t;

/**
 * A Discord server entry from the server list
 */
typedef struct {
    char id[DCHAT_MAX_ID_LEN];
    char name[DCHAT_MAX_NAME_LEN];
} dchat_server_t;

/**
 * A Discord channel entry from the channel list
 */
typedef struct {
    char id[DCHAT_MAX_ID_LEN];
    char name[DCHAT_MAX_NAME_LEN];
} dchat_channel_t;

/**
 * Discross session and data state
 */
typedef struct {
    /* Connection config (from DISCROSS.CFG) */
    char host[DCHAT_MAX_HOST_LEN];
    int  port;
    char username[DCHAT_MAX_CRED_LEN];
    char password[DCHAT_MAX_CRED_LEN];
    bool config_loaded;

    /* Session state */
    char session_id[DCHAT_MAX_SESSION_LEN];
    bool logged_in;

    /* Server list */
    dchat_server_t servers[DCHAT_MAX_SERVERS];
    int server_count;

    /* Channel list for selected server */
    dchat_channel_t channels[DCHAT_MAX_CHANNELS];
    int channel_count;
    char current_server_id[DCHAT_MAX_ID_LEN];

    /* Messages for selected channel */
    dchat_message_t messages[DCHAT_MAX_MESSAGES];
    int message_count;
    char current_channel_id[DCHAT_MAX_ID_LEN];
    bool messages_valid;

    /* Long-poll state */
    int longpoll_last_id;

    /* Error state */
    char error_message[128];
} dchat_data_t;

/**
 * Load Discross config from /cd/DISCROSS.CFG on the SD card.
 *
 * Config file format (plain text, one per line):
 *   HOST=discross.net
 *   PORT=4000
 *   USERNAME=myuser
 *   PASSWORD=mypass
 *
 * @param data  Pointer to dchat_data_t to fill with config
 * @return 0 on success, negative on error
 */
int dchat_init(dchat_data_t *data);

/**
 * Login to the Discross server.
 * POST /login with form-encoded username/password.
 * Captures the sessionID cookie from the response.
 *
 * @param data       Session data (must have config loaded)
 * @param timeout_ms Network timeout in milliseconds
 * @return 0 on success, negative on error
 */
int dchat_login(dchat_data_t *data, uint32_t timeout_ms);

/**
 * Fetch the list of Discord servers the user belongs to.
 * GET /server/ - parses HTML response for server IDs and names.
 *
 * @param data       Session data (must be logged in)
 * @param timeout_ms Network timeout
 * @return 0 on success, negative on error
 */
int dchat_fetch_servers(dchat_data_t *data, uint32_t timeout_ms);

/**
 * Fetch channel list for a specific server.
 * GET /server/{serverID} - parses HTML for channel IDs and names.
 *
 * @param data       Session data (must be logged in)
 * @param server_id  Discord server/guild ID string
 * @param timeout_ms Network timeout
 * @return 0 on success, negative on error
 */
int dchat_fetch_channels(dchat_data_t *data, const char *server_id, uint32_t timeout_ms);

/**
 * Fetch messages from a channel.
 * GET /channels/{channelID} - parses HTML for message content.
 *
 * @param data       Session data (must be logged in)
 * @param channel_id Discord channel ID string
 * @param timeout_ms Network timeout
 * @return 0 on success, negative on error
 */
int dchat_fetch_messages(dchat_data_t *data, const char *channel_id, uint32_t timeout_ms);

/**
 * Send a message to the current channel.
 * GET /send?message=...&channel=... (URL-encoded).
 *
 * @param data       Session data (must be logged in, channel selected)
 * @param channel_id Discord channel ID
 * @param message    Message text to send
 * @param timeout_ms Network timeout
 * @return 0 on success, negative on error
 */
int dchat_send_message(dchat_data_t *data, const char *channel_id,
                       const char *message, uint32_t timeout_ms);

/**
 * Check if network is available for Discross.
 * @return true if a network device is active
 */
bool dchat_network_available(void);

/**
 * Shutdown and cleanup.
 */
void dchat_shutdown(dchat_data_t *data);

#endif /* DCHAT_API_H */
