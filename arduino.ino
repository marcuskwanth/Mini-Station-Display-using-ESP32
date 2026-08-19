// Code for ESP32 Firebeetle (2026-08-19)

#include <PWMOutESP32.h>
#include <Wire.h>  // Arduino IDE built-in
#include <LiquidCrystal_I2C.h>
#include <ButtonDebounce.h>
#include <SPI.h>
#include <GyverMAX7219.h>
#include <RunningGFX.h>

// Define the SPI pins for LED (SPI).
#define SCK 4     // Serial Clock (SCK) pin
#define MOSI 17   // Master Output Slave Input (MOSI) pin
#define CS_PIN 16
#define LED_SPD 18 // Scrolling speed (pixels/second)

// Defining the ports for LCD (I2C).
#define I2C_SDA 21
#define I2C_SCL 22
#define MAX_DISPLAY  8

// Defining the key and debounce ratae for button detection.
#define MON 5
#define DEBOUNCE 200 // in ms
long unsigned int last_mon_pressed = 0;
bool keypressed = false;

// Defining ports for LEDs & Max. PWM value for LEDs
#define RED   13
#define GRN   27
#define BLUE  2
#define MAX_RESOLUTION 4095
#define MIN_RESOLUTION 0

MAX7219<2, 1, CS_PIN, MOSI, SCK> mtrx;
RunningGFX run(&mtrx);
LiquidCrystal_I2C lcd(0x27,16,2); //Address: 0x27 for PCF8754; 0x3F for PCF8754A
PWMOutESP32 pwm(12, 5000);
byte display = -1;

// For train display sequence
unsigned long trainMessageTimer = 0;
int stationIndex = 0;
bool trainSequenceActive = false;
const unsigned long TRAIN_MESSAGE_INTERVAL = 17000; // Set delay time (in ms) between each LED message.

const int* activeStationRoute = nullptr;
int activeStationRouteLength = 0;

// For LCD display sequence
unsigned long lcdMessageTimer = 0;
bool lcdShowingDestination = false;
bool localExpBlueActive = false;
String lcdRouteTitle;
String lcdRouteOrigin;
String lcdRouteDestination;
const unsigned long LCD_MESSAGE_INTERVAL = 3000; // Set delay time (in ms) between each LCD info display.

// Button Debounce
void IRAM_ATTR mon_pressed() {
  long int button_time = millis();
  if (button_time - last_mon_pressed > DEBOUNCE) {
    keypressed = true;
    display++;
    if (display == MAX_DISPLAY) display = 0;
    last_mon_pressed = button_time; // Update last_mon_pressed here
  }
}

// LEDs Initialize (PWM)
void initializeLed() {
  pwm.analogWrite(RED, MIN_RESOLUTION*2);
  pwm.analogWrite(GRN, MIN_RESOLUTION*2);
  pwm.analogWrite(BLUE, MIN_RESOLUTION*2);
}

void localLED() {
  pwm.analogWrite(RED, MAX_RESOLUTION);
  pwm.analogWrite(GRN, MAX_RESOLUTION);
  pwm.analogWrite(BLUE, 1540);
}
void expressLED() {
  pwm.analogWrite(RED, MIN_RESOLUTION);
  pwm.analogWrite(GRN, 3840);
  pwm.analogWrite(BLUE, MAX_RESOLUTION);
}
void rapidLED() {
  pwm.analogWrite(RED, MIN_RESOLUTION);
  pwm.analogWrite(GRN, 2840);
  pwm.analogWrite(BLUE, MAX_RESOLUTION);
}

void setup() {
  delay(15); // wait around 15ms until power is stable during start-up

  // Initialize the LED matrix
  mtrx.begin();
  mtrx.setRotation(1);
  mtrx.setBright(1);
  mtrx.clear();
  run.setText("Welcome Aboard!");
  run.setSpeed(LED_SPD);      // pixel per second
  run.start();

  // Initialize LCD
  lcd.begin();
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL, 100000);

  lcd.setCursor(0,0);
  lcd.print("Press the button");
  lcd.setCursor(0,1);
  lcd.print("to get started!");
  
  // Key interrupts
  attachInterrupt(digitalPinToInterrupt(MON),mon_pressed,RISING);

  // LEDs
  pinMode(RED, OUTPUT);
  pinMode(GRN, OUTPUT);
  pinMode(BLUE, OUTPUT);

  initializeLed();
}

