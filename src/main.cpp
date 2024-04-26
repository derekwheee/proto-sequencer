#include <Arduino.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
#include <Adafruit_MCP23X17.h>
#include <Adafruit_DotStar.h>
#include "SAMDTimerInterrupt.h"
#include "NoteTable.h"

// Gate resolution should ideally be divisible by 4
#define GATE_RESOLUTION 16

// Timer config
#define USING_TIMER_TC3 true
#define SELECTED_TIMER TIMER_TC3

SAMDTimer sequenceTimer(SELECTED_TIMER);

// DotStar config
#define NUM_LEDS 1
#define LED_DATA_PIN 8
#define LED_CLCK_PIN 6

Adafruit_DotStar strip(NUM_LEDS, LED_DATA_PIN, LED_CLCK_PIN, DOTSTAR_BRG);
Adafruit_7segment displayMatrix = Adafruit_7segment();
Adafruit_MCP23X17 gpiox;

#define DAC_OUTPUT_PIN A1
#define POTENTIOMETER_PIN A4
#define BPM_POTENTIOMETER_PIN A2
#define GATE_POTENTIOMETER_PIN A3
#define BUTTON_1_PIN 2
#define BUTTON_2_PIN 1
#define BUTTON_3_PIN 0
#define BUTTON_4_PIN 5
#define BUTTON_5_PIN 4
#define BUTTON_6_PIN 3
#define BUTTON_7_PIN 15
#define BUTTON_8_PIN 7
#define BUTTON_9_PIN 6
#define GATE_PIN 13
#define SYNC_PIN 14

// Constants
const int ANALOG_HIGH = 1023;
const int DAC_HIGH = 4095;

const double dacRange[2] = {0, DAC_HIGH};
const double voltageRange[2] = {0, ANALOG_HIGH};
const double inverseVoltageRange[2] = {ANALOG_HIGH, 0};
const double cvRange[2] = {0, 5};
// These potentiometers are wired backwards, so the ranges are inverted
const double bpmRange[2] = {600, 50};
const double gateRange[2] = {GATE_RESOLUTION, 0};

float scale(float value, const double inputRange[2], const double outputRange[2]);
float scale(float value, const double inputRange[2], const double outputRange[2], int precision);
float clamp(float value, const double range[2]);

const int BUTTON_PINS[] = {
    BUTTON_1_PIN,
    BUTTON_2_PIN,
    BUTTON_3_PIN,
    BUTTON_4_PIN,
    BUTTON_5_PIN,
    BUTTON_6_PIN,
    BUTTON_7_PIN,
    BUTTON_8_PIN,
    BUTTON_9_PIN};

struct Button
{
    int pin;
    uint32_t cv;
    bool state;
    long beginPressedState;
    int value;
    int lastValue;
    long lastPress;
};

Button buttons[sizeof(BUTTON_PINS) / sizeof(BUTTON_PINS[0])];

int buttonCount = sizeof(buttons) / sizeof(buttons[0]);

