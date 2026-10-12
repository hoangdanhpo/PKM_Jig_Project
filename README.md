# PKM JIG

Test JIG for PKM boards, based on STM32F103C8.

The JIG can:

- Turn the 12V power supply to the board under test ON or OFF.
- Route the JTAG signal to one of three targets: ST uC, BLE or UWB.
- Measure supply voltage and current.
- Work standalone (buttons + 20x4 LCD) or be controlled from a PC App over USB.

## Repository structure

| Folder            | Content                                         |
|-------------------|-------------------------------------------------|
| `01_Input`        | Specification documents                         |
| `02_Sim_Proteus`  | Proteus simulation                              |
| `03_Investigate`  | LCD test project                                |
| `04_SourceCode`   | Firmware (STM32CubeMX + Keil MDK)               |
| `05_Tool`         | PC App (Python) and released `.exe` packages    |
| `06_BackUp`       | Backup of old source code                       |

## Firmware

Project: `04_SourceCode/PKM_JIG`

- MCU: STM32F103C8, HAL driver, configured with STM32CubeMX (`PKM_JIG.ioc`)
- IDE: Keil MDK, ARM Compiler 6 (`MDK-ARM/PKM_JIG.uvprojx`)
- PC link: USB CDC (virtual COM port)

Application code is in `Core/Src/main.c`, inside the `USER CODE` blocks, so it is
kept when the project is regenerated with CubeMX.

### Build

1. Open `04_SourceCode/PKM_JIG/MDK-ARM/PKM_JIG.uvprojx` in Keil.
2. Build (F7) and flash.

### Pin map

| Function             | Pin                        |
|----------------------|----------------------------|
| LCD D4..D7           | PA5, PA4, PA3, PA2         |
| LCD EN / RS          | PA6 / PB12                 |
| BTN1 / BTN2 / BTN3   | PA8 / PA9 / PA10           |
| Voltage sense (ADC)  | PA0 (ADC CH0, divider x11) |
| Current sense (ADC)  | PA1 (ADC CH1, ACS712 5A)   |
| EN_12V_n / EN_24V_n  | PB7 / PB6                  |
| JTAG_EN_n            | PB13                       |
| JTAG_S0 / JTAG_S1    | PB14 / PB15                |

## Usage

### Manual mode (no App connected)

| Button | Voltage screen          | Signal screen                      |
|--------|-------------------------|------------------------------------|
| BTN3   | Toggle power OFF / ON   | Select ST uC > BLE > UWB > Not use |
| BTN2   | Go to Signal screen     | Go to Voltage screen (power OFF)   |

The Voltage screen shows live voltage and current on the last LCD row.

### App mode

1. Run `05_Tool/PKM_JIG_Tool_<date>/PKM_JIG.exe` (or `python 05_Tool/PKM_JIG.py`).
2. Select the COM port and press **Connect**.
3. Use **Power Control** and **Switch signal** to control the JIG.
4. The **Status** frame shows voltage and current, updated every 500 ms.

When the App disconnects, the JIG turns the power OFF and goes back to manual mode.

Running the Python source needs `pyserial`:

```
pip install pyserial
```

## Protocol

App -> JIG, 1 byte per command:

| Byte | Command        |
|------|----------------|
| `c`  | Connect        |
| `d`  | Disconnect     |
| `1`  | Power OFF      |
| `2`  | Power ON (12V) |
| `4`  | Signal ST uC   |
| `5`  | Signal BLE     |
| `6`  | Signal UWB     |
| `7`  | Signal Not use |

JIG -> App, every 500 ms while connected:

```
V:12.34,I:1.23\r\n
```

Full sequence diagrams: [04_SourceCode/PKM_JIG_Sequence.md](04_SourceCode/PKM_JIG_Sequence.md)
