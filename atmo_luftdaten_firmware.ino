#include <DHT.h>
#include <DHT_U.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <QuickStats.h>
#include <SdsDustSensor.h>

#define HOST_DUSTI "api.luftdaten.info"
#define URL_DUSTI "/v1/push-sensor-data/"
#define PORT_DUSTI 80

#define HOST_MADAVI "api-rrd.madavi.de"
#define URL_MADAVI "/data.php"
#define PORT_MADAVI 80

#define SDS_API_PIN 1
#define DHT_API_PIN 5

// Settings
#define CONNECT_TIME_S 120
#define WARMUP_TIME_S 15
#define SAMPLE_TIME_S 15
#define SLEEP_TIME_S 1800
#define ARRAY_SIZE 32

// Debug level
#define DEBUG_ERROR 1
#define DEBUG_WARNING 2
#define DEBUG_MIN_INFO 3
#define DEBUG_MED_INFO 4
#define DEBUG_MAX_INFO 5

// increment on change
#define SOFTWARE_VERSION "ATMO-1.0"

const char TXT_CONTENT_TYPE_JSON[] PROGMEM = "application/json";
const char data_first_part[] PROGMEM = "{\"software_version\": \"{v}\", \"sensordatavalues\":[";
const char DBG_TXT_SENDING_TO[] PROGMEM = "## Sending to ";
const int HTTP_PORT_DUSTI = 80;

String esp_chipid;

QuickStats stats;
SdsDustSensor sds(0, 4);
DHT dht(5, DHT22);

float p10_array[ARRAY_SIZE];
float p25_array[ARRAY_SIZE];
int array_index = 0;

/*****************************************************************
 * Debug output                                                  *
 *****************************************************************/
 
void debug_out(const String& text, const int level, const bool linebreak) {
  
  if (linebreak) Serial.println(text);
  else Serial.print(text);
}

/*****************************************************************
 * convert float to string with a                                *
 * precision of two (or a given number of) decimal places        *
 *****************************************************************/
 
String Float2String(const double value) {
  return Float2String(value, 2);
}

String Float2String(const double value, uint8_t digits) {
  // Convert a float to String with two decimals.
  char temp[15];

  dtostrf(value, 13, digits, temp);
  String s = temp;
  s.trim();
  return s;
}

/*****************************************************************
 * convert value to json string                                  *
 *****************************************************************/
 
String Value2Json(const String& type, const String& value) {
  String s = F("{\"value_type\":\"{t}\",\"value\":\"{v}\"},");
  s.replace("{t}", type);
  s.replace("{v}", value);
  return s;
}

/*****************************************************************
 * convert uint64 value to string                                  *
 *****************************************************************/
 
String uint64ToString(uint64_t input) {
  
  String result = "";
  uint8_t base = 10;
  
  do {

    char c = input % base;
    input /= base;
  
    if (c < 10) c +='0';
    else c += 'A' - 10;
      
    result = c + result;
    
  } while (input);
  
  return result;
}

/*****************************************************************
 * send data to rest api                                         *
 *****************************************************************/

void sendData(const String& data, const int pin, const char* host, const int httpPort, const char* url, const bool verify, const char* basic_auth_string, const String& contentType) {
//#include "ca-root.h"

  debug_out(F("Start connecting to "), DEBUG_MIN_INFO, 0);
  debug_out(host, DEBUG_MIN_INFO, 1);

  String request_head = F("POST ");
  request_head += String(url);
  request_head += F(" HTTP/1.1\r\n");
  request_head += F("Host: ");
  request_head += String(host) + "\r\n";
  request_head += F("Content-Type: ");
  request_head += contentType + "\r\n";
  if (strlen(basic_auth_string) != 0) {
    request_head += F("Authorization: Basic ");
    request_head += String(basic_auth_string) + "\r\n";
  }
  request_head += F("X-PIN: ");
  request_head += String(pin) + "\r\n";
  request_head += F("X-Sensor: esp8266-");
  request_head += esp_chipid + "\r\n";
  request_head += F("Content-Length: ");
  request_head += String(data.length(), DEC) + "\r\n";
  request_head += F("Connection: close\r\n\r\n");

  const auto doConnect = [ = ](WiFiClient * client) -> bool {
    client->setNoDelay(true);
    client->setTimeout(20000);

    if (!client->connect(host, httpPort)) {
      debug_out(F("connection failed"), DEBUG_ERROR, 1);
      return false;
    }
    return true;
  };

  const auto doRequest = [ = ](WiFiClient * client) {
    debug_out(F("Requesting URL: "), DEBUG_MIN_INFO, 0);
    debug_out(url, DEBUG_MIN_INFO, 1);
    debug_out(esp_chipid, DEBUG_MIN_INFO, 1);
    debug_out(data, DEBUG_MIN_INFO, 1);

    // send request to the server
    client->print(request_head);

    client->println(data);

    // wait for response
    int retries = 20;
    while (client->connected() && !client->available()) {
      delay(100);
      //wdt_reset();
      if (!--retries)
        break;
    }

    // Read reply from server and print them
    while(client->available()) {
      char c = client->read();
      debug_out(String(c), DEBUG_MIN_INFO, 0);
    }
    client->stop();
    debug_out(F("\nclosing connection\n----\n\n"), DEBUG_MIN_INFO, 1);
  };

  // Use WiFiClient class to create TCP connections
  if (httpPort == 443) {
    WiFiClientSecure client_s;
    if (doConnect(&client_s)) {
      doRequest(&client_s);
    }

  } else {
    WiFiClient client;
    if (doConnect(&client)) {
      doRequest(&client);
    }
  }
  debug_out(F("End connecting to "), DEBUG_MIN_INFO, 0);
  debug_out(host, DEBUG_MIN_INFO, 1);

  //wdt_reset(); // nodemcu is alive
  yield();
}

