#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include "time.h"
#include <WiFiMulti.h>

WiFiMulti wifiMulti ;

#define FIREBASE_HOST "https://iot-farm-36262-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_AUTH "hP9Srdf48azntXtqwSvisXqZKs4JhYbgyEtqETkB"

#define RELAY_PIN 32

FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseData fbUpdate;


String currentMode = "manual";
int currentStatus = 0;
String onTime = "18:00";
String offTime = "06:00";

// ตั้งค่าเวลาสำหรับประเทศไทย (UTC+7)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; 
const int   daylightOffset_sec = 0;

// ฟังก์ชันแปลงเวลา "HH:MM" เป็นนาที เพื่อให้คำนวณง่ายๆ (เช่น 01:00 = 60 นาที)
int timeToMinutes(String t) {
  if (t.length() < 5) return 0;
  int h = t.substring(0, 2).toInt();
  int m = t.substring(3, 5).toInt();
  return (h * 60) + m;
}

void setup() {
  Serial.begin(115200);
  
  // คุณใช้รีเลย์แบบ Active High เลยตั้งค่าเริ่มต้นเป็น LOW (ปิด) ไว้ก่อน
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); 

 // ปิดโหมดปล่อยสัญญาณของ ESP32 เพื่อให้รับสัญญาณอย่างเดียว (ประหยัดพลังงาน)
  WiFi.mode(WIFI_STA);

  // ใส่ชื่อและรหัสผ่าน Wi-Fi ที่ต้องการให้บอร์ดจำ (ใส่กี่ตัวก็ได้)
  wifiMulti.addAP("A_2.4G", "00251900AS");
  wifiMulti.addAP("Xiaomi_wifi", "00251900AS");

  Serial.println("กำลังค้นหาและเชื่อมต่อ Wi-Fi ");

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  
  // ให้บอร์ดพยายามหาและเชื่อมต่อ
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    Serial.println("\nเชื่อมต่อ Wi-Fi สำเร็จ!");
    Serial.print("เชื่อมต่ออยู่ที่เครือข่าย: ");
    Serial.println(WiFi.SSID());
  }
  
  Serial.println("\nเชื่อมต่อ Wi-Fi สำเร็จ!");
  Serial.print("เชื่อมต่ออยู่ที่เครือข่าย: ");
  Serial.println(WiFi.SSID()); // ปริ้นท์บอกด้วยว่าตกลงเกาะ Wi-Fi ตัวไหนได้
  // เชื่อมต่อ Firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // ดึงค่าล่าสุดจาก Firebase มาไว้ในบอร์ด 1 ครั้งตอนเปิดเครื่อง
  if (Firebase.getString(fbUpdate, "/farm/relay1/mode")) currentMode = fbUpdate.stringData();
  if (Firebase.getInt(fbUpdate, "/farm/relay1/status")) currentStatus = fbUpdate.intData();
  if (Firebase.getString(fbUpdate, "/farm/relay1/on_time")) onTime = fbUpdate.stringData();
  if (Firebase.getString(fbUpdate, "/farm/relay1/off_time")) offTime = fbUpdate.stringData();
  
  // อัปเดตรีเลย์ตามค่าเริ่มต้น
  digitalWrite(RELAY_PIN, currentStatus == 1 ? HIGH : LOW);

  // เริ่มการดักฟังข้อมูล (Stream)
  Firebase.beginStream(firebaseData, "/farm/relay1");
  Serial.println("ระบบฟาร์มอัตโนมัติพร้อมทำงาน!");
}

