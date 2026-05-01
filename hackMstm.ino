#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SdFat.h>
#include <MFRC522.h>
#include <IRremote.hpp>
#include <string.h>

// ================= SPI OBJECTS =================
SPIClass SPI_1(PA7, PA6, PA5);     // RFID
SPIClass SPI_2(PB15, PB14, PB13);  // SD

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= SD ==================
#define CS_PIN D5
SdFat SD;

// ================= RFID ==================
#define RST_PIN D9
#define SS_PIN D10   // RFID CS
MFRC522 mfrc522(SS_PIN, RST_PIN);
byte storedUID[4];

// ================= IR ==================
#define RECV_PIN D4
#define IR_SEND_PIN D3
#define RAW_BUFFER 180
#define TEXT_BUFFER 900 
uint16_t dataLength = 0;
bool saveToExisting = false;

// ================= ENCODER =============
#define CLK D6
#define DT  D7
#define SW  D8

int lastCLK;
int menuIndex = 0;
int previousMenuIndex = 0;
int deleteMenuIndex = 0;

// ================= STATES ==============
enum State {
  MAIN_MENU,
  IR_MENU,
  RFID_MENU,
  RFID_SCAN_MENU,
  SHOW_UID,
  UID_MENU,
  SAVED_UID_MENU,
  KEYBOARD_MENU,
  DELETE_MENU,
  IR_CODE_MENU,
  REMOTE_BUTTONS_MENU,
  SAVED_REMOTE_MENU,
  FILE_BROWSER
};

State currentState = MAIN_MENU;
State previousState = MAIN_MENU;

// ================= MENUS ===============
const char mainMenu0[] PROGMEM = "IR";
const char mainMenu1[] PROGMEM = "RFID";
const char mainMenu2[] PROGMEM = "USB";
const char mainMenu3[] PROGMEM = "Battery";

const char* const mainMenu[] PROGMEM = {
  mainMenu0, mainMenu1, mainMenu2, mainMenu3
};

const char irMenu0[] PROGMEM = "Clone";
const char irMenu1[] PROGMEM = "Saved";

const char* const irMenu[] PROGMEM = {
  irMenu0, irMenu1
};

const char rfidMenu0[] PROGMEM = "Scan";
const char rfidMenu1[] PROGMEM = "Saved";

const char* const rfidMenu[] PROGMEM = {
  rfidMenu0, rfidMenu1
};

const char uidMenu0[] PROGMEM = "Save";
const char uidMenu1[] PROGMEM = "Clone";

const char* const uidMenu[] PROGMEM = {
  uidMenu0, uidMenu1
};

const char savedUidMenu0[] PROGMEM = "Clone";
const char savedUidMenu1[] PROGMEM = "Delete";

const char* const savedUidMenu[] PROGMEM = {
  savedUidMenu0, savedUidMenu1
};

const char deleteMenu0[] PROGMEM = "Yes";
const char deleteMenu1[] PROGMEM = "No";

const char* const deleteMenu[] PROGMEM = {
  deleteMenu0, deleteMenu1
};

const char irCodeMenu0[] PROGMEM = "Save-New";
const char irCodeMenu1[] PROGMEM = "Save-Existing";
const char irCodeMenu2[] PROGMEM = "Transmit";

const char* const irCodeMenu[] PROGMEM = {
  irCodeMenu0, irCodeMenu1, irCodeMenu2
};

const char savedRemoteMenu0[] PROGMEM = "Transmit";
const char savedRemoteMenu1[] PROGMEM = "Delete";

const char* const savedRemoteMenu[] PROGMEM = {
  savedRemoteMenu0, savedRemoteMenu1
};

// ================= FILE SYSTEM =========
char currentPath[50] = "/";
bool inIR = false;   // to allow folder navigation
int fileCount = 0;

// ================= BUTTON ==============
unsigned long pressStart = 0;
bool buttonHeld = false;

// ================= Keyboard Variables ==============
char fileName[10];
char text[TEXT_BUFFER];
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
    case SAVED_UID_MENU: return 2;
    case DELETE_MENU: return 2;
    case IR_CODE_MENU: return 3;
    case SAVED_REMOTE_MENU: return 2;
    case REMOTE_BUTTONS_MENU: return fileCount;
    case FILE_BROWSER: return fileCount;
    default: return 1;
  }
}

