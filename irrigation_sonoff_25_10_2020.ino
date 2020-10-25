// nastavenia arduino ide - board (generic esp8285, flash size: 1MB (FS: 64KB OTA: 470KB)

// usage:
// button1 => zone1
// button2 => zone2
// button3 => zone3
// button4 => zone4
// button1 + button4 => factory reset
// button2 + button3 => start program

#include <Arduino.h>
#include <ESP8266WiFi.h>           //https://github.com/esp8266/Arduino
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager    //get data from darkSky
#include <ESP8266HTTPClient.h>    // include client to connect extender
#include "FS.h"                   // handle config SPIFFS
#include <ArduinoJson.h>
#include "Relay.h"
#include <NtpClientLib.h> 
#include <TimeAlarms.h> //CronJobs
#include <TimeLib.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include "TinyUPnP.h"
#include <EasyDDNS.h>

AlarmId TurnAllOff;

bool wifiFirstConnected = false;
const char* serverIndex PROGMEM = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

boolean syncEventTriggered = false; // True if a time even has been triggered
NTPSyncEvent_t ntpEvent; // Last triggered event
float timezone  = 1;
int ntp_failed  = 0;
bool gotNtpTime  = false;
int p_year;
int n_year;
int ProgramRunning  = 0;
int upnp_init = 0; 

struct Config {
  String morning = "6:30";
  String evening = "18:30";
  String program = "p1_10-p2_10-p3_10-p4_10";
  int monday = 0;
  int tuesday = 0;
  int wednesday = 0;
  int thursday = 0;
  int friday = 0;
  int saturday = 0;
  int sunday = 0;
  String password = "admin";
  String login = "true";
  String ddns_service_name = "dyndns";
  String ddns_hostname = "your-ddns-domain";
  String ddns_login = "your-login";
  String ddns_passwd = "your-passwd";
  int server_port = 80;
  String upnp = "true";
  String ddns = "true";
};

Config config;

TinyUPnP tinyUPnP(20000);
ESP8266WebServer *server;

//buttons / pinouts

const int zone_1 = 12;                            
const int zone_2 =  5;                             
const int zone_3 =  4;                           
const int zone_4 = 15;    
const int led    = 13;                       

int z_1_status = 0;
int z_2_status = 0;
int z_3_status = 0;
int z_4_status = 0;

const int AutoOff = 600;

const int button1 =  0;
const int button2 =  9;
const int button3 = 10;
const int button4 = 14;

Relay z_1(zone_1, false); // constructor receives (pin, isNormallyOpen) true = Normally Open, false = Normally Closed
Relay z_2(zone_2, false);
Relay z_3(zone_3, false);
Relay z_4(zone_4, false);   
Relay bLed(led, true); 

void webApi() {
  
  String zone = server->arg("zone");
  int rtime = server->arg("runtime").toInt();
  int run_time = rtime*60;
  if (server->hasArg("zone") && server->hasArg("runtime")) {
    
    if (zone == "1") { 
        allOff();
        z_1.turnOn();
        z_1_status = 1;
        Alarm.disable(TurnAllOff);
        delay(200);
        TurnAllOff = Alarm.timerOnce(run_time,allOff); 
        }
     else if (zone == "2") { 
        allOff();
        z_2.turnOn();
        z_2_status = 1;
        Alarm.disable(TurnAllOff);
        delay(200);
        TurnAllOff = Alarm.timerOnce(run_time,allOff); 
        }
     else if (zone == "3") { 
        allOff();
        z_3.turnOn();
        z_3_status = 1;
        Alarm.disable(TurnAllOff);
        delay(200);
        TurnAllOff = Alarm.timerOnce(run_time,allOff); 
        }
     else if (zone == "4") { 
        allOff();
        z_4.turnOn();
        z_4_status = 1;
        Alarm.disable(TurnAllOff);
        delay(200);
        TurnAllOff = Alarm.timerOnce(run_time,allOff); 
        }
        
     server->send(200, "application/json", "{'status':'ok'}");
     return;
  }
  
  if (zone == "all") { 
    IrrigationProgram(); 
    server->send(200, "application/json", "{'status':'ok'}");
    return;
  }
     
  if (zone == "stop") { 
    webRootSoftReset();
    server->send(200, "application/json", "{'status':'ok'}");
    return;
  } 
  server->send(200, "application/json", "{'status':'not ok'}");
 }

void webRootZones() {  
  String header;
  if (!is_authenticated()) {
    server->sendHeader("Location", "/login");
    server->sendHeader("Cache-Control", "no-cache");
    server->send(301);
    return;
  }  
  if (server->hasArg("z") && server->hasArg("c")) {

    int zone = atoi (server->arg("z").c_str ());
    String cmd = server->arg("c");

    sprinkler(zone, cmd);
    server->sendHeader("Location", "/zones");
    server->sendHeader("Cache-Control", "no-cache");
    server->send(301);
    return;
  }
  server->send ( 200, "text/html", getPageZones() );
}