const char* buffer = "                      ";

const char* stations[] = { 
                        // Odakyu Main Line
                        "Shinjuku (OH-01)", "Minami-Shinjuku (OH-02)", "Sangubashi (OH-03)", "Yoyogi-Hachiman (OH-04)",
                        "Yoyogi-Uehara (OH-05)", "Higashi-Kitazawa (OH-06)", "Shimo-Kitazawa (OH-07)", "Setagaya-Daita (OH-08)", 
                        "Umegaoka (OH-09)", "Gotokuji (OH-10)", "Kyodo (OH-11)", "Chitose-Funabashi (OH-12)", 
                        "Soshigaya-Okura (OH-13)", "Seijogakuen-mae (OH-14)", "Kitami (OH-15)", "Komae (OH-16)", 
                        "Izumi-Tamagawa (OH-17)", "Noborito (OH-18)", "Mukogaoka-yuen (OH-19)", "Ikuta (OH-20)", 
                        "Yomiuri-Land-mae (OH-21)", "Yurigaoka (OH-22)", "Shin-Yurigaoka (OH-23)", "Kakio (OH-24)", 
                        "Tsurukawa (OH-25)", "Tamagawagakuen-mae (OH-26)", "Machida (OH-27)", "Sagami-Ono (OH-28)", 
                        "Odakyu-Sagamihara (OH-29)", "Sobudai-mae (OH-30)", "Zama (OH-31)", "Ebina (OH-32)", 
                        "Atsugi (OH-33)", "Hon-Atsugi (OH-34)", "Aiko-Ishida (OH-35)", "Isehara (OH-36)", 
                        "Tsurumaki-Onsen (OH-37)", "Tokaidaigaku-mae (OH-38)", "Hadano (OH-39)", "Shibusawa (OH-40)", 
                        "Shin-Matsuda (OH-41)", "Kaisei (OH-42)", "Kayama (OH-43)", "Tomizu (OH-44)", 
                        "Hotaruda (OH-45)", "Ashigara (OH-46)", "Odawara (OH-47)",

                        // Tama Line (From Shin-Yurigaoka OH-23) [idx: 47-53]
                        "Satsukidai (OT-01)", "Kurihira (OT-02)", "Kurokawa (OT-03)", "Haruhino (OT-04)",
                        "Odakyu-Nagayama (OT-05)", "Odakyu-Tama-Center (OT-06)", "Karakida (OT-07)",
                        
                        // Enoshima Line (From Sagami-Ono OH-28) [idx: 54-69]
                        "Higashi-Rinkan (OE-01)", "Chuo-Rinkan (OE-02)", "Minami-Rinkan (OE-03)", "Tsuruma (OE-04)",
                        "Yamato (OE-05)", "Sakuragaoka (OE-06)", "Koza-Shibuya (OE-07)", "Chogo (OE-08)",
                        "Shonandai (OE-09)", "Mutsuai-Nichidaimae (OE-10)", "Zengyo (OE-11)", "Fujisawa-Hommachi (OE-12)",
                        "Fujisawa (OE-13)", "Hon-Kugenuma (OE-14)", "Kugenuma-Kaigan (OE-15)", "Katase-Enoshima (OE-16)",
                        };

// From Shinjuku (OH-01) to Hon-Atsugi (OH-34)
const int localStation1[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33};
const int localStation1Length = sizeof(localStation1) / sizeof(localStation1[0]);

// From Shin-Yurigaoka (OH-23) to Karakida (OT-07)
const int localStation2[] = {47, 48, 49, 50, 51, 52, 53};
const int localStation2Length = sizeof(localStation2) / sizeof(localStation2[0]);

// From Sagami-Ono (OH-28) to Katase-Enoshima (OE-16)
const int localStation3[] = {54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69};
const int localStation3Length = sizeof(localStation3) / sizeof(localStation3[0]);

// From [EXP] Machida (OH-27) to Shin-Matsuda (OH-41) and [LOC] to Odawara (OH-47)
const int localExpStation1[] = {27, 31, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46};
const int localExpStation1Length = sizeof(localExpStation1) / sizeof(localExpStation1[0]);

