/*
 * Discross Chat Client for openMenu
 *
 * Native Discross (https://discross.net) client for Dreamcast.
 * Talks to a Discross relay server over plain HTTP (no TLS needed).
 *
 * Discross protocol:
 *   POST /login            - form-encoded username/password -> Set-Cookie: sessionID
 *   GET  /server/          - HTML server list (parse for guild IDs)
 *   GET  /server/{id}      - HTML channel list (parse for channel IDs)
 *   GET  /channels/{id}    - HTML message history (parse <th> and <td>)
 *   GET  /send?message=..&channel=.. - send message (returns 302)
 */

#include "dchat_api.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

/* ========================================================================
 * Internal HTTP helpers
 * ======================================================================== */

#ifdef _arch_dreamcast
/**
 * Open a TCP connection to the Discross server.
 * @return socket fd on success, negative on error
 */
static int dchat_connect(const char *host, int port) {
    if (!net_default_dev) {
        printf("Discross: No network device\n");
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }
    if (sock < 0) {
        printf("Discross: Socket failed\n");
        return -2;
    }

    struct hostent *he = gethostbyname(host);
    if (!he) {
        printf("Discross: DNS lookup failed for %s\n", host);
        close(sock);
        return -3;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);

    printf("Discross: Connecting to %s:%d...\n", host, port);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("Discross: Connect failed\n");
        close(sock);
        return -4;
    }

    return sock;
}

/**
 * Send a full HTTP request and receive the response.
 * @return total bytes received on success, negative on error
 */
static int dchat_http_exchange(int sock, const char *request, int req_len,
                                char *response, int buf_size, uint32_t timeout_ms) {
    int sent = send(sock, request, req_len, 0);
    if (sent <= 0) {
        printf("Discross: Send failed\n");
        return -5;
    }

    uint64_t start = timer_ms_gettime64();
    int total = 0;

    while (total < buf_size - 1) {
        if (timer_ms_gettime64() - start > timeout_ms) break;

        int n = recv(sock, response + total, buf_size - total - 1, 0);
        if (n > 0) {
            total += n;
            start = timer_ms_gettime64();  /* reset timeout on data */
        } else if (n == 0) {
            break;  /* connection closed by server */
        } else {
            if (total == 0) return -6;
            break;
        }
        thd_pass();
    }

    response[total] = '\0';
    return total;
}

/**
 * Get the HTTP status code from a response.
 */
static int dchat_http_status(const char *response) {
    /* "HTTP/1.x NNN ..." */
    if (strncmp(response, "HTTP/1.", 7) != 0) return -1;
    const char *sp = strchr(response, ' ');
    if (!sp) return -1;
    return atoi(sp + 1);
}

/**
 * Find the body in an HTTP response (after \r\n\r\n).
 */
static const char *dchat_http_body(const char *response) {
    const char *p = strstr(response, "\r\n\r\n");
    return p ? p + 4 : NULL;
}

/**
 * Extract Set-Cookie: sessionID=VALUE from HTTP response headers.
 */
static int dchat_extract_session(const char *response, char *out, int out_size) {
    const char *cookie = strstr(response, "sessionID=");
    if (!cookie) return -1;

    cookie += 10;  /* skip "sessionID=" */
    int i = 0;
    while (cookie[i] && cookie[i] != ';' && cookie[i] != '\r' && cookie[i] != '\n'
           && cookie[i] != ' ' && i < out_size - 1) {
        out[i] = cookie[i];
        i++;
    }
    out[i] = '\0';
    return (i > 0) ? 0 : -1;
}
#endif /* _arch_dreamcast */

/**
 * URL-encode a string for use in query parameters.
 * Only encodes the most necessary characters for Discross.
 */
static void url_encode(const char *src, char *dst, int dst_size) {
    int di = 0;
    for (int i = 0; src[i] && di < dst_size - 4; i++) {
        char c = src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = c;
        } else if (c == ' ') {
            dst[di++] = '+';
        } else {
            snprintf(dst + di, 4, "%%%02X", (unsigned char)c);
            di += 3;
        }
    }
    dst[di] = '\0';
}

