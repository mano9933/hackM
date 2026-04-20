#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include <MFRC522.h>

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= SD ==================
#define CS_PIN 5

// ================= RFID ==================
#define RST_PIN 9
#define SS_PIN 10   // RFID CS
MFRC522 mfrc522(SS_PIN, RST_PIN);
byte storedUID[4];

// ================= ENCODER =============
#define CLK 6
#define DT  7
#define SW  8

int lastCLK;
int menuIndex = 0;

// ================= STATES ==============
enum State {
  MAIN_MENU,
  IR_MENU,
  RFID_MENU,
  RFID_SCAN_MENU,
  SHOW_UID,
  UID_MENU,
  KEYBOARD_MENU,
  FILE_BROWSER
};

State currentState = MAIN_MENU;
State previousState = MAIN_MENU;

// ================= MENUS ===============
const char *mainMenu[] = {"IR", "RFID", "USB", "Battery"};
const char *irMenu[]   = {"Clone", "Saved"};
const char *rfidMenu[] = {"Scan", "Saved"};
const char *uidMenu[] = {"Save", "Clone"};

// ================= FILE SYSTEM =========
char currentPath[40] = "/";
File currentDir;
bool inIR = false;   // to allow folder navigation

// ================= BUTTON ==============
unsigned long pressStart = 0;
bool buttonHeld = false;

// ================= Keyboard Variables ==============
char fileName[17];
char text[100];
const char chars[] = " 0123456789abcdefghijklmnopqrstuvwxyz_";
const int charCount = sizeof(chars) - 1;

// =====================================================
// 🔹 GET MENU SIZE
// =====================================================
int getMenuSize() {
  switch (currentState) {
    case MAIN_MENU: return 4;
    case IR_MENU: return 2;
    case RFID_MENU: return 2;
    case UID_MENU: return 2;
    case KEYBOARD_MENU: return 38;
    case FILE_BROWSER: return getFileCount();
    default: return 1;
  }
}

// =====================================================
// 🔹 COUNT FILES
// =====================================================
int getFileCount() {
  File dir = SD.open(currentPath);
  int count = 0;

  File file = dir.openNextFile();
  while (file) {
    count++;
    file = dir.openNextFile();
  }

  dir.close();
  return count;
}

// =====================================================
// 🔹 DISPLAY MENU
// =====================================================
void displayMenu(const char **menu, int size) {
  lcd.clear();

  int start = menuIndex;
  if (start > size - 2) start = size - 2;
  if (start < 0) start = 0;

  for (int i = 0; i < 2; i++) {
    int idx = start + i;
    if (idx >= size) break;

    lcd.setCursor(0, i);
    if (idx == menuIndex) lcd.print("> ");
    else lcd.print("  ");

    lcd.print(menu[idx]);
  }
}

// =====================================================
// 🔹 DISPLAY FILES
// =====================================================
void displayFiles() {
  lcd.clear();

  File dir = SD.open(currentPath);
  int index = 0;
  int shown = 0;

  File file = dir.openNextFile();

  while (file) {

    if (index >= menuIndex && shown < 2) {
      lcd.setCursor(0, shown);

      if (index == menuIndex) lcd.print("> ");
      else lcd.print("  ");

      char name[20];
      strcpy(name, file.name());

      // remove extension
      for (int i = 0; i < strlen(name); i++) {
        if (name[i] == '.') {
          name[i] = '\0';
          break;
        }
      }

      lcd.print(name);
      shown++;
    }

    index++;
    file = dir.openNextFile();
  }

  dir.close();
}

// =====================================================
// 🔹 WRITE FILES
// =====================================================
void writeFile(const char *path, const char *data) {
  File file = SD.open(path, FILE_WRITE);
  if (file) {
    file.println(data);
    file.close();
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Saved");
  } else {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Error");
  }
}

// =====================================================
// 🔹 UPDATE DISPLAY
// =====================================================
void updateMenu() {
  switch (currentState) {
    case MAIN_MENU: displayMenu(mainMenu, 4); break;
    case IR_MENU: displayMenu(irMenu, 2); break;
    case RFID_MENU: displayMenu(rfidMenu, 2); break;
    case FILE_BROWSER: displayFiles(); break;
    case RFID_SCAN_MENU: readUID(); break;
    case UID_MENU: displayMenu(uidMenu, 2); break;
  }
}

