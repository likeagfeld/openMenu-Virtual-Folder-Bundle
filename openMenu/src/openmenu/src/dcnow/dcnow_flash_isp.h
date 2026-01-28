#ifndef DCNOW_FLASH_ISP_H
#define DCNOW_FLASH_ISP_H

#include <stdbool.h>

#define MAX_ISP_CONFIGS 5
#define MAX_ISP_NAME_LEN 32
#define MAX_PHONE_LEN 32
#define MAX_USERNAME_LEN 32
#define MAX_PASSWORD_LEN 32

typedef struct {
    char name[MAX_ISP_NAME_LEN];        /* ISP display name */
    char phone[MAX_PHONE_LEN];          /* Phone number to dial */
    char username[MAX_USERNAME_LEN];     /* Login username */
    char password[MAX_PASSWORD_LEN];     /* Login password */
    bool valid;                          /* Is this config valid/readable */
} isp_config_t;

/* Read ISP configurations from Dreamcast flash memory */
int dcnow_flash_read_isps(isp_config_t* configs, int max_configs);

/* Get a specific ISP config by index */
bool dcnow_flash_get_isp(int index, isp_config_t* config);

/* Get number of available ISP configurations */
int dcnow_flash_get_isp_count(void);

#endif /* DCNOW_FLASH_ISP_H */
