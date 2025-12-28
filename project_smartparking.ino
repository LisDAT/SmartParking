#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <SPI.h>
#include <MFRC522.h>

// ====== CHÂN KẾT NỐI ======
const int ledPin = 3;
const int servoGatePin = 8;
const int trigPin = 7;
const int echoPin = 6;
const int flamePin = 5;
const int buzzerPin = 4;

// RFID
#define SS_PIN 10
#define RST_PIN 9
MFRC522 mfrc522(SS_PIN, RST_PIN);

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo servoGate;

// Thẻ RFID hợp lệ
byte validCard1[] = { 0x57, 0x52, 0xCA, 0x01 };
byte validCard2[] = { 0xB3, 0x86, 0x87, 0x56 };

int availableSpots = 3;
int maxSpots = 3;

// Biến siêu âm
const int detectionDistance = 15;
bool waitingForCarDecision = false;
unsigned long gateOpenTime = 0;
const unsigned long decisionTimeout = 10000;

// Biến cảm biến lửa
bool fireDetected = false;
bool fireAlarmActive = false;
unsigned long fireAlarmStart = 0;
const unsigned long fireAlarmDuration = 2000;

// Biến timing - ĐÃ CHỈNH DÀI HƠN
unsigned long lastUltrasonicTime = 0;
const unsigned long ultrasonicInterval = 200;
unsigned long lastFireCheckTime = 0;
const unsigned long fireCheckInterval = 1000;
unsigned long lastLCDUpdateTime = 0;
const unsigned long lcdUpdateInterval = 2000;
unsigned long lastRfidCheckTime = 0;
const unsigned long rfidCheckInterval = 500;

int currentDistance = 999;
bool carInRange = false;

// ====== LCD ======
void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);

  if (fireDetected) {
    lcd.print("CANH BAO CHAY!");
    lcd.setCursor(0, 1);
    lcd.print("K.cach: ");
    lcd.print(currentDistance);
    lcd.print("cm");
    return;
  }

  if (availableSpots > 0) {
    lcd.print("Con trong: ");
    lcd.print(availableSpots);
  } else {
    lcd.print("DA DAY!");
    lcd.setCursor(0, 1);
    lcd.print("Chuyen ben");
  }

  lastLCDUpdateTime = millis();
}

// ====== NHÁY LED KHÔNG CHẶN - ĐÃ CHỈNH 500ms ======
void blinkLEDNoBlock(int times) {
  static unsigned long lastBlinkTime = 0;
  static int blinkCount = 0;
  static bool ledState = LOW;

  if (blinkCount >= times) return;

  if (millis() - lastBlinkTime >= 500) {  
    lastBlinkTime = millis();
    ledState = !ledState;
    digitalWrite(ledPin, ledState);

    if (ledState == LOW) {
      blinkCount++;
    }
  }
}

// ====== ĐO KHOẢNG CÁCH KHÔNG CHẶN ======
void checkUltrasonic() {
  if (millis() - lastUltrasonicTime < ultrasonicInterval) return;
  lastUltrasonicTime = millis();

  //Phát xung từ chân trig
  digitalWrite(trigPin, 0);
  delayMicroseconds(2);
  digitalWrite(trigPin, 1);
  delayMicroseconds(10);
  digitalWrite(trigPin, 0);

  //Đọc thời gian với timeout ngắn
  unsigned long thoigian = pulseIn(echoPin, HIGH, 10000);  // Timeout 10ms

  if (thoigian == 0) {
    currentDistance = 999;
  } else {
    currentDistance = int(thoigian / 2 / 29.412);
  }

  carInRange = (currentDistance <= detectionDistance && currentDistance > 0);

  // Debug
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 2000) {
    lastPrint = millis();
    Serial.print("Distance: ");
    Serial.print(currentDistance);
    Serial.print("cm | Car: ");
    Serial.println(carInRange ? "YES" : "NO");
  }
}

// ====== KIỂM TRA CẢM BIẾN LỬA ======
void checkFireSensor() {
  if (millis() - lastFireCheckTime < fireCheckInterval) return;
  lastFireCheckTime = millis();

  bool flameDetected = (digitalRead(flamePin) == LOW);

  if (flameDetected) {
    if (!fireDetected) {
      fireDetected = true;
      fireAlarmActive = true;
      fireAlarmStart = millis();

      Serial.println("CANH BAO: Phat hien lua!");
      tone(buzzerPin, 1000);
      updateLCD();
    }
  } else {
    if (fireDetected) {
      fireDetected = false;
      Serial.println("Lua da duoc kiem soat!");
      updateLCD();
    }
  }

  if (fireAlarmActive && (millis() - fireAlarmStart >= fireAlarmDuration)) {
    fireAlarmActive = false;
    noTone(buzzerPin);
  }
}

// ====== KIỂM TRA THẺ RFID HỢP LỆ ======
bool isCardValid() {
  bool matchCard1 = true;
  bool matchCard2 = true;

  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] != validCard1[i]) matchCard1 = false;
    if (mfrc522.uid.uidByte[i] != validCard2[i]) matchCard2 = false;
  }

  return (matchCard1 || matchCard2);
}