// From Shinjuku (OH-01) to Shin-Yurigaoka (OH-23) and *through service* to Karakida (OT-07)
const int expressStation1[] = {4, 6, 10, 13, 17, 18, 22, 47, 48, 49, 50, 51, 52, 53};
const int expressStation1Length = sizeof(expressStation1) / sizeof(expressStation1[0]);

// From Machida (OH-27) to Shinjuku (OH-01) (UPPER BOUND)
const int expressStation2[] = {22, 18, 17, 13, 10, 6, 4, 0};
const int expressStation2Length = sizeof(expressStation2) / sizeof(expressStation2[0]);

// From Shinjuku (OH-01) to Odawara (OH-47)
const int rapidexStation1[] = {4, 6, 17, 22, 26, 27, 31, 33, 34, 35, 36, 37, 38, 39, 40, 41, 46};
const int rapidexStation1Length = sizeof(rapidexStation1) / sizeof(rapidexStation1[0]);

// From Shinjuku (OH-01) to Sagami-Ono (OH-28) and *through service* to Fujisawa (OE-13)
const int rapidexStation2[] = {4, 6, 17, 22, 26, 27, 55, 58, 62, 66}; 
const int rapidexStation2Length = sizeof(rapidexStation2) / sizeof(rapidexStation2[0]);

void startTrainSequence(const int* route, int routeLength, String introMessage) {
  localExpBlueActive = false;
  
  trainSequenceActive = true;
  activeStationRoute = route;
  activeStationRouteLength = routeLength;
  stationIndex = 0;
  trainMessageTimer = millis();
  run.setText(introMessage);
  run.start();
}

void startLocal1TrainSequence() {
  localLED();
  startTrainSequence(localStation1, localStation1Length, String("The local train bound for Hon-Atsugi." + String(buffer)));
}

void startLocal2TrainSequence() {
  localLED();
  startTrainSequence(localStation2, localStation2Length, String("The local train bound for Karakida." + String(buffer)));
}

void startLocal3TrainSequence() {
  localLED();
  startTrainSequence(localStation3, localStation3Length, String("The local train bound for Katase-Enoshima." + String(buffer)));
}

void startLocalExp1TrainSequence() {
  expressLED();
  startTrainSequence(localExpStation1, localExpStation1Length, String("The express bound for Odawara." + String(buffer)));
}

void startExpress1TrainSequence() {
  expressLED();
  startTrainSequence(expressStation1, expressStation1Length, String("The express bound for Karakida." + String(buffer)));
}

void startExpress2TrainSequence() {
  expressLED();
  startTrainSequence(expressStation2, expressStation2Length, String("The express bound for Shinjuku." + String(buffer)));
}

void startRapid1TrainSequence() {
  rapidLED();
  startTrainSequence(rapidexStation1, rapidexStation1Length, String("The rapid-express bound for Odawara." + String(buffer)));
}

void startRapid2TrainSequence() {
  rapidLED();
  startTrainSequence(rapidexStation2, rapidexStation2Length, String("The rapid-express bound for Fujisawa." + String(buffer)));
}

void restartActiveTrainSequence() {
  if (activeStationRoute == localStation1) {
    startLocal1TrainSequence();
  } 
  else if (activeStationRoute == localStation2) {
    startLocal2TrainSequence();
  } 
  else if (activeStationRoute == localStation3) {
    startLocal3TrainSequence();
  } 
  else if (activeStationRoute == localExpStation1) {
    startLocalExp1TrainSequence();
  } 
  else if (activeStationRoute == expressStation1) {
    startExpress1TrainSequence();
  } 
  else if (activeStationRoute == expressStation2) {
    startExpress2TrainSequence();
  } 
  else if (activeStationRoute == rapidexStation1) {
    startRapid1TrainSequence();
  } 
  else if (activeStationRoute == rapidexStation2) {
    startRapid2TrainSequence();
  }
}

