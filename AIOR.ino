/*
 *  ALL_IN_ONE_ARDUINO_ROBOT
 *  - Line Follower + Obstacle Avoiding + Manual Control
 *  - IR Remote + HC-05 Bluetooth (Android app via MIT App Inventor)
 *  - Apache License 2.0
 *
 *  App files (AIOR.aia and AIOR.apk) are included separately in the repo.
 */

#include <SoftwareSerial.h>
#include <IRremote.h>   // Use IRremote v3.x+ API
#include <Servo.h>

// -------------------- Pins --------------------
SoftwareSerial BT_Serial(2, 3); // RX, TX (HC-05)
const int RECV_PIN = A5;        // IR receiver

#define enA 10  // L298N Enable A (Right)
#define in1 9   // Right motor IN1
#define in2 8   // Right motor IN2
#define in3 7   // Left motor IN3
#define in4 6   // Left motor IN4
#define enB 5   // L298N Enable B (Left)

#define SERVO_PIN A4

#define R_S A0   // Right IR sensor
#define L_S A1   // Left IR sensor

#define ECHO_PIN    A2
#define TRIG_PIN    A3

// -------------------- Globals --------------------
Servo scanServo;

int Speed = 130;          // PWM speed (0-255), adjustable via app slider
int mode = 0;             // 0=Manual, 1=Line follower, 2=Obstacle avoid
int bt_ir_data = 5;       // Last command (default Stop)
int setDistance = 20;     // Threshold distance in cm

int distance_F = 30, distance_L = 0, distance_R = 0;
unsigned long lastCmdPrint = 0;

// -------------------- Setup --------------------
void setup() {
  Serial.begin(9600);
  BT_Serial.begin(9600);

  // Motors
  pinMode(enA, OUTPUT);
  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);
  pinMode(in3, OUTPUT);
  pinMode(in4, OUTPUT);
  pinMode(enB, OUTPUT);

  // Sensors
  pinMode(R_S, INPUT);
  pinMode(L_S, INPUT);

  pinMode(ECHO_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);

  // IR
  IrReceiver.begin(RECV_PIN, ENABLE_LED_FEEDBACK);

  // Servo
  scanServo.attach(SERVO_PIN);
  scanServo.write(90); // center

  // Initial sweep
  sweepServo(70, 140, 5);
  sweepServo(140, 0, -5);
  sweepServo(0, 70, 5);
  delay(300);
}

// -------------------- Main Loop --------------------
void loop() {
  // Read Bluetooth app commands
  readBluetooth();

  // Read IR remote commands
  readIR();

  // Mode switching (from bt_ir_data)
  if (bt_ir_data == 8) { mode = 0; Stop(); }        // Manual
  else if (bt_ir_data == 9) { mode = 1; Speed = 130; } // Line follower
  else if (bt_ir_data == 10) { mode = 2; Speed = 255; } // Obstacle avoid

  // Apply speed
  analogWrite(enA, Speed);
  analogWrite(enB, Speed);

  // Execute modes
  if (mode == 0) {
    // Manual key control
    if      (bt_ir_data == 1) { forward(); }
    else if (bt_ir_data == 2) { backward(); }
    else if (bt_ir_data == 3) { turnLeft(); }
    else if (bt_ir_data == 4) { turnRight(); }
    else if (bt_ir_data == 5 || bt_ir_data == 0) { Stop(); } // 0 or 5 = Stop

    // Voice control (momentary)
    else if (bt_ir_data == 6) { turnLeft();  delay(400); bt_ir_data = 5; Stop(); }
    else if (bt_ir_data == 7) { turnRight(); delay(400); bt_ir_data = 5; Stop(); }
  }
  else if (mode == 1) {
    // Line follower
    bool r = digitalRead(R_S);
    bool l = digitalRead(L_S);

    if (!r && !l) { forward(); }  // both white
    else if ( r && !l) { turnRight(); } // right black, left white
    else if (!r &&  l) { turnLeft(); }  // right white, left black
    else if ( r &&  l) { Stop(); }      // both black (junction/end)
  }
  else if (mode == 2) {
    // Obstacle avoid
    distance_F = Ultrasonic_read();
    if (distance_F > setDistance) {
      forward();
    } else {
      Check_side();
    }
  }

  // Optional: print current command (rate-limited)
  if (millis() - lastCmdPrint > 500) {
    Serial.print("Mode: "); Serial.print(mode);
    Serial.print("  Speed: "); Serial.print(Speed);
    Serial.print("  Cmd: "); Serial.println(bt_ir_data);
    lastCmdPrint = millis();
  }

  delay(10);
}