// =====================================================
// 🔹 ENCODER (SMOOTH + CYCLIC)
// =====================================================
void handleEncoder() {
  int clkState = digitalRead(CLK);

  if (clkState != lastCLK && clkState == LOW) {

    if (digitalRead(DT) != clkState) menuIndex++;
    else menuIndex--;

    int maxItems = getMenuSize();

    // 🔁 CYCLIC
    if (menuIndex >= maxItems) menuIndex = 0;
    if (menuIndex < 0) menuIndex = maxItems - 1;

    updateMenu();
  }

  lastCLK = clkState;
}

// =====================================================
// 🔹 BUTTON HANDLING
// =====================================================
void handleButton() {

  if (digitalRead(SW) == LOW && !buttonHeld) {
    buttonHeld = true;
    pressStart = millis();
  }

  if (digitalRead(SW) == HIGH && buttonHeld) {
    buttonHeld = false;

    unsigned long duration = millis() - pressStart;

    if (duration > 800) {
      goBack();
    } else {
      selectItem();
    }

    updateMenu();
  }
}

// =====================================================
// 🔹 SELECT ITEM
// =====================================================
void selectItem() {

  if (currentState == MAIN_MENU) {

    if (menuIndex == 0) {
      currentState = IR_MENU;
    }
    else if (menuIndex == 1) {
      currentState = RFID_MENU;
    }
    else if (menuIndex == 2) {
      strcpy(currentPath, "/usb");
      currentState = FILE_BROWSER;
      inIR = false;
    }

    menuIndex = 0;
  }

  else if (currentState == IR_MENU) {

    if (menuIndex == 1) {
      strcpy(currentPath, "/ir");
      currentState = FILE_BROWSER;
      inIR = true;
      menuIndex = 0;
    }
  }

  else if (currentState == RFID_MENU) {

    if (menuIndex == 1) {
      strcpy(currentPath, "/rfid");
      currentState = FILE_BROWSER;
      inIR = false;
      menuIndex = 0;
    }
    else if (menuIndex == 0) {
      currentState = RFID_SCAN_MENU;
      inIR = false;
      menuIndex = 0;
    }
  }

  else if(currentState == SHOW_UID)
  {
    currentState = UID_MENU;
  }

  else if(currentState == UID_MENU)
  {
    if(menuIndex == 0)
    {
      currentState = KEYBOARD_MENU;
      showKeyboard(); 
      lcd.clear();
      lcd.noBlink();
      lcd.noCursor();
      char newFileName[32];
      snprintf(newFileName, sizeof(newFileName), "/rfid/%s.txt", fileName);
      writeFile(newFileName,text);
      delay(1500);
      currentState = RFID_MENU;
    }
    else if (menuIndex == 1) {
      //call clone function
    }
  }

  else if (currentState == FILE_BROWSER) {

    File dir = SD.open(currentPath);
    File file = dir.openNextFile();
    int i = 0;

    while (file) {

      if (i == menuIndex) {

        if (file.isDirectory() && inIR) {
          strcat(currentPath, "/");
          strcat(currentPath, file.name());
          menuIndex = 0;
        }

        break;
      }

      i++;
      file = dir.openNextFile();
    }

    dir.close();
  }
}

// =====================================================
// 🔹 GO BACK
// =====================================================
void goBack() {

  if (currentState == FILE_BROWSER) {
    currentState = MAIN_MENU;
    strcpy(currentPath, "/");
  }
  else if (currentState == IR_MENU || currentState == RFID_MENU) {
    currentState = MAIN_MENU;
  }

  menuIndex = 0;
}

void useRFID() {
  digitalWrite(CS_PIN, HIGH);   // Disable SD
  digitalWrite(SS_PIN, LOW);   // Enable RFID
}

void disableRFID() {
  digitalWrite(SS_PIN, HIGH);
}

