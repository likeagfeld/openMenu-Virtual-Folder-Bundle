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
 *   GET  /channels/{id}    - HTML message history (parse message divs)
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
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

/* ========================================================================
 * Internal HTTP helpers
 * ======================================================================== */

#ifdef _arch_dreamcast

/* Cached DNS resolution to avoid repeated lookups over slow modem link.
 * gethostbyname() sends a UDP packet each time - on a 33.6k modem this
 * can easily fail if the link is busy with TCP data from a previous request. */
static struct in_addr dchat_cached_addr;
static char dchat_cached_host[DCHAT_MAX_HOST_LEN];
static bool dchat_dns_cached = false;

/**
 * Gracefully close a socket. Sends SHUT_RDWR to signal the remote end,
 * then closes. This helps KOS's limited TCP stack release the socket
 * cleanly instead of sending RST, which avoids socket table exhaustion.
 */
static void dchat_close_socket(int sock) {
    shutdown(sock, SHUT_RDWR);
    close(sock);
}

/**
 * Open a TCP connection to the Discross server.
 * Caches DNS resolution after first successful lookup.
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
        printf("Discross: Socket creation failed\n");
        return -2;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    /* Use cached DNS if available and host hasn't changed */
    if (dchat_dns_cached && strcmp(host, dchat_cached_host) == 0) {
        memcpy(&addr.sin_addr, &dchat_cached_addr, sizeof(addr.sin_addr));
    } else {
        struct hostent *he = gethostbyname(host);
        if (!he) {
            printf("Discross: DNS lookup failed for %s\n", host);
            close(sock);
            return -3;
        }
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
        /* Cache the result */
        memcpy(&dchat_cached_addr, he->h_addr, he->h_length);
        strncpy(dchat_cached_host, host, DCHAT_MAX_HOST_LEN - 1);
        dchat_cached_host[DCHAT_MAX_HOST_LEN - 1] = '\0';
        dchat_dns_cached = true;
        printf("Discross: DNS resolved %s, cached\n", host);
    }

    printf("Discross: Connecting to %s:%d...\n", host, port);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("Discross: TCP connect failed\n");
        close(sock);
        return -4;
    }

    return sock;
}

/**
 * Send a full HTTP request and receive the response.
 * If the response is larger than the buffer, drains remaining data
 * before returning to ensure clean TCP close (avoids RST on close
 * which can cause socket exhaustion on KOS).
 * @return total bytes stored in response on success, negative on error
 */
static int dchat_http_exchange(int sock, const char *request, int req_len,
                                char *response, int buf_size, uint32_t timeout_ms) {
#ifdef _arch_dreamcast
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    int sent = send(sock, request, req_len, 0);
    if (sent <= 0) {
        printf("Discross: Send failed\n");
        return -5;
    }

    uint64_t start = timer_ms_gettime64();
    int total = 0;
    bool buffer_full = false;
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
    buffer_full = (total >= buf_size - 1);

    /* If buffer filled up, the server may still be sending data.
     * Drain remaining data to allow clean TCP close instead of RST.
     * This prevents socket table exhaustion on KOS's limited TCP stack. */
    if (buffer_full) {
        char drain[1024];
        uint64_t drain_start = timer_ms_gettime64();
        int drained = 0;
        while (timer_ms_gettime64() - drain_start < 3000) {  /* 3s drain timeout */
            int n = recv(sock, drain, sizeof(drain), 0);
            if (n <= 0) break;
            drained += n;
            drain_start = timer_ms_gettime64();  /* reset on data */
            thd_pass();
        }
        if (drained > 0)
            printf("Discross: Drained %d extra bytes\n", drained);
    }

    return total;
}

/**
 * Send a HTTP request and read only the response headers.
 * Returns bytes stored in response (header-only), negative on error.
 */
