#include "dcnow_flash_isp.h"
#include <stdio.h>
#include <string.h>

#ifdef _arch_dreamcast
#include <dc/flashrom.h>
#include <kos.h>

/* KOS flash ISP configuration structure */
typedef struct {
    uint8_t method;
    uint8_t valid;
    uint8_t flags;
    uint8_t ip_type;
    char name[32];
    char phone[24];
    char username[32];
    char password[32];
    /* Additional fields exist but we only need the above */
} flash_isp_t;

#endif

static isp_config_t cached_isps[MAX_ISP_CONFIGS];
static int cached_count = -1;  /* -1 means not yet loaded */

int dcnow_flash_read_isps(isp_config_t* configs, int max_configs) {
#ifdef _arch_dreamcast
    if (!configs || max_configs <= 0) {
        return 0;
    }

    printf("DC Now: Reading ISP configurations from flash...\n");

    int count = 0;

    /* ISP configs are stored in partition 2 (FLASHROM_PT_BLOCK_1) */
    /* Each ISP config is 0xC0 (192) bytes */
    for (int i = 0; i < max_configs && i < MAX_ISP_CONFIGS; i++) {
        flash_isp_t isp_data;

        /* Calculate offset for this ISP slot */
        /* ISP configs start at offset 0x1A056 in syscfg partition */
        /* Each config is 0xC0 bytes apart */
        int offset = 0x1A056 + (i * 0xC0);

        /* Read directly from flashrom syscfg */
        int result = flashrom_read(offset, &isp_data, sizeof(flash_isp_t));

        if (result < 0) {
            printf("DC Now: ISP slot %d: read failed (error %d)\n", i, result);
            configs[i].valid = false;
            continue;
        }

        /* Check if this slot is valid (valid flag set) */
        if (isp_data.valid == 0 || isp_data.valid == 0xFF) {
            printf("DC Now: ISP slot %d: not valid (flag=0x%02x)\n", i, isp_data.valid);
            configs[i].valid = false;
            continue;
        }

        /* Check if name is present */
        if (isp_data.name[0] == 0 || isp_data.name[0] == 0xFF) {
            printf("DC Now: ISP slot %d: no name\n", i);
            configs[i].valid = false;
            continue;
        }

        /* Copy ISP name */
        strncpy(configs[i].name, isp_data.name, MAX_ISP_NAME_LEN - 1);
        configs[i].name[MAX_ISP_NAME_LEN - 1] = '\0';

        /* Copy phone number and sanitize */
        strncpy(configs[i].phone, isp_data.phone, MAX_PHONE_LEN - 1);
        configs[i].phone[MAX_PHONE_LEN - 1] = '\0';
        /* Remove any non-printable or garbage characters */
        for (int j = 0; configs[i].phone[j]; j++) {
            if (configs[i].phone[j] < 32 || configs[i].phone[j] > 126) {
                configs[i].phone[j] = '\0';
                break;
            }
        }

        /* Copy username and sanitize */
        strncpy(configs[i].username, isp_data.username, MAX_USERNAME_LEN - 1);
        configs[i].username[MAX_USERNAME_LEN - 1] = '\0';
        for (int j = 0; configs[i].username[j]; j++) {
            if (configs[i].username[j] < 32 || configs[i].username[j] > 126) {
                configs[i].username[j] = '\0';
                break;
            }
        }

        /* Copy password and sanitize */
        strncpy(configs[i].password, isp_data.password, MAX_PASSWORD_LEN - 1);
        configs[i].password[MAX_PASSWORD_LEN - 1] = '\0';
        for (int j = 0; configs[i].password[j]; j++) {
            if (configs[i].password[j] < 32 || configs[i].password[j] > 126) {
                configs[i].password[j] = '\0';
                break;
            }
        }

        configs[i].valid = true;
        count++;

        printf("DC Now: ISP slot %d: '%s' (phone: %s, user: %s)\n",
               i, configs[i].name, configs[i].phone, configs[i].username);
    }

    /* Cache the results */
    memcpy(cached_isps, configs, sizeof(isp_config_t) * max_configs);
    cached_count = count;

    printf("DC Now: Found %d ISP configuration(s) in flash\n", count);
    return count;

#else
    /* Non-Dreamcast: no ISP configs available */
    printf("DC Now: Not running on Dreamcast - no flash ISP configs\n");
    cached_count = 0;
    return 0;
#endif
}

bool dcnow_flash_get_isp(int index, isp_config_t* config) {
    if (!config || index < 0 || index >= MAX_ISP_CONFIGS) {
        return false;
    }

    /* Load from flash if not cached */
    if (cached_count < 0) {
        dcnow_flash_read_isps(cached_isps, MAX_ISP_CONFIGS);
    }

    if (index >= cached_count) {
        return false;
    }

    if (!cached_isps[index].valid) {
        return false;
    }

    memcpy(config, &cached_isps[index], sizeof(isp_config_t));
    return true;
}

int dcnow_flash_get_isp_count(void) {
    /* Load from flash if not cached */
    if (cached_count < 0) {
        dcnow_flash_read_isps(cached_isps, MAX_ISP_CONFIGS);
    }

    return cached_count;
}
