#include "ai_client.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

static const char* OLLAMA_URL = "https://<your-worker-name>.<your-username>.workers.dev/api/generate";

static const char* MODEL_NAME = "@cf/meta/llama-3.2-1b-instruct";

static const char* NVS_NS  = "cfg";
static const char* NVS_KEY = "auth";

static String gToken;
static bool   gTokenLoaded = false;

static String nvsLoadToken()
{
  Preferences prefs;
  prefs.begin(NVS_NS, true);
  String tok = prefs.getString(NVS_KEY, "");
  prefs.end();
  tok.trim();
  return tok;
}

static void nvsSaveToken(const String& tok)
{
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.putString(NVS_KEY, tok);
  prefs.end();
}

static void nvsClearToken()
{
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.remove(NVS_KEY);
  prefs.end();
}

static void ensureTokenLoaded()
{
  if (gTokenLoaded) return;
  gToken = nvsLoadToken();
  gTokenLoaded = true;
}

static void provisionTokenFromSerialBlocking()
{

  ensureTokenLoaded();
  if (gToken.length() > 0) {
    Serial.println("AI token loaded from NVS.");
    return;
  }

  Serial.println("\n=== AI TOKEN SETUP ===");
  Serial.println("No token in NVS.");
  Serial.println("Paste your token, press Enter:");

  while (!Serial.available()) delay(10);

  String tok = Serial.readStringUntil('\n');
  tok.trim();

  if (tok.length() == 0) {
    Serial.println("Empty token. Not saved.");
    return;
  }

  nvsSaveToken(tok);
  gToken = tok;
  Serial.println("Saved token to NVS ");
}

void ai_begin()
{

  ensureTokenLoaded();

  if (gToken.length() == 0) {

    provisionTokenFromSerialBlocking();
  }
}

void ai_pollSerial()
{

  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  if (line == "CLEAR_TOKEN") {
    nvsClearToken();
    gToken = "";
    gTokenLoaded = true;
    Serial.println("Token cleared from NVS ✅");
    return;
  }

  const String prefix = "SET_TOKEN ";
  if (line.startsWith(prefix)) {
    String tok = line.substring(prefix.length());
    tok.trim();
    if (tok.length() == 0) {
      Serial.println("SET_TOKEN needs a value.");
      return;
    }
    nvsSaveToken(tok);
    gToken = tok;
    gTokenLoaded = true;
    Serial.println("Token saved to NVS ✅");
    return;
  }

  Serial.println("Unknown command. Use CLEAR_TOKEN or SET_TOKEN <token>");
}

String ai_sendMessage(const String& userMessage)
{
  if (WiFi.status() != WL_CONNECTED) return "WiFi not connected";

  ensureTokenLoaded();
  if (gToken.length() == 0) {
    return "No token. Open Serial and run ai_begin() once.";
  }

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, OLLAMA_URL)) {
    return "HTTP begin failed";
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Auth", gToken);

  String prompt =
    "Reply in 1 short sentence. No lists.\n"
    "User: " + userMessage + "\nAssistant:";

  StaticJsonDocument<1024> req;
  req["model"] = MODEL_NAME;
  req["prompt"] = prompt;
  req["stream"] = false;

  String payload;
  serializeJson(req, payload);

  int code = http.POST(payload);
  if (code != 200) {
    String err = "HTTP " + String(code);
    String body = http.getString();
    http.end();

    if (code == 401) return "401 Unauthorized (token?)";
    if (body.length() > 0) return err + " " + body;
    return err;
  }

  String body = http.getString();
  http.end();

  StaticJsonDocument<4096> resp;
  if (deserializeJson(resp, body)) return "JSON error";

  String out = resp["response"].as<String>();
  out.trim();

  if (out.length() > 120) out = out.substring(0, 120) + "...";
  return out;
}
