
// General utilities not specific to this app to support:
// - wifi
// - NTP
// - remote logging
// - base64 encoding
// - battery voltage
//
// s60sc 2021, some functions based on code contributed by gemi254

#include "myConfig.h"

bool dbgVerbose = false;
bool timeSynchronized = false;

/************************** Wifi **************************/

char hostName[32] = ""; // Default Host name
char ST_SSID[32]  = ""; //Default router ssid
char ST_Pass[MAX_PWD_LEN] = ""; //Default router passd

// leave following blank for dhcp
char ST_ip[16]  = ""; // Static IP
char ST_sn[16]  = ""; // subnet normally 255.255.255.0
char ST_gw[16]  = ""; // gateway to internet, normally router IP
char ST_ns1[16] = ""; // DNS Server, can be router IP (needed for SNTP)
char ST_ns2[16] = ""; // alternative DNS Server, can be blank

// Access point Config Portal SSID and Pass
String AP_SSID = String(APP_NAME) + "_" + String((uint32_t)ESP.getEfuseMac(),HEX); 
char   AP_Pass[MAX_PWD_LEN] = AP_PASSWD;
char   AP_ip[16]  = ""; //Leave blank to use 192.168.4.1
char   AP_sn[16]  = "";
char   AP_gw[16]  = "";

static esp_ping_handle_t pingHandle = NULL;
static void startPing();




static void setupMndsHost() {  //Mdns services   
  if (MDNS.begin(hostName) ) {
    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "udp", 81);
    // MDNS.addService("ftp", "tcp", 21);    
    LOG_INF("mDNS service: http://%s.local", hostName );
  } else {LOG_ERR("mDNS host name: %s Failed", hostName);}
}

static bool setWifiAP() {
  // Set access point
  WiFi.mode(WIFI_AP);
  //set static ip
  if(strlen(AP_ip)>1){
    LOG_DBG("Setting ap static ip :%s, %s, %s", AP_ip,AP_gw,AP_sn);  
    IPAddress _ip,_gw,_sn,_ns1,_ns2;
    _ip.fromString(AP_ip);
    _gw.fromString(AP_gw);
    _sn.fromString(AP_sn);
    //set static ip
    WiFi.softAPConfig(_ip, _gw, _sn);
  } 
  WiFi.softAP(AP_SSID.c_str(), AP_Pass );
  LOG_INF("Created Access Point with SSID: %s", AP_SSID.c_str()); 
  LOG_INF("Use 'http://%s' to connect", WiFi.softAPIP().toString().c_str()); 
  setupMndsHost();
  return true;
}

bool startWifi() {
  WiFi.disconnect();
  WiFi.persistent(false); // prevent the flash storage WiFi credentials
  WiFi.setAutoReconnect(false); //Set whether module will attempt to reconnect to an access point in case it is disconnected
  WiFi.setAutoConnect(false);
  LOG_INF("Setting wifi hostname: %s", hostName);
  WiFi.setHostname(hostName);
  if (strlen(ST_SSID) > 0) { 
    LOG_INF("Got stored router credentials. Connecting to: %s", ST_SSID);
    if (strlen(ST_ip) > 1) {
      LOG_INF("Set config static ip: %s, %s, %s, %s", ST_ip, ST_gw, ST_sn, ST_ns1);
      IPAddress _ip, _gw, _sn, _ns1, _ns2;
      if (!_ip.fromString(ST_ip)) LOG_ERR("Failed to parse %s", ST_ip);
      _ip.fromString(ST_ip);
      _gw.fromString(ST_gw);
      _sn.fromString(ST_sn);
      _ns1.fromString(ST_ns1);
      _ns2.fromString(ST_ns2);
      // set static ip
      WiFi.config(_ip, _gw, _sn, _ns1); // need DNS for SNTP
    } else {LOG_INF("Getting ip from dhcp ...");}
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ST_SSID, ST_Pass);
    uint32_t startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS)  {
      //Stop waiting on failure.. Will reconnect later by keep alive
      if (WiFi.status() == WL_CONNECT_FAILED){
        LOG_ERR("Connect FAILED to: %s. ", ST_SSID);
        startPing();      
        return false;
      }
      Serial.print(".");
      delay(500);
      Serial.flush();
    }
    if (WiFi.status() == WL_CONNECTED) {
      startPing();
      LOG_INF("Use 'http://%s' to connect", WiFi.localIP().toString().c_str()); 
    } else {
      if (ALLOW_AP) {
        LOG_INF("Unable to connect to router, start Access Point");
        // Start AP config portal
        return setWifiAP();
      } 
      return false;
    }
  } else {
    // Start AP config portal
    LOG_INF("No stored Credentials. Starting Access point");
    return setWifiAP();
  }
  return true;
}

