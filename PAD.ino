/*
Autor: Adrian Nowogrodzki
Data: 11.11.2023
Temat: Zdalnie sterowana platforma gąsienicowa strzelająca do zadanego celu
Wersja środowiska: Arduino IDE 2.1.1
*/
#include <SPI.h>
#include <RF24.h>
#include <RF24_config.h>
#include <nRF24L01.h>
#include <printf.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>


// DEFINICJE
#define pPotX A0    // pin potentiometr X
#define pPotY A1    // pin potencjometr Y
#define pJoyX A2    // pin joystick X
#define pJoyY A3    // pin joystick Y
#define pButt0 3
#define pButt1 4
#define pButt2 5
#define pButt3 6


// OBIEKTY
LiquidCrystal_I2C lcd(0x27, 16, 2);
RF24 radio(7, 8);  // CE, CSN


// STRUKTURY
struct Potentiometer {
  int X;
  int Y;
};
struct Joystick {
  int X;
  int Y;
};
struct Pad {
  Potentiometer pot;  // potencjometr
  Joystick joy;       // joystick
  byte button[4];     // przyciski
};
struct DataToSend{    // pakiet wysyłanych danych
  byte manual_auto;
  int xJoy_none;
  int yJoy_none;
  int fi_xTarget;
  int ro_yTarget;
  byte strike_start;
  byte load_none;
  byte none_giveFedbackPositon;
};
struct CarCoords{
  double X;
  double Y;
};

// ZMIENNE
Pad pad;
DataToSend payload;
CarCoords carCoords;

const byte addresses[][6] = { "00001", "00002" };
const long minJoy = 0;
const long maxJoy = 100;
const long minPot = 0;
const long maxPot = 100;
unsigned long now = 0;
unsigned long delayButt0 = 0;
unsigned long delayButt1 = 0;
unsigned long delayButt2 = 0;
unsigned long delayButt3 = 0;
unsigned long timeForDownload = 0;
unsigned long timeForUpload = 0;
unsigned long gapInDownload = 0;
unsigned long gapInFeedback = 0;
byte lastButt;
int tempW = 1;
int tempR = 0;


// DEFINICJE FUNKCJI
void ifPress(int pButt, unsigned long & delayButt, byte num, byte cCnum);
void ifShift();
void giveMeCarPosition();
void print(int x);
byte changeCondition(byte x, int num);
void prepareData();
void sendData();
void receiveData();


// PROGRAM
void setup() {
  pinMode(pButt0, INPUT_PULLUP);    // podciągania pinów do +5V
  pinMode(pButt1, INPUT_PULLUP);
  pinMode(pButt2, INPUT_PULLUP);
  pinMode(pButt3, INPUT_PULLUP);

  Serial.begin(9600);               // rozpoczęcie komunikacji z komputerem

  lcd.init();                       // ustawienia początkowe wyświetlacza
  lcd.backlight();

  radio.begin();                            // ustawienia transmisji bezprzewodowej
  radio.openWritingPipe(addresses[1]);      // 00002
  radio.openReadingPipe(1, addresses[0]);   // 00001
  radio.setPALevel(RF24_PA_MIN);
}

void loop() {
  now = millis();

  ifPress(pButt0, delayButt0, 0, 2);    // gdy wciśnięto jakiś przycisk
  ifPress(pButt1, delayButt1, 1, 2);
  ifPress(pButt2, delayButt2, 2, 1);
  ifPress(pButt3, delayButt3, 3, 1);
  ifShift();                            // gdy ruszono potencjometrem lub joystickiem
  giveMeCarPosition();                  // co 0.5 sekundy otrzymuje pozycję pojazdu       |jeśli się będzie bugowało to dać 1 sek|
}