void setup() {
  SPIFFS.begin();
  Serial.begin(115200);
  Serial.println(F("\n\nWifi Irrigation Node\n"));                         
  z_1.begin(); // inicializes the pin
  z_1.turnOn();
  z_1.turnOff();
  z_2.begin();
  z_2.turnOn();
  z_2.turnOff();
  z_3.begin();
  z_3.turnOn();
  z_3.turnOff();
  z_4.begin();
  z_4.turnOn();
  z_4.turnOff();
  //bLed.begin();
  //bLed.turnOff();
  pinMode(led, OUTPUT);
  digitalWrite(led, HIGH);
  BlueLed("on");
  if (!loadConfig(config)) {
    Serial.println(F("Generating new config\n"));
    if (!saveConfig()) {
        Serial.println(F("Config Error\n"));
      } else {
        loadConfig(config);
    }
  } 
 
  WiFiManager wifiManager;
  wifiManager.setTimeout(180);
  wifiManager.setConfigPortalTimeout(180);
  if (!wifiManager.autoConnect("irrigation_node")) {                                                                                       
    ESP.reset();                                                  //Serial.println("failed two connect and hit timeout");
    delay(1000);                                                  //reset and try again, or maybe put it to deep sleep
  }

  server = new ESP8266WebServer(config.server_port);
 
  server->onNotFound(handleNotFound);
  server->on("/", webRoot);
  server->on("/factory_reset", webRootFactoryReset);
  server->on("/reset", webRootSoftReset);
  server->on("/login", handleLogin);
  server->on("/config", webRootConfig);
  server->on("/setup", webRootSetup);
  server->on("/zones", webRootZones);
  server->on("/api", webApi);
  server->on("/firmware", HTTP_GET, []() {
    server->sendHeader("Connection", "close");
    server->send(200, "text/html", serverIndex);
   });
   server->on("/update", HTTP_POST, []() {
      server->sendHeader("Connection", "close");
      server->send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
      ESP.restart();
    }, []() {
          HTTPUpload& upload = server->upload();
          if (upload.status == UPLOAD_FILE_START) {
            NTP.stop(); // NTP sync can be disabled to avoid sync errors
            Serial.setDebugOutput(true);
            WiFiUDP::stopAll();
            Serial.printf("Update: %s\n", upload.filename.c_str());
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if (!Update.begin(maxSketchSpace)) { //start with max available size
              Update.printError(Serial);
            }
          } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
              Update.printError(Serial);
            }
          } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) { //true to set the size to the current progress
              Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
            } else {
              Update.printError(Serial);
            }
            Serial.setDebugOutput(false);
          }
          yield();
     });

  NTP.onNTPSyncEvent ([](NTPSyncEvent_t event) {
      ntpEvent = event;
      syncEventTriggered = true;
  });

  if (config.upnp == "true") {  

    boolean portMappingAdded = false;
    tinyUPnP.addPortMappingConfig(WiFi.localIP(), config.server_port, RULE_PROTOCOL_TCP, 36000, "Wifi-Irrigation-System");
    while (!portMappingAdded) {
      portMappingAdded = tinyUPnP.commitPortMappings();
      Serial.println("");
    
      if (!portMappingAdded) {
        Serial.println(F("This was printed because adding the required port mapping failed"));
        delay(30000);  // 30 seconds before trying again
      } else { upnp_init = 1; }
    }
    
    Serial.println(F("UPnP done"));
  }


  if (config.ddns == "true") { 
    // DDNS
    EasyDDNS.service(config.ddns_service_name);
    EasyDDNS.client(config.ddns_hostname, config.ddns_login, config.ddns_passwd);
  
    Serial.println(F("DDNS service started"));
  }
  
  // server
  if (MDNS.begin("esp8266")) {
    Serial.println(F("MDNS responder started"));
  }
  
  const char * headerkeys[] = {"User-Agent", "Cookie"} ;
  size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
  //ask server to track these headers
  server->collectHeaders(headerkeys, headerkeyssize);  
  server->begin();
  wifiFirstConnected = true;
}


bool is_authenticated() {
  if (server->hasHeader("Cookie")) {
    Serial.print(F("Found cookie: "));
    String cookie = server->header("Cookie");
    Serial.println(cookie);
    if (cookie.indexOf("WifiSprinkler=1") != -1) {
      Serial.println(F("Authentication Successful"));
      return true;
    }
  }
  Serial.println(F("Authentication Failed"));
  return false;
}

void handleLogin() {
  if (server->hasHeader("Cookie")) {
    Serial.print(F("Found cookie: "));
    String cookie = server->header("Cookie");
    Serial.println(cookie);
  }
  if (server->hasArg("DISCONNECT")) {
    server->sendHeader("Location", "/login");
    server->sendHeader("Cache-Control", "no-cache");
    server->sendHeader("Set-Cookie", "WifiSprinkler=0");
    server->send(301);
    return;
  }
  if (server->hasArg("USERNAME") && server->hasArg("PASSWORD")) {
    if (server->arg("USERNAME") == "admin" &&  server->arg("PASSWORD") == config.password) {
      server->sendHeader("Location", "/");
      server->sendHeader("Cache-Control", "no-cache");
      server->sendHeader("Set-Cookie", "WifiSprinkler=1");
      server->send(301);
      return;
    }
  }
  if (config.login == "false") {
    server->sendHeader("Location", "/");
    server->sendHeader("Cache-Control", "no-cache");
    server->sendHeader("Set-Cookie", "WifiSprinkler=1");
    server->send(301);
    return;
  }

  String page = getPageHead();
  page += F("<div style='text-align:left; display:block; max-width:320px; margin:0 auto;'>");
  page += F("<div><b><center>Wifi Irrigation System (31052020)");
  page += F("</center></b></div>");
  page += F("<body><div style='text-align:left;display:block;max-width:260px; margin: 0 auto;'>");
  page += F("<form action='/login' method='POST'>");
  page += F("Login:<input  name='USERNAME' type='text' value='admin'>");
  page += F("Password:<input  name='PASSWORD' type='text' value=''>");
  page += F("<br />");
  page += F("<button class='off' !important;'>Submit</button>");
  page += F("</form>");
  page += F("</div>");
  page += F("</body>");
  page += F("</html>");

  server->send(200, "text/html", page);
}


bool loadConfig(Config &config) {

  SPIFFS.begin();
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println(F("Failed to open config file"));
    return false;
  }

  size_t size = configFile.size();
  if (size > 1512) {
    Serial.println(F("Config file size is too large"));
    SPIFFS.end();
    return false;
  }

  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);

  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    Serial.println(F("Failed to parse config file"));
    SPIFFS.end();
    return false;
  }

  config.morning    = json["morning"].as<char*>();
  config.evening    = json["evening"].as<char*>();
  config.program    = json["program"].as<char*>();
  config.login      = json["login"].as<char*>();
  config.password   = json["password"].as<char*>();
  config.monday     = json["monday"];
  config.tuesday    = json["tuesday"];
  config.wednesday  = json["wednesday"];
  config.thursday   = json["thursday"];
  config.friday     = json["friday"];
  config.saturday   = json["saturday"];
  config.sunday     = json["sunday"];

  config.ddns_passwd        = json["ddns_passwd"].as<char*>();
  config.ddns_hostname      = json["ddns_hostname"].as<char*>();
  config.ddns_service_name  = json["ddns_service_name"].as<char*>();
  config.ddns_login         = json["ddns_login"].as<char*>();
  config.server_port        = json["server_port"];
  config.ddns               = json["ddns"].as<char*>();
  config.upnp               = json["upnp"].as<char*>();
  
  //json.prettyPrintTo(Serial);
  SPIFFS.end();
  return true;
}

