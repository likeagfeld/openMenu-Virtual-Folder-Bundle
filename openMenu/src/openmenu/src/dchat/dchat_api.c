#include "dchat_api.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef _arch_dreamcast
#include <kos.h>
#include <kos/net.h>
#include <kos/thread.h>
#include <arch/timer.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

/* Cached data from last successful fetch */
static dchat_data_t cached_data = {0};
static bool cache_valid = false;

/* Discord API hostname and base path */
#define DISCORD_API_HOST    "discord.com"
#define DISCORD_API_PORT    443
/* Note: Discord requires HTTPS (TLS). On Dreamcast hardware without TLS support,
 * a proxy/bridge server is needed. The config file can specify a custom host.
 * For direct use, a local HTTP-to-HTTPS bridge on the DreamPi is recommended.
 * Default: Use a plain HTTP proxy endpoint that the user configures. */
#define DISCORD_PROXY_HOST  "discord.com"
#define DISCORD_PROXY_PORT  80

/* Simple JSON string value extractor - finds "key":"value" and copies value to out */
static int json_extract_string(const char *json, const char *key, char *out, int out_size) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *pos = strstr(json, search);
    if (!pos) return -1;

    pos += strlen(search);

    /* Skip whitespace */
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;

    if (*pos == '"') {
        pos++;  /* Skip opening quote */
        int i = 0;
        while (*pos && *pos != '"' && i < out_size - 1) {
            /* Handle escaped characters */
            if (*pos == '\\' && *(pos + 1)) {
                pos++;
                switch (*pos) {
                    case 'n': out[i++] = ' '; break;  /* Newline -> space for display */
                    case 't': out[i++] = ' '; break;
                    case '"': out[i++] = '"'; break;
                    case '\\': out[i++] = '\\'; break;
                    default: out[i++] = *pos; break;
                }
            } else {
                out[i++] = *pos;
            }
            pos++;
        }
        out[i] = '\0';
        return 0;
    } else if (*pos == 'n') {
        /* null value */
        strncpy(out, "", out_size);
        return 0;
    }

    return -1;
}

/**
 * Parse Discord messages JSON array response.
 * Discord returns messages newest-first as a JSON array:
 * [{"id":"...","content":"...","author":{"username":"..."},"timestamp":"..."}, ...]
 *
 * This is a minimal parser suitable for the Dreamcast's limited memory.
 */
static int dchat_parse_messages(const char *json, dchat_data_t *data) {
    if (!json || !data) return -1;

    data->message_count = 0;

    /* Find start of array */
    const char *pos = strchr(json, '[');
    if (!pos) return -1;
    pos++;

    int msg_idx = 0;

    while (*pos && msg_idx < DCHAT_MAX_MESSAGES) {
        /* Find next message object */
        const char *obj_start = strchr(pos, '{');
        if (!obj_start) break;

        /* Find matching closing brace - handle nested objects (like "author":{}) */
        int depth = 0;
        const char *obj_end = obj_start;
        do {
            if (*obj_end == '{') depth++;
            else if (*obj_end == '}') depth--;
            if (depth > 0) obj_end++;
        } while (*obj_end && depth > 0);

        if (!*obj_end) break;

        /* Extract fields from this message object */
        int obj_len = (int)(obj_end - obj_start + 1);
        /* Use a temporary buffer for the single message object */
        char *msg_buf = (char *)malloc(obj_len + 1);
        if (!msg_buf) break;
        memcpy(msg_buf, obj_start, obj_len);
        msg_buf[obj_len] = '\0';

        /* Extract message ID */
        json_extract_string(msg_buf, "id", data->messages[msg_idx].id, DCHAT_MAX_MSG_ID_LEN);

        /* Extract content */
        json_extract_string(msg_buf, "content", data->messages[msg_idx].content, DCHAT_MAX_CONTENT_LEN);

        /* Extract timestamp */
        json_extract_string(msg_buf, "timestamp", data->messages[msg_idx].timestamp, DCHAT_MAX_TIMESTAMP_LEN);

        /* Extract author username - find "author" object first */
        char *author_pos = strstr(msg_buf, "\"author\"");
        if (author_pos) {
            char *author_obj = strchr(author_pos, '{');
            if (author_obj) {
                json_extract_string(author_obj, "username",
                    data->messages[msg_idx].username, DCHAT_MAX_USERNAME_LEN);
            }
        }

        /* Fallback if username empty */
        if (data->messages[msg_idx].username[0] == '\0') {
            strncpy(data->messages[msg_idx].username, "???", DCHAT_MAX_USERNAME_LEN);
        }

        free(msg_buf);
        msg_idx++;
        pos = obj_end + 1;
    }

    data->message_count = msg_idx;
    return msg_idx;
}

