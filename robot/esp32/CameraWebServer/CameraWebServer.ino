#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ================= الإعدادات وتوصيل الأطراف =================
#define DHTPIN 15
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// أطراف المحركات (L298N)
#define IN1 25
#define IN2 26
#define IN3 27
#define IN4 14
#define ENA 12  
#define ENB 13  

// نظام الإطفاء
#define RELAY_PUMP 4 
#define SERVO_PIN 19 
Servo myservo; 

// حساسات النار (IR Flame Sensors)
#define SENSOR_FC 32 // الأمامي الأوسط
#define SENSOR_FL 33 // الأمامي يسار
#define SENSOR_FR 34 // الأمامي يمين
#define SENSOR_BL 35 // الخلفي يسار
#define SENSOR_BR 36 // الخلفي يمين

// حساس المسافة (Ultrasonic)
#define TRIG_PIN 5
#define ECHO_PIN 18

// ثوابت التشغيل
#define DISTANCE_CRITICAL 22 // مسافة الفرملة والالتفاف
#define DISTANCE_CAUTION 55 // مسافة التباطؤ
#define FIRE_DETECTED LOW 
#define PUMP_ON HIGH  
#define PUMP_OFF LOW

// إعدادات السرعة
int searchSpeed = 180;   
int cautionSpeed = 115;  
int turnSpeed = 170;     
int backwardSpeed = 140; 

// توقيت النظام
unsigned long workDuration = 3 * 60 * 1000;  
unsigned long sleepDuration = 5 * 60 * 1000; 
unsigned long stateStartTime = 0;
unsigned long previousMillisDHT = 0;
bool isSleeping = false;

void setup() {
  Serial.begin(115200);
  dht.begin();
  lcd.init();
  lcd.backlight();
  
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);
  pinMode(RELAY_PUMP, OUTPUT);
  digitalWrite(RELAY_PUMP, PUMP_OFF);

  ESP32PWM::allocateTimer(0);
  myservo.setPeriodHertz(50); 
  myservo.attach(SERVO_PIN, 500, 2400); 
  myservo.write(90); 

  pinMode(SENSOR_FC, INPUT); pinMode(SENSOR_FL, INPUT);
  pinMode(SENSOR_FR, INPUT); pinMode(SENSOR_BL, INPUT);
  pinMode(SENSOR_BR, INPUT);
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);

  stateStartTime = millis();
}

void loop() {
  // قراءة الحساسات الخمسة للنار
  bool fireFC = (digitalRead(SENSOR_FC) == FIRE_DETECTED);
  bool fireFL = (digitalRead(SENSOR_FL) == FIRE_DETECTED);
  bool fireFR = (digitalRead(SENSOR_FR) == FIRE_DETECTED);
  bool fireBL = (digitalRead(SENSOR_BL) == FIRE_DETECTED);
  bool fireBR = (digitalRead(SENSOR_BR) == FIRE_DETECTED);
  bool fireAnywhere = fireFC || fireFL || fireFR || fireBL || fireBR;

  unsigned long currentTime = millis();

  // الاستيقاظ التلقائي عند استشعار نار
  if (fireAnywhere && isSleeping) {
    isSleeping = false;
    lcd.backlight();
    stateStartTime = currentTime; 
  }

  if (!isSleeping) {
    if (fireAnywhere) {
      stateStartTime = currentTime; // إعادة ضبط وقت النوم طالما هناك عمل
      handleFireTracking(fireFC, fireFL, fireFR, fireBL, fireBR);
    } 
    else {
      manageNavigationAndPower(currentTime);
    }
  } 
  else {
    stopMotors();
    if (currentTime - stateStartTime >= sleepDuration) {
      isSleeping = false;
      lcd.backlight();
      stateStartTime = currentTime;
    }
  }

  updateDHTData();
}

// ================= نظام مطاردة وإطفاء النيران المحترف =================