/**
 * Decode basic HTML entities (&amp; &lt; &gt; &quot; &#39;).
 */
static void html_decode_inplace(char *str) {
    char *r = str, *w = str;
    while (*r) {
        if (*r == '&') {
            if (strncmp(r, "&amp;", 5) == 0) { *w++ = '&'; r += 5; }
            else if (strncmp(r, "&lt;", 4) == 0) { *w++ = '<'; r += 4; }
            else if (strncmp(r, "&gt;", 4) == 0) { *w++ = '>'; r += 4; }
            else if (strncmp(r, "&quot;", 6) == 0) { *w++ = '"'; r += 6; }
            else if (strncmp(r, "&#39;", 5) == 0) { *w++ = '\''; r += 5; }
            else { *w++ = *r++; }
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

/**
 * Strip HTML tags from a string, leaving only text content.
 */
static void strip_html_tags(char *str) {
    char *r = str, *w = str;
    bool in_tag = false;
    while (*r) {
        if (*r == '<') { in_tag = true; r++; continue; }
        if (*r == '>') { in_tag = false; r++; continue; }
        if (!in_tag) *w++ = *r;
        r++;
    }
    *w = '\0';
}

/* ========================================================================
 * Public API
 * ======================================================================== */

int dchat_init(dchat_data_t *data) {
    if (!data) return -1;
    memset(data, 0, sizeof(dchat_data_t));

    /* Defaults */
    strcpy(data->host, "discross.net");
    data->port = DCHAT_DEFAULT_PORT;

    /* Load config from /cd/DISCROSS.CFG */
    FILE *cfg = fopen("/cd/DISCROSS.CFG", "r");
    if (!cfg) {
        printf("Discross: No /cd/DISCROSS.CFG found\n");
        data->config_loaded = false;
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), cfg)) {
        /* Trim trailing whitespace */
        int len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ')) {
            line[--len] = '\0';
        }

        if (strncmp(line, "HOST=", 5) == 0) {
            strncpy(data->host, line + 5, DCHAT_MAX_HOST_LEN - 1);
            printf("Discross: Host = %s\n", data->host);
        } else if (strncmp(line, "PORT=", 5) == 0) {
            data->port = atoi(line + 5);
            if (data->port <= 0) data->port = DCHAT_DEFAULT_PORT;
            printf("Discross: Port = %d\n", data->port);
        } else if (strncmp(line, "USERNAME=", 9) == 0) {
            strncpy(data->username, line + 9, DCHAT_MAX_CRED_LEN - 1);
            printf("Discross: Username = %s\n", data->username);
        } else if (strncmp(line, "PASSWORD=", 9) == 0) {
            strncpy(data->password, line + 9, DCHAT_MAX_CRED_LEN - 1);
            printf("Discross: Password loaded\n");
        }
    }
    fclose(cfg);

    if (data->username[0] != '\0' && data->password[0] != '\0') {
        data->config_loaded = true;
        printf("Discross: Config loaded OK\n");
        return 0;
    }

    printf("Discross: Config incomplete (need USERNAME and PASSWORD)\n");
    data->config_loaded = false;
    return -2;
}

