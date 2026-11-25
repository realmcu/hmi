/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "rtk_fs.h"
#include "cust_file_manage.h"
#include "trace.h"


// DEFAULT
#define CUST_DIR_FILEPATH       "L:/file.cfg"   // config file
#define CUST_FILE_START_ADDR    (0x4700000)     // save filename and file
#define CUST_FILE_SIZE          (1024 * 100)    // file storage total

#define CUST_SECTOR_SIZE              ((uint32_t)4096)  // 4K
#define CUST_SECTOR_SIZE_SHIFT        ((uint32_t)12)    // 4K

#define CUST_CEIL_BY_SECTOR(x)   (uint32_t)((x + CUST_SECTOR_SIZE - 1) & (~(CUST_SECTOR_SIZE - 1)))

#define CUST_MAX_FILE_NUM       (256)


static cust_dir_t cust_dir;
static cust_file_node_t *cust_file_root;


// order list
static int cust_file_insert_node(cust_file_node_t *node)
{
    if (!cust_file_root)
    {
        cust_file_root = node;
        return 0;
    }
    else
    {
        cust_file_node_t *cur = cust_file_root;
        cust_file_node_t *nxt = cust_file_root;
        while (nxt)
        {
            // addr increase
            if (nxt->file_info.name_addr > node->file_info.name_addr)
            {
                if (nxt == cust_file_root)
                {
                    cust_file_root = node;
                }
                else
                {
                    cur->nxt = node;
                }
                node->nxt = nxt;
                return 0;
            }
            cur = nxt;
            nxt = cur->nxt;
        }
        cur->nxt = node;
    }

    return 0;
}


static int cust_file_delete_node(const char *name)
{
    cust_file_node_t *cur = cust_file_root;
    cust_file_node_t *nxt = cust_file_root;

    while (nxt)
    {
        if (0 == strcmp(name, (const char *)nxt->file_info.name_addr))
        {
            cur->nxt = nxt->nxt;
            if (nxt == cust_file_root)
            {
                cust_file_root = cur->nxt;
            }
            free(nxt);
            return 0;
        }
        cur = nxt;
        nxt = nxt->nxt;
    }
    return -1;
}


// FLASH
#include "flash_nor_device.h"
// #include "drv_flash.h"
static int cust_file_flash_write(uint8_t *addr, const char *file_name, const uint8_t *buff,
                                 size_t file_sz)
{
    size_t name_sz = (((strlen(file_name) + 1) + 3) >> 2) << 2; // 4 Byte align
    size_t total_sz = name_sz + file_sz;
    uint8_t *name_buff = (uint8_t *)malloc(name_sz);
    uint32_t erase_num = CUST_CEIL_BY_SECTOR(total_sz) >> CUST_SECTOR_SIZE_SHIFT;
    uint8_t *pop = addr;
    uint32_t res = 0;

    memset((void *)name_buff, 0, name_sz);
    memcpy(name_buff, file_name, strlen(file_name));

    // flash erase
    // DBG_DIRECT("flash erase 0x%x -> 0x%x %d %d\n", addr, addr + total_sz, total_sz, erase_num);
    // realtek_flash_erase((uint32_t)addr, total_sz);
    for (uint32_t i = 0; i < erase_num; i++)
    {
        // flash earse
        // 4K
        // DBG_DIRECT("flash erase 0x%x\n", pop);
        res = flash_nor_erase_locked((uint32_t)pop, FLASH_NOR_ERASE_SECTOR);
        if (res != FLASH_NOR_RET_SUCCESS)
        {
            DBG_DIRECT("flash erase 0x%x error:%d\n", pop, res);
        }
        pop += CUST_SECTOR_SIZE;
    }

    // flash wr
    // DBG_DIRECT("flash wr 0x%x -> 0x%x %d, %s\n", name_buff, addr, name_sz, name_buff);
    res = flash_nor_write_locked((uint32_t)addr, (uint8_t *)name_buff, name_sz);
    if (res != FLASH_NOR_RET_SUCCESS)
    {
        DBG_DIRECT("flash wr  error:%d\n", res);
    }
    free(name_buff);

    // flash wr
    // DBG_DIRECT("flash wr 0x%x -> 0x%x %d\n", buff, addr + name_sz, file_sz);
    res = flash_nor_write_locked((uint32_t)addr + name_sz, (uint8_t *)buff, file_sz);
    if (res != FLASH_NOR_RET_SUCCESS)
    {
        DBG_DIRECT("flash wr  error:%d\n", res);
    }

    return res;
}



