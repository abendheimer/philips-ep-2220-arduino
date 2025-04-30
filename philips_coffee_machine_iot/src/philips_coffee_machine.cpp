#include "philips_coffee_machine.h"
#include <SoftwareSerial.h>
#include "config.h"
#include <HardwareSerial.h>



PhilipsCoffeeMachine::PhilipsCoffeeMachine() 
    : displayUart(DISPLAY_UART_RX_PIN, DISPLAY_UART_TX_PIN),
      mainboardUart(Serial),
      powerPin(POWER_PIN),
      invertPowerPin(INVERT_POWER_PIN),
      powerTripDelay(POWER_TRIP_DELAY),
      shouldPowerTrip(false),
      lastPowerTrip(0),
      powerTripCount(0),
      powerMessageRepetitions(25),
      initialPinState(!invertPowerPin),
      lastMessageFromMainboardTime(0),
      lastMessageFromDisplayTime(0),
    currentStatus(String(state_unknown.c_str())),
      poweredOn(false),
      isLongPressing(false),
      shouldLongPress(false),
      pressStart(0),
      lastMessageSent(0),
      currentAction(PLAY_PAUSE),
      currentSource(NONE_SOURCE),
      playPauseLed(false),
      playPauseLastChange(0),
      showSizeLedLastChange(0),
      showSizeLedLastState(false),
      newStateCounter(0),
      newState(String(state_unknown.c_str()))
      
{
    // Initialize beverage settings array to default values (-1 = not set)
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 7; j++) {
            beverageSettings[i][j] = -1;
        }
    }
    
    // Initialize the last checksum
    lastMainboardMessageChecksum[0] = 0;
    lastMainboardMessageChecksum[1] = 0;
}

void PhilipsCoffeeMachine::begin() {
  Serial.begin(115200);
  Serial.println("Starting Philips Coffee Machine controller...");
  
  // Complete the initialization without using .begin()
  // Configure power pin
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN, initialPinState);
  
  // Initialize UART manually - this avoids the crash in begin()
  Serial.println("UART initialization...");
  SoftwareSerial displayUart(DISPLAY_UART_RX_PIN, DISPLAY_UART_TX_PIN);
  SoftwareSerial mainboardUart(MAINBOARD_UART_RX_PIN, MAINBOARD_UART_TX_PIN);
  
  displayUart.begin(UART_BAUD_RATE);
  Serial.println("DisplayUart Begin initialization...");
  delay(100);
  Serial.println("Mainbaord Begin initialization...");
  delay(100);
  
  // Clear any pending data
  while (displayUart.available()) displayUart.read();
  while (mainboardUart.available()) mainboardUart.read();
  
  Serial.println("PhilipsCoffeeMachine initialized");
}