static int dchat_http_exchange_headers(int sock, const char *request, int req_len,
                                       char *response, int buf_size, uint32_t timeout_ms) {
#ifdef _arch_dreamcast
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    int sent = send(sock, request, req_len, 0);
    if (sent <= 0) {
        printf("Discross: Send failed\n");
        return -5;
    }

    uint64_t start = timer_ms_gettime64();
    int total = 0;
    bool timed_out = false;

    while (total < buf_size - 1) {
        if (timer_ms_gettime64() - start > timeout_ms) {
            timed_out = true;
            break;
        }

        int n = recv(sock, response + total, buf_size - total - 1, 0);
        if (n > 0) {
            total += n;
            response[total] = '\0';
            start = timer_ms_gettime64();
            if (strstr(response, "\r\n\r\n")) {
                break;
            }
        } else if (n == 0) {
            break;
        } else {
            if (total == 0) return -6;
            break;
        }
        thd_pass();
    }

    response[total] = '\0';
    if (timed_out && total == 0) return -7;
    return total;
}

/**
 * Like dchat_http_exchange but skips data until a marker string is found.
 * This is critical for message pages where the <head> section is 30-40KB
 * of CSS/JS that would waste our limited buffer. By skipping to <body> or
 * the first message marker, we use the buffer for actual message content.
 *
 * @param skip_to    Marker string to search for (e.g. "<body")
 * @return total bytes stored in response (starting from marker), negative on error
 */