int dchat_init(void) {
    memset(&cached_data, 0, sizeof(cached_data));
    cache_valid = false;

    /* Try to load Discord config from /cd/DISCORD.CFG */
    FILE *cfg = fopen("/cd/DISCORD.CFG", "r");
    if (!cfg) {
        printf("Discord Chat: No /cd/DISCORD.CFG found\n");
        cached_data.config_loaded = false;
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), cfg)) {
        /* Remove trailing newline/carriage return */
        int len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (strncmp(line, "TOKEN=", 6) == 0) {
            strncpy(cached_data.bot_token, line + 6, DCHAT_MAX_TOKEN_LEN - 1);
            cached_data.bot_token[DCHAT_MAX_TOKEN_LEN - 1] = '\0';
            printf("Discord Chat: Token loaded (%d chars)\n", (int)strlen(cached_data.bot_token));
        } else if (strncmp(line, "CHANNEL=", 8) == 0) {
            strncpy(cached_data.channel_id, line + 8, DCHAT_MAX_CHANNEL_LEN - 1);
            cached_data.channel_id[DCHAT_MAX_CHANNEL_LEN - 1] = '\0';
            printf("Discord Chat: Channel ID: %s\n", cached_data.channel_id);
        }
    }
    fclose(cfg);

    if (cached_data.bot_token[0] != '\0' && cached_data.channel_id[0] != '\0') {
        cached_data.config_loaded = true;
        printf("Discord Chat: Config loaded successfully\n");
        return 0;
    }

    printf("Discord Chat: Config incomplete (need TOKEN and CHANNEL)\n");
    cached_data.config_loaded = false;
    return -2;
}

void dchat_shutdown(void) {
    cache_valid = false;
}

#ifdef _arch_dreamcast
/**
 * Perform an HTTP request to the Discord API (via proxy).
 *
 * Discord requires HTTPS which the Dreamcast doesn't natively support.
 * This function connects to an HTTP proxy/bridge (e.g., running on DreamPi)
 * that forwards requests to Discord's HTTPS API.
 *
 * The proxy should be configured to accept:
 *   GET /api/v10/channels/{id}/messages -> forwarded to discord.com
 *   POST /api/v10/channels/{id}/messages -> forwarded to discord.com
 *
 * @param method   "GET" or "POST"
 * @param path     API path (e.g., "/api/v10/channels/123/messages")
 * @param token    Bot token for Authorization header
 * @param body     POST body (NULL for GET)
 * @param response Buffer for response
 * @param buf_size Size of response buffer
 * @param timeout_ms Timeout
 * @return bytes received on success, negative on error
 */
static int discord_http_request(const char *method, const char *path,
                                 const char *token, const char *body,
                                 char *response, int buf_size, uint32_t timeout_ms) {
    int sock = -1;
    struct hostent *host;
    struct sockaddr_in server_addr;
    char request_buf[1024];
    int total_received = 0;
    uint64_t start_time;

    if (!net_default_dev) {
        printf("Discord Chat: No network device\n");
        return -2;
    }

    /* Create socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }
    if (sock < 0) {
        printf("Discord Chat: Socket creation failed (errno=%d)\n", errno);
        return -2;
    }

    printf("Discord Chat: Socket created (fd=%d)\n", sock);

    /* Resolve proxy host */
    printf("Discord Chat: Resolving %s...\n", DISCORD_PROXY_HOST);
    host = gethostbyname(DISCORD_PROXY_HOST);
    if (!host) {
        printf("Discord Chat: DNS lookup failed\n");
        close(sock);
        return -3;
    }

    /* Connect */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DISCORD_PROXY_PORT);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    printf("Discord Chat: Connecting...\n");
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("Discord Chat: Connect failed (errno=%d)\n", errno);
        close(sock);
        return -4;
    }

    /* Build HTTP request */
    if (body) {
        int body_len = strlen(body);
        snprintf(request_buf, sizeof(request_buf),
                 "%s %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "Authorization: %s\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: %d\r\n"
                 "User-Agent: openMenu-Dreamcast/1.2-dchat\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "%s",
                 method, path, DISCORD_PROXY_HOST, token, body_len, body);
    } else {
        snprintf(request_buf, sizeof(request_buf),
                 "%s %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "Authorization: %s\r\n"
                 "User-Agent: openMenu-Dreamcast/1.2-dchat\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 method, path, DISCORD_PROXY_HOST, token);
    }

    /* Send request */
    int sent = send(sock, request_buf, strlen(request_buf), 0);
    if (sent <= 0) {
        printf("Discord Chat: Send failed (errno=%d)\n", errno);
        close(sock);
        return -5;
    }

    /* Receive response */
    start_time = timer_ms_gettime64();
    total_received = 0;

    while (total_received < buf_size - 1) {
        if (timer_ms_gettime64() - start_time > timeout_ms) {
            printf("Discord Chat: Receive timeout\n");
            break;
        }

        int received = recv(sock, response + total_received,
                           buf_size - total_received - 1, 0);

        if (received > 0) {
            total_received += received;
            start_time = timer_ms_gettime64();
        } else if (received == 0) {
            break;  /* Connection closed */
        } else {
            if (total_received == 0) {
                printf("Discord Chat: Receive failed (errno=%d)\n", errno);
                close(sock);
                return -6;
            }
            break;
        }

        thd_pass();
    }

    response[total_received] = '\0';
    close(sock);

    printf("Discord Chat: Received %d bytes\n", total_received);
    return (total_received > 0) ? total_received : -6;
}
#endif

