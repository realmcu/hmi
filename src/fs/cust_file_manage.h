/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef ____CUST_FILE_MANAGE__H_
#define ____CUST_FILE_MANAGE__H_

#include "lv_img_buf.h"

typedef struct
{
    uint8_t *name_addr;
    uint8_t *file_addr;
    uint32_t file_sz;
    void *usr_data;         // lvgl img cf value
} cust_file_info_t;

typedef struct
{
    uint32_t file_num;
    uint32_t ver;
    uint32_t start_addr;
    uint32_t size;
    uint32_t rsv;
} cust_dir_t;


// list
struct cust_file_nd
{
    struct cust_file_nd *nxt;
    void *data;                     // lv_img_dsc_t *
    cust_file_info_t file_info;
};
typedef struct cust_file_nd cust_file_node_t;



/**
 * Init directory record from flash fs
 * @return    Returns a negative error code on failure.
 */
int cust_dir_init(void);

/**
 * Init all file nodes as lvgl image desc
 * @return    Returns a negative error code on failure.
 */
int cust_file_init_as_lvimg(void);

/**
 * Get file size by name
 * @param name   path to the file beginning with the driver letter (e.g. L:/image.rle)
 * @return       Returns 0 on failure.
 */
size_t cust_file_get_size(const char *name);

/**
 * Get lv_img_dsc_t pointer of image file by name
 * @param name   path to the file beginning with the driver letter (e.g. L:/image.rle)
 * @return       Returns NULL on failure.
 */
lv_img_dsc_t *cust_file_get_lvimg(const char *name);

/**
 * Delet image file by name
 * @param name   path to the file beginning with the driver letter (e.g. L:/image.rle)
 * @return       Returns a negative error code on failure.
 */
int cust_file_delete_lvimg(const char *name);

/**
 * Attempts to determine if there is sufficient space for a specified file.
 * @param file_name   path to the file beginning with the driver letter (e.g. L:/image.rle)
 * @param file_sz     The size of the file in bytes.
 * @return            boolean value to indicate if there is enough space available for the file
 */
bool cust_file_add_try(const char *file_name, size_t file_sz);

/**
 * Adds a image file and saves it to flash memory.
 *
 * This function attempts to add a image to the flash storage.
 * If an image with the same file name already exists, the function will not add it again
 * to avoid duplication.
 *
 * @param file_name   path to the file beginning with the driver letter (e.g. L:/image.rle)
 * @param buff        A buffer containing the file data.
 * @param file_sz     The size of the file in bytes.
 * @param usr_data    A pointer to user-defined data that will be saved alongside the
 *                    file, typically representing metadata such as color format.
 * @return            Returns a negative error code on failure.
 */
int cust_file_add_lvimg(const char *file_name, const uint8_t *buff, size_t file_sz, void *usr_data);


/**
 * Logs information about all files managed by the system.
 */
void cust_file_log(void);

#endif