// =====================================================
// 🔹 COUNT FILES
// =====================================================
int getFileCount() {
  File dir = SD.open(currentPath);
  if (!dir) return 0;

  int count = 0;

  File file = dir.openNextFile();

  while (file) {
    count++;
    file.close();               // 🔥 IMPORTANT
    file = dir.openNextFile();
  }

  dir.close();
  return count;
}
// =====================================================
// 🔹 DISPLAY MENU
// =====================================================
void displayMenu(const char *const menu[], int size) {
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

    char buffer[17];
    strcpy_P(buffer, (char*)pgm_read_word(&(menu[idx])));
    lcd.print(buffer);
  }
}
// =====================================================
// 🔹 DISPLAY FILES
// =====================================================
void displayFiles() {
  digitalWrite(SS_PIN, HIGH);
  lcd.clear();

  File dir = SD.open(currentPath);
  if (!dir) {
    lcd.print(F("Dir Error"));
    return;
  }

  int total = fileCount;   // use cached value

  if (total == 0) {
    lcd.print(F("No Files"));
    return;
  }

  // 🔁 SCROLL WINDOW
  int start = menuIndex;
  if (start > total - 2) start = total - 2;
  if (start < 0) start = 0;

  int index = 0;
  int shown = 0;

  File file = dir.openNextFile();

  while (file) {

    if (index >= start && shown < 2) {

      lcd.setCursor(0, shown);

      if (index == menuIndex) lcd.print(F("> "));
      else lcd.print(F("  "));

      char name[20];
      file.getName(name, sizeof(name));

      // remove extension
      for (int i = 0; name[i] != '\0'; i++) {
        if (name[i] == '.') {
          name[i] = '\0';
          break;
        }
      }

      lcd.print(name);

      shown++;
    }

    index++;

    file.close();                // 🔥 VERY IMPORTANT
    file = dir.openNextFile();   // move to next
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
    lcd.print(F("Saved"));
  } else {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(F("Error"));
  }
}

// =====================================================
// 🔹 READ FILES
// =====================================================
void readFileByIndex(const char *dirname, int index) {
  File dir = SD.open(dirname);

  if (!dir || !dir.isDirectory()) {
    return;
  }

  File file = dir.openNextFile();
  int i = 0;

  while (file) {

    if (i == index) {
      int j = 0;
      while (file.available() && j < sizeof(text) - 1) {
        char c = file.read();
        text[j++] = c;
      }
      text[j] = '\0';

      file.close();
      dir.close();
      return;
    }

    file = dir.openNextFile();
    i++;
  }

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("Invalid Index"));
  dir.close();
}

// =====================================================
// 🔹 DELETE FILES
// =====================================================
void deleteFileByIndex(const char *dirname, int index) {
  File dir = SD.open(dirname);

  if (!dir || !dir.isDirectory()) {
    return;
  }

  File file = dir.openNextFile();
  int i = 0;

  while (file) {

    if (i == index) {

      // Build full path
      char fullPath[25];
      char name[20];
      file.getName(name, sizeof(name));   // ✅ FIX

      snprintf(fullPath, sizeof(fullPath), "%s/%s", dirname, name);

      for (int i = 0; fullPath[i] != '\0'; i++) {
        if (fullPath[i] >= 'A' && fullPath[i] <= 'Z') {
            fullPath[i] += ('a' - 'A');
        } 
      }

      file.close();   // MUST close before deleting

      if (SD.remove(fullPath)) {
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print(F("Deleted"));
        delay(1500);
      } else {
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print(F("Delete Failed"));
        delay(1500);
      }

      dir.close();
      return;
    }

    file = dir.openNextFile();
    i++;
  }

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("Invalid Index"));
  dir.close();
  fileCount = getFileCount();
}

// =====================================================
// 🔹 CREATE FOLDER
// =====================================================
void createFolder(const char *path)
{
  if (SD.mkdir(path)) {
    Serial.println("Folder created");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Remote Created"));
    delay(1500);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Failed!"));
    delay(1500);
    currentState = IR_MENU;
  }
}