int dchat_fetch_messages(dchat_data_t *data, uint32_t timeout_ms) {
    if (!data) return -1;

    /* Preserve config across fetch */
    char token_backup[DCHAT_MAX_TOKEN_LEN];
    char channel_backup[DCHAT_MAX_CHANNEL_LEN];
    bool config_backup = data->config_loaded;
    strncpy(token_backup, data->bot_token, DCHAT_MAX_TOKEN_LEN);
    strncpy(channel_backup, data->channel_id, DCHAT_MAX_CHANNEL_LEN);

    /* Clear message data but keep config */
    data->message_count = 0;
    data->data_valid = false;
    data->error_message[0] = '\0';

    /* Restore config */
    strncpy(data->bot_token, token_backup, DCHAT_MAX_TOKEN_LEN);
    strncpy(data->channel_id, channel_backup, DCHAT_MAX_CHANNEL_LEN);
    data->config_loaded = config_backup;

    if (!data->config_loaded) {
        snprintf(data->error_message, sizeof(data->error_message),
                "No Discord config - place DISCORD.CFG on SD card");
        return -10;
    }

#ifdef _arch_dreamcast
    if (!net_default_dev) {
        snprintf(data->error_message, sizeof(data->error_message),
                "No network connection");
        return -11;
    }

    char path[128];
    snprintf(path, sizeof(path), "/api/v10/channels/%s/messages?limit=%d",
             data->channel_id, DCHAT_MAX_MESSAGES);

    char response[8192];
    int result = discord_http_request("GET", path, data->bot_token,
                                       NULL, response, sizeof(response), timeout_ms);

    if (result < 0) {
        const char *error_msg = "Unknown error";
        switch (result) {
            case -2: error_msg = "Socket failed"; break;
            case -3: error_msg = "DNS failed"; break;
            case -4: error_msg = "Connect failed"; break;
            case -5: error_msg = "Send failed"; break;
            case -6: error_msg = "Receive failed"; break;
        }
        snprintf(data->error_message, sizeof(data->error_message),
                "%s (err %d)", error_msg, result);
        return result;
    }

    /* Find JSON body (skip HTTP headers) */
    char *json_start = strstr(response, "\r\n\r\n");
    if (!json_start) {
        strcpy(data->error_message, "Invalid HTTP response");
        return -7;
    }
    json_start += 4;

    /* Check HTTP status */
    if (strncmp(response, "HTTP/1.", 7) == 0) {
        char *status_start = strchr(response, ' ');
        if (status_start) {
            int status_code = atoi(status_start + 1);
            if (status_code != 200) {
                snprintf(data->error_message, sizeof(data->error_message),
                        "HTTP error %d", status_code);
                printf("Discord Chat: HTTP %d\n", status_code);
                return -8;
            }
        }
    }

    /* Parse messages */
    int parsed = dchat_parse_messages(json_start, data);
    if (parsed < 0) {
        strcpy(data->error_message, "JSON parse error");
        return -9;
    }

    /* Discord returns newest first - reverse so oldest is first for display */
    for (int i = 0; i < data->message_count / 2; i++) {
        int j = data->message_count - 1 - i;
        dchat_message_t tmp;
        memcpy(&tmp, &data->messages[i], sizeof(dchat_message_t));
        memcpy(&data->messages[i], &data->messages[j], sizeof(dchat_message_t));
        memcpy(&data->messages[j], &tmp, sizeof(dchat_message_t));
    }

    data->data_valid = true;
    data->last_update_time = (uint32_t)timer_ms_gettime64();

    /* Update cache */
    memcpy(&cached_data, data, sizeof(dchat_data_t));
    cache_valid = true;

    printf("Discord Chat: Fetched %d messages\n", data->message_count);
    return 0;

#else
    /* Non-Dreamcast stub data for testing */
    #ifdef DCHAT_USE_STUB_DATA
    strcpy(data->messages[0].username, "SonicFan99");
    strcpy(data->messages[0].content, "Anyone playing PSO tonight?");
    strcpy(data->messages[0].timestamp, "2026-02-05T20:00:00");

    strcpy(data->messages[1].username, "DreamcastLive");
    strcpy(data->messages[1].content, "Server is up! 12 players online");
    strcpy(data->messages[1].timestamp, "2026-02-05T20:01:00");

    strcpy(data->messages[2].username, "RetroGamer");
    strcpy(data->messages[2].content, "Just got my BBA working, feels good");
    strcpy(data->messages[2].timestamp, "2026-02-05T20:02:00");

    data->message_count = 3;
    data->data_valid = true;
    data->last_update_time = 0;

    memcpy(&cached_data, data, sizeof(dchat_data_t));
    cache_valid = true;
    return 0;
    #else
    strcpy(data->error_message, "Network not available");
    return -100;
    #endif
#endif
}

