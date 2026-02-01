# SmartRace Fuel Display - Web Configurable Version

## Features
- Web-based configuration
- Automatic AP mode for initial setup
- Falls back to AP mode if WiFi fails
- Accessible via mDNS: `sr-fuel-N.local` (where N = controller ID)
- EEPROM storage for persistent settings
- Button controls for IP display and reset
- Unique mDNS name per controller (no conflicts with multiple units)
- Runs on ESP8266 hardware with an I2C display device at 128 x 32 pixels and any momentary button that closes on contact

## First Time Setup

1. **Power on the device** - It will create a WiFi access point since it's not configured yet
   
2. **Display will show:**
   ```
   Setup Mode
   AP: SmartRace-Setup
   192.168.4.1
   PW: smartrace2025
   ```

3. **Connect to the WiFi network** "SmartRace-Setup" 
   - Password: `smartrace2025` (shown on display)

4. **Open browser and go to:** `http://192.168.4.1`

5. **Fill in the configuration form:**
   - WiFi Network Name (SSID): Your home WiFi name
   - WiFi Password: Your WiFi password
   - SmartRace Server IP: IP address of your SmartRace server (e.g., `192.168.1.100`)
   - SmartRace Server Port: Usually `53919`
   - Controller ID: Which controller to monitor (1-6)

6. **Click "Save & Restart"** - Device will reboot and connect to your WiFi

## Normal Operation

Once configured, the device will:
- Connect to your WiFi network automatically
- Connect to SmartRace WebSocket server
- Display fuel level for configured controller
- Be accessible at `http://sr-fuel-N.local` (N = your controller ID)

### Driver and Car Information

When you assign a driver and car to your controller in SmartRace:
- **Display automatically shows:**
  - Line 1: `Ctrl X: DriverName` (e.g., "Ctrl 1: Player1")
  - Line 2: Car name (e.g., "57 Porsche 911 RSR")
- **Display remains until:**
  - You press the button (short press dismisses it)
  - The race starts (automatically returns to fuel display)
- This helps confirm you're monitoring the correct controller before racing

### Button Functions

**Short Press (< 2 seconds):**
- Sends emergency STOP command to race
- Shows "STOPPED!" for 5 seconds
- Dismisses driver/car info, IP address, or final position displays

**Medium Press (2-8 seconds):**
- At 2 seconds: IP address appears on display (no need to release)
- Release before 8 seconds: IP stays locked on screen
- Press button again (short press) to dismiss IP and return to fuel display

**Long Press (8+ seconds):**
- Continues showing IP from second 2-8
- At 8 seconds: Automatically resets configuration and goes to Setup Mode (AP)
- Use this to reconfigure or fix connection issues

## Accessing Configuration

After initial setup, you can change settings two ways:

1. **Via mDNS name:** `http://sr-fuel-N.local` (where N = controller ID)
   - Example: Controller 1 → `http://sr-fuel-1.local`
   - Example: Controller 4 → `http://sr-fuel-4.local`
   - Works on most devices (may not work on some Android devices)

2. **Via IP address:**
   - Hold button for 2-8 seconds to see IP on display
   - Go to that IP in your browser

## Troubleshooting

**Display shows "Setup Mode" every time:**
- Your WiFi credentials might be incorrect
- Hold button for 8+ seconds to reset and try again

**Can't connect to "SmartRace-Setup" AP:**
- Make sure you're close to the device
- Password is shown on the display: `PW: smartrace2025` (default)
- Try forgetting the network and reconnecting

**"WiFi Failed!" message:**
- Check your WiFi name and password in configuration
- Make sure WiFi is 2.4GHz (ESP8266 doesn't support 5GHz)
- Device will automatically switch to AP mode for reconfiguration

**WebSocket won't connect:**
- Verify SmartRace server IP address is correct
- Check that port 53919 is correct for your setup
- Make sure SmartRace software is running

**Memory concerns:**
- IRAM usage will be higher (~98-99%)
- Should still work fine, but monitor for stability
- If crashes occur, may need to reduce features

## Memory Usage Estimate
- Base code: ~28KB IRAM
- ESP8266WebServer: ~3-4KB IRAM  
- mDNS: ~1-2KB IRAM
- Total: ~33-35KB IRAM (leaving ~2-3KB free)

## Security Notes

**Setup Mode Security:**
- The setup AP is password-protected (WPA2) to prevent unauthorized access
- Only someone with the AP password can connect and access the configuration page
- Your WiFi password is still transmitted over HTTP (not HTTPS), but an attacker would need to:
  1. Know the AP password to connect to "SmartRace-Setup"
  2. Sniff traffic on the AP network
  3. This provides reasonable protection for a hobbyist slot car setup

**Changing the AP Password:**
- Edit the `ap_password` variable in the code (around line 50)
- Use a strong, unique password
- The password is displayed on the OLED during setup mode

**Additional Security (Advanced):**
- HTTPS/TLS would provide full encryption but requires SSL certificates and significant IRAM
- Not practical on ESP8266 with current memory constraints (94% IRAM usage)
- For maximum security: Configure in a trusted environment away from public WiFi

## Technical Details

**EEPROM Layout:**
- Stores: WiFi credentials, WebSocket server info, controller ID
- Validated with "CFG" magic bytes
- Survives power loss and reboots

**AP Mode Settings:**
- SSID: `SmartRace-Setup`
- Password: `smartrace2025` (WPA2 - prevents WiFi password sniffing)
- IP: `192.168.4.1`

**mDNS Name:**
- `sr-fuel-N.local` (where N = controller ID)
- Example: Controller 1 → `sr-fuel-1.local`
- This ensures no conflicts when running multiple units

**Web Server Port:**
- Port 80 (HTTP)

## Running Multiple Units

You can run multiple fuel displays on the same network! Each unit:
- Monitors its own configured controller (1-6)
- Has a unique mDNS name (sr-fuel-1.local, sr-fuel-2.local, etc.)
- Uses minimal power (~40mA average per unit)
- Can all share one USB hub with a 1A power supply

**Power for 4 units:** ~160mA average, peaks at ~680mA during simultaneous WiFi connection