uint8_t *cust_file_alloc(size_t sz)
{
    cust_file_node_t *pnode = cust_file_root;
    uint32_t cur_addr = 0;
    uint32_t end_addr = 0;
    uint32_t sz_wr = CUST_CEIL_BY_SECTOR(sz);

    // cfg file not exist, should init cust_dir here
    if (cust_dir.start_addr == 0)
    {
        cust_dir.start_addr = (uint32_t)CUST_FILE_START_ADDR;
        cust_dir.size = (uint32_t)CUST_FILE_SIZE;
    }
    end_addr = cust_dir.start_addr + cust_dir.size;

    if (!pnode)
    {
        if (sz_wr < end_addr)
        {
            return (uint8_t *)cust_dir.start_addr;
        }
        else
        {
            return NULL;
        }
    }
    // go to the last node
    while (pnode->nxt)
    {
        pnode = pnode->nxt;
    }

    cur_addr = CUST_CEIL_BY_SECTOR((uint32_t)pnode->file_info.file_addr + pnode->file_info.file_sz);
    if (cur_addr + sz_wr < end_addr)
    {
        // append to the end
        return (uint8_t *)cur_addr;
    }
    else
    {
        // check start address
        cur_addr = cust_dir.start_addr;
        end_addr = (uint32_t)cust_file_root->file_info.name_addr;
        if (cur_addr + sz_wr < end_addr)
        {
            return (uint8_t *)cur_addr;
        }


        // loop from root to find a suitable space
        pnode = cust_file_root;
        // >= 2 nodes
        while (pnode->nxt)
        {
            cur_addr = CUST_CEIL_BY_SECTOR((uint32_t)pnode->file_info.file_addr + pnode->file_info.file_sz);
            end_addr = (uint32_t)pnode->nxt->file_info.name_addr;
            if (cur_addr + sz_wr < end_addr)
            {
                return (uint8_t *)cur_addr;
            }
            else
            {
                pnode = pnode->nxt;
            }
        }
    }

    return NULL;
}



//
int cust_dir_init(void)
{
    int res = -1;
    int f;
    cust_file_node_t *pnode = NULL;

    f = rtk_fs_open(CUST_DIR_FILEPATH, RTK_FS_O_RDONLY);
    if (f < 0)
    {
        DBG_DIRECT("find cfg file fail\n");
        return -1;
    }

    res = rtk_fs_read(f, &cust_dir, sizeof(cust_dir));
    if (res != sizeof(cust_dir))
    {
        DBG_DIRECT("read cfg file fail: %d\n", res);
        rtk_fs_close(f);
        return -1;
    }

    if (cust_dir.file_num == 0 || cust_dir.file_num > CUST_MAX_FILE_NUM)
    {
        memset((void *)&cust_dir, 0, sizeof(cust_dir));
        DBG_DIRECT("no file recorded\n");
        rtk_fs_close(f);
        return 0;
    }
    cust_file_root = (cust_file_node_t *)malloc(sizeof(cust_file_node_t));
    // DBG_DIRECT("ROOT 0x%x", cust_file_root);
    if (!cust_file_root)
    {
        DBG_DIRECT("init file list fail\n");
        rtk_fs_close(f);
        return -1;
    }
    memset((void *)cust_file_root, 0, sizeof(cust_file_node_t));
    res = rtk_fs_read(f, &(cust_file_root->file_info), sizeof(cust_file_info_t));
    if (res != sizeof(cust_file_info_t))
    {
        DBG_DIRECT("read info fail: 0 %d\n", res);
        return -1;
    }

    pnode = cust_file_root;
    for (uint32_t i = 1; i < cust_dir.file_num; i++)
    {
        cust_file_node_t *nxt = (cust_file_node_t *)malloc(sizeof(cust_file_node_t));
        if (!nxt)
        {
            DBG_DIRECT("init nxt fail\n");
            rtk_fs_close(f);
            return -1;
        }
        memset((void *)nxt, 0, sizeof(cust_file_node_t));

        pnode->nxt = nxt;
        res = rtk_fs_read(f, &(nxt->file_info), sizeof(cust_file_info_t));
        if (res != sizeof(cust_file_info_t))
        {
            DBG_DIRECT("read info fail: %d %d\n", i, res);
            break;
        }
        pnode = nxt;
    }
    rtk_fs_close(f);

    return 0;
}


