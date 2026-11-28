# ARIES v3.0 Betaflight Flashing & Testing Guide (For Teammates)

This step-by-step guide walks you through **flashing the firmware onto the ARIES v3.0 board**, **running the bridge script**, **connecting to the Betaflight Configurator website**, **testing**, and **sending back the logs**.

> **Before You Start (Safety First):**
> - **Remove all propellers** from the motors before plugging anything into your computer.
> - Only **one program** can use the USB `COM` port at a time. Always finish flashing **before** starting the Python script, and always close the Python script (`Ctrl + C`) **before** re-flashing.

---

## Step 1: Find Your Board's COM Port (Windows)

1. Plug the ARIES v3.0 board into your PC with a micro-USB data cable (on the `Header J1` / USB-UART side).
2. Press the **Windows key**, type **Device Manager**, and open it.
3. Expand **Ports (COM & LPT)**.
4. Look for **Silicon Labs CP210x USB to UART Bridge (COMx)** — note the number (for example `COM9`, `COM5`, `COM12`).
   - That `COMx` name is what you will use in the steps below.

---

## Step 2: Flash the Firmware Using the VEGA SDK Tool

1. Open **File Explorer** and go to the VEGA flasher folder on your PC:
   ```text
   C:\Users\<username>\AppData\Local\Arduino15\packages\vega\tools\vegaflasher\02
   ```
   *(Replace `<username>` with your Windows user folder name. If `AppData` is hidden, click the address bar in File Explorer and paste the path directly, or enable **View → Show → Hidden items**.)*
2. Make sure you know where the firmware **`.bin`** file you were sent is saved (for example `Desktop\betaflight_ARIESV3.bin`).
   - **Important:** Always use the **`.bin`** file (never `.hex` or `.elf`).
3. Inside that `02` folder, click the File Explorer address bar, type **`cmd`**, and press **Enter** to open Command Prompt right inside that folder.
4. Run `flasher.bat` with your `COM` port and the path to the `.bin` file:
   ```bat
   flasher.bat COMx path\to\firmware\bin\file.bin
   ```
   **Example** (if your board is on `COM9` and the file is on your Desktop):
   ```bat
   flasher.bat COM9 C:\Users\<username>\Desktop\betaflight_ARIESV3.bin
   ```
   *(Tip: you can type `flasher.bat COM9 `, then drag-and-drop the `.bin` file straight into the Command Prompt window and press **Enter**!)*
5. Wait until flashing completes and returns you to the command prompt.
   - If flashing fails or hangs, close any other serial monitor/proxy window, unplug and re-plug the USB cable, and run the command again.

---

## Step 3: Install Python Dependencies (One-Time Setup)

The bridge script needs **Python 3** and one small library called **`pyserial`**.

1. Open a Command Prompt (press **Windows key**, type **`cmd`**, press **Enter**).
2. Check that Python is installed:
   ```bat
   python --version
   ```
   *(If Windows says Python is not found, install Python 3 from `https://www.python.org/downloads/` and make sure to check **"Add Python to PATH"** during installation.)*
3. Install `pyserial`:
   ```bat
   pip install pyserial
   ```

---

## Step 4: Run the Python Bridge Script

The browser cannot talk directly to the ARIES `COM` port reliably without the helper script [`com9_betaflight_proxy.py`](tempfiles/com9_betaflight_proxy.py) (also in [`scripts/com9_betaflight_proxy.py`](scripts/com9_betaflight_proxy.py)). This script bridges your board to the Betaflight website and records a log file automatically.

