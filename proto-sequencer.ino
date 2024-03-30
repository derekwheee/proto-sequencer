// TODO: Smooth tuning pin potentiometer value?
// TODO: Add comments where it would be helpful

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>

#include "SAMDTimerInterrupt.h"
#include <Adafruit_DotStar.h>
#include "NoteTable.h"

// Gate resolution should ideally be divisible by 4
#define GATE_RESOLUTION 12

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

#define DAC_OUTPUT_PIN A0
#define POTENTIOMETER_PIN A5
#define BPM_POTENTIOMETER_PIN A4
#define GATE_POTENTIOMETER_PIN A3
#define BUTTON_1_PIN 7
#define BUTTON_2_PIN 9
#define BUTTON_3_PIN 10
#define BUTTON_4_PIN 11
#define BUTTON_5_PIN 12
#define BUTTON_6_PIN 12
#define BUTTON_7_PIN 12
#define BUTTON_8_PIN 12
#define BUTTON_9_PIN 12
#define GATE_PIN 2

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
    long beginHighState;
    int value;
    int lastValue;
    long lastPress;
};

Button buttons[sizeof(BUTTON_PINS) / sizeof(BUTTON_PINS[0])];

int buttonCount = sizeof(buttons) / sizeof(buttons[0]);

void initializeButtons()
{
    for (int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
    {
        buttons[i].pin = BUTTON_PINS[i];
        buttons[i].cv = 0.0;
        buttons[i].state = true;
        buttons[i].beginHighState = 0;
        buttons[i].value = LOW;
        buttons[i].lastValue = LOW;
        pinMode(buttons[i].pin, INPUT_PULLUP);
    }
}

// Constants
const int ANALOG_HIGH = 1023;
const int DAC_HIGH = 4095;

const double dacRange[2] = {0, DAC_HIGH};
const double voltageRange[2] = {0, ANALOG_HIGH};
const double bpmRange[2] = {1, 4096};
const double cvRange[2] = {0, 5};
const double gateRange[2] = {0, 12};

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
const unsigned long updateInterval = 100;

int gateState = LOW;

float smoothingFactor = 0.2;
float smoothedBpmValue = 0;
int smoothedGateValue = 0;

int sequenceCounter = -1; // Initial state

void setup()
{
    initializeButtons();
    pinMode(GATE_PIN, OUTPUT);

    bpm = scale(120, bpmRange, voltageRange);
    fillNoteMap();

    // This is the slowest we can run this timer
    sequenceTimer.attachInterruptInterval_MS(1000 / (scale(bpm, voltageRange, bpmRange) / 60) / GATE_RESOLUTION, playSequence);

    strip.begin();
    strip.show();

    displayMatrix.begin(0x70);

    Serial.begin(115200);
}

void loop()
{
    unsigned long now = millis();

    if (now - lastUpdateTime >= updateInterval)
    {
        // Record the last update time
        lastUpdateTime = now;

        updateButtonState(now);
        lastPotentiometerValue = potentiometerValue;
        lastBpmPotentiometerValue = potentiometerValue;

        potentiometerValue = clamp(analogRead(POTENTIOMETER_PIN), voltageRange);
        bpmPotentiometerValue = clamp(analogRead(BPM_POTENTIOMETER_PIN), voltageRange);     
        gatePotentiometerValue = (int) scale(analogRead(GATE_POTENTIOMETER_PIN), voltageRange, gateRange, 1);

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

        if (wasAnyButtonPressed(now))
        {
            handleButtonChange(now);
        }

        Serial.println(bpm);
        Serial.println(scale(bpm, voltageRange, bpmRange));
        Serial.println(1000 / (scale(bpm, voltageRange, bpmRange) / 60) / GATE_RESOLUTION);
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

bool shouldSkipSequence = false;

void incrementSequence()
{
    sequenceCounter = sequenceCounter >= GATE_RESOLUTION - 1 ? 0 : 1 + sequenceCounter;
}

void playSequence()
{
    incrementSequence();

    if (sequenceCounter >= GATE_RESOLUTION - abs(GATE_RESOLUTION - smoothedGateValue))
    {
        digitalWrite(GATE_PIN, LOW);
    }

    if (sequenceCounter % GATE_RESOLUTION != 0)
    {
        return;
    }

    unsigned long now = millis();

    showingPixel = !showingPixel;

    int heldButton = getHeldButton(now);
    int beatIndex = heldButton > -1 ? heldButton : currentBeat - 1;

    analogWrite(DAC_OUTPUT_PIN, scale(buttons[beatIndex].cv, voltageRange, dacRange));
    digitalWrite(GATE_PIN, HIGH);

    updateDisplay();

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
        if (buttons[i].value == HIGH && now - buttons[i].beginHighState > 500)
        {
            _heldButtonIndex = i;
            break;
        }
    }

    return _heldButtonIndex;
}

bool wasAnyButtonPressed(long now)
{
    bool _wasAnyButtonPressed = false;

    for (int i = 0; i < buttonCount; ++i)
    {
        if (buttons[i].value == LOW && buttons[i].lastValue == HIGH && now - buttons[i].beginHighState < 500)
        {
            _wasAnyButtonPressed = true;
            break;
        }
    }

    return _wasAnyButtonPressed;
}

void updateButtonState(long now)
{
    for (int i = 0; i < buttonCount; ++i)
    {
        buttons[i].lastValue = buttons[i].value;
        buttons[i].value = digitalRead(BUTTON_PINS[i]);

        if (buttons[i].value == HIGH && buttons[i].lastValue == LOW)
            buttons[i].beginHighState = now;
    }
}

void handlePotentiometerChange(float nextValue)
{
    for (int i = 0; i < buttonCount; ++i)
    {
        if (digitalRead(buttons[i].pin) == HIGH)
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
        if (i == buttonCount - 1)
        {
            if (now - buttons[buttonCount - 1].lastPress > 500)
            {
                buttons[buttonCount - 1].state = !buttons[buttonCount - 1].state;
            }
            else
            {
                buttons[buttonCount - 1].state = numSequences == buttonCount ? false : true;
                numSequences = numSequences == buttonCount ? buttonCount - 1 : buttonCount;
            }

            buttons[buttonCount - 1].lastPress = now;
            break;
        }
        else
        {
            if (buttons[i].lastValue == HIGH)
            {
                buttons[i].state = !buttons[i].state;
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

    if (now - lastBpmChange < 300)
    {
        return;
    }

    movingBpm[0] = movingBpm[1];
    movingBpm[1] = movingBpm[2];
    movingBpm[2] = nextValue;

    int threshold = 10;
    float movingAverage = (movingBpm[0] + movingBpm[1] + movingBpm[2]) / 3;

    if (nextValue > (movingAverage + threshold) || nextValue < (movingAverage - threshold))
    {
        bpm = nextValue;
    }

    sequenceTimer.detachInterrupt();
    sequenceTimer.attachInterruptInterval_MS(60 * 1000 / scale(bpm, voltageRange, bpmRange) / 4, playSequence);

    lastBpmChange = now;
}

void updateDisplay()
{
    // TODO: Update this to handle up to 9 beats
    long now = millis();
    int heldButtonIndex = getHeldButton(now);

    for (int i = 0; i < 5; ++i)
    {
        if (i == 2)
        {
            displayMatrix.drawColon(showingPixel);
        }
        else
        {
            int sequenceIndex = i > 2 ? i - 1 : i;

            NoteMap noteMap = getNoteFromVoltage(scale(buttons[sequenceIndex].cv, voltageRange, cvRange));
            char displayChar = sequenceIndex + 1 != currentBeat ? '_' : noteMap.note;
            bool isSharp = noteMap.isSharp;

            if (currentBeat == sequenceIndex + 1 && buttons[sequenceIndex].state == false)
            {
                displayChar = '-';
            }

            if (currentBeat == 5)
            {
                NoteMap noteMap5 = getNoteFromVoltage(scale(buttons[4].cv, voltageRange, cvRange));
                displayChar = noteMap5.note;
                isSharp = noteMap5.isSharp;
            }

            if (heldButtonIndex > -1)
            {
                NoteMap noteMapHeld = getNoteFromVoltage(scale(buttons[heldButtonIndex].cv, voltageRange, cvRange));
                displayChar = noteMapHeld.note;
                isSharp = noteMapHeld.isSharp;
            }

            displayMatrix.writeDigitAscii(i, displayChar, isSharp);
        }
    }

    displayMatrix.writeDisplay();
}