// -------------------- Input Handlers --------------------
void readBluetooth() {
  if (BT_Serial.available() > 0) {
    int data = BT_Serial.read();
    Serial.print("BT: "); Serial.println(data);

    // Slider values (speed)
    if (data >= 21 && data <= 255) {
      Speed = data;
    } else {
      // Command codes from app buttons/voice
      bt_ir_data = data;
    }
  }
}

void readIR() {
  if (IrReceiver.decode()) {
    uint32_t raw = IrReceiver.decodedIRData.decodedRawData;
    Serial.print("IR RAW: 0x"); Serial.println(raw, HEX);
    bt_ir_data = IRremote_command(raw);
    Serial.print("IR CMD: "); Serial.println(bt_ir_data);
    IrReceiver.resume();
    delay(80);
  }
}

// -------------------- IR Mapping --------------------
int IRremote_command(uint32_t value) {
  // Map your IR remote button codes to unified command IDs:
  // 1=Forward, 2=Backward, 3=Left, 4=Right, 5=Stop
  // 8=Manual, 9=Line Follower, 10=Obstacle Avoid
  if      (value == 0xFF02FD) return 1;   // Forward
  else if (value == 0xFF9867) return 2;   // Backward
  else if (value == 0xFFE01F) return 3;   // Left
  else if (value == 0xFF906F) return 4;   // Right
  else if (value == 0xFF629D || value == 0xFFA857) return 5; // Stop
  else if (value == 0xFF30CF) return 8;   // Manual mode
  else if (value == 0xFF18E7) return 9;   // Line follower mode
  else if (value == 0xFF7A85) return 10;  // Obstacle avoid mode
  else return 0; // Unknown
}

// -------------------- Servo Helpers --------------------
void sweepServo(int startAngle, int endAngle, int step) {
  if (step == 0) return;
  // Direction comes from the endpoints; callers may pass either step sign.
  long stepSize = step;
  if (stepSize < 0) stepSize = -stepSize;
  if (startAngle < endAngle) {
    for (long a = startAngle; a <= endAngle; a += stepSize) {
      scanServo.write(a);
      delay(20);
    }
  } else {
    for (long a = startAngle; a >= endAngle; a -= stepSize) {
      scanServo.write(a);
      delay(20);
    }
  }
}

// -------------------- Ultrasonic --------------------
int Ultrasonic_read() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout (~5m)
  if (duration == 0) return 400; // No echo
  int cm = duration / 29 / 2;
  return cm;
}

void compareDistance() {
  if (distance_L > distance_R) {
    turnLeft();
    delay(350);
  } else if (distance_R > distance_L) {
    turnRight();
    delay(350);
  } else {
    backward();
    delay(300);
    turnRight();
    delay(600);
  }
}

void Check_side() {
  Stop();
  delay(100);

  // Look left
  sweepServo(70, 140, 5);
  delay(150);
  distance_L = Ultrasonic_read();

  // Look right
  sweepServo(140, 0, -5);
  delay(150);
  distance_R = Ultrasonic_read();

    // Return center
  sweepServo(0, 70, 5);
  delay(150);

  compareDistance();
}

// -------------------- Motor Control --------------------
void forward() {
  // Right motor forward
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);
  // Left motor forward
  digitalWrite(in3, LOW);
  digitalWrite(in4, HIGH);
}

void backward() {
  // Right motor backward
  digitalWrite(in1, LOW);
  digitalWrite(in2, HIGH);
  // Left motor backward
  digitalWrite(in3, HIGH);
  digitalWrite(in4, LOW);
}

void turnRight() {
  // In-place right turn (right backward, left forward)
  digitalWrite(in1, LOW);
  digitalWrite(in2, HIGH);
  digitalWrite(in3, LOW);
  digitalWrite(in4, HIGH);
}

void turnLeft() {
  // In-place left turn (right forward, left backward)
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);
  digitalWrite(in3, HIGH);
  digitalWrite(in4, LOW);
}

void Stop() {
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
  digitalWrite(in3, LOW);
  digitalWrite(in4, LOW);
}
