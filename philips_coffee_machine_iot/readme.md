# Philips Coffee Machine AWS IoT Controller

This project allows you to control a Philips EP2220/EP2235/EP3243 coffee machine using AWS IoT Core with custom MQTT topics. It replaces the ESPHome-based implementation with a direct AWS IoT Core integration.

## Features

- Full control of Philips coffee machine functions via AWS IoT Core
- Status reporting to AWS IoT using custom topics
- Support for Philips EP2220 coffee machine
- Remote power control and button actions
- Beverage settings control
- Real-time status updates

## Hardware Requirements

- ESP32 development board
- Philips EP2220 coffee machine
- Connections to the coffee machine UART interfaces (display and mainboard)
- Power control circuit for display power cycling

## Setup Instructions

### 1. AWS IoT Core Setup

1. Create an AWS IoT Core Thing for your coffee machine
2. Generate certificates for your Thing
3. Create a policy granting the necessary permissions:
   - `iot:Connect`
   - `iot:Subscribe` on topics: `backendToPhilipsep2220/#`
   - `iot:Publish` on topics: `philipsep2220/#`
4. Attach the policy to your Thing's certificate

### 2. Update WiFi and AWS IoT Configuration

Edit the `src/main.cpp` file to include your:

- WiFi credentials (WIFI_SSID and WIFI_PASSWORD)
- AWS IoT endpoint (AWS_IOT_ENDPOINT)

### 3. Update Certificates

Edit the `include/certificates.h` file to include your:

- AWS IoT Root CA certificate (already included)
- Thing certificate
- Thing private key

You can use the `tools/update_certificates.py` script to automate this.

### 4. Build and Flash

1. Install PlatformIO (if not already installed)
2. Clone this repository
3. Open the project in PlatformIO
4. Build and upload to your ESP32 device

## MQTT Topic Structure

### Status Topics (published by the device)

| Topic                              | Description                   | Payload                           |
| ---------------------------------- | ----------------------------- | --------------------------------- |
| `philipsep2220/waterIsEmpty`       | Water tank status             | `"true"` or `"false"`             |
| `philipsep2220/wasteIsFull`        | Waste container status        | `"true"` or `"false"`             |
| `philipsep2220/opState`            | Operation state               | Text description of current state |
| `philipsep2220/selectedOption`     | Currently selected beverage   | `"COFFEE"`, `"ESPRESSO"`, etc.    |
| `philipsep2220/selectedBeanAmount` | Current bean strength setting | `"1"`, `"2"`, or `"3"`            |
| `philipsep2220/selectedCupSize`    | Current cup size setting      | `"1"`, `"2"`, or `"3"`            |
| `philipsep2220/brewedType`         | Currently brewing beverage    | `"COFFEE"`, `"ESPRESSO"`, etc.    |

### Command Topics (subscribed by the device)

| Topic                                       | Description               | Payload                                                |
| ------------------------------------------- | ------------------------- | ------------------------------------------------------ |
| `backendToPhilipsep2220/turnPrePowerOn`     | Send pre-power on command | (empty)                                                |
| `backendToPhilipsep2220/turnPowerOn`        | Turn machine on           | `{"cleaning": true/false}` (optional)                  |
| `backendToPhilipsep2220/turnPowerOff`       | Turn machine off          | (empty)                                                |
| `backendToPhilipsep2220/resetCoffeeMachine` | Reset coffee machine      | (empty)                                                |
| `backendToPhilipsep2220/pressPlayOrPause`   | Press play/pause button   | (empty)                                                |
| `backendToPhilipsep2220/selectType`         | Select beverage type      | `{"type": "COFFEE", "make": true/false}`               |
| `backendToPhilipsep2220/selectBean`         | Set bean strength         | `{"value": 1-3, "source": "COFFEE"}` (source optional) |
| `backendToPhilipsep2220/selectCupSize`      | Set cup size              | `{"value": 1-3, "source": "COFFEE"}` (source optional) |
| `backendToPhilipsep2220/selectCalcClean`    | Press calc clean button   | (empty)                                                |
| `backendToPhilipsep2220/selectAquaClean`    | Press aqua clean button   | (empty)                                                |

## Command Examples

### Turn on coffee machine

```
Topic: backendToPhilipsep2220/turnPowerOn
Payload: {"cleaning": true}
```

### Turn off coffee machine

```
Topic: backendToPhilipsep2220/turnPowerOff
Payload: (empty)
```

### Select coffee and brew

```
Topic: backendToPhilipsep2220/selectType
Payload: {"type": "COFFEE", "make": true}
```

### Set bean strength

```
Topic: backendToPhilipsep2220/selectBean
Payload: {"value": 2}
```

### Set cup size for specific beverage

```
Topic: backendToPhilipsep2220/selectCupSize
Payload: {"value": 3, "source": "ESPRESSO"}
```

## Wiring

Connect your ESP32 to the coffee machine as follows:

- ESP32 GPIO16 -> Display UART RX
- ESP32 GPIO17 -> Display UART TX
- ESP32 GPIO18 -> Mainboard UART RX
- ESP32 GPIO19 -> Mainboard UART TX
- ESP32 GPIO21 -> Power control pin (connect to transistor/relay to control display power)

## Customization

### Language

You can change the language of status messages by editing `platformio.ini`:

```ini
build_flags =
    -D PHILIPS_COFFEE_LANG_en_US  # Available options: en_US, de_DE, it_IT, hu_HU
```

## Troubleshooting

- If unable to connect to AWS IoT, double-check your certificates and endpoint
- Verify the device is showing up in the AWS IoT Console
- Check the device logs via the serial monitor at 115200 baud
- If the coffee machine is not responding, verify your wiring and UART connections

## Credits

This project is based on the ESPHome Philips coffee machine component, converted to work directly with AWS IoT Core.

## License

This project is available under the MIT License.
