
/*
 ESP32 BedrockTogether Pro v3.0
 Colour OLED (SSD1351) Edition
*/

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <WiFiServer.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <SPI.h>

/* ========= DISPLAY CONFIG ========= */
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17
#define SCREEN_W 128
#define SCREEN_H 128

Adafruit_SSD1351 display = Adafruit_SSD1351(
  SCREEN_W, SCREEN_H, &SPI, TFT_CS, TFT_DC, TFT_RST
);

/* ========= SYSTEM CONFIG ========= */
#define BUTTON_PIN 0
#define UDP_PORT 19132
#define ADMIN_PORT 7777
#define MAX_SERVERS 8
#define BROADCAST_INTERVAL 1000
#define ADMIN_KEY "2298"

WebServer web(80);
WiFiUDP udp;
DNSServer dns;
WiFiServer adminServer(ADMIN_PORT);
WiFiClient adminClient;

struct ServerEntry {
  String name;
  String host;
  int port;
};

ServerEntry servers[MAX_SERVERS];
int serverCount = 0;
bool broadcastEnabled = true;
unsigned long lastBroadcast = 0;

/* ========= UI ========= */
void drawStatus() {
  display.fillScreen(0x0000);
  display.setTextColor(0xFFFF);
  display.setCursor(0,0);
  display.setTextSize(1);
  display.println("Bedrock Bridge v3");
  display.println("----------------");
  display.print("Servers: ");
  display.println(serverCount);
  display.println(broadcastEnabled ? "Broadcast: ON" : "Broadcast: OFF");
  display.println();
  display.print("IP: ");
  display.println(WiFi.localIP());
}

/* ========= CONFIG ========= */
void loadConfig() {
  if (!SPIFFS.exists("/config.json")) return;
  File f = SPIFFS.open("/config.json");
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, f);
  f.close();
  JsonArray arr = doc["servers"];
  serverCount = 0;
  for (JsonObject s : arr) {
    servers[serverCount++] = {s["name"].as<String>(), s["host"].as<String>(), s["port"]};
  }
}

void saveConfig() {
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.createNestedArray("servers");
  for (int i = 0; i < serverCount; i++) {
    JsonObject s = arr.createNestedObject();
    s["name"] = servers[i].name;
    s["host"] = servers[i].host;
    s["port"] = servers[i].port;
  }
  File f = SPIFFS.open("/config.json", "w");
  serializeJson(doc, f);
  f.close();
}

/* ========= WEB ========= */
void handleRoot() {
  String html = "<h2>ESP32 BedrockTogether Pro v3</h2>";
  html += "<form method='POST' action='/add'>Name:<input name='name'><br>Host:<input name='host'><br>Port:<input name='port'><br><button>Add</button></form><hr>";
  for (int i = 0; i < serverCount; i++) html += String(i) + ": " + servers[i].name + "<br>";
  web.send(200, "text/html", html);
}

void handleAdd() {
  if (serverCount >= MAX_SERVERS) {
    web.send(400, "text/plain", "Limit reached");
    return;
  }
  servers[serverCount++] = {web.arg("name"), web.arg("host"), web.arg("port").toInt()};
  saveConfig();
  web.sendHeader("Location", "/");
  web.send(303);
}

/* ========= BEDROCK ========= */
void broadcastServers() {
  for (int i = 0; i < serverCount; i++) {
    String packet =
      "MCPE;" + servers[i].name + ";527;1.20.50;0;20;" +
      String(random(100000,999999)) + ";ESP32;Survival;1;" +
      servers[i].port + ";" + servers[i].port + ";";
    udp.beginPacket(IPAddress(255,255,255,255), UDP_PORT);
    udp.print(packet);
    udp.endPacket();
  }
}

/* ========= ADMIN TERMINAL ========= */
void handleAdmin(String cmd) {
  cmd.trim();
  if (cmd == "STATUS") {
    adminClient.println("OK");
    adminClient.println("Servers: " + String(serverCount));
    adminClient.println(broadcastEnabled ? "Broadcast ON" : "Broadcast OFF");
  } else if (cmd == "LIST") {
    for (int i = 0; i < serverCount; i++)
      adminClient.println(String(i) + " " + servers[i].name);
  } else if (cmd.startsWith("ADD ")) {
    String d = cmd.substring(4);
    int a = d.indexOf('|');
    int b = d.lastIndexOf('|');
    servers[serverCount++] = {d.substring(0,a), d.substring(a+1,b), d.substring(b+1).toInt()};
    saveConfig();
    adminClient.println("OK");
  } else if (cmd.startsWith("DEL ")) {
    int idx = cmd.substring(4).toInt();
    for (int i = idx; i < serverCount-1; i++) servers[i] = servers[i+1];
    serverCount--;
    saveConfig();
    adminClient.println("OK");
  } else if (cmd == "REBOOT") {
    adminClient.println("REBOOT");
    delay(100);
    ESP.restart();
  } else {
    adminClient.println("UNKNOWN");
  }
}

void adminLoop() {
  if (!adminClient || !adminClient.connected()) {
    adminClient = adminServer.available();
    return;
  }
  static bool authed = false;
  if (adminClient.available()) {
    String cmd = adminClient.readStringUntil('\n');
    if (!authed) {
      if (cmd.startsWith("AUTH ") && cmd.substring(5).trim() == ADMIN_KEY) {
        authed = true;
        adminClient.println("AUTH OK");
      } else {
        adminClient.println("AUTH FAIL");
        adminClient.stop();
      }
      return;
    }
    handleAdmin(cmd);
  }
}

/* ========= SETUP ========= */
void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  SPIFFS.begin(true);

  SPI.begin();
  display.begin();
  display.fillScreen(0x0000);
  display.setTextColor(0xFFFF);
  display.setCursor(0,0);
  display.println("Booting...");

  loadConfig();

  WiFi.begin();
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 5000) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.softAP("ESP32-Bedrock", "minecraft");
    dns.start(53, "*", WiFi.softAPIP());
  }

  udp.begin(UDP_PORT);
  adminServer.begin();
  web.on("/", handleRoot);
  web.on("/add", HTTP_POST, handleAdd);
  web.begin();
}

/* ========= LOOP ========= */
void loop() {
  web.handleClient();
  dns.processNextRequest();
  adminLoop();

  if (broadcastEnabled && millis() - lastBroadcast > BROADCAST_INTERVAL) {
    broadcastServers();
    lastBroadcast = millis();
    drawStatus();
  }
}