int dchat_login(dchat_data_t *data, uint32_t timeout_ms) {
    if (!data || !data->config_loaded) return -1;

#ifdef _arch_dreamcast
    int sock = dchat_connect(data->host, data->port);
    if (sock < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Connection failed (%d)", sock);
        return sock;
    }

    /* Build form-encoded POST body */
    char enc_user[128], enc_pass[128];
    url_encode(data->username, enc_user, sizeof(enc_user));
    url_encode(data->password, enc_pass, sizeof(enc_pass));

    char body[300];
    snprintf(body, sizeof(body), "username=%s&password=%s", enc_user, enc_pass);
    int body_len = strlen(body);

    char request[512];
    int req_len = snprintf(request, sizeof(request),
        "POST /login HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: %d\r\n"
        "User-Agent: openMenu-Dreamcast/1.2-discross\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        data->host, data->port, body_len, body);

    char response[4096];
    int result = dchat_http_exchange(sock, request, req_len, response, sizeof(response), timeout_ms);
    close(sock);

    if (result < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Login request failed (%d)", result);
        return result;
    }

    /* Check for session cookie in response (any status - redirects have cookies) */
    if (dchat_extract_session(response, data->session_id, DCHAT_MAX_SESSION_LEN) == 0) {
        data->logged_in = true;
        printf("Discross: Login OK, session=%s\n", data->session_id);
        return 0;
    }

    /* Login failed - check status */
    int status = dchat_http_status(response);
    snprintf(data->error_message, sizeof(data->error_message),
            "Login failed (HTTP %d)", status);
    printf("Discross: Login failed, HTTP %d\n", status);
    return -10;

#else
    /* Stub for non-DC builds */
    strcpy(data->session_id, "stub-session");
    data->logged_in = true;
    return 0;
#endif
}

int dchat_fetch_servers(dchat_data_t *data, uint32_t timeout_ms) {
    if (!data || !data->logged_in) return -1;

#ifdef _arch_dreamcast
    int sock = dchat_connect(data->host, data->port);
    if (sock < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Connection failed");
        return sock;
    }

    char request[512];
    int req_len = snprintf(request, sizeof(request),
        "GET /server/ HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Cookie: sessionID=%s\r\n"
        "User-Agent: openMenu-Dreamcast/1.2-discross\r\n"
        "Connection: close\r\n"
        "\r\n",
        data->host, data->port, data->session_id);

    char response[8192];
    int result = dchat_http_exchange(sock, request, req_len, response, sizeof(response), timeout_ms);
    close(sock);

    if (result < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Server list failed (%d)", result);
        return result;
    }

    int status = dchat_http_status(response);
    if (status == 303 || status == 302) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Session expired - re-login needed");
        data->logged_in = false;
        return -10;
    }

    const char *body = dchat_http_body(response);
    if (!body) {
        strcpy(data->error_message, "Invalid response");
        return -7;
    }

    /* Parse server list from HTML.
     * Server links look like: href="./SNOWFLAKE_ID"
     * Server names are in alt="NAME" or title="NAME" on the icon images */
    data->server_count = 0;
    const char *pos = body;

    while (data->server_count < DCHAT_MAX_SERVERS) {
        /* Find server link: href="./ followed by a numeric ID */
        const char *href = strstr(pos, "href=\"./");
        if (!href) break;
        href += 7;  /* skip href="./ */

        /* Extract server ID (digits only) */
        char id_buf[DCHAT_MAX_ID_LEN];
        int i = 0;
        while (href[i] && href[i] != '"' && href[i] != '/' && i < DCHAT_MAX_ID_LEN - 1) {
            id_buf[i] = href[i];
            i++;
        }
        id_buf[i] = '\0';

        /* Validate it looks like a Discord snowflake (all digits, > 10 chars) */
        if (i < 10) {
            pos = href + i;
            continue;
        }
        bool all_digits = true;
        for (int j = 0; j < i; j++) {
            if (id_buf[j] < '0' || id_buf[j] > '9') { all_digits = false; break; }
        }
        if (!all_digits) {
            pos = href + i;
            continue;
        }

        strcpy(data->servers[data->server_count].id, id_buf);

        /* Try to find server name in alt="..." or title="..." nearby */
        const char *alt = strstr(href, "alt=\"");
        const char *next_href = strstr(href + 1, "href=\"./");
        if (alt && (!next_href || alt < next_href)) {
            alt += 5;
            int ni = 0;
            while (alt[ni] && alt[ni] != '"' && ni < DCHAT_MAX_NAME_LEN - 1) {
                data->servers[data->server_count].name[ni] = alt[ni];
                ni++;
            }
            data->servers[data->server_count].name[ni] = '\0';
            html_decode_inplace(data->servers[data->server_count].name);
        } else {
            /* Fallback: use the ID as the name */
            snprintf(data->servers[data->server_count].name, DCHAT_MAX_NAME_LEN,
                    "Server %s", id_buf);
        }

        printf("Discross: Server [%d] %s = %s\n", data->server_count,
               data->servers[data->server_count].id,
               data->servers[data->server_count].name);

        data->server_count++;
        pos = href + i;
    }

    printf("Discross: Found %d servers\n", data->server_count);
    return 0;

#else
    /* Stub data */
    strcpy(data->servers[0].id, "123456789012345678");
    strcpy(data->servers[0].name, "Test Server");
    data->server_count = 1;
    return 0;
#endif
}