void PhilipsCoffeeMachine::loop() {
    uint8_t displayBuffer[DISPLAY_BUFFER_SIZE];
    uint8_t mainboardBuffer[MAINBOARD_BUFFER_SIZE];

    Serial.println("loooping!!!!"); // Neue Zeile am Ende


    // Pipe display to mainboard
    if (displayUart.available()) {
        uint8_t size = min((int)displayUart.available(), DISPLAY_BUFFER_SIZE);
        displayUart.readBytes(displayBuffer, size);
        
        // Only forward if not currently long pressing a button
        if (!isLongPressing) {
            mainboardUart.write(displayBuffer, size);
        }
        
        lastMessageFromDisplayTime = millis();
    }
    
    // Read and forward until valid start bytes have been received
    while (mainboardUart.available()) {

        Serial.println("MainboardUart availabe....");
        mainboardBuffer[0] = mainboardUart.read();
        displayUart.write(mainboardBuffer[0]);
        if (mainboardBuffer[0] == message_header[0]) {
            mainboardBuffer[1] = mainboardUart.read();
            displayUart.write(mainboardBuffer[1]);
            if (mainboardBuffer[1] == message_header[1]) {
                break;
            }
        }
    }

    if (mainboardUart.available()) {
        Serial.println("MainboardUart availabe....");

        uint8_t size = min((int)mainboardUart.available(), MAINBOARD_BUFFER_SIZE - 2);
        mainboardUart.readBytes(mainboardBuffer + 2, size);
        
        displayUart.write(mainboardBuffer + 2, size);
        
        if (size >= MAINBOARD_BUFFER_SIZE - 2) {
            // Only process messages starting with start bytes
            // Only process duplicate messages (crude checksum alternative)
            if (mainboardBuffer[0] == message_header[0] &&
                mainboardBuffer[1] == message_header[1] &&
                memcmp(mainboardBuffer + 17, lastMainboardMessageChecksum, 2) == 0) {
                
                lastMessageFromMainboardTime = millis();
                
                // Update status
                updateStatus(mainboardBuffer);
            }
            
            // Retain last checksum for comparison with next checksum
            memcpy(lastMainboardMessageChecksum, mainboardBuffer + 17, 2);
        }
    }
    
    // Process power trip if needed
    if (shouldPowerTrip && millis() - lastPowerTrip > powerTripDelay + POWER_TRIP_RETRY_DELAY) {
        if (powerTripCount >= MAX_POWER_TRIP_COUNT) {
            shouldPowerTrip = false;
            Serial.println("Power tripping display failed!");
            return;
        }
        
        // Perform power trip (invert state twice)
        digitalWrite(powerPin, !initialPinState);
        delay(powerTripDelay);
        digitalWrite(powerPin, initialPinState);
        
        lastPowerTrip = millis();
        powerTripCount++;
        
        // If machine is now powered on, update state
        if (poweredOn) {
            Serial.printf("Performed %d power trip(s).\n", powerTripCount);
            shouldPowerTrip = false;
            powerTripCount = 0;
        }
    }
    
    // Process long press button if active
    if (shouldLongPress && millis() - pressStart <= LONG_PRESS_DURATION) {
        if (millis() - lastMessageSent > LONG_PRESS_REPETITION_DELAY) {
            lastMessageSent = millis();
            performButtonAction(currentAction);
        }
        isLongPressing = true;
    } else {
        isLongPressing = false;
    }
    
    // Pipe to display and process data
    
    
    // Update power state based on communication activity
    if (millis() - lastMessageFromDisplayTime > POWER_STATE_TIMEOUT) {
        if (poweredOn) {
            poweredOn = false;
            currentStatus = state_off.c_str();
            currentSource = NONE_SOURCE;
        }
    } else {
        if (!poweredOn) {
            poweredOn = true;
        }
    }
    
    // Flush UARTs
    displayUart.flush();
    mainboardUart.flush();
}

void PhilipsCoffeeMachine::powerOn(bool withCleaning) {
    // Send pre-power on message
    sendPrePowerOn();

    for (unsigned int i = 0; i <= powerMessageRepetitions; i++) {
        mainboardUart.write(command_pre_power_on.data(), command_pre_power_on.size());
    }
    
    // Send power on message
    if (withCleaning) {
        // Send power on command with cleaning
        for (unsigned int i = 0; i <= powerMessageRepetitions; i++) {
            mainboardUart.write(command_power_with_cleaning.data(), command_power_with_cleaning.size());
            Serial.print("Power on command sent (with cleaning): ");
            for (uint8_t byte : command_power_with_cleaning) {
                Serial.printf("%02X ", byte); // Zweistellige Hex-Ausgabe mit führender Null
            }
            Serial.println(); // Neue Zeile am Ende
        }
    } else {
        // Send power on command without cleaning
        for (unsigned int i = 0; i <= powerMessageRepetitions; i++) {
            mainboardUart.write(command_power_without_cleaning.data(), command_power_without_cleaning.size());
        }
            Serial.print("Power on command sent (without cleaning): ");
            for (uint8_t byte : command_power_without_cleaning) {
                Serial.printf("%02X ", byte); // Zweistellige Hex-Ausgabe mit führender Null
            }
            Serial.println(); // Neue Zeile am Ende
    }
    
    mainboardUart.flush();
    
    // Perform power trip in component loop
    shouldPowerTrip = true;
    powerTripCount = 0;
}

void PhilipsCoffeeMachine::powerOff() {
    // Send power off message
    for (unsigned int i = 0; i <= powerMessageRepetitions; i++) {
        mainboardUart.write(command_power_off.data(), command_power_off.size());
    }
    mainboardUart.flush();
}

bool PhilipsCoffeeMachine::isPoweredOn() {
    return poweredOn;
}

