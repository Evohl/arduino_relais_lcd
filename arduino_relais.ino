#include <U8g2lib.h>
#include <EEPROM.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Set to 16 for one 16-channel board or 32 for two boards.
#ifndef RELAY_COUNT
#define RELAY_COUNT 16
#endif

#if RELAY_COUNT != 16 && RELAY_COUNT != 32
#error "RELAY_COUNT must be 16 or 32"
#endif

const uint8_t relayPins[32] = {
    22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42, 43, 44, 45,
    46, 47, 48, 49, 50, 51, 52, 53
};

const uint8_t encoderA = 2;
const uint8_t encoderB = 3;
const uint8_t encoderButton = 4;
const uint8_t killInput = 5;
const uint8_t beeperPin = 8;
const uint8_t displayClock = 13;
const uint8_t displayData = 11;
const uint8_t displayCs = 12;

U8G2_ST7920_128X64_F_SW_SPI display(U8G2_R0, displayClock, displayData, displayCs, U8X8_PIN_NONE);

bool relayOn[32] = {};
uint8_t activeRelayCount = RELAY_COUNT;
uint8_t selectedRelay = 0;
uint8_t relayNumberBase = 1;
bool displayDirty = true;
bool menuActive = false;
uint8_t menuSelection = 0;
const uint8_t menuItemCount = 4;
bool relayTestActive = false;
uint8_t relayTestNext = 0;
unsigned long relayTestLastStepAt = 0;
const unsigned long relayTestIntervalMs = 1000;

volatile uint8_t encoderState = 0;
volatile int8_t encoderDelta = 0;
volatile unsigned long lastEncoderEdgeUs = 0;
const unsigned long encoderDebounceUs = 1000;
const int8_t encoderTransitions[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0
};
int8_t encoderRemainder = 0;

char serialLine[64];
uint8_t serialLength = 0;
bool serialOverflow = false;

bool buttonLastReading = HIGH;
bool buttonStableState = HIGH;
bool buttonLongPressHandled = false;
unsigned long buttonChangedAt = 0;
unsigned long buttonPressedAt = 0;
const unsigned long buttonDebounceMs = 35;
const unsigned long buttonLongPressMs = 5000;
bool killLastReading = HIGH;
bool killStableState = HIGH;
unsigned long killChangedAt = 0;
const unsigned long killDebounceMs = 35;

const uint8_t eepromRecordSize = 4;
const uint8_t eepromValidMarker = 0xA7;
int8_t activeEepromSlot = -1;
uint8_t eepromSequence = 0;
const int eepromRelayBaseAddress = 8;
const uint8_t eepromRelayBaseMarker = 0xB8;

void onEncoderChange() {
    const unsigned long now = micros();
    if (now - lastEncoderEdgeUs < encoderDebounceUs) {
        return;
    }
    lastEncoderEdgeUs = now;

    const uint8_t currentState = (digitalRead(encoderA) << 1) | digitalRead(encoderB);
    const uint8_t transition = (encoderState << 2) | currentState;
    const int8_t step = -encoderTransitions[transition];
    encoderState = currentState;

    if (step != 0) {
        if ((step > 0 && encoderDelta < 64) || (step < 0 && encoderDelta > -64)) {
            encoderDelta += step;
        }
    }
}

void beep() {
    tone(beeperPin, 2400, 35);
}

void toggleAllRelays() {
    bool allRelaysOn = true;
    for (uint8_t i = 0; i < activeRelayCount; i++) {
        if (!relayOn[i]) {
            allRelaysOn = false;
            break;
        }
    }

    const bool turnOn = !allRelaysOn;
    relayTestActive = false;
    for (uint8_t i = 0; i < activeRelayCount; i++) {
        setRelay(i, turnOn);
    }
    Serial.println(turnOn ? F("KILL switch: all relays ON") : F("KILL switch: all relays OFF"));
    beep();
}

void handleKillInput() {
    const unsigned long now = millis();
    const bool reading = digitalRead(killInput);

    if (reading != killLastReading) {
        killLastReading = reading;
        killChangedAt = now;
    }

    if (now - killChangedAt >= killDebounceMs && reading != killStableState) {
        killStableState = reading;
        if (killStableState == LOW) {
            toggleAllRelays();
        }
    }
}

bool readConfigRecord(uint8_t slot, uint8_t &relayCount, uint8_t &sequence) {
    const int address = slot * eepromRecordSize;
    const uint8_t marker = EEPROM.read(address);
    relayCount = EEPROM.read(address + 1);
    sequence = EEPROM.read(address + 2);
    const uint8_t checksum = EEPROM.read(address + 3);

    return marker == eepromValidMarker &&
        (relayCount == 16 || relayCount == 32) &&
        checksum == static_cast<uint8_t>(relayCount ^ sequence ^ 0x5A);
}

