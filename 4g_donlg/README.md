# 4G USB Dongle

[简体中文](README_zh.md)

The 4G USB Dongle demo is a USB network tethering and local Portal management solution built on UniRTOS.

Running on a Quectel cellular module, the demo combines network attachment, PDP context configuration, data call establishment, USBNET, subnet configuration, and NAT forwarding to share a cellular connection over USB. Windows, Linux, and compatible Android devices can use the module as a USB network adapter and access its built-in management page through a browser.

The Portal displays the module model, firmware version, network status, operator, USBNET status, uptime, upload/download rates, and daily, weekly, and monthly traffic statistics. It also provides connection, disconnection, reboot, and RNDIS/ECM protocol-switching controls. This project serves as a reference for cellular USB dongles, USB tethering devices, and embedded local management interfaces.

# Development Resources

| Resource | Quantity | Description | How to Obtain |
| :--- | :--- | :--- | :--- |
| pico development board | 1 | Hardware platform for the demo; use a board and module supported by the selected UniRTOS firmware. | Prepare separately |
| USB data cable | 1 | USB-A to USB-C cable supporting data transfer, not a charge-only cable. | Prepare separately |
| PC | 1 | Windows 10 or Windows 11 for development and firmware flashing. | Prepare separately |
| SIM card | 1 | USIM card with an active mobile data plan. | Prepare separately |
| Ubuntu device | 1 | Optional, for Linux USB tethering verification. | Prepare separately |
| Android device and OTG adapter | 1 set | Optional; the device must support USB host/OTG and the selected USB Ethernet protocol. | Prepare separately |

# Quick Start

## Hardware Connection

<img src="./media/hardware-connection.png" alt="Development board, SIM card, and USB cable" width="300" style="max-width: 100%; height: auto;" />

1. Insert the SIM card into the development board's SIM slot.
2. Ensure that the cellular antenna is connected according to the board documentation.
3. Connect the development board to the PC using the USB data cable.

For Android verification, connect the board through a suitable OTG adapter and ensure that the board has sufficient power.

## Software Deployment

### Development Environment

Follow the [UniRTOS Quick Start](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) to install the development environment and confirm that `unirtos-cli` is available in PowerShell.

### Get the Code

Open a new PowerShell window and run:

```powershell
unirtos-cli new -r unirtos-maker-examples
cd unirtos-maker-examples
```

Locate the USBNET demo in the downloaded repository and change to its project directory, which contains both [CMakeLists.txt](CMakeLists.txt) and [usb_net.c](usb_net.c). Run the remaining commands from that directory.

### Configure the Project

Application settings are defined in [usb_net.c](usb_net.c). The build module and SDK version are configured in [env_config.json](env_config.json).

| Category | Setting | Description |
| :--- | :--- | :--- |
| Cellular APN | `USBNET_DEMO_DEFAULT_APN` | Empty by default, using the operator/default APN configuration. Set the APN supplied by the operator if required. |
| SIM and PDP profile | `simid`, `profile_idx` in `qosa_usbnet_demo_task()` | Defaults to SIM ID `0` and PDP profile `1`. |
| Sharing method | `method` in `qosa_usbnet_demo_task()` | Defaults to `QOSA_USBNET_METHOD_ONE_SHOT`. |
| USB protocol | RNDIS / ECM | The demo retains a supported current mode at startup. Switch modes using the Portal or the protocol-switch button. Switching reboots the device. |
| Protocol-switch button | `USBNET_DEMO_BUTTON_PIN` | Defaults to `QOSA_PIN_82`. Confirm the board's pin mapping and button wiring before use. |
| Local subnet | `USBNET_DEMO_PORTAL_IP_TEXT`, `USBNET_DEMO_USB_HOST_IP_TEXT` | Defaults to module address `192.168.6.1` and USB host address `192.168.6.100`, with subnet mask `255.255.255.0`. Related address strings and octet arrays must remain consistent when changing the subnet. |
| Subnet configuration | `USBNET_DEMO_FORCE_APP_SUBNET` | Defaults to `1`, enabling application-side subnet configuration. |
| HTTP ports | `USBNET_DEMO_CAPTIVE_PORT`, `USBNET_DEMO_PORTAL_PORT` | Defaults to `80` and `8080`. |
| Status sampling | `USBNET_DEMO_STATUS_POLL_INTERVAL_MS`, `USBNET_DEMO_STATUS_HEAVY_POLL_CYCLES` | Samples every `1000 ms`, with a full status collection every `5` cycles. |
| Application DNS | `USBNET_DEMO_USE_APP_DNS_TASK` | Defaults to `0`. The application DNS task is disabled, retaining the SDK DHCP/DNS relay path. |
| Build environment | `build.module`, `sdk.version` | Currently `EG800ZCN_LA` and SDK `1.0.5`. Adjust to match the actual module and environment. |

### Build the Project

Run the environment setup command in PowerShell:

```powershell
unirtos-cli env-setup
```

Build the firmware with the module and firmware version used in the supplied example:

```powershell
unirtos-cli build -m EG800ZCN_LA -v EG800ZCN_LA_BETA_20260904
```

Replace the module and firmware version with values available for your hardware and SDK environment if they differ from the example.