int dchat_send_message(dchat_data_t *data, const char *message, uint32_t timeout_ms) {
    if (!data || !message || message[0] == '\0') return -1;

    if (!data->config_loaded) {
        snprintf(data->error_message, sizeof(data->error_message),
                "No Discord config");
        return -10;
    }

#ifdef _arch_dreamcast
    if (!net_default_dev) {
        snprintf(data->error_message, sizeof(data->error_message),
                "No network connection");
        return -11;
    }

    char path[128];
    snprintf(path, sizeof(path), "/api/v10/channels/%s/messages",
             data->channel_id);

    /* Build JSON body - escape special characters in the message */
    char body[512];
    char escaped_msg[300];
    int ei = 0;
    for (int i = 0; message[i] && ei < (int)sizeof(escaped_msg) - 2; i++) {
        if (message[i] == '"') {
            escaped_msg[ei++] = '\\';
            escaped_msg[ei++] = '"';
        } else if (message[i] == '\\') {
            escaped_msg[ei++] = '\\';
            escaped_msg[ei++] = '\\';
        } else if (message[i] == '\n') {
            escaped_msg[ei++] = '\\';
            escaped_msg[ei++] = 'n';
        } else {
            escaped_msg[ei++] = message[i];
        }
    }
    escaped_msg[ei] = '\0';

    snprintf(body, sizeof(body), "{\"content\":\"%s\"}", escaped_msg);

    char response[2048];
    int result = discord_http_request("POST", path, data->bot_token,
                                       body, response, sizeof(response), timeout_ms);

    if (result < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Send failed (err %d)", result);
        return result;
    }

    /* Check HTTP status */
    char *json_start = strstr(response, "\r\n\r\n");
    if (json_start && strncmp(response, "HTTP/1.", 7) == 0) {
        char *status_start = strchr(response, ' ');
        if (status_start) {
            int status_code = atoi(status_start + 1);
            if (status_code != 200 && status_code != 201) {
                snprintf(data->error_message, sizeof(data->error_message),
                        "Send HTTP error %d", status_code);
                return -8;
            }
        }
    }

    printf("Discord Chat: Message sent successfully\n");
    return 0;

#else
    printf("Discord Chat: [STUB] Would send: %s\n", message);
    return 0;
#endif
}

bool dchat_get_cached_data(dchat_data_t *data) {
    if (!data || !cache_valid) return false;
    memcpy(data, &cached_data, sizeof(dchat_data_t));
    return true;
}

void dchat_clear_cache(void) {
    memset(&cached_data.messages, 0, sizeof(cached_data.messages));
    cached_data.message_count = 0;
    cached_data.data_valid = false;
    cache_valid = false;
}

bool dchat_network_available(void) {
#ifdef _arch_dreamcast
    return (net_default_dev != NULL);
#else
    return false;
#endif
}
