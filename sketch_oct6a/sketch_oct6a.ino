// =================================================================
// חלק א': הגדרות חומרה, רגיסטרים, זיכרון RAM ו-ROM, ומערך העזרה
// =================================================================

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <EEPROM.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte ROWS = 4; 
const byte COLS = 4; 

char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'}, 
  {'4','5','6','B'}, 
  {'7','8','9','C'}, 
  {'*','0','#','D'}  
};

byte rowPins[ROWS] = {9, 8, 7, 6}; 
byte colPins[COLS] = {5, 4, 3, 2}; 

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS); 

// הגדרת פינים פיזיים לחומרה
const int BUZZER_PIN = 10;
const int LED_PIN_11 = 11;
const int LED_PIN_12 = 12;
const int LED_PIN_13 = 13;
const int BUTTON_PIN = 14; 
const int ANALOG_PIN = A1; 

// אוגרים ומשתני מעבד
long accumulator = 0;      
long regX = 0;             
long regY = 0;             
long currentInput = 0;     
int programCounter = 0;    

// התיקון: הגדרה נכונה של מערכי ה-RAM וה-ROM עם גודל מוגדר מראש
long ramAddresses[16] = {0}; 
const long romAddresses[16] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 255};

// ניהול שמירת תוכניות מרובות
int activeSlot = 0; 

// משתני מצב מערכת ועזרה
bool isAssemblyMode = false; 
bool asmExpectsCommand = false; 
bool isNokiaMode = false; 
int commandPage = 1; 
String lastAction = "READY";

// משתני ניהול הקלדת נוקיה (Multi-tap)
char lastNokiaKey = ' ';
int nokiaTapCount = 0;
unsigned long lastNokiaTapTime = 0;
const unsigned long NOKIA_TIMEOUT = 1000; 

// התיקון: הגדרת מערך מחרוזות מפורש בגודל 10 עבור אותיות נוקיה
String nokiaLetters[10] = {" ", ".,!?", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ"};

// משתני זמנים ללחיצות ארוכות
unsigned long startTime = 0;
bool isZeroHeld = false;
bool isHashHeld = false;

// הצהרות מראש על פונקציות (Prototypes) לקומפילציה תקינה
void updateDisplay();
void resetAll();
void handleStandardKeys(char key);
void handleNormalCalculator(char key);
void handleAssemblyMode(char key);
void executeAsmCommand(char commandKey);
void processNokiaKey(char key);
void checkNokiaTimeout();
void saveProgramToEEPROM(int slot);
void loadProgramFromEEPROM(int slot);
// =================================================================
// חלק ב': אתחול המערכת (SETUP) והלולאה הראשית ומדידת זמני לחיצה
// =================================================================

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN_11, OUTPUT);
  pinMode(LED_PIN_12, OUTPUT);
  pinMode(LED_PIN_13, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP); 
  
  lcd.init();
  lcd.backlight();
  resetAll();
}

void loop() {
  customKeypad.getKeys();
  
  if (isNokiaMode) {
    checkNokiaTimeout();
  }
  
  for (int i = 0; i < LIST_MAX; i++) {
    if (customKeypad.key[i].stateChanged) {
      char key = customKeypad.key[i].kchar;
      
      // ניהול לחיצה ארוכה/קצרה על מקש 0 במצב Assembly
      if (isAssemblyMode && !isNokiaMode && key == '0') {
        if (customKeypad.key[i].kstate == PRESSED) {
          startTime = millis();
          isZeroHeld = true;
        } 
        else if (customKeypad.key[i].kstate == RELEASED) {
          if (isZeroHeld) {
            unsigned long pressDuration = millis() - startTime;
            isZeroHeld = false;
            if (pressDuration >= 800) { 
              asmExpectsCommand = !asmExpectsCommand;
              tone(BUZZER_PIN, asmExpectsCommand ? 1200 : 800, 150);
              lastAction = asmExpectsCommand ? "P" + String(commandPage) + " CMD MODE" : "NUM MODE";
              updateDisplay();
            } else { 
              if (!asmExpectsCommand) {
                currentInput = (currentInput * 10) + 0;
                lastAction = "IN:" + String(currentInput);
                tone(BUZZER_PIN, 880, 40);
                updateDisplay();
              }
            }
          }
        }
      }
      // ניהול לחיצה ארוכה על מקש # להפעלת מקלדת נוקיה לטקסט
      else if (key == '#') {
        if (customKeypad.key[i].kstate == PRESSED) {
          startTime = millis();
          isHashHeld = true;
        } 
        else if (customKeypad.key[i].kstate == RELEASED) {
          if (isHashHeld) {
            unsigned long pressDuration = millis() - startTime;
            isHashHeld = false;
            if (pressDuration >= 800) {
              isNokiaMode = !isNokiaMode;
              tone(BUZZER_PIN, isNokiaMode ? 1400 : 600, 200);
              lastAction = isNokiaMode ? "NOKIA TEXT ON" : "NOKIA TEXT OFF";
              updateDisplay();
            } else {
              handleStandardKeys('#');
            }
          }
        }
      }
      else if (customKeypad.key[i].kstate == PRESSED) {
        handleStandardKeys(key);
      }
    }
  }
}
// =================================================================
// חלק ג': מנוע ניתוב המקשים, מקלדת נוקיה, 5 דפי הפקודות ועדכון מסך
// =================================================================