After a successful build, the command output ends with:

```text
SUCCESS: Unirtos project built successfully!
```

Use QFlash to flash the generated firmware onto the development board.

### Verify the Project

1. Press and hold the board's power key (`PWKY`) to power on. Configure EPAT using the [logging guide](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/日志调试/日志调试.html), then restart the board and capture a complete log from power-on.

	<img src="./media/epat-log.png" alt="Example of module logs captured in EPAT" width="960" style="max-width: 100%; height: auto;" />

2. Wait for network attachment, data call establishment, and USBNET startup. Useful log messages include `usbnet wait attached success`, `usbnet start`, and `portal task create ok`.
3. Confirm that the host recognizes the USB network adapter and obtains a local IP address.
4. Open `http://192.168.6.1/` in a browser. Alternatively, use `http://192.168.6.1:8080/`. If subnet configuration differs, use the actual USB gateway address reported by the host and module logs.
5. Check the Portal's device information, connection status, rates, and traffic statistics. The `/status` endpoint provides the dynamic status data as JSON.
6. Temporarily disable other network interfaces on the test host and visit an external website to verify cellular Internet access. Restore those interfaces after testing. Opening the Portal alone only verifies local management access, not Internet connectivity.

## Platform Verification

### Windows

1. Use RNDIS mode and connect the board to the PC.
2. Open **Device Manager > Network adapters** and check for a device such as **Remote NDIS based Internet Sharing Device**. The displayed name may vary by driver.
3. Run `ipconfig` in Command Prompt to check the USB adapter's address and default gateway.
4. Verify external Internet access and open the Portal as described above.

<img src="./media/portal-overview.png" alt="Desktop Portal dashboard showing network status and device information" width="960" style="max-width: 100%; height: auto;" />

### Linux (Ubuntu)

1. Use the protocol-switch button connected to PIN82, or the Portal control, to select ECM mode if needed. Wait for the module to reboot and the USB adapter to re-enumerate.
2. Run `ip addr` or `ifconfig` to check the new interface and its IP address. Its name may be `usb1`, `usb0`, or an `enx...` name, depending on the system.
3. Run `ping www.baidu.com` to check connectivity, then open the Portal in a browser. If ICMP is restricted, verify connectivity by visiting an external website instead.

### Android

1. Enable OTG if required by the phone, then connect and power the board.
2. Select a USB Ethernet protocol supported by the phone. Compatibility depends on the Android device, kernel, and USB Ethernet driver; OTG support alone does not guarantee tethering compatibility.
3. Confirm that Android recognizes the Ethernet connection, then verify external Internet access and open the Portal in a browser. Connection indicators vary by device.

<img src="./media/mobile-portal.png" alt="Mobile layout of the Portal dashboard" width="299" style="max-width: 100%; height: auto;" />

# Software Design

| Component | Entry Point / Function | Responsibility |
| :--- | :--- | :--- |
| Application initialization | `unir_usbnet_demo_init()`, `UNIRTOS_APP_EXPORT()` | Registers the application entry point and creates the main task. |
| Main task | `qosa_usbnet_demo_task()` | Initializes the protocol, waits for network attachment, configures PDP/subnet/NAT, establishes the data call, and starts USBNET and service tasks. |
| Connection control | `_usbnet_demo_connect_data()` | Establishes the cellular data path and starts USB sharing when processing a connection request. |
| Status collection | `qosa_usbnet_status_task()` | Combines low-frequency full collection with high-frequency lightweight updates and stores results in a shared cache. |
| Portal service | `qosa_usbnet_portal_task()`, `_usbnet_demo_handle_portal_client()` | Serves the built-in HTML/CSS/JavaScript page and HTTP requests. |
| Status endpoint | `_usbnet_demo_build_status_json()` | Serializes cached status for `/status`. |
| Action processing | `_usbnet_demo_process_pending_action()` | Executes queued connection, disconnection, reboot, and protocol-switching actions in the main task. |
| Asynchronous callback | `_usbnet_demo_func_cb()` | Logs the USBNET operation result and releases its configuration context. |

The Portal is embedded in the C application and does not require a separate frontend build. HTTP handlers queue control actions, while the main task executes them serially. This separates HTTP request handling from time-consuming link operations.

# Notes and Limitations

- Network policy settings include network access, schedules, total traffic limits, and Windows/Linux device policies. Configuration and status reporting are implemented, but `_usbnet_demo_enforce_policy()` does not enforce hard blocking. Do not rely on these settings for access control or billing.
- Daily, weekly, and monthly traffic figures are derived from cumulative data call counters and runtime baselines. They are demonstration statistics, not persistent billing records; period accuracy depends on valid device time.
- Reboot and protocol switching interrupt the USB network connection. Wait for the adapter to re-enumerate before reopening the Portal. Disconnecting USBNET may also make the Portal unreachable until sharing is restored or the device restarts.
- Automatic captive-portal pop-up behavior depends on the host's network probes and the SDK DHCP/DNS path. It is not guaranteed; access the Portal URL manually when necessary.
- This is a demonstration project, not a production-ready secure management system. Add authentication, authorization, configuration persistence, and validated policy enforcement before deployment outside a trusted test environment.