/*****************************************************************
 * send single sensor data to luftdaten.info api                 *
 *****************************************************************/
 
void sendLuftdaten(const String& data, const int pin, const char* host, const int httpPort, const char* url, const bool verify, const char* replace_str) {
  
  String data_4_dusti = FPSTR(data_first_part);
  data_4_dusti.replace("{v}", SOFTWARE_VERSION);
  data_4_dusti += data;
  data_4_dusti.remove(data_4_dusti.length() - 1);
  data_4_dusti.replace(replace_str, "");
  data_4_dusti += "]}";
  
  if (data != "") sendData(data_4_dusti, pin, host, httpPort, url, verify, "", FPSTR(TXT_CONTENT_TYPE_JSON));
  
  else debug_out(F("No data sent..."), DEBUG_MIN_INFO, 1);
}

void setup() {

  Serial.begin(115200);
  pinMode(15, OUTPUT);
  digitalWrite(15, LOW);
  sds.begin();
  dht.begin();

  esp_chipid = String(ESP.getChipId());

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(CONNECT_TIME_S);
  wifiManager.autoConnect("AtmoPodule");

  if (WiFi.status() == WL_CONNECTED) {

    digitalWrite(15, HIGH);

    // Warmup
    while (millis() < WARMUP_TIME_S*1000) {

      unsigned long loop_time = millis();
      debug_out(F("Warmup..."), DEBUG_MIN_INFO, 1);
      while (millis() - loop_time < 1000) yield();
    }
  
    // Sample
    while (millis() < (WARMUP_TIME_S + SAMPLE_TIME_S)*1000) {

      unsigned long loop_time = millis();
      
      PmResult pm = sds.readPm();      
      Serial.print("PM10  : ");
      Serial.println(pm.pm10);
      Serial.print("PM2.5 : ");
      Serial.println(pm.pm25);

      // store new value
      p10_array[array_index] = pm.pm10;
      p25_array[array_index] = pm.pm25;
      array_index = (array_index + 1) % ARRAY_SIZE;
      
      while (millis() - loop_time < 1000) yield();
    }

    digitalWrite(15, LOW);

    // Calculate PM
    float p10_filtered = stats.median(p10_array, array_index);
    float p25_filtered = stats.average(p25_array, array_index);
    String result_SDS = "";
    result_SDS += Value2Json("SDS_P1", Float2String(p10_filtered));
    result_SDS += Value2Json("SDS_P2", Float2String(p25_filtered));

    // Get temp/hum
    String result_DHT = "";
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    if (!isnan(temperature)) result_DHT += Value2Json(F("temperature"), Float2String(temperature));
    if (!isnan(humidity)) result_DHT += Value2Json(F("humidity"), Float2String(humidity));
    
    sendLuftdaten(result_SDS, SDS_API_PIN, HOST_DUSTI, HTTP_PORT_DUSTI, URL_DUSTI, true, "SDS_");
    
    if (result_DHT != "") sendLuftdaten(result_DHT, DHT_API_PIN, HOST_DUSTI, HTTP_PORT_DUSTI, URL_DUSTI, true, "DHT_");
  
    // Build data string
    String data = FPSTR(data_first_part);
    data.replace("{v}", SOFTWARE_VERSION);
    String signal_strength = String(WiFi.RSSI());
    data += result_SDS;
    data += result_DHT;
    data += Value2Json("signal", signal_strength);
  
    if ((unsigned)(data.lastIndexOf(',') + 1) == data.length()) {
      data.remove(data.length() - 1);
    }
    data += "]}";
  
    debug_out(String(FPSTR(DBG_TXT_SENDING_TO)) + F("madavi.de: "), DEBUG_MIN_INFO, 1);
    sendData(data, 0, HOST_MADAVI, 80, URL_MADAVI, true, "", FPSTR(TXT_CONTENT_TYPE_JSON));
  }

  Serial.println("Sleeping");
  ESP.deepSleep((unsigned long) SLEEP_TIME_S*1000000);
}

void loop() {

}
