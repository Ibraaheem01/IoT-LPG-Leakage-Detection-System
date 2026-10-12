/**
 * @file lpg_detector.ino
 * @brief Development and Implementation of an Internet-of-Things (IoT)-Based
 *        Dual-Zone Liquefied Petroleum Gas (LPG) Leakage Detection and
 *        Automated Mitigation System
 * @author Oyekanmi Ibraheem Oyeleke
 * @institution Federal University of Technology, Minna (FUTMINNA)
 * @target_hardware ESP32 NodeMCU (30-pin)
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>

// ====== HARDWARE PIN ALLOCATION ======
#define MQ5_PIN           34  // Zone 1 Analog Input (MQ-5 via 10k/20k Divider)
#define MQ6_PIN           35  // Zone 2 Analog Input (MQ-6 via 10k/20k Divider)
#define RELAY_PIN          2  // Solenoid Valve Relay Control (Optocoupled)
#define BUZZER_PIN        13  // Piezo Audible Alarm
#define LED_GREEN1_PIN    14  // Zone 1 Normal Indicator
#define LED_GREEN2_PIN    27  // Zone 2 Normal Indicator
#define LED_YELLOW_PIN    12  // System Warning Indicator
#define LED_RED1_PIN      26  // Zone 1 Alarm Indicator
#define LED_RED2_PIN      25  // Zone 2 Alarm Indicator
#define RX2_PIN           16  // ESP32 RX2 connected to SIM800L TX
#define TX2_PIN           17  // ESP32 TX2 connected to SIM800L RX

// ====== SYSTEM PARAMETERS & CALIBRATION ======
const float VOLTAGE_THRESHOLD_ALARM = 1.5; // Threshold voltage (V) to trigger active leak alert
const String PHONE_NUMBER = "+2348103337556"; // Target mobile number for emergency SMS alerts

// I2C LCD Configuration (Address 0x27, 16 Columns x 2 Rows)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Timing variables for non-blocking execution (FSM principle)
unsigned long previousMillisSensor = 0;
unsigned long previousMillisBlink = 0;
const long sensorInterval = 500;   // Sample gas sensors every 500 ms
const long blinkInterval = 250;    // Flash red LEDs/Buzzer every 250 ms

bool alertSent = false;
bool blinkState = false;

// Function Prototypes
float readSensorVoltage(int pin);
void sendSMS(String message);

void setup() {
  // Initialize USB Serial for debugging monitor
  Serial.begin(115200);

  // Initialize Hardware Serial 2 for SIM800L GSM Telemetry
  Serial2.begin(9600, SERIAL_8N1, RX2_PIN, TX2_PIN);

  // Configure Output Pins
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_GREEN1_PIN, OUTPUT);
  pinMode(LED_GREEN2_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED1_PIN, OUTPUT);
  pinMode(LED_RED2_PIN, OUTPUT);

  // Set initial safe standby state
  digitalWrite(RELAY_PIN, LOW);       // Solenoid valve de-energized (Open)
  digitalWrite(BUZZER_PIN, LOW);      // Buzzer muted
  digitalWrite(LED_GREEN1_PIN, HIGH); // Zone 1 OK
  digitalWrite(LED_GREEN2_PIN, HIGH); // Zone 2 OK
  digitalWrite(LED_YELLOW_PIN, LOW);
  digitalWrite(LED_RED1_PIN, LOW);
  digitalWrite(LED_RED2_PIN, LOW);

  // Initialize I2C Display (GPIO 21 = SDA, GPIO 22 = SCL)
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  
  lcd.setCursor(0, 0);
  lcd.print("LPG SAFETY SYS");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(2000);
  lcd.clear();
}

void loop() {
  unsigned long currentMillis = millis();

  // Periodic non-blocking sensor acquisition
  if (currentMillis - previousMillisSensor >= sensorInterval) {
    previousMillisSensor = currentMillis;

    // Read analog voltages
    float vZone1 = readSensorVoltage(MQ5_PIN);
    float vZone2 = readSensorVoltage(MQ6_PIN);

    bool zone1Leak = (vZone1 >= VOLTAGE_THRESHOLD_ALARM);
    bool zone2Leak = (vZone2 >= VOLTAGE_THRESHOLD_ALARM);

    // Update 16x2 LCD Readings
    lcd.setCursor(0, 0);
    lcd.print("Z1: ");
    lcd.print(vZone1, 2);
    lcd.print("V ");
    lcd.print(zone1Leak ? "LEAK!" : "OK ");

    lcd.setCursor(0, 1);
    lcd.print("Z2: ");
    lcd.print(vZone2, 2);
    lcd.print("V ");
    lcd.print(zone2Leak ? "LEAK!" : "OK ");

    // System State Machine
    if (zone1Leak || zone2Leak) {
      // HAZARD / LEAK DETECTED STATE
      digitalWrite(RELAY_PIN, HIGH); // Actuate Relay to energize/close Solenoid Shutoff Valve
      
      digitalWrite(LED_GREEN1_PIN, zone1Leak ? LOW : HIGH);
      digitalWrite(LED_GREEN2_PIN, zone2Leak ? LOW : HIGH);
      digitalWrite(LED_YELLOW_PIN, HIGH); // General Warning Indicator ON

      // Toggle flashing alarm indicators
      if (currentMillis - previousMillisBlink >= blinkInterval) {
        previousMillisBlink = currentMillis;
        blinkState = !blinkState;
        digitalWrite(BUZZER_PIN, blinkState ? HIGH : LOW);
        digitalWrite(LED_RED1_PIN, zone1Leak ? (blinkState ? HIGH : LOW) : LOW);
        digitalWrite(LED_RED2_PIN, zone2Leak ? (blinkState ? HIGH : LOW) : LOW);
      }

      // Execute telemetry alert transmission (fires once per leak incident)
      if (!alertSent) {
        String msg = "EMERGENCY: Gas Leakage Detected!\n";
        if (zone1Leak && zone2Leak) {
          msg += "- Both Locations Active\n";
        } else if (zone1Leak) {
          msg += "- Zone 1 (MQ-5) Active\n";
        } else if (zone2Leak) {
          msg += "- Zone 2 (MQ-6) Active\n";
        }
        msg += "Automated Solenoid Shutoff Actuated.";
        
        sendSMS(msg);
        alertSent = true;
      }

    } else {
      // NORMAL / SAFE STATE
      digitalWrite(RELAY_PIN, LOW);       // Solenoid Valve Open
      digitalWrite(BUZZER_PIN, LOW);      // Buzzer Off
      digitalWrite(LED_GREEN1_PIN, HIGH);
      digitalWrite(LED_GREEN2_PIN, HIGH);
      digitalWrite(LED_YELLOW_PIN, LOW);
      digitalWrite(LED_RED1_PIN, LOW);
      digitalWrite(LED_RED2_PIN, LOW);

      alertSent = false; // Reset SMS flag once environment returns to safe baseline
    }
  }
}

// ====== HELPER FUNCTIONS ======

/**
 * @brief Reads analog gas sensor voltage via ESP32 12-bit ADC
 * @param pin ESP32 GPIO pin assigned to sensor analog output
 * @return Actual scaled sensor output voltage before resistor divider
 * @note Uses 12-bit ADC resolution (0-4095) mapped across 3.3V VREF
 */
float readSensorVoltage(int pin) {
  int rawADC = analogRead(pin);
  float measuredVoltage = (rawADC / 4095.0) * 3.3; // ADC conversion for ESP32 (0-3.3V)
  float actualSensorVoltage = measuredVoltage / 0.667; // Correct for 10k/20k voltage divider attenuation
  return actualSensorVoltage;
}

/**
 * @brief Sends emergency SMS text notification via SIM800L GSM module
 * @param message String payload containing location and isolation status
 * @note Communicates over hardware UART2 using 3GPP AT commands at 9600 baud
 */
void sendSMS(String message) {
  Serial2.println("AT+CMGF=1"); // Set SMS module to text mode
  delay(200);
  Serial2.print("AT+CMGS=\"");
  Serial2.print(PHONE_NUMBER);
  Serial2.println("\"");
  delay(200);
  Serial2.print(message);
  delay(200);
  Serial2.write(26); // ASCII character 26 (Ctrl+Z) to trigger SMS send
  delay(1000);
}