void loop() {
  // 1. เช็ค Wi-Fi หลุด
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi หลุด... กำลังเชื่อมต่อใหม่");
    WiFi.reconnect();
  }
  
  Serial.println("Free RAM: " + String(ESP.getFreeHeap()));
  
  // --- ส่วนที่ 1: รับการแจ้งเตือนจากแอป (ครอบ Firebase.ready ไว้ตรงนี้) ---
  if (Firebase.ready()) {
    if (Firebase.readStream(firebaseData)) {
      if (firebaseData.streamAvailable()) {
        // ดึงเฉพาะค่าที่มีการอัปเดต
        if (Firebase.getString(fbUpdate, "/farm/relay1/mode")) currentMode = fbUpdate.stringData();
        if (Firebase.getInt(fbUpdate, "/farm/relay1/status")) currentStatus = fbUpdate.intData();
        if (Firebase.getString(fbUpdate, "/farm/relay1/on_time")) onTime = fbUpdate.stringData();
        if (Firebase.getString(fbUpdate, "/farm/relay1/off_time")) offTime = fbUpdate.stringData();
        
        Serial.println(">> อัปเดตการตั้งค่าใหม่: โหมด " + currentMode + " | เปิด: " + onTime + " | ปิด: " + offTime);
        
        // --- โค้ดสำหรับดึงเวลาปัจจุบัน (HH:MM) ---
        String currentTime = "";
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
          char timeStringBuff[6];
          strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
          currentTime = String(timeStringBuff);
        }

        Serial.println("เวลาบอร์ด: [" + currentTime + "] ");
        
        // ถ้าเป็นโหมด manual ให้เปิด-ปิดตามปุ่มทันที
        if (currentMode == "manual") {
          digitalWrite(RELAY_PIN, currentStatus == 1 ? HIGH : LOW);
          String replyMsg = currentStatus == 1 ? "บอร์ดได้รับคำสั่ง: เปิดไฟเรียบร้อย" : "บอร์ดได้รับคำสั่ง: ปิดไฟเรียบร้อย";
          Firebase.setString(fbUpdate, "/farm/relay1/feedback", replyMsg);
        }
      }
    }
  }

  // --- ส่วนที่ 2: เช็คเวลาแบบเงียบๆ (Auto Mode) ---
  if (currentMode == "auto") {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      // ดึงเวลาปัจจุบัน
      char timeBuff[6];
      strftime(timeBuff, sizeof(timeBuff), "%H:%M", &timeinfo);
      String currentTime = String(timeBuff);

      // แปลงเป็นนาที
      int curr_m = timeToMinutes(currentTime);
      int on_m = timeToMinutes(onTime);
      int off_m = timeToMinutes(offTime);
      
      bool shouldBeOn = false;
      if (on_m < off_m) {
        shouldBeOn = (curr_m >= on_m && curr_m < off_m);
      } else {
        shouldBeOn = (curr_m >= on_m || curr_m < off_m);
      }

      int desiredStatus = shouldBeOn ? 1 : 0;
      
      // ทำงาน "เฉพาะตอนที่ถึงเวลาต้องเปลี่ยนสถานะเท่านั้น"
      if (currentStatus != desiredStatus) {
        currentStatus = desiredStatus;
        
        digitalWrite(RELAY_PIN, currentStatus == 1 ? HIGH : LOW);
        Serial.println(">> [AUTO] ถึงเวลา " + currentTime + " สั่งเปลี่ยนสถานะไฟเป็น: " + String(currentStatus));
        
        // เช็คความพร้อมก่อนส่งอัปเดตกลับไปที่ Firebase เพื่อป้องกัน Error
        if (Firebase.ready()) {
          Firebase.setInt(fbUpdate, "/farm/relay1/status", currentStatus);
          String replyMsg = currentStatus == 1 ? "ระบบอัตโนมัติ: เปิดไฟแล้ว" : "ระบบอัตโนมัติ: ปิดไฟแล้ว";
          Firebase.setString(fbUpdate, "/farm/relay1/feedback", replyMsg);
        }
      }
    }
  }
  
  // หน่วงเวลา 2 วินาที (ประหยัดพลังงาน ไม่ต้องเช็คถี่เกินไป)
  delay(2000); 
}