bool saveConfig() {
  SPIFFS.begin();
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();

  json["morning"]   =  config.morning;
  json["evening"]   =  config.evening;
  json["program"]   =  config.program;
  json["login"]     =  config.login;
  json["password"]  =  config.password;
  json["monday"]    =  config.monday;
  json["tuesday"]   =  config.tuesday;
  json["wednesday"] =  config.wednesday;
  json["thursday"]  =  config.thursday;
  json["friday"]    =  config.friday;
  json["saturday"]  =  config.saturday;
  json["sunday"]    =  config.sunday;

  json["ddns_passwd"]        =  config.ddns_passwd;
  json["ddns_hostname"]      =  config.ddns_hostname;
  json["ddns_service_name"]  =  config.ddns_service_name;
  json["ddns_login"]         =  config.ddns_login;
  json["server_port"]        =  config.server_port;
  json["ddns"]               =  config.ddns;
  json["upnp"]               =  config.upnp;
  
  SPIFFS.remove("/config.json");
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println(F("Failed to open config file for writing"));
    SPIFFS.end();
    return false;
  }

  json.printTo(configFile);
  //json.prettyPrintTo(//Serial);
  Serial.println(F("New config saved."));
  SPIFFS.end();
  return true;
}

void TimeReset () {
  Serial.println(F("Restarting Time module..."));
  NTP.stop(); 
  delay(1000);
  NTP.begin ("sk.pool.ntp.org", 1, true);
  NTP.setInterval (6000);
}

// Web Interface part

void webRootSoftReset() {
  String header;
  if (!is_authenticated()) {
    server->sendHeader("Location", "/login");
    server->sendHeader("Cache-Control", "no-cache");
    server->send(301);
    return;
  }  
  server->sendHeader("Location", "/",true); //Redirect to our html web page 
  server->send(302, "text/plane",""); 
  delay(2000);
  ESP.reset();  
}

void FactoryReset() {
  SPIFFS.begin();
  Serial.println(F("Factory reset initialised."));
  SPIFFS.remove("/config.json");
  SPIFFS.end();
  WiFi.disconnect(true);
  delay(3000);
  ESP.reset();
  delay(2000);
}
void webRootFactoryReset() {
  String header;
  if (!is_authenticated()) {
    server->sendHeader("Location", "/login");
    server->sendHeader("Cache-Control", "no-cache");
    server->send(301);
    return;
  }  
  server->sendHeader("Location", "/",true); //Redirect to our html web page 
  server->send(302, "text/plane",""); 
  delay(2000);
  FactoryReset();
}

void handleNotFound(){
  server->sendHeader("Location", "/",true);   //Redirect to our html web page  
  server->send(302, "text/plane","");
}

void webRootConfig() {
  String header;
  if (!is_authenticated()) {
    server->sendHeader("Location", "/login");
    server->sendHeader("Cache-Control", "no-cache");
    server->send(301);
    return;
  }  
  saveConfig();
  String output;
  SPIFFS.begin();
  String fileName = "/config.json";
  File configFile = SPIFFS.open(fileName, "r");
  size_t size = configFile.size();
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  const size_t capacity = JSON_OBJECT_SIZE(6) + JSON_ARRAY_SIZE(6) + 60; //need to define the amount of json data
  DynamicJsonBuffer jsonBuffer(capacity);  
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  json.prettyPrintTo(output);
  SPIFFS.end();   
  server->send(200, "application/json", output);
}

void webRoot() {
  String header;
  if (!is_authenticated()) {
    server->sendHeader("Location", "/login");
    server->sendHeader("Cache-Control", "no-cache");
    server->send(301);
    return;
  }  
  Serial.print("Saving data...");
  if (server->hasArg("morning") && server->hasArg("evening") && server->hasArg("program")) {
    Serial.println("..OK");
    config.morning = server->arg("morning");
    config.evening = server->arg("evening");
    config.program = server->arg("program");

    if(server->hasArg("Monday")) {config.monday = server->arg("Monday").toInt();} else {config.monday = 0;}
    if(server->hasArg("Tuesday")) {config.tuesday = server->arg("Tuesday").toInt();} else {config.tuesday = 0;}
    if(server->hasArg("Wednesday")) {config.wednesday = server->arg("Wednesday").toInt();} else {config.wednesday = 0;}
    if(server->hasArg("Thursday")) {config.thursday = server->arg("Thursday").toInt();} else {config.thursday = 0;}
    if(server->hasArg("Friday")) {config.friday = server->arg("Friday").toInt();} else {config.friday = 0;}
    if(server->hasArg("Saturday")) {config.saturday = server->arg("Saturday").toInt();} else {config.saturday = 0;}
    if(server->hasArg("Sunday")) {config.sunday = server->arg("Sunday").toInt();} else {config.sunday = 0;}

    if (!saveConfig()) { 
        Serial.println(F("Failed to open config file for writing")); 
      } else {
        delay(200);
        EspShowPageAndReset();
        return;
      }
  }
  if (server->hasArg("run_program")) { 
    IrrigationProgram(); 
  
    server->sendHeader("Location", "/");
    server->sendHeader("Cache-Control", "no-cache");
    server->send(301);
    return;
  }
  
  server->send ( 200, "text/html", getPageConfig() );
}