static void pingSuccess(esp_ping_handle_t hdl, void *args) {
  static bool dataFilesChecked = false;
  if (!timeSynchronized) getLocalNTP();
  if (!dataFilesChecked) dataFilesChecked = checkDataFiles();
  LOG_DBG("ping successful");
}

static void pingTimeout(esp_ping_handle_t hdl, void *args) {
  LOG_WRN("Failed to ping gateway, restart wifi ...");
  esp_ping_stop(pingHandle);
  esp_ping_delete_session(pingHandle);
  startWifi();
}

static void startPing() {
  pingHandle = NULL;
  IPAddress ipAddr = WiFi.gatewayIP();
  ip_addr_t pingDest; 
  IP_ADDR4(&pingDest, ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
  esp_ping_config_t pingConfig = ESP_PING_DEFAULT_CONFIG();
  pingConfig.target_addr = pingDest;  
  pingConfig.count = ESP_PING_COUNT_INFINITE;
  pingConfig.interval_ms = WIFI_TIMEOUT_MS;
  pingConfig.timeout_ms = 5000;
  pingConfig.task_stack_size = 4096 * 2;
  pingConfig.task_prio = 1;
  // set ping task callback functions 
  esp_ping_callbacks_t cbs;
  cbs.on_ping_success = pingSuccess;
  cbs.on_ping_timeout = pingTimeout;
  cbs.on_ping_end = NULL; 
  cbs.cb_args = NULL;
  esp_ping_new_session(&pingConfig, &cbs, &pingHandle);
  esp_ping_start(pingHandle);
  LOG_INF("Started ping monitoring ");
}


/************************** NTP  **************************/

char timezone[64] = "GMT0BST,M3.5.0/01,M10.5.0/02"; 

static inline time_t getEpoch() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec;
}

void dateFormat(char* inBuff, size_t inBuffLen, bool isFolder) {
  // construct filename from date/time
  time_t currEpoch = getEpoch();
  if (isFolder) strftime(inBuff, inBuffLen, "/%Y%m%d", localtime(&currEpoch));
  else strftime(inBuff, inBuffLen, "/%Y%m%d/%Y%m%d_%H%M%S", localtime(&currEpoch));
}

bool getLocalNTP() {
  // get current time from NTP server and apply to ESP32
  const char* ntpServer = "pool.ntp.org";
  configTzTime(timezone, ntpServer);
  if (getEpoch() > 10000) {
    time_t currEpoch = getEpoch();
    char timeFormat[20];
    strftime(timeFormat, sizeof(timeFormat), "%d/%m/%Y %H:%M:%S", localtime(&currEpoch));
    timeSynchronized = true;
    LOG_INF("Got current time from NTP: %s", timeFormat);
    //RAT
    struct tm now;
    getLocalTime(&now, 0);
    Serial.print("@ ");
    Serial.println(&now, "%Y:%m:%d:%H:%M:%S");
    //RAT
    return true;
  }
  else {
    LOG_WRN("Not yet synced with NTP");
    return false;
  }

}