static int cust_file_sync_dir(void)
{
    int res = -1;
    cust_file_node_t *pnode = cust_file_root;
    int f;

    // if(!pnode)
    // {
    //     cust_dir.file_num = 0;
    // }

    f = rtk_fs_open(CUST_DIR_FILEPATH, RTK_FS_O_WRONLY | RTK_FS_O_CREAT);
    if (f < 0)
    {
        DBG_DIRECT("find cfg file fail\n");
        return -1;
    }

    rtk_fs_lseek(f, 0, RTK_FS_SEEK_SET);
    // DBG_DIRECT("sync dir: file_num %d, start_addr 0x%x, size %d\n", cust_dir.file_num, cust_dir.start_addr, cust_dir.size);
    res = rtk_fs_write(f, &cust_dir, sizeof(cust_dir));
    if (res != sizeof(cust_dir))
    {
        DBG_DIRECT("wr dir fail\n");
        rtk_fs_close(f);
        return -1;
    }

    while (pnode)
    {
        // DBG_DIRECT("sync dir: name_addr 0x%x, file_addr 0x%x, size %d\n", pnode->file_info.name_addr, pnode->file_info.file_addr, pnode->file_info.file_sz);
        res = rtk_fs_write(f, &(pnode->file_info), sizeof(pnode->file_info));
        if (res != sizeof(pnode->file_info))
        {
            DBG_DIRECT("wr dir fail\n");
            rtk_fs_close(f);
            return -1;
        }
        pnode = pnode->nxt;
    }

    rtk_fs_close(f);
    return 0;
}





int cust_file_init_as_lvimg(void)
{
    cust_file_node_t *pnode = cust_file_root;

    while (pnode)
    {
        lv_img_dsc_t *img = (lv_img_dsc_t *)malloc(sizeof(lv_img_dsc_t));
        memset((void *)img, 0, sizeof(lv_img_dsc_t));
        img->header.cf = (uint32_t)pnode->file_info.usr_data;
        if (img->header.cf == LV_IMG_CF_RAW)
        {
            img->data = pnode->file_info.file_addr;
        }// other cf
        pnode->data = img;
        pnode = pnode->nxt;
    }
    return 0;
}

static cust_file_node_t *cust_file_get_node(const char *name)
{
    cust_file_node_t *pnode = cust_file_root;

    while (pnode)
    {
        if (0 == strcmp(name, (const char *)pnode->file_info.name_addr))
        {
            break;
        }
        pnode = pnode->nxt;
    }

    return pnode;
}

size_t cust_file_get_size(const char *name)
{
    cust_file_node_t *pnode = NULL;

    pnode = cust_file_get_node(name);
    return pnode ? (size_t)(pnode->file_info.file_sz) : 0;
}


