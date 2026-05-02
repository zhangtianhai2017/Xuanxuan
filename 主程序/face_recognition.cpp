#include "face_recognition.h"
#include "config.h"
#include "debug_log.h"

// 百度云 API 凭据。教学项目中保留在源码里便于演示；
// 真实项目应放到不公开的配置文件或烧录参数中。
const char* baidu_api_key = "DAdv88KBXYga1RvELYD6n6Mx";
const char* baidu_secret_key = "uHfKwVpJ7Scy8eyLo0nRg6E1argLGqUa";
String baidu_access_token = "";

const String FACE_GROUP_ID = "visitors";
const float MATCH_THRESHOLD = 80.0;
const float GENDER_CONFIDENCE_THRESHOLD = 0.8;

int manCount = 0;
int womanCount = 0;
int unknownCount = 0;

Preferences prefs;

// 百度人脸库注册后通常不会立刻可搜索。
// 冷却时间可以减少 PIR 三连拍时重复注册 unknown1/unknown2/unknown3 的概率。
static const unsigned long REGISTER_COOLDOWN_MS = 5000;
static unsigned long lastRegisterTime = 0;

static void appendImageJsonPrefix(String& body, const String& base64Image) {
  // 不使用小容量 StaticJsonDocument 来装 Base64 图片，因为图片字符串可能几十 KB。
  // 手工拼 JSON 对初学者来说更直观，也能避免 ArduinoJson 容量不足。
  body += "{\"image\":\"";
  body += base64Image;
  body += "\",\"image_type\":\"BASE64\"";
}

String getBaiduAccessToken() {
  // 百度 API 需要 access_token；没有 WiFi 时直接跳过，让主程序继续运行。
  if (WiFi.status() != WL_CONNECTED) {
    logWarn("BAIDU", "TOKEN_SKIP", "reason=wifi_offline");
    return "";
  }

  String url = "https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id="
             + String(baidu_api_key) + "&client_secret=" + String(baidu_secret_key);

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != 200) {
    logError("BAIDU", "TOKEN_HTTP_FAILED", String("http=") + httpCode);
    http.end();
    return "";
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    logError("BAIDU", "TOKEN_JSON_FAILED", String("err=") + err.c_str());
    return "";
  }

  String token = doc["access_token"].as<String>();
  if (token.length() == 0) {
    logError("BAIDU", "TOKEN_MISSING", String("payload_chars=") + payload.length());
  } else {
    logInfo("BAIDU", "TOKEN_OK", String("chars=") + token.length());
  }
  return token;
}

String detectGender(const String& base64Image) {
  // 新访客注册前先检测性别，用于生成 manN/womanN/unknownN 这样的 user_id。
  // 注意：百度要求 face_field 放在 JSON Body 里，而不是 URL 参数里。
  if (WiFi.status() != WL_CONNECTED || baidu_access_token.length() == 0) {
    logWarn("FACE", "DETECT_SKIP", String("wifi=") + (WiFi.status() == WL_CONNECTED ? "connected" : "offline")
            + " token=" + (baidu_access_token.length() > 0 ? "present" : "missing"));
    return "";
  }

  String url = "https://aip.baidubce.com/rest/2.0/face/v3/detect?access_token=" + baidu_access_token;

  String requestBody;
  requestBody.reserve(base64Image.length() + 160);
  appendImageJsonPrefix(requestBody, base64Image);
  requestBody += ",\"face_field\":\"gender\",\"max_face_num\":1}";

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(requestBody);

  if (httpCode != 200) {
    logError("FACE", "DETECT_HTTP_FAILED", String("http=") + httpCode);
    http.end();
    return "";
  }

  String response = http.getString();
  http.end();

  DynamicJsonDocument respDoc(4096);
  DeserializationError err = deserializeJson(respDoc, response);
  if (err) {
    logError("FACE", "DETECT_JSON_FAILED", String("err=") + err.c_str() + " response_chars=" + response.length());
    return "";
  }

  int errorCode = respDoc["error_code"] | -1;
  if (errorCode != 0) {
    logError("FACE", "DETECT_API_ERROR",
             String("code=") + errorCode + " msg=" + respDoc["error_msg"].as<String>());
    return "";
  }

  int faceNum = respDoc["result"]["face_num"] | 0;
  if (faceNum <= 0) {
    // 没有人脸时不注册，避免把门口背景当成访客。
    logWarn("FACE", "DETECT_NO_FACE");
    return "";
  }

  JsonVariant genderObj = respDoc["result"]["face_list"][0]["gender"];
  if (genderObj.isNull()) {
    logWarn("FACE", "DETECT_GENDER_MISSING", String("face_num=") + faceNum);
    return "unknown";
  }

  String gender = genderObj["type"].as<String>();
  float prob = genderObj["probability"] | 0.0;
  if (prob >= GENDER_CONFIDENCE_THRESHOLD) {
    logInfo("FACE", "DETECT_GENDER_OK", String("gender=") + gender + " prob=" + String(prob, 3));
    return gender;
  }

  logWarn("FACE", "DETECT_GENDER_LOW", String("gender=") + gender + " prob=" + String(prob, 3));
  return "unknown";
}