void webRootSetup() {
  String header;
  if (!is_authenticated()) {
    server->sendHeader("Location", "/login");
    server->sendHeader("Cache-Control", "no-cache");
    server->send(301);
    return;
  }  
  Serial.print("Saving data...");
  if (server->hasArg("passwd") && server->hasArg("login")) {

    config.login = server->arg("login");
    config.password = server->arg("passwd");
    config.ddns_passwd = server->arg("ddns_passwd");
    config.ddns_hostname = server->arg("ddns_hostname");
    config.ddns_service_name = server->arg("ddns_type");
    config.ddns_login = server->arg("ddns_login");
    config.server_port = server->arg("web_port").toInt();;
    config.ddns = server->arg("ddns");
    config.upnp = server->arg("upnp");

    if (!saveConfig()) { 
        Serial.println(F("Failed to open config file for writing")); 
      } else {
        delay(200);
        EspShowPageAndReset();
      }
  }
  
  server->send ( 200, "text/html", getPageSetup() );
}

void EspShowPageAndReset() {
  server->send ( 200, "text/html", getResetPage() );
  //myDelay(10000);
  //server->sendHeader("Location","/");
  //server->send(303);
  Alarm.timerOnce(5,webRootSoftReset);
}

void myDelay(int del) {
  unsigned long myPrevMillis = millis();
  while (millis()- myPrevMillis <= del);
}

void allOff() {
  //BlueLed("off");
  z_1.turnOff();
  z_1_status = 0;
  z_2.turnOff();
  z_2_status = 0;
  z_3.turnOff();
  z_3_status = 0;
  z_4.turnOff();
  z_4_status = 0;
}

void allOffProgram() {
  BlueLed("off");
  ProgramRunning = 0; //toto je zle
  z_1.turnOff();
  z_1_status = 0;
  z_2.turnOff();
  z_2_status = 0;
  z_3.turnOff();
  z_3_status = 0;
  z_4.turnOff();
  z_4_status = 0;
}

void BlueLed(String prikaz) {
  if (prikaz == "on") {
    digitalWrite(led, LOW);
  } else {
    digitalWrite(led, HIGH);
  } 
}

void sprinkler(int zona, String prikaz) {

  if (prikaz == "off") {
        Serial.println("Turning OFF Zone #" + (String)zona);
        allOff();
      } else {
  
          if (zona == 1) {
              if (prikaz == "on") {
                Serial.println("Turning ON Zone #" + (String)zona);
                allOff();
                z_1.turnOn();
                z_1_status = 1;
              } else if (prikaz == "toggle" && ProgramRunning == 0) {
                Serial.println("Toggle Zone ##" + (String)zona);
                if (z_1_status == 1) {
                  allOff();
                } else {
                  allOff();
                  Alarm.disable(TurnAllOff);
                  delay(200);
                  TurnAllOff = Alarm.timerOnce(AutoOff,allOff);  
                  z_1.turnOn();
                  z_1_status = 1;
                }
              }
            
          } else if (zona == 2) {
            if (prikaz == "on") {
                Serial.println("Turning ON Zone #" + (String)zona);
                allOff();
                z_2.turnOn();
                z_2_status = 1;
              } else if (prikaz == "toggle" && ProgramRunning == 0) {
                Serial.println("Toggle Zone ##" + (String)zona);
                if (z_2_status == 1) {
                  allOff();
                } else {
                  allOff();
                  Alarm.disable(TurnAllOff);
                  delay(200);
                  TurnAllOff = Alarm.timerOnce(AutoOff,allOff);  
                  z_2.turnOn();
                  z_2_status = 1;
                }
              }
            
          } else if (zona == 3) {
            if (prikaz == "on") {
                Serial.println("Turning ON Zone #" + (String)zona);
                allOff();
                z_3.turnOn();
                z_3_status = 1;
              } else if (prikaz == "toggle" && ProgramRunning == 0) {
                Serial.println("Toggle Zone ##" + (String)zona);
                if (z_3_status == 1) {
                  allOff();
                } else {
                  allOff();
                  Alarm.disable(TurnAllOff);
                  delay(200);
                  TurnAllOff = Alarm.timerOnce(AutoOff,allOff);  
                  z_3.turnOn();
                  z_3_status = 1;
                }
              }
          } else if (zona == 4) {
            if (prikaz == "on") {
                Serial.println("Turning ON Zone #" + (String)zona);
                allOff();
                z_4.turnOn();
                z_4_status = 1;
              } else if (prikaz == "toggle" && ProgramRunning == 0) {
                Serial.println("Toggle Zone ##" + (String)zona);
                if (z_4_status == 1) {
                  allOff();
                } else {
                  allOff();
                  Alarm.disable(TurnAllOff);
                  delay(200);
                  TurnAllOff = Alarm.timerOnce(AutoOff,allOff);  
                  z_4.turnOn();
                  z_4_status = 1;
                }
              }
          }
      }
}

String getPageHead(){
  String page = F( "<html lang='en'>");
  page += F("<head>");
  page += F("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>");
  page += F("<title>WiFi Irrigation System (31052020)</title>");
  page += F("<style>");
  page += F("div,input{padding:5px;font-size:1em;}");
  page += F("input[type='text']{width:100%;}");
  page += F("body{font-family:verdana;}");
  page += F("button{border:0;border-radius:0.3rem;background-color:#1fa3ec;color:#fff;line-height:2.4rem;font-size:1.2rem;width:95%;}");
  page += F("label{display: inline-block; width: 33%; margin: 2px auto}");
  page += F(".on {background-color: #09ba35;}");
  page += F(".off {background-color: #ff3333;}");
  page += F(".set {background-color: #e2c106;}");
  page += F(".but {background-color: #008CBA;}");
  page += F("</style>");
  page += F("</head>");
  
  return page;
}

