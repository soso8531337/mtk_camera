#ifndef _US_CAMERA_H
#define _US_CAMERA_H
#define PID_FILE "/var/run/camera.pid"                 

int usCamera_Streaming(uint8_t **tfrPtr, uint32_t *tfrSize);
int usCamera_GetConfig(uint8_t *confPtr, uint32_t confSize, uint32_t *useSize);
int usCamera_GetCurConfig(uint8_t *confPtr, uint32_t confSize, uint32_t *useSize);
int already_running(char *pidfile);
#endif
