/*
 * Capture ESP32 Cam JPEG images into a AVI file and store on SD
 * AVI files stored on the SD card can also be selected and streamed to a browser as MJPEG.
 *
 * s60sc 2020, 2021, 2022
 */
// built using arduino-esp32 stable release v2.0.3
// #include "ping/ping.h"

#include "myConfig.h"
#include "camera_pins.h"
//#include "mjpeg2sd.cpp"

String synced_Syntiant_AVI_fileName = ""; //variable to store time received from the Syntiant TinyML board
String received_timestamp_from_ESP32CAM = ""; // stores the message received via Serial2

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  //Serial.println("begin");

  LOG_INF("=============== Starting ===============");
  if (!psramFound())
  {
    LOG_WRN("Need PSRAM to be enabled");
    delay(10000);
    ESP.restart();
  }

  if ((fs::SPIFFSFS *)&STORAGE == &SPIFFS)
    startSpiffs();
  else if (!prepSD_MMC())
  {
    LOG_WRN("Insert SD card, will restart after 10 secs");
    delay(10000);
    ESP.restart();
  }

  // configure camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = XCLK_MHZ * 1000000;
  config.pixel_format = PIXFORMAT_JPEG;
  // init with high specs to pre-allocate larger buffers
  if (psramFound())
  {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 4;
  }
  else
  {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  #if defined(CAMERA_MODEL_ESP_EYE)
    pinMode(13, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);
  #endif

  // camera init
  esp_err_t err = ESP_FAIL;
  uint8_t retries = 2;
  while (retries && err != ESP_OK)
  {
    err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
      LOG_ERR("Camera init failed with error 0x%x", err);
      digitalWrite(PWDN_GPIO_NUM, 1);
      delay(100);
      digitalWrite(PWDN_GPIO_NUM, 0); // power cycle the camera (OV2640)
      retries--;
    }
  }
  if (err != ESP_OK)
    ESP.restart();
  else
    LOG_INF("Camera init OK");

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID)
  {
    s->set_vflip(s, 1);       // flip it back
    s->set_brightness(s, 1);  // up the brightness just a bit
    s->set_saturation(s, -2); // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_SVGA);

  #if defined(CAMERA_MODEL_M5STACK_WIDE)
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
  #endif

  // Load saved user configuration
  loadConfig();

  // connect wifi or start config AP if router details not available
  #ifdef DEV_ONLY
    devSetup();
  #endif
  startWifi();

  if (!prepRecording())
  {
    LOG_ERR("Unable to continue, AVI capture fail, restart after 10 secs");
    delay(10000);
    ESP.restart();
  }

  // start rest of services
  startWebServer();
  startStreamServer();
  prepMic();
  startSDtasks();
  startFTPtask();
  prepSMTP();
  prepDS18B20();
  prepPanTilt();
  setupADC();
  checkMemory();
  startSerialIn();
  LOG_INF("Camera Ready @ %uMHz, version %s", XCLK_MHZ, APP_VER);
}

void loop()
{
}

String messages[10];
int messagePointer = 0;

void ReadFromSerial(void *pvParameters)
{
  (void)pvParameters;
  for (;;)
  {
    if (Serial.available() > 0)
    {
      String message = Serial.readStringUntil('\n');
      LOG_INF("Message received: %s", message.c_str());
      messages[messagePointer] = message;
      messagePointer++;
      messagePointer = messagePointer % 10;

     // check if Syntiant board has sent a time, capture an video and save it with the received time
     // CAUTION! Maintain same format between Data, i.e, no space after ':'
      char expectedDataPrefix[] = "[Data]:";
      if (message.startsWith(expectedDataPrefix)) {
        // Remove the first 7 characters
        synced_Syntiant_AVI_fileName = "SensorDataAVI" + message.substring(7);
        //LOG_INF("Proceeding to capture an AVI.. Filename is %s ..", synced_Syntiant_AVI_fileName);
        //LOG_INF("Proceeding to capture an AVI..");

        // save an AVI file
        capture_synced_Syntiant_AVI = true;
        // convert string name to char
        char capture_synced_Syntiant_AVI_FileName[40];
        int synced_Syntiant_AVI_fileName_length = synced_Syntiant_AVI_fileName.length() +1;
        synced_Syntiant_AVI_fileName.toCharArray(capture_synced_Syntiant_AVI_FileName, synced_Syntiant_AVI_fileName_length);
        set_synced_Syntiant_AVI_FileName(capture_synced_Syntiant_AVI_FileName);
      } 
    }
    delay(250);
  }
}

void startSerialIn()
{
  Serial.setTimeout(250);
  for (int i = 0; i < 10; i++)
  {
    messages[i] = "";
  }
  xTaskCreate(&ReadFromSerial, "ReadFromSerial", 4096, NULL, 2, NULL);
}
