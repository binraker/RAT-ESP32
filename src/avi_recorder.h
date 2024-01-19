#ifndef AVI_RECORDER_H
#define AVI_RECORDER_H

#include "myConfig.h"
#include "sensor.h"

void set_AVI_FileName(const char *asChar_AVI_FileName);
esp_err_t init_sdcard();
static void start_avi();
static void another_save_avi(camera_fb_t * fb );
static void end_avi();
void record_avi_video();

extern bool capturing_synced_Syntiant_AVI_record;
extern char capturing_synced_Syntiant_AVI_FileName[50];
#define AVI_FILE_PREFIX "/IMUSyncedVideo"
#define AVI_FILE_EXTENSION "avi"

#endif