void uidToString(char *buffer, byte uid[4]) {
  const char hexChars[] = "0123456789ABCDEF";

  for (byte i = 0; i < 4; i++) {
    buffer[i * 3]     = hexChars[(uid[i] >> 4) & 0x0F]; // high nibble
    buffer[i * 3 + 1] = hexChars[uid[i] & 0x0F];        // low nibble
    buffer[i * 3 + 2] = (i < 3) ? ' ' : '\0';           // space or end
  }
}

bool readUID() {

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scanning....");

  useRFID();

  // 🔥 WAIT for card (important)
  while (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial());

  if (mfrc522.uid.size != 4)
    return false;

  for (byte i = 0; i < 4; i++) {
    storedUID[i] = mfrc522.uid.uidByte[i];
  }

  mfrc522.PICC_HaltA();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scanned");
  delay(1500);

  char uidStr[12]; // "XX XX XX XX" + null
  uidToString(uidStr, storedUID);
  snprintf(text, sizeof(text), "%s", uidStr);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("UID:");
  lcd.setCursor(0, 1);
  lcd.print(uidStr);
  currentState = SHOW_UID;
  disableRFID();
  return true;
}

bool cloneUID() {

  useRFID();

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial())
    return false;

  if (mfrc522.MIFARE_SetUid(storedUID, 4, true)) {
    mfrc522.PICC_HaltA();
    return true;
  }

  mfrc522.PICC_HaltA();
  return false;
}

// =====================================================
// 🔹 Keyboard
// =====================================================

void showKeyboard() {

  int cursorPos = 0;
  bool lastSW = HIGH;

  // Clear filename
  for (int i = 0; i < 16; i++) fileName[i] = '\0';

  menuIndex = 0;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter name:");

  lcd.cursor();
  lcd.blink();

  while (1) {

    // 🔄 Use your encoder handler
    handleEncoder();

    // Keep index in range
    if (menuIndex >= charCount) menuIndex = 0;
    if (menuIndex < 0) menuIndex = charCount - 1;

    // 🔘 Button press
    bool currentSW = digitalRead(SW);
    if (lastSW == HIGH && currentSW == LOW) {
      delay(200); // debounce

      char selected = chars[menuIndex];

      // Finish condition
      if (selected == ' ' && cursorPos > 0) {
        fileName[cursorPos] = '\0';
        return;
      } 
      else {
        fileName[cursorPos] = selected;
        cursorPos++;

        if (cursorPos > 15) cursorPos = 15;

        menuIndex = 0; // reset to blank
      }
    }
    lastSW = currentSW;

    // 🖥 Display update
    lcd.setCursor(0, 1);

    for (int i = 0; i < 16; i++) {
      if (i < cursorPos) {
        lcd.print(fileName[i]);
      } 
      else if (i == cursorPos) {
        lcd.print(chars[menuIndex]);
      } 
      else {
        lcd.print(" ");
      }
    }

    lcd.setCursor(cursorPos, 1);
  }
}

// =====================================================
// 🔹 SETUP
// =====================================================
void setup() {

  pinMode(CLK, INPUT_PULLUP);
  pinMode(DT, INPUT_PULLUP);
  pinMode(SW, INPUT_PULLUP);
  pinMode(SS_PIN, OUTPUT);
  pinMode(CS_PIN, OUTPUT);

  digitalWrite(SS_PIN, HIGH);
  digitalWrite(CS_PIN, HIGH);

  Serial.begin(9600);

  lcd.init();
  lcd.backlight();

  SPI.begin();

  digitalWrite(CS_PIN, LOW);
  if (!SD.begin(CS_PIN)) {
    lcd.print("SD Error");
    while (1);
  }
  digitalWrite(CS_PIN, HIGH);

  useRFID();
  mfrc522.PCD_Init();

  lastCLK = digitalRead(CLK);

  // Splash
  lcd.setCursor(4, 0);
  lcd.print("Hack M");
  delay(2000);

  updateMenu();
}

// =====================================================
// 🔹 LOOP
// =====================================================
void loop() {
  handleEncoder();
  handleButton();
}