static int dchat_http_exchange_skip(int sock, const char *request, int req_len,
                                     char *response, int buf_size, uint32_t timeout_ms,
                                     const char *skip_to) {
#ifdef _arch_dreamcast
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    int sent = send(sock, request, req_len, 0);
    if (sent <= 0) {
        printf("Discross: Send failed\n");
        return -5;
    }

    uint64_t start = timer_ms_gettime64();
    int total = 0;
    bool found_marker = false;
    int skip_len = (int)strlen(skip_to);

    /* Phase 1: Read and discard until we find the marker string.
     * Use a small rolling window to detect the marker across chunk boundaries. */
    char skip_buf[2048];
    int skip_buf_len = 0;
    int total_skipped = 0;

    while (!found_marker) {
        if (timer_ms_gettime64() - start > timeout_ms) {
            printf("Discross: Timeout before finding marker '%s' (skipped %d bytes)\n",
                   skip_to, total_skipped);
            response[0] = '\0';
            return -6;
        }

        int n = recv(sock, skip_buf + skip_buf_len, (int)sizeof(skip_buf) - skip_buf_len - 1, 0);
        if (n > 0) {
            skip_buf_len += n;
            skip_buf[skip_buf_len] = '\0';
            start = timer_ms_gettime64();

            /* Search for marker in the accumulated skip buffer */
            char *marker = strstr(skip_buf, skip_to);
            if (marker) {
                /* Found! Copy everything from the marker into the response buffer */
                int remaining = skip_buf_len - (int)(marker - skip_buf);
                if (remaining > buf_size - 1) remaining = buf_size - 1;
                memcpy(response, marker, remaining);
                total = remaining;
                found_marker = true;
                total_skipped += (int)(marker - skip_buf);
                printf("Discross: Skipped %d bytes of head, found marker\n", total_skipped);
            } else {
                /* Not found yet - keep last (skip_len-1) chars in case marker spans chunks */
                int keep = skip_len - 1;
                if (keep > skip_buf_len) keep = skip_buf_len;
                total_skipped += skip_buf_len - keep;
                if (keep > 0) memmove(skip_buf, skip_buf + skip_buf_len - keep, keep);
                skip_buf_len = keep;
            }
        } else if (n == 0) {
            printf("Discross: Connection closed before marker found\n");
            response[0] = '\0';
            return -6;
        } else {
            printf("Discross: Recv error before marker found\n");
            response[0] = '\0';
            return -6;
        }
        thd_pass();
    }

    /* Phase 2: Fill remaining buffer space */
    while (total < buf_size - 1) {
        if (timer_ms_gettime64() - start > timeout_ms) break;

        int n = recv(sock, response + total, buf_size - total - 1, 0);
        if (n > 0) {
            total += n;
            start = timer_ms_gettime64();
        } else if (n == 0) {
            break;
        } else {
            break;
        }
        thd_pass();
    }

    response[total] = '\0';

    /* Drain any remaining data for clean close */
    if (total >= buf_size - 1) {
        char drain[1024];
        uint64_t drain_start = timer_ms_gettime64();
        int drained = 0;
        while (timer_ms_gettime64() - drain_start < 3000) {
            int n = recv(sock, drain, sizeof(drain), 0);
            if (n <= 0) break;
            drained += n;
            drain_start = timer_ms_gettime64();
            thd_pass();
        }
        if (drained > 0)
            printf("Discross: Drained %d extra bytes\n", drained);
    }

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

static bool dchat_response_requires_login(const char *response) {
    if (!response) return false;
    if (strstr(response, "Location: /login") ||
        strstr(response, "Location: /login/") ||
        strstr(response, "location: /login")) {
        return true;
    }
    const char *body = dchat_http_body(response);
    if (body && (strstr(body, "login") || strstr(body, "Login"))) {
        return true;
    }
    return false;
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
 * Decode basic HTML entities (&amp; &lt; &gt; &quot; &#39; &nbsp;).
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
            else if (strncmp(r, "&nbsp;", 6) == 0) { *w++ = ' '; r += 6; }
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

/**
 * Strip HTML tags from message content with special handling for media.
 * <img ...> becomes "[img]", <br> variants become spaces,
 * all other tags are removed. Works in-place.
 */
static void strip_html_tags_content(char *str) {
    char *r = str, *w = str;
    while (*r) {
        if (*r == '<') {
            if (strncmp(r + 1, "img", 3) == 0 && (r[4] == ' ' || r[4] == '/' || r[4] == '>')) {
                /* <img ...> -> [img] (5 chars, always <= tag length) */
                while (*r && *r != '>') r++;
                if (*r) r++;
                memcpy(w, "[img]", 5); w += 5;
                continue;
            }
            if (strncmp(r + 1, "br", 2) == 0 && (r[3] == ' ' || r[3] == '/' || r[3] == '>')) {
                /* <br>, <br/>, <br /> -> space */
                while (*r && *r != '>') r++;
                if (*r) r++;
                *w++ = ' ';
                continue;
            }
            /* Skip all other tags */
            while (*r && *r != '>') r++;
            if (*r) r++;
            continue;
        }
        *w++ = *r++;
    }
    *w = '\0';
}

/**
 * Find the closing </div> that matches a div starting at 'start'
 * (pointer to just after the opening '>').
 * Handles nested <div>...</div> pairs.
 * Returns pointer to the matching </div> or NULL.
 */
static const char *find_matching_div_close(const char *start) {
    const char *p = start;
    int depth = 1;
    while (*p && depth > 0) {
        if (strncmp(p, "</div>", 6) == 0) {
            depth--;
            if (depth == 0) return p;
            p += 6;
        } else if (strncmp(p, "<div", 4) == 0 &&
                   (p[4] == ' ' || p[4] == '>' || p[4] == '\t' || p[4] == '\n')) {
            depth++;
            p++;
        } else {
            p++;
        }
    }
    return NULL;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void dchat_init(dchat_data_t *data) {
    if (!data) return;
    memset(data, 0, sizeof(dchat_data_t));
    data->port = DCHAT_DEFAULT_PORT;
}

void dchat_set_config(dchat_data_t *data, const char *host, int port,
                      const char *username, const char *password) {
    if (!data) return;

    if (host && host[0]) {
        strncpy(data->host, host, DCHAT_MAX_HOST_LEN - 1);
        data->host[DCHAT_MAX_HOST_LEN - 1] = '\0';
    } else {
        strncpy(data->host, "discross.net", DCHAT_MAX_HOST_LEN - 1);
        data->host[DCHAT_MAX_HOST_LEN - 1] = '\0';
    }

    data->port = (port > 0) ? port : DCHAT_DEFAULT_PORT;

    if (username) {
        strncpy(data->username, username, DCHAT_MAX_CRED_LEN - 1);
        data->username[DCHAT_MAX_CRED_LEN - 1] = '\0';
    }
    if (password) {
        strncpy(data->password, password, DCHAT_MAX_CRED_LEN - 1);
        data->password[DCHAT_MAX_CRED_LEN - 1] = '\0';
    }

    data->config_valid = (data->host[0] && data->username[0] && data->password[0]);

    printf("Discross: Config set - host=%s port=%d user=%s valid=%d\n",
           data->host, data->port, data->username, data->config_valid);
}

int dchat_login(dchat_data_t *data, uint32_t timeout_ms) {
    if (!data || !data->config_valid) return -1;

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
    dchat_close_socket(sock);

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
    dchat_close_socket(sock);

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
        href += 8;  /* skip href="./ (8 chars: h,r,e,f,=,",.,/) */

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
    data->current_server_id[DCHAT_MAX_ID_LEN - 1] = '\0';

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

    /* Server page includes server icons + channel list; can be large */
    char *response = (char *)malloc(16384);
    if (!response) {
        strcpy(data->error_message, "Out of memory");
        dchat_close_socket(sock);
        return -20;
    }
    int result = dchat_http_exchange(sock, request, req_len, response, 16384, timeout_ms);
    dchat_close_socket(sock);

    if (result < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Channel list failed (%d)", result);
        free(response);
        return result;
    }

    const char *body = dchat_http_body(response);
    if (!body) {
        strcpy(data->error_message, "Invalid response");
        free(response);
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

        /* Extract channel name: get all content between <a> opening and </a>,
         * strip HTML tags (handles nested <font> etc.), decode entities.
         * Use a temp buffer since raw HTML with <font> tags can be 100+ bytes
         * but DCHAT_MAX_NAME_LEN is small. Must strip tags first, then copy. */
        const char *a_open_end = strchr(channels_str, '>');
        const char *a_close = strstr(channels_str, "</a>");
        if (a_open_end && a_close && a_open_end < a_close) {
            a_open_end++;
            int raw_len = (int)(a_close - a_open_end);
            char name_buf[256];
            if (raw_len > (int)sizeof(name_buf) - 1) raw_len = sizeof(name_buf) - 1;
            memcpy(name_buf, a_open_end, raw_len);
            name_buf[raw_len] = '\0';
            strip_html_tags(name_buf);
            html_decode_inplace(name_buf);

            /* Trim leading # and whitespace */
            char *trimmed = name_buf;
            while (*trimmed == '#' || *trimmed == ' ' || *trimmed == '\n' || *trimmed == '\r') trimmed++;
            /* Trim trailing whitespace */
            int ni = strlen(trimmed);
            while (ni > 0 && (trimmed[ni - 1] == ' ' || trimmed[ni - 1] == '\n'))
                trimmed[--ni] = '\0';

            strncpy(data->channels[data->channel_count].name, trimmed, DCHAT_MAX_NAME_LEN - 1);
            data->channels[data->channel_count].name[DCHAT_MAX_NAME_LEN - 1] = '\0';
        }

        /* Fallback name */
        if (data->channels[data->channel_count].name[0] == '\0') {
            snprintf(data->channels[data->channel_count].name, DCHAT_MAX_NAME_LEN,
                    "channel-%s", id_buf);
        }

        printf("Discross: Channel [%d] %s = %s\n", data->channel_count,
               data->channels[data->channel_count].id,
               data->channels[data->channel_count].name);

        data->channel_count++;
        pos = channels_str + i;
    }

    printf("Discross: Found %d channels\n", data->channel_count);
    free(response);
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
    data->current_channel_id[DCHAT_MAX_ID_LEN - 1] = '\0';

#ifdef _arch_dreamcast
    int sock = dchat_connect(data->host, data->port);
    if (sock < 0) {
        const char *reason = "unknown";
        if (sock == -1) reason = "no network";
        else if (sock == -2) reason = "socket create";
        else if (sock == -3) reason = "DNS lookup";
        else if (sock == -4) reason = "TCP connect";
        snprintf(data->error_message, sizeof(data->error_message),
                "Connection failed: %s", reason);
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

    /* Messages page has a massive <head> section (30-40KB of CSS/JS/fonts)
     * before any message content. We skip to <body> to avoid wasting buffer.
     *
     * Discross returns up to 100 messages oldest-first. With larsenv's inline
     * styles each message is ~600 bytes, so 100 messages = ~60KB of HTML.
     * Plus navigation/forms/scripts around the messages. Use 256KB buffer
     * (temporary, freed after parsing) to ensure we capture ALL messages
     * including the newest at the end. */
    const int resp_size = 262144;  /* 256KB after head skip */
    char *response = (char *)malloc(resp_size);
    if (!response) {
        strcpy(data->error_message, "Out of memory");
        dchat_close_socket(sock);
        return -20;
    }

    /* Skip past the <head> section - search for <body to start buffering.
     * Use generous timeout since modem connections are slow (~4KB/s).
     * The timeout resets on each received chunk so it's really an inactivity
     * timeout, not a total transfer timeout. */
    uint32_t msg_timeout = timeout_ms < 15000 ? 15000 : timeout_ms;
    int result = dchat_http_exchange_skip(sock, request, req_len,
                                          response, resp_size, msg_timeout, "<body");
    dchat_close_socket(sock);

    if (result < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Message fetch failed (%d)", result);
        free(response);
        return result;
    }

    /* Response starts at "<body..." since we skipped past HTTP headers and <head>.
     * No need to call dchat_http_body() - use response directly. */
    const char *body = response;
    printf("Discross: Message response: %d bytes (starting from <body)\n", result);

    /* Parse messages from Discross HTML.
     *
     * Anchored on "messagecontent" divs which are unique to top-level messages
     * in both forks. This avoids the inner class="message" collision where
     * merged message spans/divs also have class="message".
     *
     * For each messagecontent div found:
     * 1. Look backwards (up to 500 chars) for the username
     *    - class="name" (Heath123: <font class="name">USER</font>)
     *    - First <span> after message block start (larsenv)
     * 2. Extract content using div-depth matching (handles nested divs from
     *    merged messages in larsenv fork)
     * 3. Convert <img> to [img], <br> to spaces, strip other tags
     * 4. Keep ALL messages including image-only ones
     *
     * Uses circular buffer to keep the last DCHAT_MAX_MESSAGES.
     */
    data->message_count = 0;
    int total_parsed = 0;
    const char *pos = body;
    char last_username[DCHAT_MAX_NAME_LEN];
    last_username[0] = '\0';

    while (1) {
        /* Find next messagecontent div (unique to top-level messages) */
        const char *mc = strstr(pos, "messagecontent");
        if (!mc) break;

        /* Find opening > of the messagecontent tag */
        const char *content_start = strchr(mc, '>');
        if (!content_start) break;
        content_start++;

        /* Find matching </div> using depth counting (handles nested divs
         * from merged messages in larsenv fork) */
        const char *content_end = find_matching_div_close(content_start);
        if (!content_end) break;

        /* --- Extract username ---
         * Search backwards from messagecontent for the username.
         * Larsenv template has long inline styles, so we need ~800 chars lookback.
         * In Heath123: <font class="name" ...>USER</font>.
         * In larsenv: first <span ...>USER</span> after <div class="message". */
        int slot = total_parsed % DCHAT_MAX_MESSAGES;
        dchat_message_t *msg = &data->messages[slot];
        msg->username[0] = '\0';
        msg->content[0] = '\0';

        const char *search_back = (mc - 1000 > pos) ? mc - 1000 : pos;
        const char *name_start = NULL;
        const char *name_end = NULL;

        /* Try class="name" first (Heath123) - find the LAST one before mc */
        {
            const char *temp = search_back;
            const char *last_name_class = NULL;
            while (temp < mc) {
                const char *found = strstr(temp, "class=\"name\"");
                if (found && found < mc) {
                    last_name_class = found;
                    temp = found + 12;
                } else break;
            }
            if (last_name_class) {
                const char *tag_end = strchr(last_name_class, '>');
                if (tag_end && tag_end < mc) {
                    name_start = tag_end + 1;
                    name_end = strstr(name_start, "</font>");
                    if (!name_end || name_end > mc) {
                        name_end = strstr(name_start, "</span>");
                    }
                    if (name_end && name_end > mc) name_end = NULL;
                }
            }
        }

        /* Fallback: find username span in larsenv template.
         * Structure: <div class="message"><div style="display:flex;">
         *   <span onclick="..." style="font-weight:600;...">USERNAME</span>
         *   <span style="font-size:12px;...">TIMESTAMP</span>
         * </div>...
         * The username span has onclick= or font-weight. The timestamp span
         * has font-size:12px. We look for onclick= first as a reliable marker
         * of the username span, falling back to first <span> if needed. */
        if (!name_start || !name_end) {
            const char *temp = search_back;
            const char *msg_block = search_back;
            while (temp < mc) {
                const char *found = strstr(temp, "<div class=\"message\"");
                if (found && found < mc) {
                    msg_block = found;
                    temp = found + 20;
                } else break;
            }

            /* Try onclick= span first (larsenv username span) */
            const char *onclick_tag = strstr(msg_block, "onclick=");
            if (onclick_tag && onclick_tag < mc) {
                /* Found onclick attribute - back up to find the <span opening */
                const char *span_start = onclick_tag;
                while (span_start > msg_block && strncmp(span_start, "<span", 5) != 0)
                    span_start--;
                if (strncmp(span_start, "<span", 5) == 0) {
                    const char *span_close = strchr(onclick_tag, '>');
                    if (span_close && span_close < mc) {
                        name_start = span_close + 1;
                        name_end = strstr(name_start, "</span>");
                        if (name_end && name_end > mc) name_end = NULL;
                    }
                }
            }

            /* If onclick didn't work, try font-weight span */
            if (!name_start || !name_end) {
                const char *fw_tag = strstr(msg_block, "font-weight");
                if (fw_tag && fw_tag < mc) {
                    const char *span_start = fw_tag;
                    while (span_start > msg_block && strncmp(span_start, "<span", 5) != 0)
                        span_start--;
                    if (strncmp(span_start, "<span", 5) == 0) {
                        const char *span_close = strchr(fw_tag, '>');
                        if (span_close && span_close < mc) {
                            name_start = span_close + 1;
                            name_end = strstr(name_start, "</span>");
                            if (name_end && name_end > mc) name_end = NULL;
                        }
                    }
                }
            }

            /* Last resort: first <span> after message block, but skip if it
             * looks like a timestamp (contains AM/PM or digits at start) */
            if (!name_start || !name_end) {
                const char *span_tag = strstr(msg_block, "<span");
                if (span_tag && span_tag < mc) {
                    const char *span_close = strchr(span_tag, '>');
                    if (span_close && span_close < mc) {
                        const char *candidate = span_close + 1;
                        const char *candidate_end = strstr(candidate, "</span>");
                        if (candidate_end && candidate_end < mc) {
                            /* Check if this looks like a timestamp (starts with digit) */
                            const char *c = candidate;
                            while (*c == ' ' || *c == '\n') c++;
                            if (*c >= '0' && *c <= '9') {
                                /* Probably timestamp - try next span */
                                const char *next_span = strstr(candidate_end + 7, "<span");
                                if (next_span && next_span < mc) {
                                    /* skip this, don't use timestamp as username */
                                } else {
                                    name_start = candidate;
                                    name_end = candidate_end;
                                }
                            } else {
                                name_start = candidate;
                                name_end = candidate_end;
                            }
                        }
                    }
                }
            }
        }

        /* Extract and clean username */
        if (name_start && name_end && name_end > name_start) {
            int ulen = (int)(name_end - name_start);
            char ubuf[256];
            if (ulen > (int)sizeof(ubuf) - 1) ulen = (int)sizeof(ubuf) - 1;
            memcpy(ubuf, name_start, ulen);
            ubuf[ulen] = '\0';
            strip_html_tags(ubuf);
            html_decode_inplace(ubuf);
            char *u = ubuf;
            while (*u == ' ' || *u == '\n' || *u == '\r') u++;
            ulen = strlen(u);
            while (ulen > 0 && (u[ulen - 1] == ' ' || u[ulen - 1] == '\n'))
                u[--ulen] = '\0';
            if (u[0] != '\0') {
                strncpy(msg->username, u, DCHAT_MAX_NAME_LEN - 1);
                msg->username[DCHAT_MAX_NAME_LEN - 1] = '\0';
                /* Remember for merged messages that lack a username header */
                strncpy(last_username, msg->username, DCHAT_MAX_NAME_LEN - 1);
            }
        }

        if (msg->username[0] == '\0') {
            /* No username found - use last seen username (merged message) */
            if (last_username[0] != '\0') {
                strncpy(msg->username, last_username, DCHAT_MAX_NAME_LEN - 1);
            } else {
                strcpy(msg->username, "???");
            }
        }

        /* --- Extract message content ---
         * Content may contain nested divs (larsenv merged messages),
         * <font> wrappers (Heath123), <img> tags, and <a> links.
         * Use enhanced strip that converts img->[img], br->space. */
        {
            int clen = (int)(content_end - content_start);
            char cbuf[1024];
            if (clen > (int)sizeof(cbuf) - 1) clen = (int)sizeof(cbuf) - 1;
            memcpy(cbuf, content_start, clen);
            cbuf[clen] = '\0';
            strip_html_tags_content(cbuf);  /* img->[img], br->space */
            html_decode_inplace(cbuf);
            char *c = cbuf;
            while (*c == ' ' || *c == '\n' || *c == '\r' || *c == '\t') c++;
            clen = strlen(c);
            while (clen > 0 && (c[clen - 1] == ' ' || c[clen - 1] == '\n'))
                c[--clen] = '\0';

            if (c[0] == '\0') {
                /* No text content after stripping - likely pure media */
                strcpy(msg->content, "[media]");
            } else {
                strncpy(msg->content, c, DCHAT_MAX_CONTENT_LEN - 1);
                msg->content[DCHAT_MAX_CONTENT_LEN - 1] = '\0';
            }
        }

        total_parsed++;
        pos = content_end + 6;  /* past </div> */
    }

    /* Set final count */
    data->message_count = (total_parsed < DCHAT_MAX_MESSAGES)
                          ? total_parsed : DCHAT_MAX_MESSAGES;

    /* If we wrapped around the circular buffer, reorder so messages[0] is oldest */
    if (total_parsed > DCHAT_MAX_MESSAGES) {
        dchat_message_t temp[DCHAT_MAX_MESSAGES];
        int start = total_parsed % DCHAT_MAX_MESSAGES;
        for (int i = 0; i < data->message_count; i++) {
            memcpy(&temp[i], &data->messages[(start + i) % DCHAT_MAX_MESSAGES],
                   sizeof(dchat_message_t));
        }
        memcpy(data->messages, temp, sizeof(dchat_message_t) * data->message_count);
    }

    data->messages_valid = true;
    free(response);
    printf("Discross: Parsed %d messages (kept last %d)\n",
           total_parsed, data->message_count);
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
    /* URL-encode the message */
    char enc_msg[512];
    url_encode(message, enc_msg, sizeof(enc_msg));
    char enc_channel[DCHAT_MAX_ID_LEN * 3];
    url_encode(channel_id, enc_channel, sizeof(enc_channel));

    int sock = dchat_connect(data->host, data->port);
    if (sock < 0) {
        snprintf(data->error_message, sizeof(data->error_message),
                "Connection failed");
        return sock;
    }

    /* Discross fork accepts channel or channel_id in query */
    char request[1024];
    int req_len = snprintf(request, sizeof(request),
        "GET /send?message=%s&channel=%s&channel_id=%s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Cookie: sessionID=%s\r\n"
        "Referer: /channels/%s\r\n"
        "User-Agent: openMenu-Dreamcast/1.2-discross\r\n"
        "Connection: close\r\n"
        "\r\n",
        enc_msg, enc_channel, enc_channel,
        data->host, data->port, data->session_id, enc_channel);

    char response[4096];
    int result = dchat_http_exchange_headers(sock, request, req_len, response, sizeof(response), timeout_ms);
    dchat_close_socket(sock);

    if (result < 0) {
        if (result == -7) {
            printf("Discross: No response headers (timeout), assuming send OK\n");
            return 0;
        }
        snprintf(data->error_message, sizeof(data->error_message),
                "Send failed (%d)", result);
        return result;
    }

    /* Discross returns 302 redirect on success */
    int status = dchat_http_status(response);
    if (dchat_response_requires_login(response)) {
        strcpy(data->error_message, "Session expired - re-login needed");
        data->logged_in = false;
        return -10;
    }

    if (status == 302 || status == 303 || status == 200 || status == 204) {
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
#ifdef _arch_dreamcast
    dchat_dns_cached = false;
#endif
}