int dchat_fetch_channels(dchat_data_t *data, const char *server_id, uint32_t timeout_ms) {
    if (!data || !data->logged_in || !server_id) return -1;

    strncpy(data->current_server_id, server_id, DCHAT_MAX_ID_LEN - 1);

#ifdef _arch_dreamcast
    int sock = dchat_connect(data->host, data->port);
    if (sock < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Connection failed");
        return sock;
    }

    char request[512];
    int req_len = snprintf(request, sizeof(request),
        "GET /server/%s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Cookie: sessionID=%s\r\n"
        "User-Agent: openMenu-Dreamcast/1.2-discross\r\n"
        "Connection: close\r\n"
        "\r\n",
        server_id, data->host, data->port, data->session_id);

    char response[8192];
    int result = dchat_http_exchange(sock, request, req_len, response, sizeof(response), timeout_ms);
    close(sock);

    if (result < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Channel list failed (%d)", result);
        return result;
    }

    const char *body = dchat_http_body(response);
    if (!body) {
        strcpy(data->error_message, "Invalid response");
        return -7;
    }

    /* Parse channel links from HTML.
     * Channel links look like: href="../channels/CHANNEL_ID#end"
     * or href="/channels/CHANNEL_ID#end"
     * Channel names are in the link text, often preceded by # */
    data->channel_count = 0;
    const char *pos = body;

    while (data->channel_count < DCHAT_MAX_CHANNELS) {
        /* Find channel link */
        const char *channels_str = strstr(pos, "channels/");
        if (!channels_str) break;
        channels_str += 9;  /* skip "channels/" */

        /* Extract channel ID */
        char id_buf[DCHAT_MAX_ID_LEN];
        int i = 0;
        while (channels_str[i] && channels_str[i] != '#' && channels_str[i] != '"'
               && channels_str[i] != '/' && i < DCHAT_MAX_ID_LEN - 1) {
            id_buf[i] = channels_str[i];
            i++;
        }
        id_buf[i] = '\0';

        /* Validate snowflake */
        if (i < 10) { pos = channels_str + i; continue; }
        bool all_digits = true;
        for (int j = 0; j < i; j++) {
            if (id_buf[j] < '0' || id_buf[j] > '9') { all_digits = false; break; }
        }
        if (!all_digits) { pos = channels_str + i; continue; }

        /* Check for duplicate (same ID already found) */
        bool dup = false;
        for (int j = 0; j < data->channel_count; j++) {
            if (strcmp(data->channels[j].id, id_buf) == 0) { dup = true; break; }
        }
        if (dup) { pos = channels_str + i; continue; }

        strcpy(data->channels[data->channel_count].id, id_buf);

        /* Try to find channel name: look for ">" after the closing quote, take text until "<" */
        const char *close_tag = strchr(channels_str, '>');
        if (close_tag) {
            close_tag++;
            int ni = 0;
            /* Skip leading # and whitespace */
            while (*close_tag == '#' || *close_tag == ' ') close_tag++;
            while (close_tag[ni] && close_tag[ni] != '<' && ni < DCHAT_MAX_NAME_LEN - 1) {
                data->channels[data->channel_count].name[ni] = close_tag[ni];
                ni++;
            }
            data->channels[data->channel_count].name[ni] = '\0';
            html_decode_inplace(data->channels[data->channel_count].name);
            strip_html_tags(data->channels[data->channel_count].name);

            /* Trim trailing whitespace */
            while (ni > 0 && (data->channels[data->channel_count].name[ni - 1] == ' ' ||
                              data->channels[data->channel_count].name[ni - 1] == '\n')) {
                data->channels[data->channel_count].name[--ni] = '\0';
            }
        }

        /* Fallback name */
        if (data->channels[data->channel_count].name[0] == '\0') {
            snprintf(data->channels[data->channel_count].name, DCHAT_MAX_NAME_LEN,
                    "#%s", id_buf);
        }

        printf("Discross: Channel [%d] %s = %s\n", data->channel_count,
               data->channels[data->channel_count].id,
               data->channels[data->channel_count].name);

        data->channel_count++;
        pos = channels_str + i;
    }

    printf("Discross: Found %d channels\n", data->channel_count);
    return 0;

#else
    strcpy(data->channels[0].id, "987654321098765432");
    strcpy(data->channels[0].name, "general");
    strcpy(data->channels[1].id, "987654321098765433");
    strcpy(data->channels[1].name, "dreamcast-chat");
    data->channel_count = 2;
    return 0;
#endif
}