void syncToBrowser(const char *val) {
  if (timeSynchronized) return;
  
  // Synchronize clock to browser clock if no sync with NTP
  LOG_INF("Sync clock to: %s with tz:%s", val, timezone);
  struct tm now;
  getLocalTime(&now, 0);

  int Year, Month, Day, Hour, Minute, Second ;
  sscanf(val, "%d-%d-%dT%d:%d:%d", &Year, &Month, &Day, &Hour, &Minute, &Second);

  struct tm t;
  t.tm_year = Year - 1900;
  t.tm_mon  = Month - 1;    // Month, 0 - jan
  t.tm_mday = Day;          // Day of the month
  t.tm_hour = Hour;
  t.tm_min  = Minute;
  t.tm_sec  = Second;

  time_t t_of_day = mktime(&t);
  timeval epoch = {t_of_day, 0};
  struct timezone utc = {0, 0};
  settimeofday(&epoch, &utc);
  //setenv("TZ", timezone, 1);
//  Serial.print(&now, "Before sync: %B %d %Y %H:%M:%S (%A) ");
  getLocalTime(&now, 0);
//  Serial.println(&now, "After sync: %B %d %Y %H:%M:%S (%A)");
  timeSynchronized = true;
  //RAT
   Serial.print("@ ");
  Serial.println(&now, "%Y:%m:%d:%H:%M:%S   ");
//RAT
}





void getUpTime(char* timeVal) {
  uint32_t secs = millis() / 1000; //convert milliseconds to seconds
  uint32_t mins = secs / 60; //convert seconds to minutes
  uint32_t hours = mins / 60; //convert minutes to hours
  uint32_t days = hours / 24; //convert hours to days
  secs = secs - (mins * 60); //subtract the converted seconds to minutes in order to display 59 secs max
  mins = mins - (hours * 60); //subtract the converted minutes to hours in order to display 59 minutes max
  hours = hours - (days * 24); //subtract the converted hours to days in order to display 23 hours max
  sprintf(timeVal, "%u-%02u:%02u:%02u", days, hours, mins, secs);
}


/********************** misc functions ************************/

bool startSpiffs(bool deleteAll) {
  if (!SPIFFS.begin(true)) {
    LOG_ERR("SPIFFS not mounted");
    return false;
  } else {    
    // list details of files on SPIFFS
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while(file) {
      LOG_INF("File: %s, size: %u", file.path(), file.size());
      if (deleteAll) {
        SPIFFS.remove(file.path());
        LOG_WRN("Deleted %s", file.path());
      }
      file = root.openNextFile();
    }
    LOG_INF("SPIFFS: Total bytes %d, Used bytes %d", SPIFFS.totalBytes(), SPIFFS.usedBytes());
    LOG_INF("Sketch size %d kB", ESP.getSketchSize() / 1024);
    return true;
  }
}

bool changeExtension(char* outName, const char* inName, const char* newExt) {
  // replace original file extension with supplied extension
  size_t inNamePtr = strlen(inName);
  // find '.' before extension text
  while (inNamePtr > 0 && inName[inNamePtr] != '.') inNamePtr--;
  inNamePtr++;
  size_t extLen = strlen(newExt);
  memcpy(outName, inName, inNamePtr);
  memcpy(outName + inNamePtr, newExt, extLen);
  outName[inNamePtr + extLen] = 0;
  return (inNamePtr > 1) ? true : false;
}

void showProgress() {
  // show progess as dots if not verbose
  static uint8_t dotCnt = 0;
////  if (!dbgVerbose) {
    Serial.print("."); // progress marker
    if (++dotCnt >= 50) {
      dotCnt = 0;
      Serial.println("");
////    }
    Serial.flush();
  }
}

void urlDecode(char* inVal) {
  // replace url encoded characters
  std::string decodeVal(inVal); 
  std::string replaceVal = decodeVal;
  std::smatch match; 
  while (regex_search(decodeVal, match, std::regex("(%)([0-9A-Fa-f]{2})"))) {
    std::string s(1, static_cast<char>(std::strtoul(match.str(2).c_str(),nullptr,16))); // hex to ascii 
    replaceVal = std::regex_replace(replaceVal, std::regex(match.str(0)), s);
    decodeVal = match.suffix().str();
  }
  strcpy(inVal, replaceVal.c_str());
}