FaceSearchResult faceSearch(const String& base64Image, String& matchedUserId) {
  // 先搜索人脸库。只有 SEARCH_NO_MATCH 才能进入注册流程；
  // SEARCH_FAILED 代表网络/API/Token 问题，不能当成陌生人。
  matchedUserId = "";
  if (WiFi.status() != WL_CONNECTED || baidu_access_token.length() == 0) {
    logWarn("FACE", "SEARCH_SKIP", String("wifi=") + (WiFi.status() == WL_CONNECTED ? "connected" : "offline")
            + " token=" + (baidu_access_token.length() > 0 ? "present" : "missing"));
    return SEARCH_FAILED;
  }

  String url = "https://aip.baidubce.com/rest/2.0/face/v3/search?access_token=" + baidu_access_token;

  String requestBody;
  requestBody.reserve(base64Image.length() + 256);
  appendImageJsonPrefix(requestBody, base64Image);
  requestBody += ",\"group_id_list\":\"";
  requestBody += FACE_GROUP_ID;
  requestBody += "\",\"match_threshold\":";
  requestBody += String((int)MATCH_THRESHOLD);
  requestBody += ",\"quality_control\":\"NONE\",\"liveness_control\":\"NONE\"}";

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(requestBody);

  if (httpCode != 200) {
    logError("FACE", "SEARCH_HTTP_FAILED", String("http=") + httpCode);
    http.end();
    return SEARCH_FAILED;
  }

  String response = http.getString();
  http.end();

  DynamicJsonDocument respDoc(4096);
  DeserializationError err = deserializeJson(respDoc, response);
  if (err) {
    logError("FACE", "SEARCH_JSON_FAILED", String("err=") + err.c_str() + " response_chars=" + response.length());
    return SEARCH_FAILED;
  }

  int errorCode = respDoc["error_code"] | -1;
  if (errorCode != 0) {
    // 百度搜索 API 可能用错误码表达“没有匹配到用户/没有人脸”。
    // 这类情况不是系统失败，可以继续走检测与注册流程。
    if (errorCode == 222207 || errorCode == 222202) {
      logInfo("FACE", "SEARCH_NO_MATCH", String("code=") + errorCode);
      return SEARCH_NO_MATCH;
    }
    logError("FACE", "SEARCH_API_ERROR",
             String("code=") + errorCode + " msg=" + respDoc["error_msg"].as<String>());
    return SEARCH_FAILED;
  }

  JsonArray users = respDoc["result"]["user_list"].as<JsonArray>();
  if (users.isNull() || users.size() == 0) {
    logInfo("FACE", "SEARCH_NO_MATCH", "reason=empty_user_list");
    return SEARCH_NO_MATCH;
  }

  float score = users[0]["score"] | 0.0;
  String user_id = users[0]["user_id"].as<String>();
  if (score >= MATCH_THRESHOLD && user_id.length() > 0) {
    matchedUserId = user_id;
    logInfo("FACE", "SEARCH_MATCHED", String("user_id=") + matchedUserId + " score=" + String(score, 2));
    return SEARCH_MATCHED;
  }

  logInfo("FACE", "SEARCH_SCORE_LOW", String("score=") + String(score, 2) + " threshold=" + String(MATCH_THRESHOLD, 2));
  return SEARCH_NO_MATCH;
}