void handleStandardKeys(char key) {
  if (isNokiaMode) {
    if (key >= '0' && key <= '9') processNokiaKey(key);
    return;
  }

  if (key == '*') {
    isAssemblyMode = !isAssemblyMode;
    currentInput = 0; asmExpectsCommand = false; commandPage = 1;
    lastAction = isAssemblyMode ? "ASM MODE" : "NORM MODE";
    tone(BUZZER_PIN, 523, 150); updateDisplay();
    return;
  }

  if (isAssemblyMode) { handleAssemblyMode(key); } 
  else { handleNormalCalculator(key); }
}

void processNokiaKey(char key) {
  int keyNum = key - '0';
  unsigned long now = millis();
  tone(BUZZER_PIN, 900, 50);

  if (key == lastNokiaKey && (now - lastNokiaTapTime < NOKIA_TIMEOUT)) {
    nokiaTapCount++;
    if (nokiaTapCount >= nokiaLetters[keyNum].length()) nokiaTapCount = 0;
    lcd.setCursor(programCounter > 0 ? programCounter - 1 : 0, 0);
    lcd.write(nokiaLetters[keyNum][nokiaTapCount]);
  } else {
    if (lastNokiaKey != ' ') programCounter++; 
    nokiaTapCount = 0;
    lastNokiaKey = key;
    lcd.setCursor(programCounter, 0);
    lcd.write(nokiaLetters[keyNum][nokiaTapCount]);
  }
  lastNokiaTapTime = now;
}

void checkNokiaTimeout() {
  if (lastNokiaKey != ' ' && (millis() - lastNokiaTapTime > NOKIA_TIMEOUT)) {
    lastNokiaKey = ' ';
  }
}

void handleNormalCalculator(char key) {
  if (key >= '0' && key <= '9') {
    currentInput = (currentInput * 10) + (key - '0');
    lastAction = "IN:" + String(currentInput);
    tone(BUZZER_PIN, 880, 50); updateDisplay();
  } 
  else if (key == 'A') { accumulator += currentInput; currentInput = 0; lastAction = "OP: +"; tone(BUZZER_PIN, 659, 100); updateDisplay(); }
  else if (key == 'B') { accumulator -= currentInput; currentInput = 0; lastAction = "OP: -"; tone(BUZZER_PIN, 587, 100); updateDisplay(); }
  else if (key == '#') { currentInput = accumulator; lastAction = "RESULT"; tone(BUZZER_PIN, 784, 150); updateDisplay(); }
}

void handleAssemblyMode(char key) {
  if (asmExpectsCommand && key == '#') {
    commandPage++; if (commandPage > 5) commandPage = 1; 
    tone(BUZZER_PIN, 1000, 100); lastAction = "PAGE " + String(commandPage) + " ACTIVE";
    updateDisplay(); return;
  }

  if (asmExpectsCommand) {
    if (key >= '1' && key <= '9') { executeAsmCommand(key); return; }
  } 
  else {
    if (key >= '0' && key <= '9') {
      currentInput = (currentInput * 10) + (key - '0');
      lastAction = "IN:" + String(currentInput);
      tone(BUZZER_PIN, 880, 40); updateDisplay(); return;
    }
    if (key == '#') { currentInput = 0; lastAction = "IN CLEARED"; tone(BUZZER_PIN, 440, 100); updateDisplay(); return; }
  }

  if (key == 'A') { accumulator = currentInput; currentInput = 0; lastAction = "LDA DONE"; tone(BUZZER_PIN, 698, 80); updateDisplay(); }
  else if (key == 'B') { accumulator += currentInput; currentInput = 0; lastAction = "ADD DONE"; tone(BUZZER_PIN, 784, 80); updateDisplay(); }
  else if (key == 'C') { accumulator -= currentInput; currentInput = 0; lastAction = "SUB DONE"; tone(BUZZER_PIN, 880, 80); updateDisplay(); }
}