void listBuff (const uint8_t* b, size_t len) {
  // output buffer content as hex, 16 bytes per line
  if (!len || !b) LOG_WRN("Nothing to print");
  else {
    for (size_t i = 0; i < len; i += 16) {
      int linelen = (len - i) < 16 ? (len - i) : 16;
      for (size_t k = 0; k < linelen; k++) printf(" %02x", b[i+k]);
      puts(" ");
    }
  }
}

size_t isSubArray(uint8_t* haystack, uint8_t* needle, size_t hSize, size_t nSize) {
  // find a subarray (needle) in another array (haystack)
  size_t h = 0, n = 0; // Two pointers to traverse the arrays
  // Traverse both arrays simultaneously
  while (h < hSize && n < nSize) {
    // If element matches, increment both pointers
    if (haystack[h] == needle[n]) {
      h++;
      n++;
      // If needle is completely traversed
      if (n == nSize) return h; // position of end of needle
    } else {
      // if not, increment h and reset n
      h = h - n + 1;
      n = 0;
    }
  }
  return 0; // not found
}

void removeChar(char *s, char c) {
  // remove specified character from string
  int writer = 0, reader = 0;
  while (s[reader]) {
    if (s[reader] != c) s[writer++] = s[reader];
    reader++;       
  }
  s[writer] = 0;
}

void checkMemory() {
  LOG_INF("Free: heap %u, block: %u, pSRAM %u", ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), ESP.getFreePsram());
}

void doRestart(String restartStr) {
  flush_log(true);
  LOG_WRN("Controlled restart: %s", restartStr.c_str());
  updateStatus("restart", restartStr.c_str());
  updateStatus("save", "1");
  delay(2000);
  ESP.restart();
}

/*********************** Remote loggging ***********************/
/*
 * Log mode selection in user interface: 0-Serial, 1-log.txt, 2-telnet
 * 0 : log to serial monitor only
 * 1 : saves log on SD card. To download the log generated, either:
 *     - To view the log, press Show Log button on the browser
  *     - To clear the log file contents, on log web page press Clear Log link
 * 2 : run telnet <ip address> 443 on a remote host eg PuTTY
 * To close an SD or Telnet connection, select log mode 0
 */

#define LOG_FORMAT_BUF_LEN 512
#define LOG_PORT 443 // Define telnet port
#define WRITE_CACHE_CYCLE 5
byte logMode = 0; // 0 - Disabled, log to serial port only, 1 - Internal log to sdcard file, 2 - Remote telnet on port 443
static int log_serv_sockfd = -1;
static int log_sockfd = -1;
static struct sockaddr_in log_serv_addr, log_cli_addr;
static char fmt_buf[LOG_FORMAT_BUF_LEN];
static FILE* log_remote_fp = NULL;
static uint32_t counter_write = 0;

static void remote_log_free_telnet() {
  if (log_sockfd != -1) {
    LOG_DBG("Sending telnet quit string Ctrl ]"); 
    send(log_sockfd, "^]\n\rquit\n\r", 10, 0);
    delay(100);
    close(log_sockfd);
    log_sockfd = -1;
  }
      
  if (log_serv_sockfd != -1) {
    if (close(log_serv_sockfd) != 0) LOG_ERR("Cannot close the socket");        
    log_serv_sockfd = -1;    
    LOG_INF("Closed telnet connection");
  }
}