// FUNKCJE
void ifPress(int pButt, unsigned long & delayButt, byte num, byte cCnum){ // reaguje na wciśnięcia przycisków
  if ((digitalRead(pButt) == 0) && (now - delayButt >= 500)) {            // wciśnięcie działa co 0.5 sek
    delayButt = now;
    pad.button[num] = changeCondition(pad.button[num], cCnum);  // pad.button[num] = stan (manualny/automat | M-pulpit1/pulpit2 | A-pulpit1/pulpit2)
    
    if (pButt == pButt3 && pad.button[0] == 0 && lastButt == 1 && pad.button[1] == 1){  // gdy włączono M-ładowanie 
      payload.strike_start = 0;
      payload.load_none = 1;
      sendData();               // wysyła '1' jako load_none
    }
    else if (pButt == pButt2){  // gdy włączono M-strzał/A-start
      payload.load_none = 0;
      payload.strike_start = 1;
      sendData();               // wysyła '1' jako strike_start
    }
    else{                       // jeśli nie wybrano load_none lub strike_start to po prostu wyślij
      prepareData();
      sendData();
    }
    print(num);
  }
}
void ifShift(){ // odczytuje ruch joysticka i potencjometrów
  if ((abs(pad.joy.X - analogRead(pJoyX)) > 10) || (abs(pad.joy.Y - analogRead(pJoyY)) > 10)) {  // obsługa joystick'a
    pad.joy.X = analogRead(pJoyX);  // przechowuje obency stan potencjometrów
    pad.joy.Y = analogRead(pJoyY);
    print(lastButt);
    prepareData();
    sendData();
  }
  if ((abs(pad.pot.X - analogRead(pPotX)) > 10) || (abs(pad.pot.Y - analogRead(pPotY)) > 10)) {  // obsługa potencjometrów
    pad.pot.X = analogRead(pPotX);  // przechowuje obency stan potencjometrów
    pad.pot.Y = analogRead(pPotY);
    print(lastButt);
    prepareData();
    sendData();
  }
}
void prepareData(){ // tworzy paczkę danych do wysłania
  payload.manual_auto = pad.button[0];
  payload.xJoy_none = map(pad.joy.X, 0, 1023, -100, 101); // prędkości pojazdu x,y
  payload.yJoy_none = map(pad.joy.Y, 0, 1023, 100, -101);
  if(payload.xJoy_none == 101){ payload.xJoy_none = 100;}
  if(payload.yJoy_none == -101){ payload.yJoy_none = -100;}
 /*
  Serial.print("joy x: ");
  Serial.print(payload.xJoy_none);
  Serial.print("\t");
  Serial.print("joy y: ");
  Serial.print(payload.yJoy_none);
  Serial.println();*/

  if (payload.manual_auto == 0){
    payload.fi_xTarget = map(pad.pot.X, 0, 1023, 0, 180); // kąty działka ro i fi 
    payload.ro_yTarget = map(pad.pot.Y, 0, 1023, 70, 110);
  }
  else{
    payload.fi_xTarget = 10 * map(pad.pot.X, 0, 1023, -30, 30);  // położenie celu w [dm]
    payload.ro_yTarget = 10 * map(pad.pot.Y, 0, 1023, -30, 30);
  }
  payload.strike_start = 0;             // w ifPress() może przyjąć '1'
  payload.load_none = 0;                // w ifPress() może przyjąć '1'
  payload.none_giveFedbackPositon = 0;  // w giveMeCarPosition() może przyjąć '1'
 /*
  Serial.print("fi: ");
  Serial.print(payload.fi_xTarget);
  Serial.print("\t");
  Serial.print("ro: ");
  Serial.print(payload.ro_yTarget);
  Serial.println();   
  Serial.println();*/  
}
void print(int x) { // wyświetlanie na LCD
  switch (x) {
    case 0:  // przycisk 0 -> wybór trybu manualny/autonomiczny
      lcd.clear();
      lcd.setCursor(0, 0);
      if (pad.button[0] == 0)
        lcd.print("Manulany");
      else
        lcd.print("Automatyczny");
      break;
    case 1:  // przycisk 1 -> menu 
      if (pad.button[0] == 0) {  // dla trybu manulanego
        lcd.clear();
        if (pad.button[1] == 0) {
          lcd.setCursor(0, 0);
          lcd.print("Katy dziala[deg]");
          lcd.setCursor(0, 1);
          lcd.print("yaw:");
          lcd.setCursor(4, 1);
          lcd.print(map(pad.pot.X, 0, 1023, -90, 90));
          lcd.setCursor(7, 1);
          lcd.print("pitch:");
          lcd.setCursor(13, 1);
          lcd.print(map(pad.pot.Y, 0, 1023, -20, 20));
        } else {
          lcd.setCursor(0, 0);
          lcd.print("Ladowanie kuli");
          lcd.setCursor(0, 1);
          lcd.print("(potwierdz)");
        }
      } else {                  // dla trybu autonomicznego
        if (pad.button[1] == 0) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Polozenie celu");
          lcd.setCursor(0, 1);
          lcd.print("x:");
          lcd.setCursor(2, 1);
          lcd.print((map(pad.pot.X, 0, 1023, -30, 30)));
          lcd.setCursor(5, 1);
          lcd.print("y:");
          lcd.setCursor(7, 1);
          lcd.print((map(pad.pot.Y, 0, 1023, -30, 30)));
          lcd.setCursor(11, 1);
          lcd.print("[dm]");
        } else {
          if (lastButt == 1){lcd.setCursor(0, 1); lcd.print("                ");} else{ lcd.clear();}
          lcd.setCursor(0, 0);
          lcd.print("Polozenie czolgu");
          lcd.setCursor(0, 1);
          lcd.print("x:");
          lcd.setCursor(2, 1);
          lcd.print((int)carCoords.X);
          lcd.setCursor(5, 1);
          lcd.print("y:");
          lcd.setCursor(7, 1);
          lcd.print((int)carCoords.Y);
          lcd.setCursor(11, 1);
          lcd.print("[dm]");
        }
      }
      break;
    case 2:   // przycisk 2 -> strzał lub start
      lcd.clear();
      if (pad.button[0] == 0) {
        lcd.setCursor(0, 0);
        lcd.print("Strzal");
      } else {
        lcd.setCursor(0, 0);
        lcd.print("Start");
      }
      break;
    case 3:   // przycisk 3 -> potwierdzanie (jeśli wymagane) [wymagane w M-ładowanie]
      if (pad.button[0] == 0 && lastButt == 1) {
        if (pad.button[1] == 1) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Potwierdzono");
        }
      }
      break;
  }
  lastButt = x;
}
void giveMeCarPosition(){ // wysyła rządanie o chęci odbioru położenia pojazdu (tylko w automacie)
  if(payload.manual_auto == 1 && (now - gapInFeedback) >= 500){   // co pół sekundy pobiera info o położeniu pojazdu
    gapInFeedback = now;
    prepareData();
    payload.none_giveFedbackPositon = 1;  // ustawia na '1' bajt który pojazd interpretuje jako gotowość pada do odbioru dancyh 
    sendData();
    receiveData();
    payload.none_giveFedbackPositon = 0;
    if (lastButt == 1 && pad.button[1] == 1){ print(lastButt); }
  }
}
void sendData() { // wysyłanie paczki danych
    radio.stopListening();
    radio.write(&payload, sizeof(payload));
}
void receiveData() { //odbiór informacji o położeniu pojazdu 
    timeForDownload = millis();
    radio.startListening();
    while (!radio.available()) {  // czeka aż będzie jakaś wiadomość do odbioru 
      //if ((millis() - timeForDownload) >= 100) {  // ale nie czeka dłużej niż 100 ms
        break;
      //}
    }
    if (radio.available()) {  // jeśli jest co odebrać do odbierz |jeśli będzie zacinało to tutaj może warto dać while, ale to może opóźnić program|
      radio.read(&carCoords, sizeof(carCoords));
      /*Serial.print("carCoords(X,Y): ");
      Serial.print(carCoords.X);
      Serial.print("\t");
      Serial.print(carCoords.Y);
      Serial.println();*/
    }
}
byte changeCondition(byte x, int num) { // przełącza tryby np. manualny na auto
  x++;
  if (x == num) {  // num trybów
    x = 0;
  }
  return x;
}