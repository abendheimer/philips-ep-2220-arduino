#ifndef PHILIPS_COFFEE_MACHINE_H
#define PHILIPS_COFFEE_MACHINE_H

#include <Arduino.h>
#include <SoftwareSerial.h>
#include "commands.h"
#include "localization.h"

// Constants
#define MAINBOARD_BUFFER_SIZE 19
#define DISPLAY_BUFFER_SIZE 12
#define POWER_STATE_TIMEOUT 500
#define MESSAGE_REPETITIONS 25
#define BUTTON_SEQUENCE_DELAY 100
#define LONG_PRESS_REPETITION_DELAY 50
#define LONG_PRESS_DURATION 3500
#define SETTINGS_BUTTON_SEQUENCE_DELAY 500
#define BLINK_THRESHOLD 750
#define REPEAT_REQUIREMENT 60
#define POWER_TRIP_RETRY_DELAY 1000
#define MAX_POWER_TRIP_COUNT 5

// Button actions
enum Action {
    SELECT_COFFEE = 0,
    MAKE_COFFEE,
    SELECT_ESPRESSO,
    MAKE_ESPRESSO,
    SELECT_HOT_WATER,
    MAKE_HOT_WATER,
    SELECT_STEAM,
    MAKE_STEAM,
    SELECT_CAPPUCCINO,
    MAKE_CAPPUCCINO,
    SELECT_LATTE,
    MAKE_LATTE,
    SELECT_AMERICANO,
    MAKE_AMERICANO,
    SELECT_BEAN,
    SELECT_SIZE,
    SELECT_MILK,
    SELECT_AQUA_CLEAN,
    SELECT_CALC_CLEAN,
    PLAY_PAUSE,
};

// Beverage setting types
enum BeverageSettingType {
    BEAN_TYPE,
    SIZE_TYPE,
    MILK_TYPE
};

// Beverage sources
enum BeverageSource {
    NONE_SOURCE = -1,
    ANY_SOURCE = 0,
    COFFEE_SOURCE,
    ESPRESSO_SOURCE,
    CAPPUCCINO_SOURCE,
    HOT_WATER_SOURCE,
    AMERICANO_SOURCE,
    LATTE_MACCHIATO_SOURCE
};

class PhilipsCoffeeMachine {
public:
    PhilipsCoffeeMachine();
    
    // Initialize the coffee machine
    void begin();
    
    // Main processing loop
    void loop();
    
    // Power control
    void sendPrePowerOn();
    void powerOn(bool withCleaning = true);
    void powerOff();
    bool isPoweredOn();
    
    // Button actions
    void executeButtonAction(Action action, bool longPress = false);
    
    // Beverage settings
    void setBeverageSetting(BeverageSettingType type, BeverageSource source, int value);
    int getBeverageSetting(BeverageSettingType type, BeverageSource source);
    
    // Status
    String getStatusString();
    BeverageSource getCurrentBeverageSource();
    
private:
    // UART communication
    SoftwareSerial displayUart;
    SoftwareSerial mainboardUart;
    
    // Power control
    uint8_t powerPin;
    bool invertPowerPin;
    uint32_t powerTripDelay;
    bool shouldPowerTrip;
    uint32_t lastPowerTrip;
    uint powerTripCount;
    uint powerMessageRepetitions;
    bool initialPinState;
    
    // Status tracking
    uint32_t lastMessageFromMainboardTime;
    uint32_t lastMessageFromDisplayTime;
    uint8_t lastMainboardMessageChecksum[2];
    String currentStatus;
    bool poweredOn;
    
    // Button and long press handling
    void performButtonAction(Action action);
    void writeArray(const std::vector<uint8_t> &data);
    bool isLongPressing;
    bool shouldLongPress;
    uint32_t pressStart;
    uint32_t lastMessageSent;
    Action currentAction;
    
    // Beverage settings tracking
    int beverageSettings[3][7]; // type, source
    BeverageSource currentSource;
    
    // Status parsing
    void updateStatus(uint8_t *data);
    bool playPauseLed;
    uint32_t playPauseLastChange;
    uint32_t showSizeLedLastChange;
    bool showSizeLedLastState;
    int newStateCounter;
    String newState;
    
    // Helper functions
    BeverageSource determineBeverageSource(String status);
    bool validateSettingForSource(BeverageSettingType type, BeverageSource source);
};

#endif // PHILIPS_COFFEE_MACHINE_H