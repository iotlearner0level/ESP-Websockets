#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
//#include <Adafruit_BMP085.h>
#include <Ticker.h>
#include "credentials.h"
#include <ESP8266mDNS.h>
#include <FS.h>
#define ONE_HOUR 3600000UL
#include <WiFiUdp.h>
#define NODEBUG_WEBSOCKETS
ADC_MODE(ADC_VCC);
Ticker timer;


// Running a web server
ESP8266WebServer server;
File fsUploadFile; 

// Adding a websocket to the server
WebSocketsServer webSocket = WebSocketsServer(81);

// Serving a web page (from flash memory)
// formatted as a string literal!
char webpage[] PROGMEM = R"=====(
<html>
<!-- Adding a data chart using Chart.js -->
<head>
  <script src='https://cdnjs.cloudflare.com/ajax/libs/Chart.js/2.5.0/Chart.min.js'></script>
</head>
<body onload="javascript:init()">
<!-- Adding a slider for controlling data rate -->
<div>
  <input type="range" min="0.1" max="10" value="5" id="dataRateSlider" oninput="sendDataRate()" />
  <label for="dataRateSlider" id="dataRateLabel">Rate: 0.2Hz</label>
</div>
<hr />
<div>
  <canvas id="line-chart" width="800" height="450"></canvas>
</div>
<!-- Adding a websocket to the client (webpage) -->
<script>
  var webSocket, dataPlot;
  var maxDataPoints = 20;
  function removeData(){
    dataPlot.data.labels.shift();
    dataPlot.data.datasets[0].data.shift();
  }
  function addData(label, data) {
    if(dataPlot.data.labels.length > maxDataPoints) removeData();
    dataPlot.data.labels.push(label);
    dataPlot.data.datasets[0].data.push(data);
    dataPlot.update();
  }
  var incomingdata;
  function init() {
    webSocket = new WebSocket('ws://' + window.location.hostname + ':81/');
    dataPlot = new Chart(document.getElementById("line-chart"), {
      type: 'line',
      data: {
        labels: [],
        datasets: [{
          data: [],
          label: "Temperature (C)",
          borderColor: "#3e95cd",
          fill: false
        }]
      }
    });
    webSocket.onmessage = function(event) {
     console.log("Received data:");console.log(event.data);
      var data = JSON.parse(event.data);
      incomingdata=event.data;
      var today = new Date();
      var t = today.getHours() + ":" + today.getMinutes() + ":" + today.getSeconds();
      addData(t, data.value);
      console.log("Parsed data:");console.log(data.value);
    }
  }
  function sendDataRate(){
    var dataRate = document.getElementById("dataRateSlider").value;
    webSocket.send(dataRate);
    //dataRate = dataRate;
    dataRate = 1.0/dataRate;
 //   document.getElementById("dataRateLabel").innerHTML = "Rate: " + dataRate + "Hz";
    document.getElementById("dataRateLabel").innerHTML = "Rate: " + dataRate.toFixed(2) + "Hz";

  }
</script>
</body>
</html>
)=====";
WiFiUDP UDP;                   // Create an instance of the WiFiUDP class to send and receive UDP messages

IPAddress timeServerIP;        // The time.nist.gov NTP server's IP address
const char* ntpServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48;          // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[NTP_PACKET_SIZE];      // A buffer to hold incoming and outgoing packets
const unsigned long intervalNTP = ONE_HOUR; // Update the time every hour
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();

const unsigned long intervalTemp = 60000;   // Do a temperature measurement every minute
unsigned long prevTemp = 0;
bool tmpRequested = false;
const unsigned long DS_delay = 750;         // Reading the temperature from the DS18x20 can take up to 750ms