void PhilipsCoffeeMachine::executeButtonAction(Action action, bool longPress) {
    // Validate that long press only applies to select options
    if (longPress && 
        (action == MAKE_COFFEE || action == MAKE_ESPRESSO || action == MAKE_HOT_WATER )) {
        Serial.printf("Action %d does not support long press.\n", action);
        return;
    }
    
    currentAction = action;
    shouldLongPress = longPress;
    
    if (longPress) {
        // Reset button press start time
        pressStart = millis();
        lastMessageSent = 0;
    } else {
        // Perform a single button press
        performButtonAction(action);
    }
}

void PhilipsCoffeeMachine::performButtonAction(Action action) {
    // Coffee
    if (action == SELECT_COFFEE || action == MAKE_COFFEE) {
        writeArray(command_press_1);
        if (action == SELECT_COFFEE)
            return;

        delay(BUTTON_SEQUENCE_DELAY);
        action = PLAY_PAUSE;
    }

    // Espresso
    if (action == SELECT_ESPRESSO || action == MAKE_ESPRESSO) {
        writeArray(command_press_2);
        if (action == SELECT_ESPRESSO)
            return;
        delay(BUTTON_SEQUENCE_DELAY);
        action = PLAY_PAUSE;
    }

    // Hot water
    if (action == SELECT_HOT_WATER || action == MAKE_HOT_WATER) {
        writeArray(command_press_3);
        if (action == SELECT_HOT_WATER)
            return;
        delay(BUTTON_SEQUENCE_DELAY);
        action = PLAY_PAUSE;
    }

#ifdef PHILIPS_EP2220
    // Steam
    if (action == SELECT_STEAM || action == MAKE_STEAM) {
        writeArray(command_press_4);
        if (action == SELECT_STEAM)
            return;
        delay(BUTTON_SEQUENCE_DELAY);
        action = PLAY_PAUSE;
    }
#endif
#ifdef PHILIPS_EP2235
    // Cappuccino
    if (action == SELECT_CAPPUCCINO || action == MAKE_CAPPUCCINO) {
        writeArray(command_press_4);
        if (action == SELECT_CAPPUCCINO)
            return;
        delay(BUTTON_SEQUENCE_DELAY);
        action = PLAY_PAUSE;
    }
#endif
#ifdef PHILIPS_EP3243
    // Latte
    if (action == SELECT_LATTE || action == MAKE_LATTE) {
        writeArray(command_press_4);
        if (action == SELECT_LATTE)
            return;
        delay(BUTTON_SEQUENCE_DELAY);
        action = PLAY_PAUSE;
    }

    // Americano
    if (action == SELECT_AMERICANO || action == MAKE_AMERICANO) {
        writeArray(command_press_5);
        if (action == SELECT_AMERICANO)
            return;
        delay(BUTTON_SEQUENCE_DELAY);
        action = PLAY_PAUSE;
    }

    // Cappuccino
    if (action == SELECT_CAPPUCCINO || action == MAKE_CAPPUCCINO) {
        writeArray(command_press_6);
        if (action == SELECT_CAPPUCCINO)
            return;
        delay(BUTTON_SEQUENCE_DELAY);
        action = PLAY_PAUSE;
    }
#endif
    // press/play or subsequent press/play
    if (action == PLAY_PAUSE)
        writeArray(command_press_play_pause);
    else if (action == SELECT_BEAN)
        // bean button
        writeArray(command_press_bean);
    else if (action == SELECT_SIZE)
        // size button
        writeArray(command_press_size);
#if defined(PHILIPS_EP3243)
    else if (action == SELECT_MILK)
        // milk button
        writeArray(command_press_milk);
#endif
    else if (action == SELECT_AQUA_CLEAN)
        // aqua clean button
        writeArray(command_press_aqua_clean);
    else if (action == SELECT_CALC_CLEAN)
        // calc clean button
        writeArray(command_press_calc_clean);
    else
        Serial.println("Invalid Action provided!");
}

void PhilipsCoffeeMachine::writeArray(const std::vector<uint8_t> &data) {
    for (unsigned int i = 0; i <= MESSAGE_REPETITIONS; i++) {
        mainboardUart.write(data.data(), data.size());
    }
    mainboardUart.flush();
}