int dchat_fetch_messages(dchat_data_t *data, const char *channel_id, uint32_t timeout_ms) {
    if (!data || !data->logged_in || !channel_id) return -1;

    strncpy(data->current_channel_id, channel_id, DCHAT_MAX_ID_LEN - 1);

#ifdef _arch_dreamcast
    int sock = dchat_connect(data->host, data->port);
    if (sock < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Connection failed");
        return sock;
    }

    char request[512];
    int req_len = snprintf(request, sizeof(request),
        "GET /channels/%s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Cookie: sessionID=%s\r\n"
        "User-Agent: openMenu-Dreamcast/1.2-discross\r\n"
        "Connection: close\r\n"
        "\r\n",
        channel_id, data->host, data->port, data->session_id);

    /* Messages page can be large - allocate on heap */
    char *response = (char *)malloc(16384);
    if (!response) {
        strcpy(data->error_message, "Out of memory");
        close(sock);
        return -20;
    }

    int result = dchat_http_exchange(sock, request, req_len, response, 16384, timeout_ms);
    close(sock);

    if (result < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Message fetch failed (%d)", result);
        free(response);
        return result;
    }

    const char *body = dchat_http_body(response);
    if (!body) {
        strcpy(data->error_message, "Invalid response");
        free(response);
        return -7;
    }

    /* Parse messages from Discross HTML.
     * Discross renders messages in <table> elements:
     *   <th>Username</th> ... message content in subsequent <td> elements
     * The structure is: <table><tr><th rowspan=N>Username</th></tr>
     *   <tr><td>message text</td></tr>
     *   ...
     * </table>
     *
     * We look for <th> tags for usernames and nearby content for messages.
     */
    data->message_count = 0;
    const char *pos = body;
    char current_user[DCHAT_MAX_NAME_LEN] = "";

    while (data->message_count < DCHAT_MAX_MESSAGES) {
        /* Look for either <th (username header) or message content after a username */
        const char *th_tag = strstr(pos, "<th");
        const char *td_tag = strstr(pos, "<td");

        /* If we find a <th> before the next <td>, extract username */
        if (th_tag && (!td_tag || th_tag < td_tag)) {
            const char *th_close = strchr(th_tag, '>');
            if (!th_close) break;
            th_close++;

            /* Extract username text (between > and </th>) */
            const char *th_end = strstr(th_close, "</th>");
            if (!th_end) break;

            int len = (int)(th_end - th_close);
            if (len > 0 && len < DCHAT_MAX_NAME_LEN) {
                memcpy(current_user, th_close, len);
                current_user[len] = '\0';
                strip_html_tags(current_user);
                html_decode_inplace(current_user);
                /* Trim whitespace */
                while (current_user[0] == ' ' || current_user[0] == '\n') {
                    memmove(current_user, current_user + 1, strlen(current_user));
                }
                int ulen = strlen(current_user);
                while (ulen > 0 && (current_user[ulen - 1] == ' ' || current_user[ulen - 1] == '\n')) {
                    current_user[--ulen] = '\0';
                }
            }

            pos = th_end + 5;
            continue;
        }

        /* Extract message from <td> */
        if (td_tag) {
            const char *td_close = strchr(td_tag, '>');
            if (!td_close) break;
            td_close++;

            /* Find end of <td> content */
            const char *td_end = strstr(td_close, "</td>");
            if (!td_end) break;

            int len = (int)(td_end - td_close);
            if (len > 0 && current_user[0] != '\0' && len < DCHAT_MAX_CONTENT_LEN) {
                dchat_message_t *msg = &data->messages[data->message_count];

                strncpy(msg->username, current_user, DCHAT_MAX_NAME_LEN - 1);
                msg->username[DCHAT_MAX_NAME_LEN - 1] = '\0';

                /* Copy content, strip HTML tags, decode entities */
                int copy_len = (len < DCHAT_MAX_CONTENT_LEN - 1) ? len : DCHAT_MAX_CONTENT_LEN - 1;
                memcpy(msg->content, td_close, copy_len);
                msg->content[copy_len] = '\0';
                strip_html_tags(msg->content);
                html_decode_inplace(msg->content);

                /* Trim whitespace */
                char *c = msg->content;
                while (*c == ' ' || *c == '\n' || *c == '\r' || *c == '\t') {
                    memmove(c, c + 1, strlen(c));
                }
                int clen = strlen(msg->content);
                while (clen > 0 && (msg->content[clen - 1] == ' ' || msg->content[clen - 1] == '\n')) {
                    msg->content[--clen] = '\0';
                }

                /* Only count if there's actual content */
                if (msg->content[0] != '\0') {
                    data->message_count++;
                }
            }

            pos = td_end + 5;
            continue;
        }

        break;  /* No more <th> or <td> found */
    }

    data->messages_valid = true;
    free(response);
    printf("Discross: Parsed %d messages\n", data->message_count);
    return 0;

#else
    /* Stub data */
    strcpy(data->messages[0].username, "SonicFan99");
    strcpy(data->messages[0].content, "Anyone playing PSO tonight?");
    strcpy(data->messages[1].username, "DreamcastLive");
    strcpy(data->messages[1].content, "Server is up! 12 players online");
    strcpy(data->messages[2].username, "RetroGamer");
    strcpy(data->messages[2].content, "Just got my BBA working");
    data->message_count = 3;
    data->messages_valid = true;
    return 0;
#endif
}