void updateTrainSequence() {
  if (!trainSequenceActive || activeStationRoute == nullptr || activeStationRouteLength <= 0) {
    return;
  }

  if (millis() - trainMessageTimer < TRAIN_MESSAGE_INTERVAL) {
    return;
  }

  if (stationIndex >= activeStationRouteLength) {
    initializeLed();
    delay(300);
    restartActiveTrainSequence();
    return;
  }

  // Specifically for the Local Express route
  if (activeStationRoute == localExpStation1 && !localExpBlueActive && activeStationRoute[stationIndex] == 39) {
    localLED();
    localExpBlueActive = true;
  }

  run.setText(String("The next stop is ") + stations[activeStationRoute[stationIndex]] + "." + String(buffer));
  run.start();
  stationIndex++;
  trainMessageTimer = millis();
}

void updateRouteLcd() {
  if (lcdRouteTitle.length() == 0) {
    return;
  }

  if (millis() - lcdMessageTimer < LCD_MESSAGE_INTERVAL) {
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(lcdRouteTitle);
  lcd.setCursor(0, 1);
  if (lcdShowingDestination) {
    lcd.print(lcdRouteDestination);
  } else {
    lcd.print(lcdRouteOrigin);
  }

  lcdShowingDestination = !lcdShowingDestination;
  lcdMessageTimer = millis();
}

void setRouteLcd(const String& title, const String& origin, const String& destination) {
  lcdRouteTitle = title;
  lcdRouteOrigin = origin;
  lcdRouteDestination = destination;
  lcdShowingDestination = false;
  lcdMessageTimer = millis() - LCD_MESSAGE_INTERVAL;
}

void loop() {
  mtrx.clear();
  run.tick();

  updateTrainSequence();
  updateRouteLcd();

  if (keypressed == true) {
    Serial.println("Key pressed!");
    
    switch (display) {
      case 0:
        detachInterrupt(digitalPinToInterrupt(MON));

        setRouteLcd("Odakyu [LOCAL]", "FROM: Shinjuku", "TO: Hon-Atsugi");

        attachInterrupt(digitalPinToInterrupt(MON),mon_pressed,RISING);
        startLocal1TrainSequence();
        break;
      case 1:
        detachInterrupt(digitalPinToInterrupt(MON));

        setRouteLcd("Odakyu Tama", "FROM: Shin-Yuri.", "TO: Karakida");

        attachInterrupt(digitalPinToInterrupt(MON),mon_pressed,RISING);
        startLocal2TrainSequence();
        break;
      case 2:
        detachInterrupt(digitalPinToInterrupt(MON));

        setRouteLcd("Odakyu Enoshima", "FROM: Sagami-Ono", "TO: Katase-Eno.");

        attachInterrupt(digitalPinToInterrupt(MON),mon_pressed,RISING);
        startLocal3TrainSequence();
        break;
      case 3:
        detachInterrupt(digitalPinToInterrupt(MON));

        setRouteLcd("Odakyu [EXP>LOC]", "FROM: Machida", "TO: Odawara");

        attachInterrupt(digitalPinToInterrupt(MON),mon_pressed,RISING);
        startLocalExp1TrainSequence();
        break;
      case 4:
        detachInterrupt(digitalPinToInterrupt(MON));

        setRouteLcd("Odakyu [EXPRESS]", "FROM: Shinjuku", "TO: Karakida");

        attachInterrupt(digitalPinToInterrupt(MON),mon_pressed,RISING);
        startExpress1TrainSequence();
        break;
      case 5:
        detachInterrupt(digitalPinToInterrupt(MON));

        setRouteLcd("Odakyu [EXPRESS]", "FROM: Machida", "TO: Shinjuku");

        attachInterrupt(digitalPinToInterrupt(MON),mon_pressed,RISING);
        startExpress2TrainSequence();
        break;
      case 6:
        detachInterrupt(digitalPinToInterrupt(MON));

        setRouteLcd("Odakyu [RAPID]", "FROM: Shinjuku", "TO: Odawara");

        attachInterrupt(digitalPinToInterrupt(MON),mon_pressed,RISING);
        startRapid1TrainSequence();
        break;
      case 7:
        detachInterrupt(digitalPinToInterrupt(MON));

        setRouteLcd("Odakyu [RAPID]", "FROM: Shinjuku", "TO: Fujisawa");

        attachInterrupt(digitalPinToInterrupt(MON),mon_pressed,RISING);
        startRapid2TrainSequence();
        break;
    }
    keypressed = false;
  }
}