static void remote_log_init_telnet() {
  LOG_DBG("Initialize telnet remote log");
  memset(&log_serv_addr, 0, sizeof(log_serv_addr));
  memset(&log_cli_addr, 0, sizeof(log_cli_addr));

  log_serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  log_serv_addr.sin_family = AF_INET;
  log_serv_addr.sin_port = htons(LOG_PORT);

  if ((log_serv_sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
    LOG_ERR("Failed to create socket, fd value: %d", log_serv_sockfd);
    return;
  }
  LOG_DBG("Socket FD is %d", log_serv_sockfd);

  int reuse_option = 1;
  if (setsockopt(log_serv_sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse_option, sizeof(reuse_option)) < 0) {
    LOG_ERR("Failed to set reuse, returned %s", strerror(errno));
    remote_log_free_telnet();
    return;
  }

  if (bind(log_serv_sockfd, (struct sockaddr *)&log_serv_addr, sizeof(log_serv_addr)) < 0) {
    LOG_ERR("Failed to bind the port, reason: %s", strerror(errno));
    remote_log_free_telnet();
    return;
  }

  if (listen(log_serv_sockfd, 1) != 0) {
    LOG_ERR("Server failed to listen");
    return;
  }

  // Set timeout
  struct timeval timeout = {
    .tv_sec = 30,
    .tv_usec = 0
  };

  // Set receive timeout
  if (setsockopt(log_serv_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
      LOG_ERR("Setting receive timeout failed");
      remote_log_free_telnet();
      return;
  }

  // Set send timeout
  if (setsockopt(log_serv_sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
    LOG_ERR("Setting send timeout failed");
    remote_log_free_telnet();
    return;
  }
  LOG_INF("Server created, make telnet connection within 30 seconds");

  size_t cli_addr_len = sizeof(log_cli_addr);
  if ((log_sockfd = accept(log_serv_sockfd, (struct sockaddr *)&log_cli_addr, &cli_addr_len)) < 0) {
      LOG_WRN("Failed to accept, returned: %s", strerror(errno));
      remote_log_free_telnet();
      return;
  }
  LOG_INF("Established telnet connection");
}

void flush_log(bool andClose) {
  if (log_remote_fp != NULL) {
    fsync(fileno(log_remote_fp));  
    if (andClose) {
      LOG_INF("Closed SD file for logging");
      fflush(log_remote_fp);
      fclose(log_remote_fp);
      log_remote_fp = NULL;
    } else delay(1000);
  }  
}

static void remote_log_init_SD() {
  SD_MMC.mkdir(DATA_DIR);
  // Open remote file
  log_remote_fp = NULL;
  log_remote_fp = fopen("/sdcard" LOG_FILE_PATH, "a");
  if (log_remote_fp == NULL) {LOG_ERR("Failed to open SD log file %s", LOG_FILE_PATH);}
  else {LOG_INF("Ospened SD file for logging");}
}

void reset_log(){
  flush_log(true); // Close log file
  SD_MMC.remove(LOG_FILE_PATH);
  LOG_INF("Cleared log file");
  if (logMode == 1) remote_log_init_SD();    
}

void remote_log_init() {
  LOG_INF("Enabling logging mode %d", logMode);
  // close off any existing remote logging
  if (logMode == 1) flush_log(false);
  else flush_log(true);
  remote_log_free_telnet();
  // setup required mode
  if (logMode == 1) remote_log_init_SD();
  if (logMode == 2) remote_log_init_telnet();
}

void logPrint(const char *fmtStr, ...) {  
  va_list arglist;
  va_start(arglist, fmtStr);
  //RAT
  if (logMode == 0)
  {
    vprintf(fmtStr, arglist); // serial monitor
  }
  //RAT
  if (log_remote_fp != NULL) { // log.txt
    vfprintf(log_remote_fp, fmtStr, arglist);
    // periodic sync to SD
    if (counter_write++ % WRITE_CACHE_CYCLE == 0) fsync(fileno(log_remote_fp));
  }
  if (log_sockfd != -1) { // telnet
    int len = vsprintf((char*)fmt_buf, fmtStr, arglist);
    fmt_buf[len++] = '\r'; // in case terminal expects carriage return
    // Send the log message, or terminate connection if unsuccessful
    if (send(log_sockfd, fmt_buf, len, 0) < 0) remote_log_free_telnet();
  }
  va_end(arglist);
  delay(FLUSH_DELAY);
}


/****************** base 64 ******************/

#define BASE64 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"

const uint8_t* encode64chunk(const uint8_t* inp, int rem) {
  // receive 3 byte input buffer and return 4 byte base64 buffer
  rem = 3 - rem; // last chunk may be less than 3 bytes 
  uint32_t buff = 0; // hold 3 bytes as shifted 24 bits
  static uint8_t b64[4];
  // shift input into buffer
  for (int i = 0; i < 3 - rem; i++) buff |= inp[i] << (8*(2-i)); 
  // shift 6 bit output from buffer and encode
  for (int i = 0; i < 4 - rem; i++) b64[i] = BASE64[buff >> (6*(3-i)) & 0x3F]; 
  // filler for last chunk if less than 3 bytes
  for (int i = 0; i < rem; i++) b64[3-i] = '='; 
  return b64;
}

const char* encode64(const char* inp) {
  // helper to base64 encode strings up to 90 chars long
  static char encoded[121]; // space for 4/3 expansion + terminator
  encoded[0] = 0;
  int len = strlen(inp);
  if (len > 90) {
    LOG_WRN("Input string too long: %u chars", len);
    len = 90;
  }
  for (int i = 0; i < len; i += 3) 
    strncat(encoded, (char*)encode64chunk((uint8_t*)inp + i, min(len - i, 3)), 4);
  return encoded;
}

/******************* battery monitoring *********************/

// if pin 33 used as input for battery voltage, set VOLTAGE_DIVIDER value to be divisor 
// of input voltage from resistor divider, or 0 if battery voltage not being monitored
#define BATT_PIN ADC1_CHANNEL_5 // ADC pin 33 for monitoring battery voltage
#define DEFAULT_VREF 1100 // if eFuse or two point not available on old ESPs
static esp_adc_cal_characteristics_t *adc_chars; // holds ADC characteristics
static const adc_atten_t ADCatten = ADC_ATTEN_DB_11; // attenuation level
static const adc_unit_t ADCunit = ADC_UNIT_1; // using ADC1
static const adc_bits_width_t ADCbits = ADC_WIDTH_BIT_11; // ADC bit resolution
float currentVoltage = -1.0; // no monitoring

static void battVoltage() {
  // get multiple readings of battery voltage from ADC pin and average
  // input battery voltage may need to be reduced by voltage divider resistors to keep it below 3V3.
  #define NO_OF_SAMPLES 16 // ADC multisampling
  uint32_t ADCsample = 0;
  static bool sentEmailAlert = false;
  for (int j = 0; j < NO_OF_SAMPLES; j++) ADCsample += adc1_get_raw(BATT_PIN); 
  ADCsample /= NO_OF_SAMPLES;
  // convert ADC averaged pin value to curve adjusted voltage in mV
  if (ADCsample > 0) ADCsample = esp_adc_cal_raw_to_voltage(ADCsample, adc_chars);
  currentVoltage = ADCsample*VOLTAGE_DIVIDER/1000.0; // convert to battery volts
  if (currentVoltage < LOW_VOLTAGE && !sentEmailAlert) {
    sentEmailAlert = true; // only sent once per esp32 session
    smtpBufferSize = 0; // no attachment
    char battMsg[20];
    sprintf(battMsg, "Voltage is %0.1fV", currentVoltage);
#ifdef INCLUDE_SMTP
    if (USE_SMTP) emailAlert("Low battery", battMsg);
#endif
  }
}

static void battTask(void* parameter) {
  delay(20 * 1000); // allow time for esp32 to start up
  while (true) {
    battVoltage();
    delay(BATT_INTERVAL * 60 * 1000); // mins
  }
  vTaskDelete(NULL);
}

void setupADC() {
  // Characterise ADC to generate voltage curve for battery monitoring 
  if (VOLTAGE_DIVIDER) {
    adc1_config_width(ADCbits);
    adc1_config_channel_atten(BATT_PIN, ADCatten);
    adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADCunit, ADCatten, ADCbits, DEFAULT_VREF, adc_chars);
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {LOG_INF("ADC characterised using eFuse Two Point Value");}
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {LOG_INF("ADC characterised using eFuse Vref");}
    else {LOG_INF("ADC characterised using Default Vref");}
    xTaskCreate(&battTask, "battTask", 2048, NULL, 1, NULL);
  }
}