int dchat_send_message(dchat_data_t *data, const char *channel_id,
                       const char *message, uint32_t timeout_ms) {
    if (!data || !data->logged_in || !channel_id || !message || message[0] == '\0') return -1;

#ifdef _arch_dreamcast
    int sock = dchat_connect(data->host, data->port);
    if (sock < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Connection failed");
        return sock;
    }

    /* URL-encode the message */
    char enc_msg[512];
    url_encode(message, enc_msg, sizeof(enc_msg));

    /* Discross uses GET /send?message=...&channel=... */
    char request[1024];
    int req_len = snprintf(request, sizeof(request),
        "GET /send?message=%s&channel=%s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Cookie: sessionID=%s\r\n"
        "User-Agent: openMenu-Dreamcast/1.2-discross\r\n"
        "Connection: close\r\n"
        "\r\n",
        enc_msg, channel_id, data->host, data->port, data->session_id);

    char response[2048];
    int result = dchat_http_exchange(sock, request, req_len, response, sizeof(response), timeout_ms);
    close(sock);

    if (result < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Send failed (%d)", result);
        return result;
    }

    /* Discross returns 302 redirect on success */
    int status = dchat_http_status(response);
    if (status == 302 || status == 303 || status == 200) {
        printf("Discross: Message sent OK (HTTP %d)\n", status);
        return 0;
    }

    snprintf(data->error_message, sizeof(data->error_message),
            "Send failed (HTTP %d)", status);
    return -10;

#else
    printf("Discross: [STUB] Would send to %s: %s\n", channel_id, message);
    return 0;
#endif
}

bool dchat_network_available(void) {
#ifdef _arch_dreamcast
    return (net_default_dev != NULL);
#else
    return true;  /* Always available for stub/testing */
#endif
}

void dchat_shutdown(dchat_data_t *data) {
    if (data) {
        memset(data->session_id, 0, sizeof(data->session_id));
        data->logged_in = false;
    }
}