1. Open Command Prompt in the folder where `com9_betaflight_proxy.py` is saved (for example, `cd` to that folder or type `cmd` in File Explorer's address bar).
2. Run the script with your port:
   - **If your board is on `COM9` (default):**
     ```bat
     python com9_betaflight_proxy.py
     ```
   - **If your board is on a different Windows port (e.g., `COM5` or `com5`):**
     ```bat
     python com9_betaflight_proxy.py COM5
     ```
   - **If you are on Linux (e.g., `/dev/ttyUSB0`):**
     ```bash
     python3 com9_betaflight_proxy.py usb0
     ```
3. You should see output like:
   ```text
   [PROXY READY] Target=COM9 @ 115200 | Log=...\com9_proxy_20261128_164500.txt
   [CONNECT URL] Betaflight Web App (Manual Mode): ws://127.0.0.1:5761
   [CONNECT URL] Betaflight Desktop (Manual):      tcp://127.0.0.1:5761
   [SERIAL CONNECTED] COM9 @ 115200
   ```
   - **Leave this window open** while using Betaflight Configurator!
   - *(Tip: if you forgot which `COM` port it is on while the script says `[WAIT SERIAL]`, type `:ports` and press Enter to list attached ports, then type `COM5` or `:port COM5` to switch immediately.)*

---

## Step 5: Open Betaflight Configurator & Connect Manually (`ws://`)

1. Open **Google Chrome** or **Microsoft Edge** and go to the Betaflight Configurator web app:
   ```text
   https://app.betaflight.com
   ```
2. Look at the **top-right corner** next to the **Connect** button:
   - Open the port dropdown menu and choose **Manual** (if you don't see **Manual**, enable **Show all serial devices** / **Manual selection** in the port dropdown or Configurator Settings).
3. In the manual port box, enter our local WebSocket URL:
   ```text
   ws://127.0.0.1:5761
   ```
4. Click **Connect**.
5. After 1–2 seconds, the Betaflight dashboard should open and the Python terminal window will start showing `[CLIENT CONNECTED]` and packet activity.

---

## Step 6: Test the Buzzer in Betaflight

1. Once connected, check the **Beeper** settings in Betaflight Configurator:
   - Click the **Configuration** tab on the left menu and scroll to the **Beeper Configuration** section to inspect/toggle beeper alerts.
   - To trigger a buzzer from a switch, go to the **Modes** tab, find **BEEPER**, add a range on an AUX channel (if your receiver is connected), and click **Save**.
   - *(If you are testing ESC/motor beeps instead, go to the **Motors** tab — **PROPS MUST BE OFF** — enable the safety toggle, and gently raise a motor slider briefly.)*

> **Important Note for the Current ARIES v3.0 Firmware:**
> In the current `ARIESV3` target configuration ([`target.h`](src/platform/ARIES/target/ARIESV3/target.h#L151-L215)), hardware beeper support is currently disabled (`#undef USE_BEEPER` and `BEEPER_PIN NONE`). Because of that, the Beeper section may be unavailable or the physical buzzer may not sound yet with this build—that is normal for this test image. **Even if the buzzer or other components are not working yet, keep going to Step 7!**

---

## Step 7: Measure Connection Time & Take Screenshots

Even if hardware components (like the buzzer or sensors) do not respond yet, testing how long the link stays alive is very important:

1. **Time the connection:** Start a timer on your phone/watch as soon as Betaflight Configurator connects.
   - Leave Configurator connected and click around a few tabs (for example **Setup**, **Ports**, **Configuration**) for **at least 2–5 minutes** (or until it disconnects on its own).
   - Note down **how long the connection stayed active** (for example: *"Stayed connected for 5 minutes with no drops"* or *"Disconnected after 12 seconds"*).
2. **Take and save screenshots** (`Win + Shift + S` on Windows) of:
   - The main Betaflight Configurator screen once connected (and any tabs you tested, like **Configuration** / **Beeper** / **Setup**).
   - Any error messages or `"Connection lost"` popups if it disconnects.

---

## Step 8: Copy the Logs from Betaflight Configurator

Before (or right after) disconnecting:

1. In Betaflight Configurator, click **Settings** (top-right / left-hand settings menu depending on view) $\rightarrow$ **Logs** (or open the **Logs** panel under **Settings → Logs**).
2. Select and **copy all the log text** shown there.
3. Open **Notepad**, paste the copied logs, and save the file (for example as **`betaflight_logs.txt`** on your Desktop).
4. Click **Disconnect** in Betaflight Configurator.

---

## Step 9: Exit the Python Script & Send Over the Files

1. Click inside the Command Prompt window running `com9_betaflight_proxy.py`.
2. Press **`Ctrl + C`** (hold `Ctrl` and press `C`) — or type `:quit` and press **Enter**.
3. The script will close cleanly and print the exact name and full path of the log file(s) it saved, for example:
   ```text
   Saved log file(s):
     - com9_proxy_20261128_164500.txt (C:\Users\<username>\Desktop\com9_proxy_20261128_164500.txt)
   ```
4. **Send all of these back to the team:**
   - **Proxy session log file** (the `.txt` file printed when you pressed `Ctrl + C`, e.g. `com9_proxy_YYYYMMDD_HHMMSS.txt`)
   - **Betaflight Configurator log file** (the `betaflight_logs.txt` copied from **Settings → Logs**)
   - **Screenshots** you captured during Step 7
   - **Short test note** stating:
     - **How long the Configurator connection lasted** (e.g. `5 min, no disconnect` or `dropped at XX sec`)
     - What the LEDs on the ARIES board were doing
     - What happened during the buzzer test

---

## TL;DR Task List

- [ ] **1. Props OFF & find port:** Remove propellers, plug in ARIES USB, find `COMx` in Windows Device Manager.
- [ ] **2. Flash first:** Open `cmd` in `C:\Users\<username>\AppData\Local\Arduino15\packages\vega\tools\vegaflasher\02` and run `flasher.bat COMx path\to\firmware.bin`.
- [ ] **3. Install dependency (once):** Run `pip install pyserial`.
- [ ] **4. Start proxy:** Run `python com9_betaflight_proxy.py COMx` (or no port if `COM9`; `usb0` on Linux) and leave it open.
- [ ] **5. Connect in browser:** Open `https://app.betaflight.com` $\rightarrow$ choose **Manual** $\rightarrow$ enter `ws://127.0.0.1:5761` $\rightarrow$ click **Connect**.
- [ ] **6. Check buzzer & hold connection:** Test buzzer in **Configuration**/**Modes**; even if components don't work, **time how long the connection stays alive** (aim for 2–5 min) and **take screenshots**.
- [ ] **7. Save Betaflight logs:** Go to **Settings → Logs**, copy all text, paste into `betaflight_logs.txt`, then click **Disconnect**.
- [ ] **8. Exit proxy & send files:** Press **`Ctrl + C`** in the Python window, check the printed `Saved log file(s):` name, and send: **(a)** the proxy `.txt` log, **(b)** `betaflight_logs.txt`, **(c)** screenshots, and **(d)** your connection-duration note.