void loadRelayCount() {
    uint8_t count0;
    uint8_t sequence0;
    uint8_t count1;
    uint8_t sequence1;
    const bool valid0 = readConfigRecord(0, count0, sequence0);
    const bool valid1 = readConfigRecord(1, count1, sequence1);

    if (valid0 && valid1) {
        activeEepromSlot = static_cast<int8_t>(sequence0 - sequence1) > 0 ? 0 : 1;
    } else if (valid0) {
        activeEepromSlot = 0;
    } else if (valid1) {
        activeEepromSlot = 1;
    } else {
        activeRelayCount = RELAY_COUNT;
        activeEepromSlot = -1;
        eepromSequence = 0;
        return;
    }

    if (activeEepromSlot == 0) {
        activeRelayCount = count0;
        eepromSequence = sequence0;
    } else {
        activeRelayCount = count1;
        eepromSequence = sequence1;
    }
}

void saveRelayCount() {
    const uint8_t targetSlot = activeEepromSlot == 0 ? 1 : 0;
    const uint8_t nextSequence = eepromSequence + 1;
    const int address = targetSlot * eepromRecordSize;

    EEPROM.update(address, 0);
    EEPROM.update(address + 1, activeRelayCount);
    EEPROM.update(address + 2, nextSequence);
    EEPROM.update(address + 3, static_cast<uint8_t>(activeRelayCount ^ nextSequence ^ 0x5A));
    EEPROM.update(address, eepromValidMarker);

    activeEepromSlot = targetSlot;
    eepromSequence = nextSequence;
}

void loadRelayNumberBase() {
    const uint8_t marker = EEPROM.read(eepromRelayBaseAddress);
    const uint8_t storedBase = EEPROM.read(eepromRelayBaseAddress + 1);
    const uint8_t checksum = EEPROM.read(eepromRelayBaseAddress + 2);
    if (marker == eepromRelayBaseMarker && storedBase <= 1 &&
        checksum == static_cast<uint8_t>(storedBase ^ 0x5A)) {
        relayNumberBase = storedBase;
    }
}

void saveRelayNumberBase() {
    EEPROM.update(eepromRelayBaseAddress, 0);
    EEPROM.update(eepromRelayBaseAddress + 1, relayNumberBase);
    EEPROM.update(eepromRelayBaseAddress + 2, static_cast<uint8_t>(relayNumberBase ^ 0x5A));
    EEPROM.update(eepromRelayBaseAddress, eepromRelayBaseMarker);
}

void toggleRelayCount() {
    if (activeRelayCount == 32) {
        for (uint8_t i = 16; i < 32; i++) {
            digitalWrite(relayPins[i], HIGH);
            relayOn[i] = false;
        }
        activeRelayCount = 16;
    } else {
        activeRelayCount = 32;
    }

    if (selectedRelay >= activeRelayCount) {
        selectedRelay = 0;
    }
    encoderRemainder = 0;
    noInterrupts();
    encoderDelta = 0;
    interrupts();

    saveRelayCount();
    displayDirty = true;
    Serial.print(F("Relay count saved: "));
    Serial.println(activeRelayCount);
}

void toggleRelayNumberBase() {
    relayNumberBase = relayNumberBase == 0 ? 1 : 0;
    saveRelayNumberBase();
    displayDirty = true;
    Serial.print(F("Relay numbering starts at: "));
    Serial.println(relayNumberBase);
}

void setRelay(uint8_t relayIndex, bool turnOn) {
    relayOn[relayIndex] = turnOn;
    digitalWrite(relayPins[relayIndex], turnOn ? LOW : HIGH);
    Serial.print(F("OK: Relay "));
    Serial.print(relayIndex + relayNumberBase);
    Serial.println(turnOn ? F(" ON") : F(" OFF"));
    displayDirty = true;
}

void startRelayTest() {
    relayTestNext = 0;
    relayTestLastStepAt = millis();
    relayTestActive = true;
    for (uint8_t i = 0; i < activeRelayCount; i++) {
        setRelay(i, true);
    }
    Serial.println(F("Relay test started"));
}

void stopRelayTest() {
    if (!relayTestActive) {
        return;
    }
    relayTestActive = false;
    for (uint8_t i = 0; i < activeRelayCount; i++) {
        if (relayOn[i]) {
            setRelay(i, false);
        }
    }
    Serial.println(F("Relay test stopped"));
    displayDirty = true;
}