String registerNewFace(const String& base64Image, const String& gender) {
  // 只有搜索确认“没有匹配”后才注册新人脸。
  // 这样可以避免百度临时错误时把同一个熟人重复注册成多个 unknown。
  if (WiFi.status() != WL_CONNECTED || baidu_access_token.length() == 0) {
    logWarn("FACE", "REGISTER_SKIP", String("wifi=") + (WiFi.status() == WL_CONNECTED ? "connected" : "offline")
            + " token=" + (baidu_access_token.length() > 0 ? "present" : "missing"));
    return "";
  }

  if (lastRegisterTime > 0 && millis() - lastRegisterTime < REGISTER_COOLDOWN_MS) {
    logWarn("FACE", "REGISTER_COOLDOWN", String("remaining_ms=") + (REGISTER_COOLDOWN_MS - (millis() - lastRegisterTime)));
    return "";
  }

  String new_user_id;
  // user_id 是人脸库里的访客编号。这里用简单计数方式，便于学生观察结果。
  if (gender == "male") {
    manCount++;
    new_user_id = "man" + String(manCount);
  } else if (gender == "female") {
    womanCount++;
    new_user_id = "woman" + String(womanCount);
  } else if (gender == "unknown") {
    unknownCount++;
    new_user_id = "unknown" + String(unknownCount);
  } else {
    logError("FACE", "REGISTER_BAD_GENDER", String("gender=") + gender);
    return "";
  }

  String url = "https://aip.baidubce.com/rest/2.0/face/v3/faceset/user/add?access_token=" + baidu_access_token;

  String requestBody;
  requestBody.reserve(base64Image.length() + 256);
  appendImageJsonPrefix(requestBody, base64Image);
  requestBody += ",\"group_id\":\"";
  requestBody += FACE_GROUP_ID;
  requestBody += "\",\"user_id\":\"";
  requestBody += new_user_id;
  requestBody += "\"}";

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(requestBody);

  if (httpCode != 200) {
    logError("FACE", "REGISTER_HTTP_FAILED", String("http=") + httpCode + " user_id=" + new_user_id);
    http.end();
    if (gender == "male") manCount--;
    else if (gender == "female") womanCount--;
    else if (gender == "unknown") unknownCount--;
    return "";
  }

  String response = http.getString();
  http.end();

  DynamicJsonDocument respDoc(2048);
  DeserializationError err = deserializeJson(respDoc, response);
  if (err) {
    logError("FACE", "REGISTER_JSON_FAILED",
             String("err=") + err.c_str() + " response_chars=" + response.length() + " user_id=" + new_user_id);
    if (gender == "male") manCount--;
    else if (gender == "female") womanCount--;
    else if (gender == "unknown") unknownCount--;
    return "";
  }

  int errorCode = respDoc["error_code"] | -1;
  if (errorCode != 0) {
    logError("FACE", "REGISTER_API_ERROR",
             String("code=") + errorCode + " msg=" + respDoc["error_msg"].as<String>() + " user_id=" + new_user_id);
    if (gender == "male") manCount--;
    else if (gender == "female") womanCount--;
    else if (gender == "unknown") unknownCount--;
    return "";
  }

  lastRegisterTime = millis();
  saveCounters();
  logInfo("FACE", "REGISTER_OK", String("user_id=") + new_user_id + " gender=" + gender);
  return new_user_id;
}

int getStrangeCount(const String& user_id) {
  // Preferences 是 ESP32 的非易失存储，断电后仍能保留计数。
  prefs.begin("strange_count", false);
  int count = prefs.getInt(user_id.c_str(), 0);
  prefs.end();
  return count;
}

int incrementStrangeCount(const String& user_id) {
  prefs.begin("strange_count", false);
  int count = prefs.getInt(user_id.c_str(), 0) + 1;
  prefs.putInt(user_id.c_str(), count);
  prefs.end();
  return count;
}

void loadCounters() {
  // 开机时恢复已经注册过多少 man/woman/unknown，避免每次从 1 重新编号。
  prefs.begin("face_db", false);
  manCount = prefs.getInt("manCount", 0);
  womanCount = prefs.getInt("womanCount", 0);
  unknownCount = prefs.getInt("unknownCount", 0);
  prefs.end();
  logInfo("FACE", "COUNTERS_LOADED", String("man=") + manCount + " woman=" + womanCount + " unknown=" + unknownCount);
}

void saveCounters() {
  prefs.begin("face_db", false);
  prefs.putInt("manCount", manCount);
  prefs.putInt("womanCount", womanCount);
  prefs.putInt("unknownCount", unknownCount);
  prefs.end();
  logInfo("FACE", "COUNTERS_SAVED", String("man=") + manCount + " woman=" + womanCount + " unknown=" + unknownCount);
}

FaceProcessResult handleFaceRecognition(const String& base64Image, bool allowNewRegistration) {
  // 所有图片上传识别都从这里进入：
  // 门铃抓拍、PIR 三连拍、Blynk 远程拍照都会复用同一流程。
  // allowNewRegistration 用来保护 PIR 一次事件：三张照片都能搜索/统计，
  // 但同一次逗留事件最多只允许注册一个新访客，避免 unknown1/unknown2 连号膨胀。
  String user_id;
  FaceSearchResult searchResult = faceSearch(base64Image, user_id);

  if (searchResult == SEARCH_MATCHED) {
    int newCount = incrementStrangeCount(user_id);
    logInfo("FACE", "KNOWN_VISIT", String("user_id=") + user_id + " count=" + newCount);
    return FACE_PROCESS_MATCHED;
  }

  if (searchResult == SEARCH_FAILED) {
    logWarn("FACE", "REGISTER_SKIP", "reason=search_failed");
    return FACE_PROCESS_SKIPPED;
  }

  if (!allowNewRegistration) {
    logWarn("FACE", "REGISTER_SKIP", "reason=event_already_registered");
    return FACE_PROCESS_SKIPPED;
  }

  String gender = detectGender(base64Image);
  if (gender.length() == 0) {
    logWarn("FACE", "REGISTER_SKIP", "reason=no_face_detected");
    return FACE_PROCESS_NO_FACE;
  }

  String new_user_id = registerNewFace(base64Image, gender);
  if (new_user_id.length() > 0) {
    int newCount = incrementStrangeCount(new_user_id);
    logInfo("FACE", "NEW_VISIT", String("user_id=") + new_user_id + " gender=" + gender + " count=" + newCount);
    return FACE_PROCESS_REGISTERED;
  } else {
    logWarn("FACE", "NEW_VISIT_SKIPPED");
    return FACE_PROCESS_SKIPPED;
  }
}