String getPageConfig() {
  
  String page = getPageHead();
  page += F("<div style='text-align:left; display:block; max-width:320px; margin:0 auto;'>");
  page += F("<div><b><center>Wifi Irrigation System (31052020)");
  page += F("</center></b></div>");
  page += F("<body><div style='text-align:left;display:block;max-width:260px; margin: 0 auto;'>");
  page += F("<strong>Status:</strong>");

  if (ProgramRunning == 1) { 
    page += F(" Irrigation Program is running(");
    if (z_1_status == 1) { page += F("Zone#1"); }
    else if (z_2_status == 1) { page += F("Zone #2"); }
    else if (z_3_status == 1) { page += F("Zone #3"); }
    else if (z_4_status == 1) { page += F("Zone #4"); }
    page += F(").<br />");
  }
  else if (z_1_status == 1) { page += F(" Zone #1 is ON <br /> "); }
  else if (z_2_status == 1) { page += F(" Zone #2 is ON <br /> "); }
  else if (z_3_status == 1) { page += F(" Zone #3 is ON <br /> "); }
  else if (z_4_status == 1) { page += F(" Zone #4 is ON <br /> "); }
  else { page += F(" relaxing <br /> "); }
  page += F("<br />");
  
  page += F("<form action='/' method='get'>");
  page += F("Morning Irrigation (time):<input  name='morning' type='text' value='");
  page += config.morning;
  page += F("'>");

  page += F("Evening Irrigation (time):<input  name='evening' type='text' value='");
  page += config.evening;
  page += F("'>");
  
  page += F("Irrigation Program:<input  name='program' type='text' value=\"");
  page += config.program;
  page += F("\">");

  page += F("<table>");
  page += F("<tr>");
  page += F("<td>Monday</td><td><input type='checkbox' id='Monday' name='Monday' value='1'");
  if (config.monday == 1) { page += F(" checked ");}
  page += F("></td>");
  page += F("<td>Tuesday</td><td><input type='checkbox' id='Tuesday' name='Tuesday' value='1'");
  if (config.tuesday == 1) { page += F(" checked ");}
  page += F("></td>");
  page += F("</tr>");
  page += F("<tr>");
  page += F("<td>Wednesday</td><td><input type='checkbox' id='Wednesday' name='Wednesday' value='1'");
  if (config.wednesday == 1) { page += F(" checked ");}
  page += F("></td>");
  page += F("<td>Thursday</td><td><input type='checkbox' id='Thursday' name='Thursday' value='1'");
  if (config.thursday == 1) { page += F(" checked ");}
  page += F("></td>");
  page += F("</tr>");
  page += F("<tr>");
  page += F("<td>Friday</td><td><input type='checkbox' id='Friday' name='Friday' value='1'");
  if (config.friday == 1) { page += F(" checked ");}
  page += F("></td>");
  page += F("<td>Saturday</td><td><input type='checkbox' id='Saturday' name='Saturday' value='1'");
  if (config.saturday == 1) { page += F(" checked ");}
  page += F("></td>");
  page += F("</tr>");
  page += F("<tr>");
  page += F("<td>Sunday</td><td><input type='checkbox' id='Sunday' name='Sunday' value='1'");
  if (config.sunday == 1) { page += F(" checked ");}
  page += F("></td>");
  page += F("<td></td><td></td>");
  page += F("</tr>");
  page += F("</table>");
  page += F("<br />");
  page += F("<br />");
  
  page += F("<button class='off' !important;'>SAVE</button>");
  page += F("</form>");
  if (ProgramRunning == 0) {
    page += F("<button class='but' onclick=\"location.href='/?run_program=on");
    page += F("'\" type='button'>ALL ZONES(Program)</button>");
    page += F("<button class='but' onclick=\"location.href='/zones");
    page += F("'\" type='button'>MANUAL</button>");
  }
  page += F("<br />");
  page += F("<br />");
  page += F("<br />");
  page += F("<button  class='off' onclick=\"location.href='/reset");
  page += F("'\" type='button'>Reset</button>");
  page += F("<button class='set' onclick=\"location.href='/factory_reset");
  page += F("'\" type='button'>Factory Reset</button>");
  page += F("<button  class='but' onclick=\"location.href='/setup");
  page += F("'\" type='button'>Setup</button>");
  page += F("<button class='on' onclick=\"location.href='/firmware");
  page += F("'\" type='button'>Firmware Update</button>");
  
  
  
  page += F("</div>");
  page += F("</body>");
  page += F("</html>");
  return page;
}