void drawMenu() {
    char labels[menuItemCount][20];
    snprintf(labels[0], sizeof(labels[0]), "Relais: %u", activeRelayCount);
    snprintf(labels[1], sizeof(labels[1]), "Start: %u", relayNumberBase);
    if (relayTestActive) {
        snprintf(labels[2], sizeof(labels[2]), "Test %u/%u", relayTestNext + relayNumberBase, activeRelayCount);
    } else {
        snprintf(labels[2], sizeof(labels[2]), "Relais-Test");
    }
    snprintf(labels[3], sizeof(labels[3]), "Zurueck");

    display.setFont(u8g2_font_5x7_tf);
    display.drawStr(2, 9, "Menue");
    for (uint8_t i = 0; i < menuItemCount; i++) {
        const uint8_t y = 19 + i * 12;
        display.drawStr(1, y, i == menuSelection ? ">" : " ");
        display.drawStr(9, y, labels[i]);
    }
}

void drawDisplay() {
    display.clearBuffer();
    if (menuActive) {
        drawMenu();
        display.sendBuffer();
        displayDirty = false;
        return;
    }

    const uint8_t columns = activeRelayCount == 16 ? 4 : 8;
    const uint8_t cellWidth = 128 / columns;
    const uint8_t cellHeight = 64 / 4;
    const bool compactLabels = activeRelayCount == 32;
    display.setFont(compactLabels ? u8g2_font_5x7_tf : u8g2_font_6x10_tf);

    for (uint8_t relayIndex = 0; relayIndex < activeRelayCount; relayIndex++) {
        const uint8_t column = relayIndex % columns;
        const uint8_t row = relayIndex / columns;
        const uint8_t x = column * cellWidth;
        const uint8_t y = row * cellHeight;
        if (relayOn[relayIndex]) {
            display.drawBox(x + 1, y + 1, cellWidth - 2, cellHeight - 3);
            display.setDrawColor(0);
        } else {
            display.drawFrame(x + 1, y + 1, cellWidth - 2, cellHeight - 3);
        }
        char label[5];
        if (compactLabels) {
            snprintf(label, sizeof(label), "%02u", relayIndex + relayNumberBase);
        } else {
            snprintf(label, sizeof(label), "R%02u", relayIndex + relayNumberBase);
        }
        const uint8_t labelWidth = compactLabels ? 10 : 18;
        const uint8_t textX = x + (cellWidth - labelWidth) / 2;
        display.drawStr(textX, y + 11, label);
        display.setDrawColor(1);

        if (relayIndex == selectedRelay) {
            display.drawHLine(x + cellWidth / 2 - 2, y + cellHeight - 1, 5);
        }
    }

    display.sendBuffer();
    displayDirty = false;
}

void handleEncoder() {
    int8_t movement;
    noInterrupts();
    movement = encoderDelta;
    encoderDelta = 0;
    interrupts();

    if (relayTestActive) {
        encoderRemainder = 0;
        return;
    }
    if (menuActive) {
        encoderRemainder += movement;
        while (encoderRemainder >= 4) {
            encoderRemainder -= 4;
            menuSelection = (menuSelection + 1) % menuItemCount;
            displayDirty = true;
        }
        while (encoderRemainder <= -4) {
            encoderRemainder += 4;
            menuSelection = (menuSelection + menuItemCount - 1) % menuItemCount;
            displayDirty = true;
        }
        return;
    }

    encoderRemainder += movement;
    while (encoderRemainder >= 4) {
        encoderRemainder -= 4;
        selectedRelay = (selectedRelay + 1) % activeRelayCount;
        displayDirty = true;
    }
    while (encoderRemainder <= -4) {
        encoderRemainder += 4;
        selectedRelay = (selectedRelay + activeRelayCount - 1) % activeRelayCount;
        displayDirty = true;
    }
}

void selectMenuItem() {
    switch (menuSelection) {
        case 0:
            toggleRelayCount();
            break;
        case 1:
            toggleRelayNumberBase();
            break;
        case 2:
            startRelayTest();
            break;
        default:
            menuActive = false;
            displayDirty = true;
            break;
    }
}

void handleButton() {
    const unsigned long now = millis();
    const bool reading = digitalRead(encoderButton);

    if (reading != buttonLastReading) {
        buttonLastReading = reading;
        buttonChangedAt = now;
    }

    if (now - buttonChangedAt >= buttonDebounceMs && reading != buttonStableState) {
        buttonStableState = reading;
        if (buttonStableState == LOW) {
            buttonPressedAt = now;
            buttonLongPressHandled = false;
        } else if (!buttonLongPressHandled) {
            if (relayTestActive) {
                stopRelayTest();
            } else if (menuActive) {
                selectMenuItem();
            } else {
                setRelay(selectedRelay, !relayOn[selectedRelay]);
            }
            beep();
        }
    }

    if (buttonStableState == LOW && !buttonLongPressHandled && now - buttonPressedAt >= buttonLongPressMs) {
        if (relayTestActive) {
            stopRelayTest();
        }
        menuActive = !menuActive;
        menuSelection = 0;
        encoderRemainder = 0;
        displayDirty = true;
        buttonLongPressHandled = true;
        beep();
    }
}

