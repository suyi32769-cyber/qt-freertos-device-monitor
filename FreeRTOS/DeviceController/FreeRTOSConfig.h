#pragma once
#include <stdint.h>
#include <stddef.h>
#define configUSE_PREEMPTION 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configTICK_RATE_HZ 1000
#define configMAX_PRIORITIES 5
#define configMINIMAL_STACK_SIZE 128
#define configTOTAL_HEAP_SIZE (128 * 1024)
#define configMAX_TASK_NAME_LEN 16
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD 1
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_TASK_NOTIFICATIONS 1
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_TIMERS 0
#define configUSE_CO_ROUTINES 0
#define configUSE_TRACE_FACILITY 0
#define configCHECK_FOR_STACK_OVERFLOW 0
#define configUSE_MALLOC_FAILED_HOOK 0
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 0
#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskSuspend 1
void appAssert(const char *file, int line);
#define configASSERT(x) do { if (!(x)) appAssert(__FILE__, __LINE__); } while(0)
