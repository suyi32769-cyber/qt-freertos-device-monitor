/* Windows simulation of an embedded controller using the FreeRTOS Win32 port. */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

typedef struct { unsigned seq; double temp, volt; } Reading;
typedef struct { char bytes[128]; } Frame;
typedef enum { STARTING, RUNNING, FAULT, RECOVERING } State;
static QueueHandle_t samples, outgoing, commands;
static SemaphoreHandle_t sharedLock, logLock;
static TickType_t faultUntil;
static State state = STARTING;
static FILE *logFile;
static int serialMode;
static HANDLE serialPort = INVALID_HANDLE_VALUE;
static SOCKET listener = INVALID_SOCKET, client = INVALID_SOCKET;
static char inputBuffer[128];
static int inputUsed, inputDiscard;

void appAssert(const char *file, int line) { fprintf(stderr, "ASSERT %s:%d\n", file, line); fflush(stderr); ExitProcess(2); }
static const char *stateName(State s) { static const char *names[] = {"STARTING", "RUNNING", "FAULT", "RECOVERING"}; return names[s]; }
static void logEvent(const char *message) {
    xSemaphoreTake(logLock, portMAX_DELAY);
    printf("[%lu] %s\n", (unsigned long)xTaskGetTickCount(), message); fflush(stdout);
    if (logFile) { fprintf(logFile, "%lu,%s\n", (unsigned long)xTaskGetTickCount(), message); fflush(logFile); }
    xSemaphoreGive(logLock);
}
static void frameBody(const char *body) {
    unsigned char sum = 0; Frame frame; const unsigned char *p = (const unsigned char *)body;
    while (*p) sum ^= *p++;
    _snprintf_s(frame.bytes, sizeof(frame.bytes), _TRUNCATE, "$%s*%02X\n", body, sum);
    if (xQueueSend(outgoing, &frame, 0) != pdPASS) logEvent("TX queue full; frame dropped");
}
static void changeState(State next) {
    char line[128];
    xSemaphoreTake(sharedLock, portMAX_DELAY); if (state == next) { xSemaphoreGive(sharedLock); return; } state = next; xSemaphoreGive(sharedLock);
    _snprintf_s(line, sizeof(line), _TRUNCATE, "STATE,%s", stateName(next)); logEvent(line); frameBody(line);
}
static void acquisitionTask(void *arg) {
    unsigned seq = 0; (void)arg;
    for (;;) {
        TickType_t deadline; Reading r; double phase;
        xSemaphoreTake(sharedLock, portMAX_DELAY); deadline = faultUntil; xSemaphoreGive(sharedLock);
        if (deadline == 0 || (int32_t)(xTaskGetTickCount() - deadline) >= 0) {
            phase = seq / 12.0; r.seq = seq++; r.temp = 35.0 + 16.0 * sin(phase); r.volt = 3.30 + 0.15 * sin(phase/2.0);
            xQueueOverwrite(samples, &r);
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
static void controlTask(void *arg) {
    Reading r; char command[16], line[128]; TickType_t lastSample = xTaskGetTickCount(), lastState = 0; int consecutive = 0; State previous = STARTING; (void)arg;
    logEvent("STATE,STARTING");
    for (;;) {
        while (xQueueReceive(commands, command, 0) == pdPASS) {
            if (!strcmp(command, "FAULT")) { xSemaphoreTake(sharedLock, portMAX_DELAY); faultUntil = xTaskGetTickCount() + pdMS_TO_TICKS(3000); xSemaphoreGive(sharedLock); logEvent("CMD,FAULT: sensor paused for 3 seconds"); frameBody("ACK,FAULT"); }
            else if (!strcmp(command, "RESET")) { xSemaphoreTake(sharedLock, portMAX_DELAY); faultUntil = 0; xSemaphoreGive(sharedLock); logEvent("CMD,RESET: sensor resumed"); frameBody("ACK,RESET"); }
        }
        if (xQueueReceive(samples, &r, pdMS_TO_TICKS(100)) == pdPASS) {
            lastSample = xTaskGetTickCount();
            if (previous == FAULT) { changeState(RECOVERING); previous = RECOVERING; consecutive = 1; }
            else if (previous == RECOVERING && ++consecutive >= 3) { changeState(RUNNING); previous = RUNNING; }
            else if (previous == STARTING) { changeState(RUNNING); previous = RUNNING; }
            _snprintf_s(line, sizeof(line), _TRUNCATE, "DATA,%u,%.2f,%.2f,%d", r.seq, r.temp, r.volt, previous != RUNNING); frameBody(line);
        } else if ((xTaskGetTickCount() - lastSample) >= pdMS_TO_TICKS(1000) && previous != FAULT) { changeState(FAULT); previous = FAULT; consecutive = 0; }
        if ((xTaskGetTickCount() - lastState) >= pdMS_TO_TICKS(1000)) { _snprintf_s(line,sizeof(line),_TRUNCATE,"STATE,%s",stateName(previous)); frameBody(line); lastState=xTaskGetTickCount(); }
    }
}
static int validCommand(char *line, char *out) {
    char *star; unsigned check; unsigned char sum = 0; char *p; size_t n = strlen(line);
    while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
    if (line[0] != '$' || !(star = strchr(line, '*')) || strlen(star+1) != 2 || strspn(star+1,"0123456789abcdefABCDEF") != 2 || sscanf_s(star+1, "%2x", &check) != 1) return 0;
    for (p = line+1; p < star; ++p) sum ^= (unsigned char)*p;
    if (sum != check) return 0; *star = 0;
    if (!strcmp(line+1,"CMD,FAULT")) strcpy_s(out,16,"FAULT"); else if (!strcmp(line+1,"CMD,RESET")) strcpy_s(out,16,"RESET"); else return 0;
    return 1;
}
static void consumeBytes(const char *bytes, int len) {
    int i;
    for (i=0; i<len; ++i) { char c=bytes[i];
        if (inputDiscard) { if (c=='\n') inputDiscard=0; continue; }
        if (inputUsed >= (int)sizeof(inputBuffer)-1) { inputUsed=0; inputDiscard=1; logEvent("Invalid command: too long"); continue; }
        inputBuffer[inputUsed++]=c;
        if (c=='\n') { char command[16]={0}; inputBuffer[inputUsed]=0; if (validCommand(inputBuffer, command)) { if (xQueueSend(commands, command, 0) != pdPASS) logEvent("Command queue full"); } else logEvent("Invalid command or checksum"); inputUsed=0; }
    }
}
static void transportTask(void *arg) {
    char buf[256]; Frame frame; (void)arg;
    for (;;) {
        if (serialMode) { DWORD got=0; if (ReadFile(serialPort, buf, sizeof(buf), &got, NULL) && got) consumeBytes(buf,(int)got); }
        else {
            if (client == INVALID_SOCKET) { client = accept(listener, NULL, NULL); if (client != INVALID_SOCKET) { u_long nb=1; ioctlsocket(client,FIONBIO,&nb); logEvent("TCP client connected"); } }
            if (client != INVALID_SOCKET) { int n=recv(client,buf,sizeof(buf),0); if (n>0) consumeBytes(buf,n); else if (n==0 || (n<0 && WSAGetLastError()!=WSAEWOULDBLOCK)) { closesocket(client); client=INVALID_SOCKET; inputUsed=0; inputDiscard=0; logEvent("TCP client disconnected"); } }
        }
        while (xQueueReceive(outgoing,&frame,0)==pdPASS) {
            int len=(int)strlen(frame.bytes);
            if (serialMode) { DWORD written=0; if (!WriteFile(serialPort,frame.bytes,len,&written,NULL) || written != (DWORD)len) logEvent("UART write failed"); }
            else if (client!=INVALID_SOCKET) { int n=send(client,frame.bytes,len,0); if (n!=len) { closesocket(client); client=INVALID_SOCKET; logEvent("TCP write failed; disconnected"); } }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
static int openTransport(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1],"--com")) {
        char path[64]; DCB dcb={0}; COMMTIMEOUTS timeouts={0}; serialMode=1;
        if (strncmp(argv[2], "COM", 3) || strlen(argv[2]) > 10) return 0;
        _snprintf_s(path,sizeof(path),_TRUNCATE,"\\\\.\\%s",argv[2]); serialPort=CreateFileA(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (serialPort==INVALID_HANDLE_VALUE) return 0; dcb.DCBlength=sizeof(dcb);
        if (!GetCommState(serialPort,&dcb)) return 0; dcb.BaudRate=115200; dcb.ByteSize=8; dcb.Parity=NOPARITY; dcb.StopBits=ONESTOPBIT; dcb.fBinary=1; dcb.fOutxCtsFlow=FALSE; dcb.fOutxDsrFlow=FALSE; dcb.fOutX=FALSE; dcb.fInX=FALSE; dcb.fRtsControl=RTS_CONTROL_DISABLE; dcb.fDtrControl=DTR_CONTROL_DISABLE;
        timeouts.ReadIntervalTimeout=MAXDWORD; timeouts.WriteTotalTimeoutConstant=100;
        return SetCommState(serialPort,&dcb) && SetCommTimeouts(serialPort,&timeouts);
    } else if (argc != 1) return 0;
    { WSADATA data; struct sockaddr_in addr; u_long nb=1;
      if (WSAStartup(MAKEWORD(2,2),&data)) return 0; listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); if (listener==INVALID_SOCKET) return 0;
      memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK); addr.sin_port=htons(47653);
      if (bind(listener,(struct sockaddr *)&addr,sizeof(addr)) || listen(listener,1) || ioctlsocket(listener,FIONBIO,&nb)) return 0;
    } return 1;
}
int main(int argc, char **argv) {
    if (!openTransport(argc,argv)) { fprintf(stderr,"Transport failed. Use no arguments for localhost:47653, or --com COM3 (115200 8N1).\n"); return 1; }
    logFile=fopen("controller.log","a"); sharedLock=xSemaphoreCreateMutex(); logLock=xSemaphoreCreateMutex(); samples=xQueueCreate(1,sizeof(Reading)); outgoing=xQueueCreate(32,sizeof(Frame)); commands=xQueueCreate(8,16);
    configASSERT(sharedLock && logLock && samples && outgoing && commands);
    printf("FreeRTOS device controller SIMULATION; %s\n",serialMode ? "UART 115200 8N1" : "TCP 127.0.0.1:47653"); fflush(stdout);
    configASSERT(xTaskCreate(acquisitionTask,"Acquire",256,NULL,2,NULL)==pdPASS);
    configASSERT(xTaskCreate(controlTask,"Control",256,NULL,2,NULL)==pdPASS);
    configASSERT(xTaskCreate(transportTask,"Transport",256,NULL,1,NULL)==pdPASS);
    vTaskStartScheduler(); return 2;
}
