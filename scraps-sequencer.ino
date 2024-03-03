#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>

#include "SAMDTimerInterrupt.h"
#include <Adafruit_DotStar.h>
#include "NoteTable.h"

// Timer config
#define USING_TIMER_TC3 true
#define SELECTED_TIMER TIMER_TC3

SAMDTimer ITimer(SELECTED_TIMER);

// DotStar config
#define NUM_LEDS 1
#define DATAPIN 8
#define CLOCKPIN 6

Adafruit_DotStar strip(NUM_LEDS, DATAPIN, CLOCKPIN, DOTSTAR_BRG);

Adafruit_7segment displayMatrix = Adafruit_7segment();

const int DAC_OUTPUT_PIN = A0;
const int POTENTIOMETER_PIN = A5;
const int BPM_POTENTIOMETER_PIN = A4;
const int BUTTON_1_PIN = 7;
const int BUTTON_2_PIN = 9;
const int BUTTON_3_PIN = 10;
const int BUTTON_4_PIN = 11;
const int BUTTON_5_PIN = 12;

const int BUTTON_PINS[] = {
    BUTTON_1_PIN,
    BUTTON_2_PIN,
    BUTTON_3_PIN,
    BUTTON_4_PIN,
    BUTTON_5_PIN};

const int numButtons = 5;
bool buttonStates[numButtons] = {true, true, true, true, true};
long buttonBeginHighStates[numButtons] = {0, 0, 0, 0, 0};
int buttonValues[numButtons] = {LOW, LOW, LOW, LOW, LOW};
int buttonLastValues[numButtons] = {LOW, LOW, LOW, LOW, LOW};

// Constants
#define ANALOG_HIGH 1023.0
#define DAC_HIGH 4095.0

const double dacRange[2] = {0, DAC_HIGH};
const double voltageRange[2] = {0, ANALOG_HIGH};
const double bpmRange[2] = {60, 600};
const double cvRange[2] = {0, 6};

uint32_t potentiometerValue;
uint32_t lastPotentiometerValue;
uint32_t bpmPotentiometerValue;
uint32_t lastBpmPotentiometerValue;
long button5LastPress;

int bpm;
int currentBeat = 1;
int numSequences = 5;
uint32_t sequences[5] = {0.0, 0.0, 0.0, 0.0, 0.0};

bool showingPixel = false;

unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 100;

void setup()
{
    bpm = scale(120, bpmRange, voltageRange);
    fillNoteMap();

    pinMode(BUTTON_1_PIN, INPUT_PULLDOWN);
    pinMode(BUTTON_2_PIN, INPUT_PULLDOWN);
    pinMode(BUTTON_3_PIN, INPUT_PULLDOWN);
    pinMode(BUTTON_4_PIN, INPUT_PULLDOWN);
    pinMode(BUTTON_5_PIN, INPUT_PULLDOWN);

    ITimer.attachInterruptInterval_MS(60 * 1000 / scale(bpm, voltageRange, bpmRange) / 4, playSequence);

    strip.begin();
    strip.show();

    displayMatrix.begin(0x70);

    Serial.begin(9600);
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

        potentiometerValue = analogRead(POTENTIOMETER_PIN);
        bpmPotentiometerValue = analogRead(BPM_POTENTIOMETER_PIN);

        if (lastBpmPotentiometerValue != bpmPotentiometerValue)
        {
            handleBpmPotentiometerChange(bpmPotentiometerValue);
        }

        if (getHeldButton(now) > -1)
        {
            handlePotentiometerChange(potentiometerValue);
        }

        if (wasAnyButtonPressed(now))
        {
            handleButtonChange(now);
        }

        // Serial.println(scale(bpm, voltageRange, bpmRange));
        // Serial.println(currentBeat);

        for (int i = 0; i < numButtons; ++i)
        {
            Serial.println(buttonStates[i]);
            Serial.println(sequences[i]);
        }
    }
}