String getPageSetup() {
  
  String page = getPageHead();
  page += F("<div style='text-align:left; display:block; max-width:320px; margin:0 auto;'>");
  page += F("<div><b><center>Wifi Irrigation System (31052020)");
  page += F("</center></b></div>");
  page += F("<body><div style='text-align:left;display:block;max-width:260px; margin: 0 auto;'>");
  page += F("<form action='/setup' method='get'>");
  
  page += F("Login Page Password:<input  name='passwd' type='text' value=\"");
  page += config.password;
  page += F("\">");
  page += F("<br />");
  page += F("<br />");
  page += F("Login Page Usage:<br/>");
  page += F("<hr>");
  page += F("<label>True");
  page += F("<input id='login' type='checkbox' name='login' onclick='selectOnlyThis(this)' value='true' ");
  if (config.login == "true") { page += F("checked"); }
  page += F(" ></label><label>False");
  page += F("<input id='login' type='checkbox' name='login' onclick='selectOnlyThis(this)' value='false' ");
  if (config.login == "false") { page += F("checked"); }
  page += F(" ></label>");
  page += F("<br />");
  page += F("<br />");

  page += F("DDNS & UPNP Service Usage:<br/>");
  page += F("<hr>");
  page += F("<label>DDNS");
  page += F("<input id='ddns' type='checkbox' name='ddns' value='true' ");
  if (config.ddns == "true") { page += F("checked"); }
  page += F(" ></label><label>UPNP");
  page += F("<input id='upnp' type='checkbox' name='upnp' value='true' ");
  if (config.upnp == "true") { page += F("checked"); }
  page += F(" ></label>");
  page += F("<br />");
  page += F("<br />");

  page += F("WebServer port:<input  name='web_port' type='text' value=\"");
  page += config.server_port;
  page += F("\">");
  page += F("<br />");
  page += F("<br />");
  
  page += F("DDNS Service:<br/>");
  page += F("<hr>");
  page += F("<label>DynDNS");
  page += F("<input id='ddns_type' type='checkbox' name='ddns_type' onclick='selectOnlyThis2(this)' value='dyndns' ");
  if (config.ddns_service_name == "dyndns") { page += F("checked"); }
  page += F(" ></label><label>Dynu");
  page += F("<input id='ddns_type' type='checkbox' name='ddns_type' onclick='selectOnlyThis2(this)' value='dynu' ");
  if (config.ddns_service_name == "dynu") { page += F("checked"); }
  page += F(" ></label><br /><label>NoIP");
  page += F("<input id='ddns_type' type='checkbox' name='ddns_type' onclick='selectOnlyThis2(this)' value='noip' ");
  if (config.ddns_service_name == "noip") { page += F("checked"); }
  page += F(" ></label><label>Duck");
  page += F("<input id='ddns_type' type='checkbox' name='ddns_type' onclick='selectOnlyThis2(this)' value='duckdns' ");
  if (config.ddns_service_name == "duckdns") { page += F("checked"); }
  page += F(" ></label>");
  page += F("<br />");
  page += F("<br />");

  page += F("DDNS Hostname:<input  name='ddns_hostname' type='text' value=\"");
  page += config.ddns_hostname;
  page += F("\">");
  page += F("<br />");

  page += F("DDNS Login:<input  name='ddns_login' type='text' value=\"");
  page += config.ddns_login;
  page += F("\">");
  page += F("<br />");

  page += F("DDNS Login:<input  name='ddns_passwd' type='text' value=\"");
  page += config.ddns_passwd;
  page += F("\">");
  page += F("<br />");
  page += F("<br />");
  
  page += F("<p>To apply change of settings you need to restart the device</p>");
  
  page += F("<button class='off' !important;'>SAVE</button>");
  page += F("</form>");
  
  page += F("<button class='on' onclick=\"location.href='/");
  page += F("'\" type='button'>BACK</button>");
  page += F("<br />");
  
  page += F("</div>");
  //page += F("<script language='javascript'> function checkOnly(stayChecked) { with(document.myForm) { for(i = 0; i < elements.length; i++) {if(elements[i].checked == true && elements[i].name != stayChecked.name) { elements[i].checked = false; } } } } </script>");
  page += F("<script language='javascript'> function selectOnlyThis(id){ var myCheckbox = document.getElementsByName('login');Array.prototype.forEach.call(myCheckbox,function(el){ el.checked = false;}); id.checked = true; } </script>");
  page += F("<script language='javascript'> function selectOnlyThis2(id){ var myCheckbox = document.getElementsByName('ddns_type');Array.prototype.forEach.call(myCheckbox,function(el){ el.checked = false;}); id.checked = true; } </script>");
  page += F("</body>");
  page += F("</html>");
  return page;
}

String getResetPage() {
  
  String page = getPageHead();
  page += F("<div style='text-align:left; display:block; max-width:320px; margin:0 auto;'>");
  page += F("<div><b><center>Wifi Irrigation System (31052020)");
  page += F("</center></b></div>");
  page += F("<body><div style='text-align:left;display:block;max-width:260px; margin: 0 auto;'>");
  //page += F("<p>System is saving data and applying new settings, please wait</p>");
  page += F("<div id='msg'></div>");
  page += F("</div>");
  page += F("</body>");
  //page += F("<script type='text/javascript'> function Redirect() { window.location='/'; }");
  //if (upnp_init == 0) { page += F("setTimeout('Redirect()', 10000);</script>"); } else { page += F("setTimeout('Redirect()', 25000);</script>"); }
  page += F("<script type='text/javascript'> var timeLeft = ");
  if (upnp_init == 0) { page += F("15"); } else { page += F("30"); }
  page += F("; var elem = document.getElementById('msg');var timerId = setInterval(countdown, 1000);");
  page += F("function countdown() { if (timeLeft == -1) { clearTimeout(timerId); Redirect(); } else { elem.innerHTML = 'System is saving data and applying new settings.<br /> <br />  The device will be restarted automatically within ' + timeLeft + ' seconds.'; timeLeft--; }} function Redirect() { window.location='/'; } </script>");
  
  page += F("</html>");
  return page;
}


String getPageZones() {
  String page = getPageHead();
  page += F("<div style='text-align:left; display:block; max-width:320px; margin:0 auto;'>");
  page += F("<div><b><center>Irrigation (set manually)");
  page += F("</center></b></div>");
  page += F("<body><div style='text-align:left;display:block;max-width:260px; margin: 0 auto;'>");

  page += F("<button class='but' onclick=\"location.href='/zones?z=1&c=toggle");
  page += F("'\" type='button'>Zone #1 (");
  if (z_1_status == 1) { page += F("Turn OFF");} else { page += F("Turn ON");}
  page += F(")</button>");
  page += F("<br />");
  
  page += F("<button class='but' onclick=\"location.href='/zones?z=2&c=toggle");
  page += F("'\" type='button'>Zone #2 (");
  if (z_2_status == 1) { page += F("Turn OFF");} else { page += F("Turn ON");}
  page += F(")</button>");
  page += F("<br />");
  
  page += F("<button class='but' onclick=\"location.href='/zones?z=3&c=toggle");
  page += F("'\" type='button'>Zone #3 (");
  if (z_3_status == 1) { page += F("Turn OFF");} else { page += F("Turn ON");}
  page += F(")</button>");
  page += F("<br />");
  
  page += F("<button class='but' onclick=\"location.href='/zones?z=4&c=toggle");
  page += F("'\" type='button'>Zone #4 (");
  if (z_4_status == 1) { page += F("Turn OFF");} else { page += F("Turn ON");}
  page += F(")</button>");
  page += F("<br />");

  page += F("<button class='on' onclick=\"location.href='/");
  page += F("'\" type='button'>BACK</button>");
  page += F("<br />");
  
  page += F("</div>");
  page += F("</body>");
  page += F("</html>");
  return page;
}


