# PKM JIG – Firmware Sequence

Firmware: `04_SourceCode/PKM_JIG` (STM32F103, USB CDC).
App: `05_Tool/PKM_JIG.py`.

## Participants

| Name      | Description                                                            |
|-----------|------------------------------------------------------------------------|
| User      | Operator pressing BTN2 / BTN3 on the JIG                               |
| App       | PC tool, talks to JIG over USB virtual COM port                        |
| USB ISR   | `CDC_Receive_FS` -> `USBRxHandler` -> `f_handle_app_cmd`               |
| Main loop | `while(1)` in `main()`: `f_manual_mode_process` / `f_app_mode_process` |
| LCD       | 20x4 character LCD                                                     |
| Outputs   | Power (`EN_12V_n`, `EN_24V_n`) and JTAG mux (`JTAG_EN_n`, `S0`, `S1`)  |
| ADC       | CH0 = voltage (divider x11), CH1 = current (ACS712 5A)                 |

## Protocol

App -> JIG: 1 byte per command.

| Byte | Command        | Accepted when |
|------|----------------|---------------|
| `c`  | Connect        | Always        |
| `d`  | Disconnect     | Always        |
| `1`  | Power OFF (0V) | App connected |
| `2`  | Power ON (12V) | App connected |
| `4`  | Signal ST uC   | App connected |
| `5`  | Signal BLE     | App connected |
| `6`  | Signal UWB     | App connected |
| `7`  | Signal Not use | App connected |

JIG -> App: one text line every 500 ms while App is connected.

```
V:12.34,I:1.23\r\n
```

`V` in volt, `I` in ampere, 2 decimals.

JTAG mux select:

| Signal  | S1 | S0 |
|---------|----|----|
| ST uC   | 0  | 0  |
| BLE     | 0  | 1  |
| UWB     | 1  | 0  |
| Not use | 1  | 1  |

## Mode overview

```mermaid
stateDiagram-v2
    [*] --> Manual : power on
    state "Manual mode" as Manual {
        [*] --> Voltage
        Voltage --> Signal : BTN2
        Signal --> Voltage : BTN2
    }
    state "App mode" as AppMode
    Manual --> AppMode : App sends 'c'
    AppMode --> Manual : App sends 'd'
```

## 1. Startup

```mermaid
sequenceDiagram
    participant Main as Main loop
    participant ADC
    participant LCD
    participant Out as Outputs

    Main->>ADC: HAL_ADCEx_Calibration_Start()
    Main->>LCD: CLCD_4BIT_Init()
    Note over Main: f_enter_voltage_screen()
    Main->>Out: Power OFF, ST selected, switch disabled
    Main->>LCD: Draw voltage screen
    Note over Main: Manual mode - Voltage screen
```

## 2. Manual mode – Voltage screen

```mermaid
sequenceDiagram
    actor User
    participant Main as Main loop
    participant ADC
    participant LCD
    participant Out as Outputs

    loop every 500 ms
        Main->>ADC: Read CH0, CH1
        ADC-->>Main: Raw values
        Main->>LCD: "Vol: XX.XXV  Cur: X.XXA" (row 4)
    end

    User->>Main: Press BTN3 (debounce 5 ms)
    Main->>LCD: Move '>' (OFF / ON)
    Main->>Out: Power 0V / 12V
    User->>Main: Release BTN3

    User->>Main: Press BTN2
    Note over Main: f_enter_signal_screen()
    Main->>Out: ST selected, switch enabled
    Main->>LCD: Draw signal screen
    User->>Main: Release BTN2
    Note over Main: Signal screen (power kept as selected)
```

## 3. Manual mode – Signal screen

```mermaid
sequenceDiagram
    actor User
    participant Main as Main loop
    participant LCD
    participant Out as Outputs

    User->>Main: Press BTN3 (debounce 5 ms)
    Main->>LCD: Move '>' ST > BLE > UWB > Not use
    Main->>Out: Set S0 / S1
    User->>Main: Release BTN3

    User->>Main: Press BTN2
    Note over Main: f_enter_voltage_screen()
    Main->>Out: Power OFF, switch disabled
    Main->>LCD: Draw voltage screen
    User->>Main: Release BTN2
    Note over Main: Voltage screen
```

## 4. App connect

```mermaid
sequenceDiagram
    participant App
    participant ISR as USB ISR
    participant Main as Main loop
    participant LCD
    participant Out as Outputs

    App->>ISR: 'c'
    ISR->>ISR: Is_App_Connect = CON_APP<br/>Is_App_Screen_Ready = 0
    Note over Main: f_app_mode_process()
    Main->>Main: f_enter_app_screen()
    Main->>Out: Power OFF, ST selected, switch enabled
    Main->>LCD: "Connected JIG App"
    Note over Main: App mode - buttons ignored
```

## 5. App control

```mermaid
sequenceDiagram
    participant App
    participant ISR as USB ISR
    participant Main as Main loop
    participant LCD
    participant Out as Outputs

    App->>ISR: '2' (Power ON)
    ISR->>ISR: App_Voltage = EN_12V
    Main->>Out: Power 12V
    Main->>LCD: "Voltage: 12V"
    Main->>Main: App_Voltage = EN_VOL_DEFAULT

    App->>ISR: '5' (BLE)
    ISR->>ISR: App_Signal = EN_BLE
    Main->>Out: S0 / S1 = BLE
    Main->>LCD: "Signal : BLE"
    Main->>Main: App_Signal = EN_SIG_DEFAULT
```

The ISR only stores the request. The main loop applies it, so GPIO and LCD are
never driven from interrupt context.

## 6. Voltage / current reporting

```mermaid
sequenceDiagram
    participant App
    participant Main as Main loop
    participant ADC

    loop every 500 ms while App connected
        Main->>ADC: Read CH0, CH1
        ADC-->>Main: Raw values
        Main->>Main: Convert to V and A
        alt USB free
            Main->>App: "V:12.34,I:1.23\r\n" (CDC_Transmit_FS)
            App->>App: Update Status frame
        else USB busy
            Main->>Main: Skip frame
        end
    end
```

## 7. App disconnect

```mermaid
sequenceDiagram
    participant App
    participant ISR as USB ISR
    participant Main as Main loop
    participant LCD
    participant Out as Outputs

    App->>ISR: 'd' (Disconnect button or window closed)
    ISR->>ISR: Is_App_Connect = DIS_APP<br/>enl_setting_state = EN_DEFAULT
    Note over Main: f_manual_mode_process()
    Main->>Main: f_enter_voltage_screen()
    Main->>Out: Power OFF, switch disabled
    Main->>LCD: Draw voltage screen
    Note over Main: Manual mode - Voltage screen
```