void handleFireTracking(bool fc, bool fl, bool fr, bool bl, bool br) {
  if (fc) {
    // النار في الأمام: التحقق من المسافة للاقتراب أو الإطفاء
    long dist = getFastDistance();
    if (dist > 30) {
      lcd.setCursor(0, 0); lcd.print("CHASING FIRE... ");
      moveForward(cautionSpeed);
    } else {
      stopMotors();
      executeExtinguishing();
    }
  } 
  else if (fl || bl) {
    // النار جهة اليسار أو الخلف اليسار: التفاف مستمر حتى المحاذاة
    lcd.setCursor(0, 0); lcd.print("ALIGNING LEFT.. ");
    moveLeft(turnSpeed);
    while(digitalRead(SENSOR_FC) != FIRE_DETECTED) {
      delay(5); // نبضة صغيرة لضمان عدم تعليق الكود
    }
    stopMotors();
  } 
  else if (fr || br) {
    // النار جهة اليمين أو الخلف اليمين: التفاف مستمر حتى المحاذاة
    lcd.setCursor(0, 0); lcd.print("ALIGNING RIGHT. ");
    moveRight(turnSpeed);
    while(digitalRead(SENSOR_FC) != FIRE_DETECTED) {
      delay(5);
    }
    stopMotors();
  }
}

void executeExtinguishing() {
  lcd.setCursor(0, 0); lcd.print("EXTINGUISHING...");
  digitalWrite(RELAY_PUMP, PUMP_ON);
  
  unsigned long fireLostTime = 0;
  bool fireStillExists = true;

  while (true) {
    sweepServo(); 
    if (digitalRead(SENSOR_FC) == FIRE_DETECTED) {
      fireStillExists = true;
      fireLostTime = 0;
    } else {
      if (fireStillExists) {
        fireStillExists = false;
        fireLostTime = millis();
        lcd.setCursor(0, 0); lcd.print("Final Cooling..."); 
      }
    }
    // شرط الأمان: التوقف فقط بعد انطفاء النار بـ 8 ثوانٍ
    if (!fireStillExists && (millis() - fireLostTime >= 8000)) break; 
  }
  
  digitalWrite(RELAY_PUMP, PUMP_OFF);
  myservo.write(90);
  lcd.clear();
}

// ================= نظام الملاحة والفرملة النشطة =================

void manageNavigationAndPower(unsigned long currentTime) {
  unsigned long elapsedTime = currentTime - stateStartTime;
  
  if (elapsedTime >= workDuration) {
    enterSleepMode(currentTime);
  } 
  else {
    lcd.setCursor(0, 0);
    if (elapsedTime >= (workDuration - 10000)) lcd.print("repos en cour...");
    else lcd.print("robot en service");

    long dist = getFastDistance();

    if (dist > 0 && dist < DISTANCE_CRITICAL) {
      executeActiveBraking(); 
    } 
    else if (dist < DISTANCE_CAUTION) {
      moveForward(cautionSpeed); 
    } 
    else {
      moveForward(searchSpeed); 
    }
  }
}

long getFastDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  // مهلة استجابة قصيرة (20ms) لضمان سرعة الكود
  long duration = pulseIn(ECHO_PIN, HIGH, 20000); 
  if (duration == 0) return 999;
  return (duration * 0.034 / 2);
}

void executeActiveBraking() {
  // فرملة نشطة: عكس المحركات لـ 150ms لإيقاف الاندفاع فورا
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  analogWrite(ENA, 220); analogWrite(ENB, 220);
  delay(150); 
  
  stopMotors();
  delay(100);
  moveBackward(backwardSpeed);
  delay(600);
  moveRight(turnSpeed);
  delay(500);
  stopMotors();
}

// ================= التحكم في المحركات والمساعدات =================

void setSpeed(int sA, int sB) {
  analogWrite(ENA, sA);
  analogWrite(ENB, sB);
}

void moveForward(int speed) {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  setSpeed(speed, speed);
}

void moveBackward(int speed) {
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  setSpeed(speed, speed);
}

void moveLeft(int speed) {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  setSpeed(speed, speed);
}

void moveRight(int speed) {
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  setSpeed(speed, speed);
}

void stopMotors() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  setSpeed(0, 0);
}

void sweepServo() {
  for (int i = 40; i <= 140; i += 8) { myservo.write(i); delay(25); }
  for (int i = 140; i >= 40; i -= 8) { myservo.write(i); delay(25); }
}

void enterSleepMode(unsigned long t) {
  isSleeping = true;
  stateStartTime = t;
  stopMotors();
  lcd.clear(); lcd.print("le rebot en repos");
  delay(2000); lcd.noBacklight();
}

void updateDHTData() {
  if (isSleeping) return;
  unsigned long cm = millis();
  if (cm - previousMillisDHT >= 2000) {
    previousMillisDHT = cm;
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h) && !isnan(t)) {
      lcd.setCursor(0, 1);
      lcd.print("T:"); lcd.print((int)t); lcd.print("C H:"); lcd.print((int)h); lcd.print("% ");
    }
  }
}