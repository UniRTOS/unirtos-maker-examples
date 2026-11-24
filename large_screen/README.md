# Large-Screen Gateway

[中文](README.zh.md)

The Large-Screen Gateway is a multi-scenario touch interaction and device visualization solution built with UniRTOS and LVGL. Running on the Quectel EG800ZCN_LA module, the project uses UniRTOS as its device-side runtime and LVGL as its graphics framework to provide an ST7796S LCD display, FT6336 capacitive touch input, a home overview, an agriculture dashboard, an industrial HMI, and a smart home control panel.

The project organizes four screens within a single application: home, agriculture, industrial, and smart home. It supports switching between scenarios and rooms, device status displays, trend charts, gauges, sliders, alarm dialogs, and control confirmations.

The project demonstrates UniRTOS capabilities in graphics, touch interaction, multi-scenario screen organization, device status visualization, and application extensibility. It serves as a UI reference for agriculture control panels, lightweight industrial HMIs, smart home panels, and edge-to-cloud gateway terminals.

# Development Resources

| Resource | Quantity | Description | How to Obtain |
| :--- | :--- | :--- | :--- |
| EG800ZCN_LA development platform | 1 | Runs UniRTOS and the graphics application, with access to display and touch signals. | Use the project development board or compatible hardware. |
| ST7796S LCD | 1 | Native panel resolution of 320 x 480, used in 480 x 320 landscape orientation. | Use a display supported by this project. |
| FT6336 capacitive touch component | 1 | Reads touch points over software I2C; may be integrated with the display. | Use a touch component supported by this project. |
| USB data cable | 1 | Connects the PC and development platform. Connector type depends on the board; a charge-only cable is not suitable. | Prepare separately. |
| PC | 1 | Windows 10 / Windows 11 for development, building, flashing, and log analysis. | Prepare separately. |
| Power supply and connecting wires | 1 set | Provides power and connections meeting module, display, and backlight requirements. | Select according to board and display specifications. |
| UniRTOS development environment | 1 set | CLI, cross-compilation toolchain, and SDK. | Follow Development Environment Setup below. |
| LCD-enabled base firmware package | 1 | Includes the LCD APIs required by this project. | Use a platform-approved version matching the target module and SDK. |
| LTE antenna and SIM card with data access | As needed | For future cellular and cloud integration; the local demo does not require network access. | Prepare separately. |
| Sensors, actuators, or external control devices | As needed | For future agriculture, industrial, or smart home integration. | Select for the intended application. |

# Quick Start

## Hardware Connections

Connect the display to the development board.

![Development board display side](media/1280X1280.PNG)

Connect the development board to the PC using a USB data cable.

![Development board mainboard side](media/1280X1280%20%281%29.PNG)

The current source configures the following physical pins:

| Component | Signal | Physical Pin | Description |
| :--- | :--- | :--- | :--- |
| LCD | RST | 49 | Configured display reset pin. |
| LCD | DS | 51 | Data / command selection. |
| LCD | CS | 52 | Chip select. |
| LCD | DOUT | 50 | Display data output. |
| LCD | CLK | 53 | Display clock. |
| FT6336 | SDA | 38 | Software I2C data. |
| FT6336 | SCL | 39 | Software I2C clock. |
| FT6336 | RST | 100 | Touch reset. |
| FT6336 | INT | 5 | Touch interrupt signal; the current implementation polls for input. |

## Software Deployment

### Development Environment Setup

Follow the [UniRTOS Quick Start](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) (Chinese) to install the development environment, and verify that `unirtos-cli` is available in PowerShell.

### Get the Code

Open a new PowerShell window and run the following commands to obtain the base project for the specified release:

```powershell
unirtos-cli new -r unirtos-maker-examples
cd unirtos-maker-examples/large_screen
```

### Configure Parameters

Configuration is not yet centralized in a single header. The main configuration locations are:

| Category | Location | Description |
| :--- | :--- | :--- |
| Module and dependencies | [env_config.json](env_config.json) | Target module, SDK, and library dependencies. |
| LCD pins, UI task, and tick | [lvgl_demo.c](main/src/app/lvgl_demo.c) | Display pin multiplexing, 12 KiB task stack, task priority, and 5 ms tick. |
| Display driver | [lcd_drv_st7796s.c](main/src/drivers/display/lcd_drv_st7796s.c) | ST7796S initialization, landscape orientation, and display interface parameters. |
| Display buffer and output | [lvgl_port_display.c](main/src/drivers/display/lvgl_port_display.c) | 480 x 320 resolution, 480 x 40 RGB565 partial buffer, and flush callback. |
| Touch parameters | [lvgl_touch_ft6336.c](main/src/drivers/touch/lvgl_touch_ft6336.c) | FT6336 address, software I2C pins, and coordinate mapping. |
| System features | [menuconfig/.config](menuconfig/.config) | Keep `CONFIG_QOSA_LCD_FUNC=y`. |
| LVGL configuration | `include/lv_conf.h` in the external library | 64 KiB heap, RGB565, default font, charts, and input sampling settings. |
| Shared screen styling | [lvgl_ui_material.h](main/inc/lvgl_ui_material.h) | Gradients, thin highlights, shadows, and pressed feedback. |

### Build the Project

Run the following commands in PowerShell:

```PowerShell
unirtos-cli env-setup
unirtos-cli build -m EG800ZCN_LA
```

After a successful build, the command line displays:

```Plain
SUCCESS: Unirtos project built successfully!
```

Flash the generated firmware to the development board using QFlash.

### Verify the Project

The images below are 480 x 320 software-rendered previews generated from actual LVGL source during Windows host tests, not photographs of the target board. Both language versions use the same previews; the English documentation does not imply that the device UI has been localized into English.

1. Start the development board after flashing and capture complete startup logs using the platform's logging tool. Wait for display initialization and confirm that the home screen appears without a touch calibration dialog. Check the agriculture, industrial, and smart home overview columns and the settings and user buttons at the bottom.

   ![Large-Screen Gateway home overview](media/lvgl-home-preview.png)

2. Tap the agriculture title bar to open the dashboard. Inspect environmental readings, trends, and the six device cards for irrigation, grow lights, fans, roller shades, water pumps, and valves. Toggle devices, open and close alarm or settings dialogs, and tap the home icon to return.

   ![Agriculture dashboard](media/lvgl-agri-preview.png)

3. Open the industrial panel and check the load trend, load bar, oil temperature, oil pressure, online device count, and alarm count. Operate the valve switch, open and close the alarm dialog, confirm the screen is restored, and return home.

   ![Industrial monitoring panel](media/lvgl-industry-preview.png)

4. Open the smart home panel and switch between the living room, bedroom, kitchen, and study. Operate light brightness, air conditioning temperature, ventilation, curtains, and quick scenes, and inspect power readings and the security confirmation dialog. Switch rooms again to confirm that each room retains its own state. Security controls do not unlock a physical door lock.

   ![Smart home panel](media/lvgl-smart-home-preview.png)

5. Check that dialog overlays block touch input to underlying controls, and verify touch alignment at the four screen corners and center. Repeat screen switching, dialog operations, and slider interactions to check colors, gradients, shadows, responsiveness, and stability. Smart home sliders submit control commands on release.