String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void processSyncEvent (NTPSyncEvent_t ntpEvent) {
    if (ntpEvent) {
        NTP.stop(); 
        ntp_failed++;
        if (ntp_failed > 10) {ESP.reset();} // to prevent to get corrent time
        delay(2000);

        if (gotNtpTime) {syncEventTriggered = false;} else { syncEventTriggered = true;}
        
        Serial.print(F("Time Sync error: "));
        if (ntpEvent == noResponse)
            Serial.println(F("NTP server not reachable"));
        else if (ntpEvent == invalidAddress)
            Serial.println(F("Invalid NTP server address"));
        NTP.begin ("sk.pool.ntp.org", 1, true);
        NTP.setInterval (6000);
    } else {
        syncEventTriggered = false;
        gotNtpTime = true;
        Serial.print(F("Got NTP time: "));
        Serial.println (NTP.getTimeDateString (NTP.getLastNTPSync ()));
        ntp_failed = 0;
        n_year = year();

        if (n_year > 1980 && n_year < 2030) {
          if (n_year != p_year) { //if ntp not sync correctly
            //CronJob - register as soon as we got time for first time
            Serial.println ("Registering CRON jobs...");
            //Alarm.timerRepeat(600, cronTask); //each 600sec
            Alarm.alarmRepeat(0, 1, 0, cronTask);  //each day 1 minute after midnight
            cronTask();
            p_year = year();
          }
        } else {
          Serial.println(F("trying to get correct time..."));
          delay(3000);
          Alarm.timerOnce(30, TimeReset); 
        }
    }
}

void cronTask() {
  int m_hours = atoi (getValue(config.morning, ':', 0).c_str ());
  int m_minutes = atoi (getValue(config.morning, ':', 1).c_str ());

  int e_hours = atoi (getValue(config.evening, ':', 0).c_str ());
  int e_minutes = atoi (getValue(config.evening, ':', 1).c_str ());

  int wDay = weekday();         // day of the week (1-7), Sunday = 1, Monday = 2, Tuesday = 3, Wednesday = 4, Thursaday = 5, Friday = 6, Saturday = 7

  if      (config.monday == 1 && wDay == 2)    {
     if (m_hours > 0) { Alarm.alarmOnce(dowMonday,m_hours,m_minutes,0, IrrigationProgram);Serial.println(F("Registering Monday morning cron job .."));} 
     if (e_hours > 0) { Alarm.alarmOnce(dowMonday,e_hours,e_minutes,0, IrrigationProgram);Serial.println(F("Registering Monday evening cron job .."));}
     }
  else if (config.tuesday == 1 && wDay == 3)   { 
     if (m_hours > 0) { Alarm.alarmOnce(dowTuesday,m_hours,m_minutes,0, IrrigationProgram);Serial.println(F("Registering Tuesday morning cron job .."));} 
     if (e_hours > 0) { Alarm.alarmOnce(dowTuesday,e_hours,e_minutes,0, IrrigationProgram);Serial.println(F("Registering Tuesday evening cron job .."));}
    }
  else if (config.wednesday == 1 && wDay == 4) {
     if (m_hours > 0) { Alarm.alarmOnce(dowWednesday,m_hours,m_minutes,0, IrrigationProgram);Serial.println(F("Registering Wednesday morning cron job .."));} 
     if (e_hours > 0) { Alarm.alarmOnce(dowWednesday,e_hours,e_minutes,0, IrrigationProgram);Serial.println(F("Registering Wednesday evening cron job .."));}
    }
  else if (config.thursday == 1 && wDay == 5)  { 
     if (m_hours > 0) { Alarm.alarmOnce(dowThursday,m_hours,m_minutes,0, IrrigationProgram);Serial.println(F("Registering Thursday morning cron job .."));} 
     if (e_hours > 0) { Alarm.alarmOnce(dowThursday,e_hours,e_minutes,0, IrrigationProgram);Serial.println(F("Registering Thursday evening cron job .."));}
    }
  else if (config.friday == 1 && wDay == 6)    { 
     if (m_hours > 0) { Alarm.alarmOnce(dowFriday,m_hours,m_minutes,0, IrrigationProgram);Serial.println(F("Registering Friday morning cron job .."));} 
     if (e_hours > 0) { Alarm.alarmOnce(dowFriday,e_hours,e_minutes,0, IrrigationProgram);Serial.println(F("Registering Friday evening cron job .."));}
    }
  else if (config.saturday == 1 && wDay == 7)  { 
     if (m_hours > 0) { Alarm.alarmOnce(dowSaturday,m_hours,m_minutes,0, IrrigationProgram);Serial.println(F("Registering Saturday morning cron job .."));} 
     if (e_hours > 0) { Alarm.alarmOnce(dowSaturday,e_hours,e_minutes,0, IrrigationProgram);Serial.println(F("Registering Saturday evening cron job .."));}
    }
  else if (config.sunday == 1 && wDay == 1)    { 
     if (m_hours > 0) { Alarm.alarmOnce(dowSunday,m_hours,m_minutes,0, IrrigationProgram);Serial.println(F("Registering Sunday morning cron job .."));} 
     if (e_hours > 0) { Alarm.alarmOnce(dowSunday,e_hours,e_minutes,0, IrrigationProgram);Serial.println(F("Registering Sunday evening cron job .."));}
    }  
  BlueLed("off");
}