float scale(float value, const double inputRange[2], const double outputRange[2])
{
    return (value - inputRange[0]) * (outputRange[1] - outputRange[0]) / (inputRange[1] - inputRange[0]) + outputRange[0];
}

bool shouldSkipSequence = false;

void playSequence()
{
    if (!shouldSkipSequence)
    {
        shouldSkipSequence = true;
    }
    else
    {
        shouldSkipSequence = false;
        return;
    }

    updateDisplay();

    strip.setPixelColor(0, strip.Color(0, 0, !showingPixel ? 64 : 0));
    strip.show();

    showingPixel = !showingPixel;

    for (int i = 0; i < numButtons; ++i)
    {
        if (currentBeat == i + 1 && buttonStates[i] == true)
        {
            analogWrite(DAC_OUTPUT_PIN, scale(sequences[i], voltageRange, dacRange));
            break;
        }
    }

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

    for (int i = 0; i < numButtons; ++i)
    {
        if (buttonValues[i] == HIGH && now - buttonBeginHighStates[i] > 500)
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

    for (int i = 0; i < numButtons; ++i)
    {
        if (buttonValues[i] == LOW && buttonLastValues[i] == HIGH && now - buttonBeginHighStates[i] < 500)
        {
            _wasAnyButtonPressed = true;
            break;
        }
    }

    return _wasAnyButtonPressed;
}

void updateButtonState(long now)
{
    for (int i = 0; i < numButtons; ++i)
    {
        buttonLastValues[i] = buttonValues[i];
        buttonValues[i] = digitalRead(BUTTON_PINS[i]);

        if (buttonValues[i] == HIGH && buttonLastValues[i] == LOW)
            buttonBeginHighStates[i] = now;
    }
}

void handlePotentiometerChange(float nextValue)
{
    for (int i = 0; i < numButtons; ++i)
    {
        if (digitalRead(BUTTON_PINS[i]) == HIGH)
        {
            sequences[i] = nextValue;
            break; // Exit loop after setting the sequence value
        }
    }
}

void handleButtonChange(long now)
{
    for (int i = 0; i < numButtons; ++i)
    {
        if (i == 4)
        {
            if (now - button5LastPress > 500)
            {
                buttonStates[4] = !buttonStates[4];
            }
            else
            {
                buttonStates[4] = numSequences == 5 ? false : true;
                numSequences = numSequences == 5 ? 4 : 5;
            }

            button5LastPress = now;
            break;
        }
        else
        {
            if (buttonLastValues[i] == HIGH)
            {
                buttonStates[i] = !buttonStates[i];
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

    ITimer.detachInterrupt();
    ITimer.attachInterruptInterval_MS(60 * 1000 / scale(bpm, voltageRange, bpmRange) / 4, playSequence);

    lastBpmChange = now;
}

void updateDisplay()
{
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

            NoteMap noteMap = getNoteFromVoltage(scale(sequences[sequenceIndex], voltageRange, cvRange));
            char displayChar = sequenceIndex + 1 != currentBeat ? '_' : noteMap.note;
            bool isSharp = noteMap.isSharp;

            if (currentBeat == sequenceIndex + 1 && buttonStates[sequenceIndex] == false)
            {
                displayChar = '-';
            }

            if (currentBeat == 5)
            {
                NoteMap noteMap5 = getNoteFromVoltage(scale(sequences[4], voltageRange, cvRange));
                displayChar = noteMap5.note;
                isSharp = noteMap5.isSharp;
            }

            if (heldButtonIndex > -1)
            {
                NoteMap noteMapHeld = getNoteFromVoltage(scale(sequences[heldButtonIndex], voltageRange, cvRange));
                displayChar = noteMapHeld.note;
                isSharp = noteMapHeld.isSharp;
            }

            displayMatrix.writeDigitAscii(i, displayChar, isSharp);
        }
    }

    displayMatrix.writeDisplay();
}