// =====================================================
// 🔹 OPEN FOLDER
// =====================================================
void openFolder(const char *dirname, int index) {
  File dir = SD.open(dirname);

  if (!dir || !dir.isDirectory()) {
    Serial.println("Invalid directory");
    return;
  }

  File file = dir.openNextFile();
  int i = 0;

  while (file) {

    if (i == index) {

      char name[50];
      file.getName(name, sizeof(name));   // ✅ correct way

      char fullPath[100];
      snprintf(fullPath, sizeof(fullPath), "%s/%s", dirname, name);
      strncpy(currentPath, fullPath, sizeof(currentPath) - 1);
      currentPath[sizeof(currentPath) - 1] = '\0';
      
      file.close();
      dir.close();
      return;
    }

    file = dir.openNextFile();
    i++;
  }

  Serial.println("Invalid index");
  dir.close();
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
    case SAVED_UID_MENU: displayMenu(savedUidMenu, 2); break;
    case DELETE_MENU: displayMenu(deleteMenu,2); break;
    case IR_CODE_MENU: displayMenu(irCodeMenu,3); break;
    case REMOTE_BUTTONS_MENU: displayFiles(); break;
    case SAVED_REMOTE_MENU:displayMenu(savedRemoteMenu,2); break;
  }
}

// =====================================================
// 🔹 ENCODER (SMOOTH + CYCLIC)
// =====================================================
void handleEncoder() {
  int clkState = digitalRead(CLK);

  if (clkState != lastCLK && clkState == LOW) {

    int maxItems = getMenuSize();

    if (maxItems <= 0) {
      lastCLK = clkState;
      return;
    }

    if (digitalRead(DT) != clkState) menuIndex++;
    else menuIndex--;

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
  previousMenuIndex = menuIndex;
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
      //fileCount = getFileCount();  
      inIR = false;
    }

    menuIndex = 0;
  }

  else if (currentState == IR_MENU) {

    if (menuIndex == 1) {
      strcpy(currentPath, "/ir");
      currentState = FILE_BROWSER;
      fileCount = getFileCount();  
      inIR = true;
      menuIndex = 0;
    }
    else if(menuIndex == 0)
    {
      receiveIR();
    }
  }

  else if(currentState == IR_CODE_MENU)
  {
    if(menuIndex == 0)
    {
      currentState = KEYBOARD_MENU;
      showKeyboard();
      lcd.clear();
      lcd.noBlink();
      lcd.noCursor();
      char newFileName[32];
      snprintf(newFileName, sizeof(newFileName), "/ir/%s", fileName);
      createFolder(newFileName);

      showKeyboard();
      lcd.clear();
      lcd.noBlink();
      lcd.noCursor();
      snprintf(newFileName, sizeof(newFileName), "%s/%s.txt", newFileName,fileName);
      writeFile(newFileName,text);
      delay(1500);
      currentState = IR_MENU;
    }
    else if(menuIndex == 1)
    {
      strcpy(currentPath, "/ir");
      currentState = FILE_BROWSER;
      fileCount = getFileCount();  
      inIR = true;
      menuIndex = 0;
      saveToExisting = true;
    }
    else if(menuIndex == 2)
    {
      transmitIR();
    }
  }

  else if(currentState == FILE_BROWSER && strcmp(currentPath, "/ir") == 0)
  {
    openFolder(currentPath,menuIndex);
    fileCount = getFileCount();
    if(saveToExisting)
    {
      showKeyboard();
      lcd.clear();
      lcd.noBlink();
      lcd.noCursor();
      char newFileName[32];
      snprintf(newFileName, sizeof(newFileName), "/%s/%s.txt", currentPath,fileName);
      writeFile(newFileName,text);
      delay(1500);
      currentState = IR_MENU;
      saveToExisting = false;
    }
    else
    {
      currentState = REMOTE_BUTTONS_MENU;
    }
  }

  else if(currentState == REMOTE_BUTTONS_MENU)
  {
    currentState = SAVED_REMOTE_MENU;
    readFileByIndex(currentPath,menuIndex);
    deleteMenuIndex = menuIndex;
  }

  else if(currentState == SAVED_REMOTE_MENU)
  {
    if(menuIndex == 0)
    {  
      transmitIR();
      currentState = REMOTE_BUTTONS_MENU;
    }
    else if (menuIndex == 1)
    {
      currentState = DELETE_MENU;
    }
  }

  else if (currentState == RFID_MENU) {

    if (menuIndex == 1) {
      strcpy(currentPath, "/rfid");
      currentState = FILE_BROWSER;
      fileCount = getFileCount();   // 🔥 IMPORTANT
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
      cloneUID();
      currentState = UID_MENU;
    }
  }

  else if(currentState == FILE_BROWSER && strcmp(currentPath, "/rfid") == 0)
  {
    currentState = SAVED_UID_MENU;
    readFileByIndex(currentPath,previousMenuIndex);
    deleteMenuIndex = menuIndex;
  }

  else if(currentState == SAVED_UID_MENU)
  {
    if(menuIndex == 0)
    {
      cloneUID();
      currentState = SAVED_UID_MENU;
    }
    else if (menuIndex == 1)
    {
      currentState = DELETE_MENU;
    }
  }

  else if(currentState == DELETE_MENU)
  {
    if(menuIndex == 0)
    {
      deleteFileByIndex(currentPath, deleteMenuIndex);
      currentState = FILE_BROWSER;
    }
    else if(menuIndex == 1)
    {
      currentState = FILE_BROWSER;
    }
  }

  /*else if (currentState == FILE_BROWSER && strcmp(currentPath, "/ir") == 0)
  {
    if (fileCount == 0) return;

    File dir = SD.open(currentPath);
    if (!dir) return;

    dir.rewindDirectory();   // 🔥 CRITICAL FIX

    File file;
    int i = 0;

    while (true) {

      file = dir.openNextFile();
      if (!file) break;

      if (i == menuIndex) {

        if (file.isDirectory() && inIR) {
          strcat(currentPath, "/");
          char name[20];
          file.getName(name, sizeof(name));   // ✅ FIX

          strcat(currentPath, name);
          fileCount = getFileCount();
          menuIndex = 0;

          file.close();
          dir.close();
          return;
        }

        // ✅ file selected
        file.close();
        dir.close();

        menuIndex = 0;
        return;
      }

      file.close();   // 🔥 VERY IMPORTANT
      i++;
    }

    dir.close();
    currentState = SAVED_UID_MENU;
  }*/
}

