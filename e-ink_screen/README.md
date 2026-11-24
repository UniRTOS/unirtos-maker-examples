# E-Paper Price Tags

[简体中文](README_zh.md)

This project is a product information display and low-memory graphics output example built on UniRTOS.

The project arranges six product areas in three columns and two rows, using bitmap glyphs from a Chinese sans-serif typeface and a consistent page layout. It supports structured product data validation, row-by-row rendering, mounting orientation compensation, full-screen updates, and panel deep sleep. The PC preview and device share the same C layout and rendering code, allowing text, images, barcodes, and QR codes to be checked before flashing.

## Development Resources

| Resource | Quantity | Description |
| --- | --- | --- |
| Quecduino development board | 1 | EG800ZCN_LA with the pin mapping used by this project |
| 7.5-inch V2 monochrome e-paper panel | 1 | 800 × 480; compatible with the current V2 OTP full-refresh driver |
| E-paper driver board | 1 | Current hardware uses an E-Paper Driver HAT Rev2.3 with PWR enable |
| Power supply and connecting wires | 1 set | Stable panel supply, jumper wires, and a reliable common ground |
| USB data cable | 1 | Connects the board to a PC; charging-only cables are unsuitable |
| PC | 1 | Windows 10 / Windows 11 |

## Quick Start

### Hardware Connections

1. Disconnect power from the board and panel. Insert and lock the panel ribbon cable correctly.
2. Connect the controller and driver board according to the table below, with a common ground.
3. Check the power and signal requirements of your hardware. The current application sets module IO to 3.3 V and uses a stable 3.3 V panel supply.
4. Connect the board to the PC using a USB data cable, check the wiring, and apply power.

| Panel Signal | Quecduino Header | Module PIN | QOSA GPIO | Purpose |
| --- | --- | --- | --- | --- |
| DIN | D11 | 63 | GPIO13 | Data output |
| CLK | D13 | 49 | GPIO15 | Clock output |
| CS | D10 | 64 | GPIO12 | Chip select |
| DC | D9 | 67 | GPIO9 | Command / data selection |
| RST | D8 | 66 | GPIO8 | Reset |
| BUSY | D7 | 29 | GPIO11 | Busy status input |
| PWR | D6 | 28 | GPIO10 | Driver board enable, not a power supply output |
| GND | GND | N/A | N/A | Common ground |
| VCC | Stable 3.3 V supply | N/A | N/A | Do not power the panel directly from a GPIO |

### Software Deployment

#### Development Environment

Follow the [UniRTOS Quick Start](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) to set up the development environment and confirm that `unirtos-cli` is available in PowerShell.

#### Obtain the Code

Open a new PowerShell window and run:

```PowerShell
unirtos-cli new -r unirtos-maker-examples
cd unirtos-maker-examples/e-ink_screen
```

#### Configuration

| Category | Location / Setting | Description |
| --- | --- | --- |
| Module and SDK | [env_config.json](env_config.json) | EG800ZCN_LA, SDK 1.0.5 |
| Demo enable | [main/inc/epaper_demo_config.h](main/inc/epaper_demo_config.h): `EPD_DEMO_ENABLED` | Default `1`; starts the display task |
| Panel dimensions | Same header: `EPD_WIDTH`, `EPD_HEIGHT` | Fixed at 800 × 480; changing macros alone cannot support another panel |
| GPIO mapping | Same header: `EPD_*_PIN`, `EPD_*_GPIO`, `EPD_*_FUNC` | Default connections are listed above |
| BUSY timeout | Same header: `EPD_BUSY_TIMEOUT_MS` | Default 30000 ms |
| Panel enable | Same header: `EPD_PWR_ENABLED`, `EPD_PWR_STARTUP_MS` | Enabled by default; 100 ms startup delay |
| Software SPI delay | Same header: `EPD_SOFT_SPI_DELAY_LOOPS` | Default 32; a loop count, not a calibrated frequency |
| Signal capture | Same header: `EPD_SPI_CAPTURE_BURSTS` | Default `0`; diagnostic bursts disabled |
| Product data | [main/src/price_tag_data.c](main/src/price_tag_data.c) | Six product records; prices are integer cents |
| Layout and orientation | [main/src/price_tags.c](main/src/price_tags.c) | Three columns, two rows; 180° compensation for panel output |

#### Build the Project

Run the following in PowerShell from the project root:

```powershell
unirtos-cli env-setup
unirtos-cli build -m EG800ZCN_LA
```

After a successful build, the command line will end with:

```text
SUCCESS: Unirtos project built successfully!
```

Use QFlash to flash the generated firmware to the development board.

### Verify the Project

1. Configure EPAT using the [UniRTOS logging guide](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/日志调试/日志调试.html) and capture logs from module startup.
2. Look for `EPD app init: creating display task` and `EPD price tags: SOFTWARE SPI` to confirm that the display task has started.
3. Wait for the clear cycle and full-page update. Black-and-white transitions during full refresh are expected.
4. Check all six labels for complete text, prices, illustrations, and symbols, and verify the installed orientation.
5. Look for `EPD price tag transfer complete` and confirm that the image remains after the update.
6. Scan each barcode and QR code with the intended device and check the product identifiers.

![EPAT logging tool interface](media/epat_logs.jpg)

Image file: [epat_logs.jpg](media/epat_logs.jpg)

## Project Structure

```text
unirtos-msp/
├── README.md                 # English guide
├── README_zh.md              # Chinese guide
├── CMakeLists.txt            # SDK external application build entry
├── env_config.json           # Module and SDK configuration
├── menuconfig/
│   └── .config               # Project feature configuration
├── main/
│   ├── inc/                  # Configuration, driver, and price tag interfaces
│   └── src/                  # Application, renderer, drivers, and C resources
└── media/                    # Images used by the READMEs
    ├── epat_logs.jpg
    └── epaper_price_tags.jpg
```

## Demonstration

After the board is powered on using its normal procedure, the system automatically runs the registered e-paper application. Once initialization, clearing, and the full-page update are complete, the panel enters sleep and retains the six-product image. No touch input or button press is required to open the page.

### Six Product Labels

| Product | Unit | Price (CNY) | Demo Identifier |
| --- | --- | --- | --- |
| Tomato | 500g | 3.98 | 100001 |
| Cucumber | 500g | 2.58 | 100002 |
| Potato | 500g | 1.99 | 100003 |
| Carrot | 500g | 2.68 | 100004 |
| Apple | 500g | 5.98 | 100005 |
| Banana | 500g | 3.88 | 100006 |

![Photograph of the six-product page on the e-paper panel](media/epaper_price_tags.jpg)