void executeAsmCommand(char commandKey) {
  tone(BUZZER_PIN, 987, 80);
  
  if (commandPage == 1) {
    switch (commandKey) {
      case '1': 
        if (currentInput >= 0 && currentInput <= 15) { accumulator = ramAddresses[currentInput]; lastAction = "LDR R[" + String(currentInput) + "]"; } 
        currentInput = 0; programCounter++; break;
      case '2': 
        if (currentInput >= 0 && currentInput <= 15) { ramAddresses[currentInput] = accumulator; lastAction = "STR R[" + String(currentInput) + "]"; } 
        else if (currentInput == 20) { digitalWrite(LED_PIN_11, accumulator > 0 ? HIGH : LOW); lastAction = "IO: PIN 11=" + String(accumulator > 0 ? 1 : 0); }
        else if (currentInput == 21) { digitalWrite(LED_PIN_12, accumulator > 0 ? HIGH : LOW); lastAction = "IO: PIN 12=" + String(accumulator > 0 ? 1 : 0); }
        else if (currentInput == 22) { digitalWrite(LED_PIN_13, accumulator > 0 ? HIGH : LOW); lastAction = "IO: PIN 13=" + String(accumulator > 0 ? 1 : 0); }
        currentInput = 0; programCounter++; break;
      case '3': regX = accumulator; lastAction = "TAX: A->X"; programCounter++; break;
      case '4': regY = accumulator; lastAction = "TAY: A->Y"; programCounter++; break;
      case '5': accumulator = regX; lastAction = "TXA: X->A"; programCounter++; break;
      case '6': accumulator = regY; lastAction = "TYA: Y->A"; programCounter++; break;
      case '7': regX++; lastAction = "INX: X++"; programCounter++; break;
      case '8': regY--; lastAction = "DEY: Y--"; programCounter++; break;
      case '9': if (currentInput > 0) programCounter = currentInput; else programCounter++; currentInput = 0; lastAction = "PC STEP"; break;
    }
    updateDisplay();
  } 
  else if (commandPage == 2) {
    switch (commandKey) {
      case '1': accumulator *= currentInput; lastAction = "MUL DONE"; currentInput = 0; programCounter++; break;
      case '2': if (currentInput != 0) accumulator /= currentInput; currentInput = 0; programCounter++; break;
      case '3': if (currentInput != 0) accumulator %= currentInput; currentInput = 0; programCounter++; break;
      case '4': accumulator = accumulator * accumulator; lastAction = "SQR ACC"; programCounter++; break;
      case '5': accumulator = regX + regY; lastAction = "A = X + Y"; programCounter++; break;
      case '6': accumulator = regX - regY; lastAction = "A = X - Y"; programCounter++; break;
      case '7': accumulator &= currentInput; lastAction = "AND DONE"; currentInput = 0; programCounter++; break;
      case '8': accumulator |= currentInput; lastAction = "OR DONE"; currentInput = 0; programCounter++; break;
      case '9': accumulator ^= currentInput; lastAction = "XOR DONE"; currentInput = 0; programCounter++; break;
    }
    updateDisplay();
  }
  else if (commandPage == 3) {
    switch (commandKey) {
      case '1': lcd.clear(); lastAction = "LCD CLEAR"; programCounter++; break;
      case '2': lcd.home(); lastAction = "LCD HOME"; programCounter++; break;
      case '3': lcd.backlight(); lastAction = "LCD B_LIGHT ON"; programCounter++; break;
      case '4': lcd.noBacklight(); lastAction = "LCD B_LIGHT OFF"; programCounter++; break;
      case '5': lcd.scrollDisplayLeft(); lastAction = "LCD SCROLL L"; programCounter++; break;
      case '6': lcd.scrollDisplayRight(); lastAction = "LCD SCROLL R"; programCounter++; break;
      case '7': lcd.write((char)accumulator); lastAction = "PRINT CHAR: " + String((char)accumulator); programCounter++; break;
      case '8': lcd.blink(); lastAction = "CURSOR BLINK"; programCounter++; break;
      case '9': lcd.noBlink(); lastAction = "CURSOR OFF"; programCounter++; break;
    }
    lcd.setCursor(0, 1); lcd.print("A:"); lcd.print(accumulator); lcd.print(" P:"); lcd.print(programCounter);
  }
  else if (commandPage == 4) {
    switch (commandKey) {
      case '1': accumulator = accumulator << 1; lastAction = "SHL ACC"; programCounter++; break;
      case '2': accumulator = accumulator >> 1; lastAction = "SHR ACC"; programCounter++; break;
      case '3': accumulator = (accumulator << 1) | (accumulator >> 31); lastAction = "ROL ACC"; programCounter++; break; 
      case '4': accumulator = ~accumulator; lastAction = "NOT ACC"; programCounter++; break;
      case '5': accumulator = -accumulator; lastAction = "NEG ACC"; programCounter++; break;
      case '6': accumulator = abs(accumulator); lastAction = "ABS ACC"; programCounter++; break;
      case '7': { long temp = regX; regX = regY; regY = temp; lastAction = "SWAP X<->Y"; programCounter++; break; }
      case '8': regX = 0; regY = 0; lastAction = "CLR X & Y"; programCounter++; break;
      case '9': accumulator = (regX == regY) ? 0 : ((regX > regY) ? 1 : -1); lastAction = "CMP X,Y"; programCounter++; break;
    }
    updateDisplay();
  }
  else if (commandPage == 5) {
    switch (commandKey) {
      case '1': saveProgramToEEPROM(activeSlot); break;
      case '2': loadProgramFromEEPROM(activeSlot); break;
      case '3': accumulator = min(accumulator, regX); lastAction = "MIN(A,X)"; programCounter++; break;
      case '4': accumulator = max(accumulator, regX); lastAction = "MAX(A,X)"; programCounter++; break;
      case '5': { long sum = 0; for(int i=0; i<16; i++) sum += ramAddresses[i]; accumulator = sum; lastAction = "SUM 16 RAM"; programCounter++; break; }
      case '6': if (currentInput >= 0 && currentInput <= 15) accumulator = romAddresses[currentInput]; currentInput = 0; programCounter++; break;
      
      case '7': 
        if (currentInput == 30) { accumulator = digitalRead(BUTTON_PIN); lastAction = "INP: BUTTON=" + String(accumulator); }
        else if (currentInput == 31) { accumulator = analogRead(ANALOG_PIN); lastAction = "INP: ANLG=" + String(accumulator); }
        currentInput = 0; programCounter++; break;
        
      case '8': { long fact = 1; for (int i = 1; i <= accumulator && i <= 12; i++) fact *= i; accumulator = fact; lastAction = "FACT DONE"; programCounter++; break; }
      case '9': { lcd.clear(); lcd.setCursor(0, 0); lcd.print("Active Prog Slot:"); lcd.setCursor(0, 1); lcd.print("Slot #"); lcd.print(activeSlot); delay(2000); lastAction = "PROG CHECK"; break; }
    }
    updateDisplay();
  }
}

