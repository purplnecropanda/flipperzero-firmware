#include "furi.h"
#include "cmsis_os.h"

#define DEBUG

#ifdef DEBUG
#include <stdio.h>
#endif

#define DEFAULT_STACK_SIZE 1024 // Stack size in bytes
#define MAX_TASK_COUNT 8

static StaticTask_t task_info_buffer[MAX_TASK_COUNT];
static StackType_t stack_buffer[MAX_TASK_COUNT][DEFAULT_STACK_SIZE / 4];
static FuriApp task_buffer[MAX_TASK_COUNT];

static size_t current_buffer_idx = 0;

// find task pointer by handle
FuriApp* find_task(TaskHandle_t handler) {
    FuriApp* res = NULL;
    for(size_t i = 0; i < MAX_TASK_COUNT; i++) {
        if(task_equal(task_buffer[i].handler, handler)) {
            res = &task_buffer[i];
        }
    }

    return res;
}

FuriApp* furiac_start(FlipperApplication app, const char* name, void* param) {
    #ifdef DEBUG
        printf("[FURIAC] start %s\n", name);
    #endif

    // TODO check first free item (.handler == NULL) and use it

    if(current_buffer_idx >= MAX_TASK_COUNT) {
        // max task count exceed
        #ifdef DEBUG
            printf("[FURIAC] max task count exceed\n");
        #endif
        return NULL;
    }

    // create task on static stack memory
    task_buffer[current_buffer_idx].handler = xTaskCreateStatic(
        (TaskFunction_t)app,
        (const char * const)name,
        DEFAULT_STACK_SIZE / 4, // freertos specify stack size in words
        (void * const) param,
        tskIDLE_PRIORITY + 3, // normal priority
        stack_buffer[current_buffer_idx],
        &task_info_buffer[current_buffer_idx]
    );

    // save task
    task_buffer[current_buffer_idx].application = app;
    task_buffer[current_buffer_idx].prev_name = NULL;
    task_buffer[current_buffer_idx].prev = NULL;
    task_buffer[current_buffer_idx].records_count = 0;
    task_buffer[current_buffer_idx].name = name;

    current_buffer_idx++;

    return &task_buffer[current_buffer_idx - 1];
}

bool furiac_kill(FuriApp* app) {
    #ifdef DEBUG
        printf("[FURIAC] kill %s\n", app->name);
    #endif

    // check handler
    if(app == NULL || app->handler == NULL) return false;

    // kill task
    vTaskDelete(app->handler);

    // cleanup its registry
    // TODO realy free memory
    app->handler = NULL;

    return true;
}

void furiac_exit(void* param) {
    // get current task handler
    FuriApp* current_task = find_task(xTaskGetCurrentTaskHandle());

    // run prev
    if(current_task != NULL) {
        #ifdef DEBUG
            printf("[FURIAC] exit %s\n", current_task->name);
        #endif

        if(current_task->prev != NULL) {
            furiac_start(current_task->prev, current_task->prev_name, param);
        } else {
            #ifdef DEBUG
                printf("[FURIAC] no prev\n");
            #endif
        }

        // cleanup registry
        // TODO realy free memory
        current_task->handler = NULL;
    }

    // kill itself
     vTaskDelete(NULL);
}

void furiac_switch(FlipperApplication app, char* name, void* param) {
    // get current task handler
    FuriApp* current_task = find_task(xTaskGetCurrentTaskHandle());

    if(current_task == NULL) {
        #ifdef DEBUG
            printf("[FURIAC] no current task found\n");
        #endif
    }

    #ifdef DEBUG
        printf("[FURIAC] switch %s to %s\n", current_task->name, name);
    #endif

    // run next
    FuriApp* next = furiac_start(app, name, param);

    if(next != NULL) {
        // save current application pointer as prev
        next->prev = current_task->application;
        next->prev_name = current_task->name;

        // kill itself
        vTaskDelete(NULL);
    }
}