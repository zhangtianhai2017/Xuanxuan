/*
  百度云人脸识别头文件

  这部分响应项目的人脸识别需求：
    - 门铃照片要上传识别。
    - PIR 异常逗留照片也要上传识别。
    - 已有人脸优先匹配；确认为新人时再注册到人脸库。

  初学者重点理解 FaceSearchResult：
    搜索失败和“确实没有匹配到人”不是一回事。
    如果网络/API 失败就注册新人，会把数据库弄乱，所以要分状态。
*/

#ifndef FACE_RECOGNITION_H
#define FACE_RECOGNITION_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Base64.h>
#include <Preferences.h>

extern const char* baidu_api_key;
extern const char* baidu_secret_key;
extern String baidu_access_token;

extern const String FACE_GROUP_ID;
extern const float MATCH_THRESHOLD;
extern const float GENDER_CONFIDENCE_THRESHOLD;

extern int manCount;
extern int womanCount;
extern int unknownCount;

enum FaceSearchResult {
  SEARCH_MATCHED,
  SEARCH_NO_MATCH,
  SEARCH_FAILED
};

enum FaceProcessResult {
  FACE_PROCESS_MATCHED,
  FACE_PROCESS_REGISTERED,
  FACE_PROCESS_NO_FACE,
  FACE_PROCESS_SKIPPED
};

String getBaiduAccessToken();
String detectGender(const String& base64Image);
FaceSearchResult faceSearch(const String& base64Image, String& matchedUserId);
String registerNewFace(const String& base64Image, const String& gender);
int getStrangeCount(const String& user_id);
int incrementStrangeCount(const String& user_id);
void loadCounters();
void saveCounters();
FaceProcessResult handleFaceRecognition(const String& base64Image, bool allowNewRegistration = true);

#endif