void PhilipsCoffeeMachine::setBeverageSetting(BeverageSettingType type, BeverageSource source, int value) {
    // Validate input
    if (value < 1 || value > 3) {
        Serial.println("Invalid setting value. Must be 1-3.");
        return;
    }
    
    if (!validateSettingForSource(type, source)) {
        Serial.println("Invalid setting type for source");
        return;
    }
    
    // Store the setting value
    beverageSettings[type][source] = value;
    
    // If the machine is currently displaying this beverage type, apply the setting
    if (poweredOn && (source == currentSource || source == ANY_SOURCE)) {
        // Press the appropriate button until we reach the desired value
        int currentVal = getBeverageSetting(type, source);
        
        if (currentVal != value) {
            // Press button to change setting
            for (int i = 0; i < 3; i++) { // Max 3 presses to cycle through all options
                Action buttonAction;
                
                switch (type) {
                    case BEAN_TYPE:
                        buttonAction = SELECT_BEAN;
                        break;
                    case SIZE_TYPE:
                        buttonAction = SELECT_SIZE;
                        break;
                    case MILK_TYPE:
                        buttonAction = SELECT_MILK;
                        break;
                    default:
                        return;
                }
                
                executeButtonAction(buttonAction);
                delay(SETTINGS_BUTTON_SEQUENCE_DELAY);
                
                // Check if we've reached the target value
                if (getBeverageSetting(type, source) == value) {
                    break;
                }
            }
        }
    }
}

int PhilipsCoffeeMachine::getBeverageSetting(BeverageSettingType type, BeverageSource source) {
    // Return the stored setting if available
    if (source != NONE_SOURCE && type >= 0 && type < 3 && source >= 0 && source < 7) {
        return beverageSettings[type][source];
    }
    
    return -1; // Not set
}

String PhilipsCoffeeMachine::getStatusString() {
    return currentStatus;
}

BeverageSource PhilipsCoffeeMachine::getCurrentBeverageSource() {
    return currentSource;
}