// ====== MỞ CỔNG VÀ CHỜ QUYẾT ĐỊNH - ĐÃ CHỈNH SERVO VÀ LED ======
void openGateAndWaitForDecision() {
  if (fireDetected) {
    Serial.println("KHONG MO CUA: Dang co chay!");
    lcd.clear();
    lcd.print("CO CHAY!");
    lcd.setCursor(0, 1);
    lcd.print("Khong mo cua");

    // Nhấp nháy LED với 500ms mỗi lần
    for (int i = 0; i < 5; i++) {
      digitalWrite(ledPin, HIGH);
      delay(500);  // ĐÃ CHỈNH: 200ms → 500ms
      digitalWrite(ledPin, LOW);
      delay(500);  // ĐÃ CHỈNH: 200ms → 500ms
    }

    delay(1000);
    updateLCD();
    return;
  }

  lcd.clear();
  lcd.print("Mo cua...");

  // Mở servo với thời gian 1000ms
  servoGate.write(90);
  delay(500);  // ĐÃ CHỈNH: Thêm delay 1000ms cho servo

  // Nhấp nháy với 500ms mỗi lần
  for (int i = 0; i < 3; i++) {
    digitalWrite(ledPin, HIGH);
    delay(500);  // ĐÃ CHỈNH: 100ms → 500ms
    digitalWrite(ledPin, LOW);
    delay(500);  // ĐÃ CHỈNH: 100ms → 500ms
  }

  waitingForCarDecision = true;
  gateOpenTime = millis();

  Serial.println("Cua da mo! Dang kiem tra xe...");
}

// ====== KIỂM TRA VÀ QUYẾT ĐỊNH TĂNG/GIẢM CHỖ ======
void checkAndMakeDecision() {
  if (!waitingForCarDecision) return;

  // Kiểm tra timeout (10 giây)
  if (millis() - gateOpenTime > decisionTimeout) {
    Serial.println("Het thoi gian! Dong cua.");
    closeGate();
    waitingForCarDecision = false;
    return;
  }

  // Sử dụng giá trị khoảng cách đã đo trước đó
  if (carInRange) {
    // CÓ xe trong vùng -> XE VÀO
    if (availableSpots > 0) {
      availableSpots--;
      Serial.println("Xe vao!");
      Serial.print("Cho con trong: ");
      Serial.println(availableSpots);
    }

    // Đóng cổng
    closeGate();
    waitingForCarDecision = false;

    // Hiển thị thông báo nhanh
    lcd.clear();
    lcd.print("Xe da vao!");
    delay(500);
    updateLCD();

  } else {
    // KHÔNG có xe trong vùng -> XE RA
    availableSpots++;
    if (availableSpots > maxSpots) {
      availableSpots = maxSpots;
    }

    Serial.println("Xe ra!");
    Serial.print("Cho con trong: ");
    Serial.println(availableSpots);

    // Đóng cổng
    closeGate();
    waitingForCarDecision = false;

    // Hiển thị thông báo nhanh
    lcd.clear();
    lcd.print("Xe da ra!");
    delay(500);
    updateLCD();
  }
}

// ====== ĐÓNG CỔNG - ĐÃ CHỈNH SERVO ======
void closeGate() {
  servoGate.write(0);
  delay(500);  // ĐÃ CHỈNH: 100ms → 1000ms
  Serial.println("Cua da dong.");
}

// ====== KIỂM TRA RFID ======
void checkRFID() {
  if (millis() - lastRfidCheckTime < rfidCheckInterval) return;
  lastRfidCheckTime = millis();

  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    // Hiển thị UID
    Serial.print("UID the: ");
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
      Serial.print(mfrc522.uid.uidByte[i], HEX);
    }
    Serial.println();

    if (isCardValid()) {
      Serial.println("The hop le!");
      openGateAndWaitForDecision();
    } else {
      Serial.println("The RFID KHONG HOP LE!");
      lcd.clear();
      lcd.print("The SAI!");
      lcd.setCursor(0, 1);
      lcd.print("Thu lai!");

      // Nhấp nháy với 500ms
      for (int i = 0; i < 5; i++) {
        digitalWrite(ledPin, HIGH);
        delay(500);  // ĐÃ CHỈNH: 50ms → 500ms
        digitalWrite(ledPin, LOW);
        delay(500);  // ĐÃ CHỈNH: 50ms → 500ms
      }

      delay(800);
      updateLCD();
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }
}

// ====== SETUP ======
void setup() {
  Serial.begin(9600);
  Serial.println("=== KHOI DONG HE THONG ===");

  // Cấu hình chân
  pinMode(ledPin, OUTPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(flamePin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);

  digitalWrite(ledPin, LOW);
  digitalWrite(trigPin, LOW);
  digitalWrite(buzzerPin, LOW);
  noTone(buzzerPin);

  // Khởi tạo I2C trước
  Wire.begin();

  // Khởi tạo LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Khoi dong...");
  delay(500);

  // Khởi tạo RFID
  SPI.begin();
  mfrc522.PCD_Init();

  // Khởi tạo servo
  servoGate.attach(servoGatePin);
  servoGate.write(0);

  // Hiển thị ban đầu
  updateLCD();

  Serial.println("He thong san sang!");
}

// ====== LOOP - KHÔNG DÙNG DELAY LỚN ======
void loop() {
  // 1. Kiểm tra siêu âm (quan trọng nhất)
  checkUltrasonic();

  // 2. Kiểm tra cảm biến lửa
  checkFireSensor();

  // 3. Kiểm tra nếu đang chờ quyết định
  if (waitingForCarDecision) {
    checkAndMakeDecision();
  }

  // 4. Kiểm tra RFID
  checkRFID();

  // 5. Cập nhật LCD định kỳ
  if (millis() - lastLCDUpdateTime >= lcdUpdateInterval) {
    updateLCD();
  }

  // Delay nhỏ để ổn định
  delay(10);
}