void saveProgramToEEPROM(int slot) {
  int startAddress = slot * 16 * sizeof(long); int addr = startAddress;
  for (int i = 0; i < 16; i++) { EEPROM.put(addr, ramAddresses[i]); addr += sizeof(long); }
  lastAction = "SAVED TO SLOT " + String(slot); programCounter++;
}

void loadProgramFromEEPROM(int slot) {
  int startAddress = slot * 16 * sizeof(long); int addr = startAddress;
  for (int i = 0; i < 16; i++) { EEPROM.get(addr, ramAddresses[i]); addr += sizeof(long); }
lastAction = "LOADED SLOT " + String(slot); programCounter++;
}
void updateDisplay() {
lcd.clear(); lcd.setCursor(0, 0); lcd.print(lastAction);
lcd.setCursor(12, 0);
if (isAssemblyMode) { lcd.print(asmExpectsCommand ? "[C" + String(commandPage) + "]" : "[N]"); }
else { lcd.print("[M]"); }
lcd.setCursor(0, 1);
if (isAssemblyMode) {
lcd.print("A:"); lcd.print(accumulator);
lcd.print(" X:"); lcd.print(regX);
lcd.print(" Y:"); lcd.print(regY);
lcd.print(" S:"); lcd.print(activeSlot);
} else {
lcd.print("ACC:"); lcd.print(accumulator);
lcd.print("  IN:"); lcd.print(currentInput);
}
}
void resetAll() {
accumulator = 0; regX = 0; regY = 0; currentInput = 0; programCounter = 0; activeSlot = 0;
for(int i = 0; i < 16; i++) ramAddresses[i] = 0;
digitalWrite(LED_PIN_11, LOW); digitalWrite(LED_PIN_12, LOW); digitalWrite(LED_PIN_13, LOW);
isAssemblyMode = false; asmExpectsCommand = false; isNokiaMode = false; commandPage = 1;
randomSeed(analogRead(0)); lastAction = "SYS INIT"; updateDisplay();
}