// =====================================================
// 🔹 GO BACK
// =====================================================
void goBack() {

   if (currentState == IR_MENU || currentState == RFID_MENU) {
    currentState = MAIN_MENU;
  }
  else if (currentState == SHOW_UID ||(currentState == FILE_BROWSER && strcmp(currentPath, "/rfid") == 0)){
    currentState = RFID_MENU;
  }
  else if (currentState == SAVED_UID_MENU) {
    currentState = FILE_BROWSER;   // ✅ go back to file list
  }
  else if (currentState == UID_MENU) {
    currentState = SHOW_UID;   // ✅ go back to file list
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

byte hexCharToByte(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0; // fallback
}

void stringToUID(const char *str, byte uid[4]) {

  for (byte i = 0; i < 4; i++) {
    byte high = hexCharToByte(str[i * 3]);
    byte low  = hexCharToByte(str[i * 3 + 1]);
    uid[i] = (high << 4) | low;
  }
}

bool readUID() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Scanning...."));

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
  lcd.print(F("Scanned"));
  delay(1500);

  char uidStr[12]; // "XX XX XX XX" + null
  uidToString(uidStr, storedUID);
  snprintf(text, sizeof(text), "%s", uidStr);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("UID:"));
  lcd.setCursor(0, 1);
  lcd.print(uidStr);
  currentState = SHOW_UID;
  disableRFID();
  return true;
}

bool cloneUID() {
  useRFID();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Place the Clonable Card..."));

  stringToUID(text, storedUID);

  while (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial());

  if (mfrc522.MIFARE_SetUid(storedUID, 4, true)) {
    mfrc522.PICC_HaltA();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Write Success"));
    delay(1500);
    return true;
  }

  mfrc522.PICC_HaltA();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Writing Failed"));
  delay(1500);
  return false;
}

// =====================================================
// 🔹 Keyboard
// =====================================================