void handleRelayTest() {
    if (!relayTestActive) {
        return;
    }

    const unsigned long now = millis();
    if (now - relayTestLastStepAt < relayTestIntervalMs) {
        return;
    }

    setRelay(relayTestNext, false);
    relayTestNext++;
    relayTestLastStepAt = now;
    if (relayTestNext >= activeRelayCount) {
        relayTestActive = false;
        Serial.println(F("Relay test complete"));
    }
}

char *trimWhitespace(char *text) {
    while (*text == ' ' || *text == '\t') {
        text++;
    }

    char *end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t')) {
        *--end = '\0';
    }
    return text;
}

bool processCommand(char *line) {
    char *separator = strchr(line, ':');
    if (separator == NULL || strchr(separator + 1, ':') != NULL) {
        return false;
    }

    *separator = '\0';
    char *relayList = trimWhitespace(line);
    char *operation = trimWhitespace(separator + 1);
    if (relayList[0] == '\0') {
        return false;
    }
    const bool turnOn = strcmp(operation, "ON") == 0;
    if (!turnOn && strcmp(operation, "OFF") != 0) {
        return false;
    }

    bool requested[32] = {};
    if (strcmp(relayList, "all") == 0) {
        for (uint8_t i = 0; i < activeRelayCount; i++) {
            requested[i] = true;
        }
    } else {
        char *token = relayList;
        while (*token != '\0') {
            char *nextToken = strchr(token, ',');
            if (nextToken != NULL) {
                *nextToken = '\0';
            }

            token = trimWhitespace(token);
            char *end = NULL;
            const long relayNumber = strtol(token, &end, 10);
            const long firstRelayNumber = relayNumberBase;
            const long lastRelayNumber = firstRelayNumber + activeRelayCount - 1;
            if (token[0] == '\0' || *end != '\0' || relayNumber < firstRelayNumber || relayNumber > lastRelayNumber) {
                return false;
            }
            requested[relayNumber - firstRelayNumber] = true;

            if (nextToken == NULL) {
                break;
            }
            token = nextToken + 1;
            if (*token == '\0') {
                return false;
            }
        }
    }

    for (uint8_t i = 0; i < activeRelayCount; i++) {
        if (requested[i]) {
            setRelay(i, turnOn);
        }
    }
    Serial.println(F("DONE"));
    return true;
}

void handleSerial() {
    while (Serial.available() > 0) {
        const char character = static_cast<char>(Serial.read());
        if (character == '\r' || character == '\n') {
            if (serialOverflow) {
                Serial.println(F("ERROR: command too long"));
            } else if (serialLength > 0) {
                serialLine[serialLength] = '\0';
                if (!processCommand(serialLine)) {
                    Serial.println(F("ERROR: expected relay[,relay]:ON|OFF or all:ON|OFF"));
                }
            }
            serialLength = 0;
            serialOverflow = false;
        } else if (!serialOverflow) {
            if (serialLength < sizeof(serialLine) - 1) {
                serialLine[serialLength++] = character;
            } else {
                serialOverflow = true;
            }
        }
    }
}

void setup() {
    Serial.begin(115200);

    for (uint8_t i = 0; i < 32; i++) {
        digitalWrite(relayPins[i], HIGH);
        pinMode(relayPins[i], OUTPUT);
    }
    loadRelayCount();
    loadRelayNumberBase();

    pinMode(encoderA, INPUT_PULLUP);
    pinMode(encoderB, INPUT_PULLUP);
    pinMode(encoderButton, INPUT_PULLUP);
    pinMode(killInput, INPUT_PULLUP);
    pinMode(beeperPin, OUTPUT);
    encoderState = (digitalRead(encoderA) << 1) | digitalRead(encoderB);
    attachInterrupt(digitalPinToInterrupt(encoderA), onEncoderChange, CHANGE);
    attachInterrupt(digitalPinToInterrupt(encoderB), onEncoderChange, CHANGE);

    display.begin();
    displayDirty = true;
    drawDisplay();
    Serial.print(F("Ready. Relay numbers start at "));
    Serial.println(relayNumberBase);
}

void loop() {
    handleKillInput();
    handleEncoder();
    handleButton();
    handleSerial();
    handleRelayTest();

    if (displayDirty) {
        drawDisplay();
    }
}