void PhilipsCoffeeMachine::updateStatus(uint8_t *data) {
    // Check if the play/pause button is on/off/blinking
    if ((data[16] == led_on) != playPauseLed) {
        playPauseLastChange = millis();
    }
    playPauseLed = data[16] == led_on;
    
    if ((data[11] == led_on) != showSizeLedLastState) {
        showSizeLedLastChange = millis();
    }
    showSizeLedLastState = data[11] == led_on;
    
    String status = state_unknown.c_str();
    
    // Check for idle state (selection led on)
#ifdef PHILIPS_EP3243
    if (data[3] == led_on && data[4] == led_on && data[5] == led_on && data[13] == led_off && data[14] == led_off && data[15] == led_off) {
#else
    if (data[3] == led_on && data[4] == led_on && data[5] == led_on && data[6] != led_off) {
#endif
        // selecting a beverage can result in a short "busy" period since the play/pause button has not been blinking
        // This can be circumvented: if the user is on the selection screen/idle we can reset the timer
        playPauseLastChange = millis();
        
        status = state_idle.c_str();
    } else {
        bool isPlayPauseBlinking = millis() - playPauseLastChange < BLINK_THRESHOLD;
        bool showSizeChangedRecently = showSizeLedLastChange < BLINK_THRESHOLD;
        
        // Check for rotating icons - pre heating
        if (data[3] == led_half || data[4] == led_half || data[5] == led_half || data[6] == led_half) {
            if (playPauseLed)
                status = state_cleaning.c_str();
            else
                status = state_preparing.c_str();
        }
        
        // 3 warning lights indicate an internal error (i.e. overheating)
        else if (data[15] != led_off && data[14] == led_second) {
            status = state_internal_error.c_str();
        }
        
        // Warning/Error led
        else if (data[15] == led_second) {
            status = state_error.c_str();
        }
        
        // Water empty led
        else if (data[14] == led_second) {
            status = state_water_empty.c_str();
        }
        
        // Waste container led
        else if (data[15] == led_on) {
            status = state_waste_warning.c_str();
        }
        
        // Coffee selected
        else if (data[3] == led_off && data[4] == led_off && (data[5] == led_on || data[5] == led_second) && data[6] == led_off) {
            if (isPlayPauseBlinking) {
                if (data[9] == led_second) {
                    status = state_ground_coffee_selected.c_str();
                } else if (data[11] == led_off && showSizeChangedRecently) {
                    status = state_coffee_programming_mode.c_str();
                } else {
                    status = (data[5] == led_on) ? state_coffee_selected.c_str() : state_coffee_2x_selected.c_str();
                }
            } else {
                status = (data[5] == led_on) ? state_coffee_brewing.c_str() : state_coffee_2x_brewing.c_str();
            }
        }
        
        // Steam selected
        else if (data[3] == led_off && data[4] == led_off && data[5] == led_off && (data[6] == led_on || data[6] == led_third)) {
#ifdef PHILIPS_EP2235
            if (isPlayPauseBlinking) {
                if (data[9] == led_second) {
                    status = state_ground_cappuccino_selected;
                } else if (data[11] == led_off && showSizeChangedRecently) {
                    status = state_cappuccino_programming_mode;
                } else {
                    status = state_cappuccino_selected;
                }
            } else {
                status = state_cappuccino_brewing;
            }
#elif defined(PHILIPS_EP3243)
            if (isPlayPauseBlinking) {
                if (data[11] == led_off && showSizeChangedRecently) {
                    status = state_latte_programming_mode;
                } else {
                    status = data[9] == led_second ? state_ground_latte_selected : state_latte_selected;
                }
            } else {
                status = state_latte_brewing;
            }
#else
               if (isPlayPauseBlinking) {
        status = String(state_steam_selected.c_str());
    } else {
        status = String(state_steam_brewing.c_str());
    }
#endif
        }
        
        // Hot water selected
#ifdef PHILIPS_EP3243
        else if (data[3] == led_off && data[4] == led_off && data[5] == led_off && data[6] == led_off && data[7] == led_second) {
#else
        else if (data[3] == led_off && data[4] == led_on && data[5] == led_off && data[6] == led_off) {
#endif
            if (isPlayPauseBlinking) {
                if (data[11] == led_off && showSizeChangedRecently) {
                    status = state_hot_water_programming_mode.c_str();
                } else {
                    status = state_hot_water_selected.c_str();
                }
            } else {
                status = state_hot_water_brewing.c_str();
            }
        }
        
        // Espresso selected
        else if ((data[3] == led_on || data[3] == led_second) && data[4] == led_off && data[5] == led_off && data[6] == led_off) {
            if (isPlayPauseBlinking) {
                if (data[9] == led_second) {
                    status = state_ground_espresso_selected.c_str();
                } else if (data[11] == led_off && showSizeChangedRecently) {
                    status = state_espresso_programming_mode.c_str();
                } else {
                    status = (data[3] == led_on) ? state_espresso_selected.c_str() : state_espresso_2x_selected.c_str();
                }
            } else {
                status = (data[3] == led_on) ? state_espresso_brewing.c_str() : state_espresso_2x_brewing.c_str();
            }
        }
        
#ifdef PHILIPS_EP3243
        // Cappuccino selected
        else if (data[3] == led_off && data[4] == led_on && data[5] == led_off && data[6] == led_off) {
            if (isPlayPauseBlinking) {
                if (data[11] == led_off && showSizeChangedRecently) {
                    status = state_cappuccino_programming_mode;
                } else {
                    status = data[9] == led_second ? state_ground_cappuccino_selected : state_cappuccino_selected;
                }
            } else {
                status = state_cappuccino_brewing;
            }
        }
        
        // Americano selected
        else if (data[3] == led_off && data[4] == led_off && data[5] == led_off && (data[6] == led_second || data[7] == led_on)) {
            if (isPlayPauseBlinking) {
                if (data[9] == led_second) {
                    status = state_ground_americano_selected;
                } else if (data[11] == led_off && showSizeChangedRecently) {
                    status = state_americano_programming_mode;
                } else {
                    status = (data[6] == led_second) ? state_americano_selected : state_americano_2x_selected;
                }
            } else {
                status = (data[6] == led_second) ? state_americano_brewing : state_americano_2x_brewing;
            }
        }
#endif
    }
    
    // Update status if it has been repeated enough times
    if (status == newState) {
        if (newStateCounter >= REPEAT_REQUIREMENT) {
            if (currentStatus != status) {
                currentStatus = status;
                // Update the current beverage source based on status
                currentSource = determineBeverageSource(status);
            }
        } else {
            newStateCounter++;
        }
    } else {
        newStateCounter = 0;
        newState = status;
    }
    
    // Check bean and size settings LEDs
    if (poweredOn && currentSource != NONE_SOURCE) {
        // Bean setting
        if (data[9] == led_on) {
            uint8_t enableByte = 9;
            uint8_t amountByte = 8;
            
            if (data[enableByte] == led_on) {
                int value = 1; // Default to small
                
                switch (data[amountByte]) {
                    case led_off:
                        value = 1;
                        break;
                    case led_second:
                        value = 2;
                        break;
                    case led_third:
                        value = 3;
                        break;
                }
                
                // Update the stored setting
                beverageSettings[BEAN_TYPE][currentSource] = value;
            }
        }
        
        // Size setting
        if (data[11] == led_on) {
            uint8_t enableByte = 11;
            uint8_t amountByte = 10;
            
            if (data[enableByte] == led_on) {
                int value = 1; // Default to small
                
                switch (data[amountByte]) {
                    case led_off:
                        value = 1;
                        break;
                    case led_second:
                        value = 2;
                        break;
                    case led_third:
                        value = 3;
                        break;
                }
                
                // Update the stored setting
                beverageSettings[SIZE_TYPE][currentSource] = value;
            }
        }
        
        // Milk setting (only on EP3243)
#ifdef PHILIPS_EP3243
        if (data[13] == led_on) {
            uint8_t enableByte = 13;
            uint8_t amountByte = 12;
            
            if (data[enableByte] == led_on) {
                int value = 1; // Default to small
                
                switch (data[amountByte]) {
                    case led_off:
                        value = 1;
                        break;
                    case led_second:
                        value = 2;
                        break;
                    case led_third:
                        value = 3;
                        break;
                }
                
                // Update the stored setting
                beverageSettings[MILK_TYPE][currentSource] = value;
            }
        }
#endif
    }
}

BeverageSource PhilipsCoffeeMachine::determineBeverageSource(String status) {
    if (status == state_coffee_selected.c_str() || status == state_coffee_2x_selected.c_str() ||
        status == state_coffee_brewing.c_str() || status == state_coffee_2x_brewing.c_str() ||
        status == state_ground_coffee_selected.c_str() || status == state_coffee_programming_mode.c_str()) {
        return COFFEE_SOURCE;
    }
    else if (status == state_espresso_selected.c_str() || status == state_espresso_2x_selected.c_str() ||
             status == state_espresso_brewing.c_str() || status == state_espresso_2x_brewing.c_str() ||
             status == state_ground_espresso_selected.c_str() || status == state_espresso_programming_mode.c_str()) {
        return ESPRESSO_SOURCE;
    }
    else if (status == state_hot_water_selected.c_str() || status == state_hot_water_brewing.c_str() ||
             status == state_hot_water_programming_mode.c_str()) {
        return HOT_WATER_SOURCE;
    }
    else if (status == state_americano_selected.c_str() || status == state_americano_2x_selected.c_str() ||
             status == state_americano_brewing.c_str() || status == state_americano_2x_brewing.c_str() ||
             status == state_ground_americano_selected.c_str() || status == state_americano_programming_mode.c_str()) {
        return AMERICANO_SOURCE;
    }
    else if (status == state_cappuccino_selected.c_str() || status == state_cappuccino_brewing.c_str() ||
             status == state_ground_cappuccino_selected.c_str() || status == state_cappuccino_programming_mode.c_str()) {
        return CAPPUCCINO_SOURCE;
    }
    else if (status == state_latte_selected.c_str() || status == state_latte_brewing.c_str() ||
             status == state_ground_latte_selected.c_str() || status == state_latte_programming_mode.c_str()) {
        return LATTE_MACCHIATO_SOURCE;
    }
    else if (status == state_steam_selected.c_str() || status == state_steam_brewing.c_str()) {
        return ANY_SOURCE; // Steam is not specifically tied to a beverage
    }
    
    return NONE_SOURCE;
}

bool PhilipsCoffeeMachine::validateSettingForSource(BeverageSettingType type, BeverageSource source) {
    // Hot water doesn't have bean settings
    if (type == BEAN_TYPE && source == HOT_WATER_SOURCE) {
        return false;
    }
    
    // Milk settings only available for cappuccino and latte
    if (type == MILK_TYPE && 
        source != CAPPUCCINO_SOURCE && 
        source != LATTE_MACCHIATO_SOURCE && 
        source != ANY_SOURCE) {
        return false;
    }
    
    return true;
}

void PhilipsCoffeeMachine::sendPrePowerOn() {
    // Send only the pre-power on message
    for (unsigned int i = 0; i <= powerMessageRepetitions; i++) {
        mainboardUart.write(command_pre_power_on.data(), command_pre_power_on.size());
    }
    mainboardUart.flush();
    Serial.println("Pre-power on command executed");
}