void showKeyboard() {

  int cursorPos = 0;
  bool lastSW = HIGH;
  int keyIndex = 0;

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
    int clkState = digitalRead(CLK);

    if (clkState != lastCLK && clkState == LOW) {
      if (digitalRead(DT) != clkState) keyIndex++;
      else keyIndex--;

      if (keyIndex >= charCount) keyIndex = 0;
      if (keyIndex < 0) keyIndex = charCount - 1;
    }
    lastCLK = clkState;

    // 🔘 Button press
    bool currentSW = digitalRead(SW);
    if (lastSW == HIGH && currentSW == LOW) {
      delay(200); // debounce

      char selected = chars[keyIndex];

      // Finish condition
      if (selected == ' ' && cursorPos > 0) {
        fileName[cursorPos] = '\0';
        return;
      } 
      else {
        fileName[cursorPos] = selected;
        cursorPos++;

        if (cursorPos > 15) cursorPos = 15;

        keyIndex = 0; // reset to blank
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
        lcd.print(chars[keyIndex]);
      } 
      else {
        lcd.print(" ");
      }
    }

    lcd.setCursor(cursorPos, 1);
  }
}

// ====================== RECEIVE IR ======================
void receiveIR() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Press the remote...");

  while(1)
  {
    if (IrReceiver.decode()) {

      uint16_t len = IrReceiver.irparams.rawlen;

      if (len > 60) {

        dataLength = len - 1;

        compressToText();

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Success");
        delay(1500);
        currentState = IR_CODE_MENU;
        Serial.println(F("✅ Signal stored as HEX text"));
        break;

      } else {
        Serial.println(F("⚠️ Noise ignored"));
      }

      IrReceiver.resume();
    }
  }
}

// ====================== COMPRESS TO TEXT ======================
void compressToText() {

  uint16_t index = 0;

  for (uint16_t i = 1; i <= dataLength; i++) {

    uint16_t val = IrReceiver.irparams.rawbuf[i] * 50;

    // 🔥 store as 4-digit HEX
    index += sprintf(&text[index], "%04X", val);

    // separator
    text[index++] = ' ';

    if (index >= TEXT_BUFFER - 6) break;
  }

  text[index] = '\0';
}

// ====================== TEXT TO RAW ======================
void textToRaw(uint16_t rawData[]) {

  uint16_t i = 0;

  // strtok modifies string → use directly (safe here)
  char *ptr = strtok(text, " ");

  while (ptr != NULL && i < RAW_BUFFER) {
    rawData[i++] = strtol(ptr, NULL, 16);
    ptr = strtok(NULL, " ");
  }

  dataLength = i;
}

// ====================== TRANSMIT ======================
void transmitIR() {

  if (strlen(text) == 0) {
    Serial.println(F("⚠️ No signal stored!"));
    return;
  }

  Serial.println(F("Rebuilding signal..."));

  static uint16_t rawData[RAW_BUFFER];  // 🔥 static = no stack overflow

  textToRaw(rawData);

  Serial.println(F("📡 Transmitting..."));
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Transmitting...");

  IrReceiver.stop();
  delay(30);

  for (uint8_t i = 0; i < 3; i++) {
    IrSender.sendRaw(rawData, dataLength, 38);
    delay(100);
  }

  IrReceiver.start();

  Serial.println(F("✅ Done"));
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Done");
  delay(1500);
  currentState = IR_CODE_MENU;
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

  SPI_1.begin();
  SPI_2.begin();

  // ===== RFID INIT (SPI1) =====
  SPI.begin(); // bind default to SPI1
  mfrc522.PCD_Init();
  Serial.println("RFID Ready");

  // ===== SD INIT (SPI2) =====
  if (!SD.begin(SdSpiConfig(CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(10), &SPI_2))) {
    Serial.println("SD init failed!");
  } else {
    Serial.println("SD init success!");
  }

  Serial.println("\n=== SYSTEM READY (DUAL SPI + RECONFIG) ===");

  // ===== IR INIT =====
  IrReceiver.begin(RECV_PIN, ENABLE_LED_FEEDBACK);
  IrSender.begin(IR_SEND_PIN);

  lastCLK = digitalRead(CLK);

  // Splash
  lcd.setCursor(4, 0);
  lcd.print(F("Hack M"));
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