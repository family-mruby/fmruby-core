#pragma once

#include "fmrb_mem.h"

int create_app_task_from_file(char* path);
int create_app_task_from_mem(char* app_name, unsigned char* app_irep);

enum FMRB_PROC_ID{
    PROC_ID_KERNEL = 0,
    PROC_ID_SYSTEM_APP,
    PROC_ID_USER_APP0,
    PROC_ID_USER_APP1,
    PROC_ID_USER_APP2,
    PROC_ID_MAX
};

enum FMRB_PROC_STATE{
    PROC_STATE_INACTIVE = 0,
    PROC_STATE_ACTIVE,
    PROC_STATE_SUSPEND,
};

enum FMRB_APP_TYPE{
    APP_TYPE_KERNEL = 0,
    APP_TYPE_SYSTEM_APP,
    APP_TYPE_USER_APP,
    APP_TYPE_MAX
};

typedef struct {
    enum FMRB_PROC_STATE state;
    enum FMRB_PROC_ID app_id;
    enum FMRB_APP_TYPE type;
    char app_name[32];
    void* mrb;
    enum FMRB_MEM_POOL_ID mempool_id;
    void* semaphore;
} fmrb_app_task_context_t;