uint32_t timeUNIX = 0;  
void setup() {
  // put your setup code here, to run once:
  WiFi.begin(ssid, password);
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  while(WiFi.status()!=WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  server.on("/",[](){
    server.send_P(200, "text/html", webpage);
  });
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
//  bmp.begin();
  timer.attach(5, getData);
  startUDP();  
    WiFi.hostByName(ntpServerName, timeServerIP); // Get the IP address of the NTP server
  Serial.print("Time server IP:\t");
  Serial.println(timeServerIP);

  sendNTPpacket(timeServerIP);
  delay(500);
}

void loop() {


   unsigned long currentMillis = millis();

  if (currentMillis - prevNTP > intervalNTP) { // Request the time from the time server every hour
    prevNTP = currentMillis;
    sendNTPpacket(timeServerIP);
  }

  uint32_t time = getTime();                   // Check if the time server has responded, if so, get the UNIX time
  if (time) {
    timeUNIX = time;
    Serial.print("NTP response:\t");
    Serial.println(timeUNIX);
    lastNTPResponse = millis();
  } else if ((millis() - lastNTPResponse) > 24UL * ONE_HOUR) {
    Serial.println("More than 24 hours since last NTP response. Rebooting.");
    Serial.flush();
    ESP.reset();
  }

  if (timeUNIX != 0) {
    if (currentMillis - prevTemp > intervalTemp) {  // Every minute, request the temperature
//      tempSensors.requestTemperatures(); // Request the temperature from the sensor (it takes some time to read it)
      tmpRequested = true;
      prevTemp = currentMillis;
      Serial.println("Temperature requested");
    }
    if (currentMillis - prevTemp > DS_delay && tmpRequested) { // 750 ms after requesting the temperature
      uint32_t actualTime = timeUNIX + (currentMillis - lastNTPResponse) / 1000;
      // The actual time is the last NTP time plus the time that has elapsed since the last NTP response
      tmpRequested = false;
      float temp = ESP.getVcc();
      //tempSensors.getTempCByIndex(0); // Get the temperature from the sensor
      temp = round(temp * 100.0) / 100.0; // round temperature to 2 digits

      Serial.printf("Appending temperature to file: %lu,", actualTime);
      Serial.println(temp);
      File tempLog = SPIFFS.open("/temp.csv", "a"); // Write the time and the temperature to the csv file
      tempLog.print(actualTime);
      tempLog.print(',');
      tempLog.println(ESP.getVcc());
      tempLog.close();
    }
  } else {                                    // If we didn't receive an NTP response yet, send another request
    sendNTPpacket(timeServerIP);
    delay(500);
  }
  yield();
  // put your main code here, to run repeatedly:
  webSocket.loop();
  server.handleClient();


  
}


void startUDP() {
  Serial.println("Starting UDP");
  UDP.begin(123);                          // Start listening for UDP messages to port 123
  Serial.print("Local port:\t");
  Serial.println(UDP.localPort());
}
void getData() {
  int vcc = ESP.getVcc();Serial.println(vcc);
//  Serial.println(bmp.readTemperature());
  String json = "{\"value\":";
  json+=vcc;
     json += ",\"heap\":",
  json+=ESP.getFreeHeap();
       json += ",\"getBootVersion\":";
  json+=ESP.getBootVersion();
 
     json += ",\"ChipId\":";
  json+=ESP.getChipId();
     json += ",\"Mhz\":";
  json+=ESP.getCpuFreqMHz();
     json += ",\"CycleCount\":";
  json+=ESP.getCycleCount();
     json += ",\"sdkVer\":";
  json+="\""+(String)ESP.getSdkVersion()+"\"";
     json += ",\"FlashSize\":";
  json+=ESP.getFlashChipSize();
     json += ",\"FlashChipRealSize\":";
  json+=ESP.getFlashChipRealSize();
     json += ",\"FlashChipVendorId\":";
  json+=ESP.getFlashChipVendorId();
     json += ",\"ChipSpeed\":";
  json+=ESP.getFlashChipSpeed();
     json += ",\"getFlashChipSizeByChipId\":";
  json+=ESP.getFlashChipSizeByChipId();
     json += ",\"SketchSize\":";
  json+=ESP.getSketchSize();
     json += ",\"FreeSketchSpace\":";
  json+=ESP.getFreeSketchSpace();
     json += ",\"ResetReason\":";
  json+="\""+ESP.getResetReason()+"\"";
     json += ",\"ResetInfo\":";
  json+="\""+ESP.getResetInfo()+"\"";



  json += "}";
//  Serial.println("Json string is:"+json);
//  Serial.println("Converted Cstyle string is:");Serial.println(json.c_str());
//  Serial.println("Length:");json.length();
  webSocket.broadcastTXT(json.c_str(), json.length());
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){
  // Do something with the data from the client
  if(type == WStype_TEXT){
    float dataRate = (float) atof((const char *) &payload[0]);
    timer.detach();
    timer.attach(dataRate, getData);
  }
}
void sendNTPpacket(IPAddress& address) {
  Serial.println("Sending NTP request");
  memset(packetBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
  // Initialize values needed to form NTP request
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode

  // send a packet requesting a timestamp:
  UDP.beginPacket(address, 123); // NTP requests are to port 123
  UDP.write(packetBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}
unsigned long getTime() { // Check if the time server has responded, if so, get the UNIX time, otherwise, return 0
  if (UDP.parsePacket() == 0) { // If there's no response (yet)
    return 0;
  }
  UDP.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  // Combine the 4 timestamp bytes into one 32-bit number
  uint32_t NTPTime = (packetBuffer[40] << 24) | (packetBuffer[41] << 16) | (packetBuffer[42] << 8) | packetBuffer[43];
  // Convert NTP time to a UNIX timestamp:
  // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  const uint32_t seventyYears = 2208988800UL;
  // subtract seventy years:
  uint32_t UNIXTime = NTPTime - seventyYears;
  return UNIXTime;
}
bool handleFileRead(String path) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";          // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
    if (SPIFFS.exists(pathWithGz))                         // If there's a compressed version available
      path += ".gz";                                         // Use the compressed verion
    File file = SPIFFS.open(path, "r");                    // Open the file
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
  return false;
}

void handleFileUpload() { // upload a new file to the SPIFFS
  HTTPUpload& upload = server.upload();
  String path;
  if (upload.status == UPLOAD_FILE_START) {
    path = upload.filename;
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.endsWith(".gz")) {                         // The file server always prefers a compressed version of a file
      String pathWithGz = path + ".gz";                  // So if an uploaded file is not compressed, the existing compressed
      if (SPIFFS.exists(pathWithGz))                     // version of that file must be deleted (if it exists)
        SPIFFS.remove(pathWithGz);
    }
    Serial.print("handleFileUpload Name: "); Serial.println(path);
    fsUploadFile = SPIFFS.open(path, "w");               // Open the file for writing in SPIFFS (create if it doesn't exist)
    path = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {                                   // If the file was successfully created
      fsUploadFile.close();                               // Close the file again
      Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
      server.sendHeader("Location", "/success.html");     // Redirect the client to the success page
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}

/*__________________________________________________________HELPER_FUNCTIONS__________________________________________________________*/

String formatBytes(size_t bytes) { // convert sizes in bytes to KB and MB
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }
}

String getContentType(String filename) { // determine the filetype of a given filename, based on the extension
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}