void IrrigationProgram() {
  ProgramRunning = 1;
  Serial.println(F("Start irrigation program"));
  Alarm.disable(TurnAllOff);
  BlueLed("on");
  int r=0;
  int TotalTime = 0;
  for (int i=0; i <= config.program.length(); i++)
  { 
   if(config.program.charAt(i) == '-' || i==config.program.length() ) 
    { 
      //sa[t] = oneLine.substring(r, i); 
      //Serial.println(config.program.substring(r, i));
      

      String Zone = getValue(config.program.substring(r, i), '_', 0);
      int Time = atoi (getValue(config.program.substring(r, i), '_', 1).c_str ());
      Zone.remove(0, 1);
      int iZona = atoi (Zone.c_str());
      Time = Time*60;
      //Serial.println("Zone: " + (String)iZona + " Time: " + (String)Time);
      //sprinkler(int iZona, String prikaz)
      if (TotalTime == 0) { Serial.println("Starting sprinkler #" +(String)iZona + " for " +(String)Time); sprinkler(iZona,"on"); } else {
        if (iZona == 1) {Serial.println("Starting sprinkler #1 for " +(String)Time + " in " +(String)TotalTime);Alarm.timerOnce(TotalTime,sprinkler1);}
        if (iZona == 2) {Serial.println("Starting sprinkler #2 for " +(String)Time + " in " +(String)TotalTime);Alarm.timerOnce(TotalTime,sprinkler2);}
        if (iZona == 3) {Serial.println("Starting sprinkler #3 for " +(String)Time + " in " +(String)TotalTime);Alarm.timerOnce(TotalTime,sprinkler3);}
        if (iZona == 4) {Serial.println("Starting sprinkler #4 for " +(String)Time + " in " +(String)TotalTime);Alarm.timerOnce(TotalTime,sprinkler4);}
      }
      TotalTime = TotalTime + Time;
      r=(i+1); 
    }
  }
  Serial.println("Scheduling end time in " +(String)TotalTime);
  Alarm.timerOnce(TotalTime,allOffProgram);
}

void sprinkler1() {
  sprinkler(1, "on");  
}

void sprinkler2() {
  sprinkler(2, "on");  
}

void sprinkler3() {
  sprinkler(3, "on");  
}

void sprinkler4() {
  sprinkler(4, "on");  
}

void loop() {
  int i = 0;
  if (wifiFirstConnected) {  
        wifiFirstConnected = false;
        NTP.begin("sk.pool.ntp.org", 1, true);
        NTP.setInterval(30);

        Serial.println(String(WiFi.macAddress()));
        Serial.println(WiFi.localIP().toString());  
    }
    
    if (syncEventTriggered) {
        processSyncEvent (ntpEvent);
        //syncEventTriggered = false;
    }

    if (digitalRead(button1) == LOW) {
      // button is pressed
        Alarm.delay(200);
      while(digitalRead(button1) == LOW) {
        Alarm.delay(200); // wait until button released
        if (digitalRead(button4) == LOW) {
          Alarm.delay(200);
          while(digitalRead(button4) == LOW) {
            Alarm.delay(200);
          }
          Serial.println(F("Factory reset initiated"));
          FactoryReset();
          i = 1;
          }
      }
      if (i != 1 && ProgramRunning == 0) {
        Serial.println(F("button1 was pressed"));
        if (z_1_status == 1) {
          allOff();
        } else {
          allOff();
          Alarm.disable(TurnAllOff);
          Alarm.delay(200);
          TurnAllOff = Alarm.timerOnce(AutoOff,allOff);  
          z_1.turnOn();
          z_1_status = 1;
        }
      }
    }
  
    if (digitalRead(button2) == LOW) {
      // button is pressed
        Alarm.delay(200);
      while(digitalRead(button2) == LOW) {
        Alarm.delay(200); // wait until button released
        if (digitalRead(button3) == LOW) {
          Alarm.delay(200);
          while(digitalRead(button3) == LOW) {
            Alarm.delay(200);
          }
          Serial.println(F("Program started"));
          if (ProgramRunning == 0) {
            IrrigationProgram();
          } else { ESP.reset(); }
          i = 1;
        }
      }
      if (i != 1 && ProgramRunning == 0) {
        Serial.println(F("button2 was pressed"));
        if (z_2_status == 1) {
          allOff();
        } else {
          allOff();
          Alarm.disable(TurnAllOff);
          Alarm.delay(200);
          TurnAllOff = Alarm.timerOnce(AutoOff,allOff);  
          z_2.turnOn();
          z_2_status = 1;
        }
      }
    }
  
    if (digitalRead(button3) == LOW) {
      // button is pressed
        Alarm.delay(200);
      while(digitalRead(button3) == LOW) {
        Alarm.delay(200); // wait until button released
        if (digitalRead(button2) == LOW) {
          Alarm.delay(200);
          while(digitalRead(button2) == LOW) {
            Alarm.delay(200);
          }
          Serial.println(F("Program started"));
          if (ProgramRunning == 0) {
            IrrigationProgram();
          } else { ESP.reset(); }
          i = 1;
        }
      }
      if (i != 1 && ProgramRunning == 0) {
        Serial.println(F("button3 was pressed"));
        if (z_3_status == 1) {
          allOff();
        } else {
          allOff();
          Alarm.disable(TurnAllOff);
          Alarm.delay(200);
          TurnAllOff = Alarm.timerOnce(AutoOff,allOff);  
          z_3.turnOn();
          z_3_status = 1;
        }
      }
    }
  
    if (digitalRead(button4) == LOW) {
      // button is pressed
        Alarm.delay(200);
      while(digitalRead(button4) == LOW) {
        Alarm.delay(200); // wait until button released
        if (digitalRead(button1) == LOW) {
          Alarm.delay(200);
          while(digitalRead(button1) == LOW) {
              Alarm.delay(200);
            }
            Serial.println(F("Factory reset initiated"));
            FactoryReset();
            i = 1;
          }  
      }
      if (i != 1 && ProgramRunning == 0) {
        Serial.println(F("button4 was pressed"));
        if (z_4_status == 1) {
          allOff();
        } else {
          allOff();
          Alarm.disable(TurnAllOff);
          Alarm.delay(200);
          TurnAllOff = Alarm.timerOnce(AutoOff,allOff);  
          z_4.turnOn();
          z_4_status = 1;
        }
      }
    }

  if (config.upnp == "true") {  tinyUPnP.updatePortMappings(600000); } // 10 minutes 
  if (config.ddns == "true") {  EasyDDNS.update(300000); }
  server->handleClient();
  Alarm.delay(100);
}