lv_img_dsc_t *cust_file_get_lvimg(const char *name)
{
    cust_file_node_t *pnode = NULL;

    // DBG_DIRECT("get_lvimg %s\n", name);
    pnode = cust_file_get_node(name);
    return pnode ? (lv_img_dsc_t *)(pnode->data) : NULL;
}


int cust_file_delete_lvimg(const char *name)
{
    int res = -1;
    lv_img_dsc_t *img = NULL;

    DBG_DIRECT("delete img %s\n", name);
    img = cust_file_get_lvimg(name);
    if (img)
    {
        free(img);
    }
    res = cust_file_delete_node(name);
    if (res == 0)
    {
        // flg_dle = 1;
        cust_dir.file_num--;
        res = cust_file_sync_dir();
        if (res < 0)
        {
            DBG_DIRECT("dir sync fail\n");
        }
    }
    return res;
}


bool cust_file_add_try(const char *file_name, size_t file_sz)
{
    int res = -1;
    size_t name_sz = (((strlen(file_name) + 1) + 3) >> 2) << 2; // 4 Byte align
    size_t total_sz = name_sz + file_sz;
    uint8_t *addr = NULL;

    addr = cust_file_alloc(total_sz);
    return addr ? true : false;
}

int cust_file_add_lvimg(const char *file_name, const uint8_t *buff, size_t file_sz, void *usr_data)
{
    int res = -1;
    size_t name_sz = (((strlen(file_name) + 1) + 3) >> 2) << 2; // 4 Byte align
    size_t total_sz = name_sz + file_sz;
    uint8_t *addr = NULL;
    cust_file_node_t *node = NULL;
    lv_img_dsc_t *img = NULL;

    DBG_DIRECT("add_lvimg %s\n", file_name);
    img = cust_file_get_lvimg(file_name);
    if (img)
    {
        DBG_DIRECT("file exist\n");
        return -2;
    }
    addr = cust_file_alloc(total_sz);
    if (!addr)
    {
        DBG_DIRECT("file alloc fail\n");
        return -1;
    }

    res = cust_file_flash_write(addr, file_name,  buff, file_sz);
    if (res < 0)
    {
        DBG_DIRECT("cust_file_flash_write fail\n");
        return -1;
    }

    node = (cust_file_node_t *)malloc(sizeof(cust_file_node_t));
    memset((void *)node, 0, sizeof(cust_file_node_t));
    node->file_info.name_addr = addr;
    node->file_info.file_addr = addr + name_sz;
    node->file_info.file_sz = file_sz;
    node->file_info.usr_data = usr_data;

    // mk lv_img
    img = (lv_img_dsc_t *)malloc(sizeof(lv_img_dsc_t));
    memset((void *)img, 0, sizeof(lv_img_dsc_t));
    img->header.cf = (uint32_t)usr_data;
    if (img->header.cf == LV_IMG_CF_RAW)
    {
        img->data = node->file_info.file_addr;
    }// other cf
    node->data = (void *)img;

    cust_file_insert_node(node);
    cust_dir.file_num ++;
    res = cust_file_sync_dir();
    if (res < 0)
    {
        DBG_DIRECT("cust_file_sync_dir fail\n");
    }
    DBG_DIRECT("cust_file_add_lvimg done\n");
    return res;
}




void cust_file_log(void)
{
    cust_file_node_t *pnode = cust_file_root;
    uint8_t cnt = 0;

    DBG_DIRECT("log: file_num %d, start_addr 0x%x, storage %d\n", cust_dir.file_num,
               cust_dir.start_addr, cust_dir.size);

    while (pnode)
    {
        DBG_DIRECT("log: sync dir: %d name_addr 0x%x, %s, file_addr 0x%x, size %d, usr 0x%x\n", cnt,
                   pnode->file_info.name_addr, pnode->file_info.name_addr, pnode->file_info.file_addr,
                   pnode->file_info.file_sz, pnode->file_info.usr_data);
        pnode = pnode->nxt;
        cnt ++;
    }

}