void initializeButtons()
{
    double cvRange[2] = {0.0, 5.0};
    for (unsigned int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
    {
        buttons[i].pin = BUTTON_PINS[i];
        buttons[i].cv = scale((i % 6), cvRange, voltageRange);
        buttons[i].state = true;
        buttons[i].beginPressedState = 0;
        buttons[i].value = HIGH;
        buttons[i].lastValue = HIGH;
        gpiox.pinMode(buttons[i].pin, INPUT_PULLUP);
    }
}

struct DisplayChar
{
    char note;
    bool isSharp;
    NoteMap noteMap;
};

DisplayChar getChar(int index);

uint32_t potentiometerValue;
uint32_t lastPotentiometerValue;
uint32_t bpmPotentiometerValue;
uint32_t lastBpmPotentiometerValue;
uint32_t gatePotentiometerValue;

int bpm;
int currentBeat = 1;
int numSequences = buttonCount;

bool showingPixel = false;

unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 10;

int gateState = LOW;

float smoothingFactor = 0.2;
float smoothedBpmValue = 0;
int smoothedGateValue = 0;

int sequenceCounter = -1; // Initial state

void incrementSequence();
void playSequence();
int getHeldButton(long now);
int wasAnyButtonPressed(long now);
void updateButtonState(long now);
void handlePotentiometerChange(float nextValue);
void handleButtonChange(long now);
void handleBpmPotentiometerChange(float nextValue);
DisplayChar getChar(int index);
void updateDisplay();
void updateGPIO();

void setup()
{
    Serial.begin(115200);

    if (!gpiox.begin_I2C())
    {
        Serial.println("Could not connect to GPIO expander");
        while (1)
            ;
    }

    initializeButtons();
    gpiox.pinMode(GATE_PIN, OUTPUT);

    bpm = scale(120, bpmRange, voltageRange);
    fillNoteMap();

    // This is the slowest we can run this timer
    sequenceTimer.attachInterruptInterval_MS(1000 / (scale(bpm, voltageRange, bpmRange) / 60) / GATE_RESOLUTION, playSequence);

    strip.begin();
    strip.show();

    displayMatrix.begin(0x70);
}

void loop()
{
    unsigned long now = millis();

    updateGPIO();
    updateDisplay();

    // Record the last update time
    lastUpdateTime = now;

    updateButtonState(now);
    lastPotentiometerValue = potentiometerValue;
    lastBpmPotentiometerValue = potentiometerValue;

    // Tuning potentiometer is wired backwards, use inverse range
    potentiometerValue = scale(analogRead(POTENTIOMETER_PIN), voltageRange, inverseVoltageRange);
    bpmPotentiometerValue = clamp(analogRead(BPM_POTENTIOMETER_PIN), voltageRange);
    gatePotentiometerValue = (int)scale(analogRead(GATE_POTENTIOMETER_PIN), voltageRange, gateRange, 1);

    smoothedBpmValue = (smoothingFactor * bpmPotentiometerValue) + ((1 - smoothingFactor) * smoothedBpmValue);
    smoothedGateValue = (smoothingFactor * gatePotentiometerValue) + ((1 - smoothingFactor) * smoothedGateValue);

    if (lastBpmPotentiometerValue != smoothedBpmValue)
    {
        handleBpmPotentiometerChange(smoothedBpmValue);
    }

    if (getHeldButton(now) > -1)
    {
        handlePotentiometerChange(potentiometerValue);
    }
    else if (wasAnyButtonPressed(now) > -1)
    {
        handleButtonChange(now);
    }

    if (Serial)
    {
        Serial.println(now);
        Serial.println(scale(bpm, voltageRange, bpmRange));
        Serial.println(buttons[currentBeat - 1].cv);
        Serial.println(scale(buttons[currentBeat - 1].cv, voltageRange, dacRange));
        Serial.println(potentiometerValue);
        Serial.println(buttons[0].value);
        Serial.println(buttons[1].value);
        Serial.println(buttons[2].value);
        Serial.println(buttons[3].value);
        Serial.println(buttons[4].value);
        Serial.println(buttons[5].value);
        Serial.println(buttons[6].value);
        Serial.println(buttons[7].value);
        Serial.println(buttons[8].value);
    }
}

float scale(float value, const double inputRange[2], const double outputRange[2])
{
    return (value - inputRange[0]) * (outputRange[1] - outputRange[0]) / (inputRange[1] - inputRange[0]) + outputRange[0];
}

float scale(float value, const double inputRange[2], const double outputRange[2], int precision)
{
    float scaled = (value - inputRange[0]) * (outputRange[1] - outputRange[0]) / (inputRange[1] - inputRange[0]) + outputRange[0];
    return round(scaled * precision) / precision;
}

float clamp(float value, const double range[2])
{
    return value < range[0] ? range[0] : value > range[1] ? range[1]
                                                          : value;
}

void incrementSequence()
{
    sequenceCounter = sequenceCounter >= GATE_RESOLUTION - 1 ? 0 : 1 + sequenceCounter;
}

void updateGPIO()
{
    gpiox.digitalWrite(GATE_PIN, gateState);
}

void playSequence()
{
    incrementSequence();

    if (sequenceCounter >= GATE_RESOLUTION - abs(GATE_RESOLUTION - smoothedGateValue))
    {
        gateState = LOW;
    }

    if (sequenceCounter % GATE_RESOLUTION != 0)
    {
        return;
    }

    unsigned long now = millis();

    showingPixel = !showingPixel;

    int heldButton = getHeldButton(now);
    int beatIndex = heldButton > -1 ? heldButton : currentBeat - 1;

    if (buttons[beatIndex].state)
    {
        analogWrite(DAC_OUTPUT_PIN, scale(buttons[beatIndex].cv, voltageRange, dacRange));
        gateState = HIGH;
    } else {
        gateState = LOW;
    }

    strip.setPixelColor(0, strip.Color(0, 0, !showingPixel ? 64 : 0));
    strip.show();

    if (currentBeat >= numSequences)
    {
        currentBeat = 1;
    }
    else
    {
        ++currentBeat;
    }
}

int getHeldButton(long now)
{
    int _heldButtonIndex = -1;

    for (int i = 0; i < buttonCount; ++i)
    {
        if (buttons[i].value == LOW && now - buttons[i].beginPressedState > 500)
        {
            _heldButtonIndex = i;
            break;
        }
    }

    return _heldButtonIndex;
}

bool wasButtonPressed(Button button, long now)
{
    return button.value == HIGH && button.lastValue == LOW && now - button.beginPressedState < 500;
}

int wasAnyButtonPressed(long now)
{
    int pressedButtonIndex = -1;

    for (int i = 0; i < buttonCount; ++i)
    {
        if (wasButtonPressed(buttons[i], now))
        {
            pressedButtonIndex = i;
            break;
        }
    }

    return pressedButtonIndex;
}

void updateButtonState(long now)
{
    for (int i = 0; i < buttonCount; ++i)
    {
        buttons[i].lastValue = buttons[i].value;
        buttons[i].value = gpiox.digitalRead(buttons[i].pin);

        if (buttons[i].value == LOW && buttons[i].lastValue == HIGH)
            buttons[i].beginPressedState = now;
    }
}

void handlePotentiometerChange(float nextValue)
{
    for (int i = 0; i < buttonCount; ++i)
    {
        if (buttons[i].value == LOW)
        {
            buttons[i].cv = nextValue;
            break; // Exit loop after setting the sequence value
        }
    }
}

void handleButtonChange(long now)
{
    for (int i = 0; i < buttonCount; ++i)
    {
        // The odd button (i.e. 5 or 9) has an additional
        // double-click interaction so handle it separately
        if (i == buttonCount - 1)
        {
            // If it has been more than 500ms since the button was
            // last pressed treat as single click, toggle state
            if (now - buttons[i].lastPress > 500)
            {
                buttons[i].state = !buttons[i].state;
            }
            // If it has been less than 500ms treat as a
            // double-click and remove this button from the sequence
            else
            {
                buttons[i].state = numSequences == buttonCount ? false : true;
                numSequences = numSequences == buttonCount ? buttonCount - 1 : buttonCount;
            }

            // Track press timing for single-/double-click handling
            buttons[i].lastPress = now;
            break;
        }
        else
        {
            // If the button has changed from HIGH to LOW toggle state
            if (wasButtonPressed(buttons[i], now))
            {
                buttons[i].state = !buttons[i].state;
                buttons[i].lastPress = now;
                break; // Exit loop after setting the sequence value
            }
        }
    }
}

float movingBpm[3] = {1229, 1229, 1229};
long lastBpmChange = 0;

void handleBpmPotentiometerChange(float nextValue)
{
    long now = millis();

    if (now - lastBpmChange < 100)
    {
        return;
    }

    movingBpm[0] = movingBpm[1];
    movingBpm[1] = movingBpm[2];
    movingBpm[2] = nextValue;

    int threshold = 2;
    float movingAverage = (movingBpm[0] + movingBpm[1] + movingBpm[2]) / 3;

    if (nextValue > (movingAverage + threshold) || nextValue < (movingAverage - threshold))
    {
        bpm = nextValue;
    }

    sequenceTimer.detachInterrupt();
    sequenceTimer.attachInterruptInterval_MS(1000 / (scale(bpm, voltageRange, bpmRange) / 60) / GATE_RESOLUTION, playSequence);

    lastBpmChange = now;
}

DisplayChar getChar(int index)
{
    NoteMap noteMap = getNoteFromVoltage(scale(buttons[index].cv, voltageRange, cvRange));

    // If this display character is not the currently playing beat write `_`
    // If this display character is the current beat but is inactive write `-`
    // If this display character is the current beat and is active write `${note}`
    DisplayChar displayChar = {
        note : index + 1 != currentBeat ? '_' : buttons[index].state ? noteMap.note
                                                                     : '-',
        isSharp : noteMap.isSharp,
        noteMap : noteMap
    };

    return displayChar;
};

void updateDisplay()
{
    long now = millis();
    int heldButtonIndex = getHeldButton(now);

    int offset = currentBeat > 4 ? 4 : 0;

    DisplayChar char1;
    DisplayChar char2;
    DisplayChar char3;
    DisplayChar char4;

    // If a button is being held down show that note
    // on all positions
    if (heldButtonIndex > -1)
    {
        DisplayChar rootChar = getChar(heldButtonIndex);
        DisplayChar fillChar = {
            note : rootChar.noteMap.note,
            isSharp : rootChar.isSharp
        };

        char1 = fillChar;
        char2 = fillChar;
        char3 = fillChar;
        char4 = fillChar;
    }
    // If the current beat is the last button, i.e.
    // 5 or 9 show that note on all positions
    else if (currentBeat == buttonCount)
    {
        char1 = getChar(currentBeat - 1);
        char2 = getChar(currentBeat - 1);
        char3 = getChar(currentBeat - 1);
        char4 = getChar(currentBeat - 1);
    }
    // Otherwise just show each note for the
    // current page of beats
    else
    {
        char1 = getChar(0 + offset);
        char2 = getChar(1 + offset);
        char3 = getChar(2 + offset);
        char4 = getChar(3 + offset);
    }

    displayMatrix.writeDigitAscii(0, char1.note, char1.isSharp);
    displayMatrix.writeDigitAscii(1, char2.note, char2.isSharp);
    // Index 2 is the colon position
    displayMatrix.drawColon(showingPixel);
    displayMatrix.writeDigitAscii(3, char3.note, char3.isSharp);
    displayMatrix.writeDigitAscii(4, char4.note, char4.isSharp);

    displayMatrix.writeDisplay();
}