// ============================================================
// ESP32 INTERNET RADIO
// ESP32-S3 Pico
// PCM5100 + 16x2 I2C LCD + Rotary Encoder
//
// WiFi logic:
//   1. MASTER (home) WiFi is hardcoded below.
//   2. If the master network is in range, connect to it.
//   3. Otherwise try the GUEST WiFi saved in flash (if any).
//   4. If nothing connects, the on-device WiFi setup starts.
//   5. Holding the encoder button at power-up forces the setup.
// ============================================================

#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h"
#include <vector>

// LCD library
#include <LiquidCrystal_I2C.h>

// Rotary encoder library
#include "AiEsp32RotaryEncoder.h"

// ESP32 Preferences - saves last station and guest WiFi in flash
#include <Preferences.h>


// ============================================================
// PCM5100 AUDIO CONNECTIONS
// ============================================================

#define I2S_BCLK  36
#define I2S_DOUT  37
#define I2S_LRC   38


// ============================================================
// LCD CONNECTIONS
// ============================================================

#define LCD_SDA       8
#define LCD_SCL       9
#define LCD_ADDRESS   0x27

LiquidCrystal_I2C lcd(
  LCD_ADDRESS,
  16,
  2
);


// ============================================================
// ROTARY ENCODER CONNECTIONS
// ============================================================

#define ROTARY_ENCODER_A_PIN       10
#define ROTARY_ENCODER_B_PIN       11
#define ROTARY_ENCODER_BUTTON_PIN  12

#define ROTARY_ENCODER_VCC_PIN     -1

// Your encoder works correctly with 2
#define ROTARY_ENCODER_STEPS       2


// ============================================================
// AUDIO VOLUME
// ============================================================

// Fixed internal volume.
// External amplifier will control listening volume.
#define RADIO_VOLUME 20


// ============================================================
// STATION PREVIEW TIMEOUT
// ============================================================

#define STATION_TIMEOUT 5000


// ============================================================
// LCD SCROLLING
// ============================================================

// Time between each scroll movement
#define SCROLL_INTERVAL 400

// Pause before a long station name starts scrolling
#define SCROLL_START_PAUSE 0

// Blank character spaces between repetitions.
// 0 = the end of the name runs straight into the start.
// Set to 2 or 3 if you want a small visual separator.
#define SCROLL_GAP 10


// ============================================================
// WIFI SETTINGS
// ============================================================

// Time allowed for each connection attempt (milliseconds)
#define WIFI_CONNECT_TIMEOUT   15000

// Button held longer than this = "long press" (milliseconds)
#define LONG_PRESS_MS          1000

// WPA/WPA2 passwords must be at least 8 characters
#define MIN_PASSWORD_LENGTH    8
#define MAX_PASSWORD_LENGTH    63

// Character shown for "backspace" while typing a password.
// On most 1602 LCDs 0x7F is a left arrow.
#define BACKSPACE_CHAR         ((char)0x7F)

// Maximum number of networks listed in the setup screen
#define MAX_NETWORKS           20


// ============================================================
// MASTER (HOME) WIFI - ALWAYS KEPT IN THE CODE
// ============================================================

// Put your real home details back in here
const char *MASTER_SSID     = "*****";
const char *MASTER_PASSWORD = "*****";


// ============================================================
// PASSWORD CHARACTER SET
// ============================================================

// Entry 0 = backspace, entries 1.. = characters below
const char PASSWORD_CHARS[] =
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "!@#$%^&*()-_=+[]{};:'\",.<>/?\\|`~ ";

// Number of characters + 1 for the backspace entry
const int PASSWORD_ENTRIES = sizeof(PASSWORD_CHARS);


// ============================================================
// SCROLL VARIABLES
// ============================================================

String scrollText = "";

int scrollPosition = 0;

bool scrolling = false;

bool scrollWaiting = false;

unsigned long scrollTimer = 0;


// ============================================================
// OBJECTS
// ============================================================

Audio audio;

AiEsp32RotaryEncoder rotaryEncoder(
  ROTARY_ENCODER_A_PIN,
  ROTARY_ENCODER_B_PIN,
  ROTARY_ENCODER_BUTTON_PIN,
  ROTARY_ENCODER_VCC_PIN,
  ROTARY_ENCODER_STEPS
);

Preferences preferences;


// ============================================================
// RADIO STATIONS
// ============================================================

typedef struct
{
  const char *url;
  const char *name;

} Station;


// ============================================================
// STATION LIST
// ============================================================

Station stations[] =
{
  {"http://de2.streamingpulse.com:8048/stream.mp3",
   "1 Waterberg Stereo FM"},

  {"https://25453.live.streamtheworld.com/RSG.mp3",
   "2 RSG"},

  {"https://stream.radio.co/s7adaa782c/listen",
   "3 Radio Orania"},

  {"https://edge.iono.fm/xice/362_medium.aac",
   "4 Pretoria FM"},

  {"https://edge.iono.fm/xice/122_medium.mp3",
   "5 Radio Rosestad"},

  {"http://anton.antfarm.co.za:8000/Bosveld",
   "6 Bosveld Stereo"},

  {"https://bokradio.highquality.radiostream.co.za/",
   "7 Bok Radio"},

  {"https://zas3.ndx.co.za:8002/stream",
   "8 Lekker FM"},

  {"http://maggie.torontocast.com:18014/24Kbps",
   "9 Impala Radio"},

  {"http://adhocstreaming.co.za:8000/LuisterFM",
   "10 Luister FM"},

  {"https://s3.radio.co/s170af14fc/listen",
   "11 Radio Suid Afrika"},

 {"https://kudu.fm/hls/radio.m3u8",
   "12 Kudu FM"},

  {"https://iceant.antfarm.co.za/VCR",
   "13 Vaal Stereo"},

  {"https://zas2.ndx.co.za:8046/stream",
   "14 Radio Laeveld"},

  {"http://cast3.my-control-panel.com/proxy/overvaalstereo/stream",
   "15 Overvaal"},

  {"https://zas3.ndx.co.za:8034/stream",
   "16 Radio Namakwaland"},

  {"https://zas7.ndx.co.za:8040/stream",
   "17 Radio Overberg"},

  {"https://s5.voscast.com:9817/listen.mp3",
   "18 Radio Klein Karoo"},

  {"https://iceant.eclipse-streaming.co.za/VCR",
   "19 90.6 FM Stereo"},

  {"https://edge.iono.fm/xice/88_low.mp3",
   "20 Kosmos FM"},

  {"https://fm.radioeendrag.com/8016/stream.mp3",
   "21 Radio Eendrag"},

  {"https://samcloud.spacial.com/api/listen?sid=74504",
   "22 Houtstok FM"},

  {"https://zas3.ndx.co.za:8040/stream",
   "23 Kanaal 7"},

  {"https://stream.rcast.net/68455",
   "24 Koepel Stereo"},

  {"https://iceant.eclipse-streaming.co.za:8443/SFM",
   "25 SFM"},

  {"https://edge.iono.fm/xice/330_medium.aac",
   "26 Groot FM"},

  {"https://edge.iono.fm/xice/116_medium.aac",
   "27 Paarl 96.7 FM"},

  {"https://stream.radiojar.com/pm9ehnq2v8quv",
   "28 Treffers Radio"},

  {"https://c44.radioboss.fm:8174/stream",
   "29 Radio Daspoort"},

  {"https://uslradio.net/8042/stream",
   "30 AWR Afrikaans"},

  {"https://c42.radioboss.fm:8073/stream",
   "31 Befonk in stereo"},

  {"https://my.fmstereo.co.za/8016/stream",
   "32 FM Stereo"},

  {"https://uslradio.net:8008/stream",
   "33 Ramkat Radio Kaapstad"},

  {"https://radio.inksnapp.co.za/listen/daybreak_radio/radio.mp3",
   "34 Daybreak Radio"},

  {"https://stream.radiojar.com/pm9ehnq2v8quv",
   "35 Treffers Radio"},

  {"https://fm.radioeendrag.com/8016/stream.mp3",
   "36 Radio Eendrag"},

  {"http://zas2.ndx.co.za:8164/stream",
   "37 Caledon FM"},
  
  {"https://ukwesta.streaming.broadcast.radio/lmradio",
    "38 LM Radio"},

  {"https://zas1.ndx.co.za:8140/stream",
   "39 Gateway Radio"},

  {"https://edge.iono.fm/xice/54_low.aac",
    "40 Algoa FM"},
  
  {"https://edge.iono.fm/xice/ofm_live_low.aac",
    "41 OFM"},

  {"https://edge.iono.fm/xice/57_medium.mp3",
   "42 HOT 102.7 FM"},

  {"https://stream.zeno.fm/lnwzymabtd3vv",
   "43 Mix 93.8"},

  {"https://playerservices.streamtheworld.com/api/livestream-redirect/FM702.mp3",
   "44 Radio 702"},

  {"https://live.jacarandafm.com/jacarandahigh.mp3",
   "45 Jacaranda FM"}


};


// ============================================================
// NUMBER OF STATIONS
// ============================================================

const int NUMBER_OF_STATIONS =
  sizeof(stations) / sizeof(stations[0]);


// ============================================================
// STATION VARIABLES
// ============================================================

// Currently playing station
int currentStation = 0;

// Station currently being previewed
int previewStation = 0;


// ============================================================
// PREVIEW TIMER
// ============================================================

unsigned long lastEncoderActivity = 0;

bool previewActive = false;


// ============================================================
// ROTARY ENCODER INTERRUPT
// ============================================================

void IRAM_ATTR readEncoderISR()
{
  rotaryEncoder.readEncoder_ISR();
}


// ============================================================
// PRINT LCD LINE
// ============================================================

void printLCDLine(
  int row,
  const char *text
)
{
  lcd.setCursor(0, row);

  // Clear complete line
  lcd.print("                ");

  lcd.setCursor(0, row);

  // Print maximum 16 characters
  for (
    int i = 0;
    i < 16 && text[i] != '\0';
    i++
  )
  {
    lcd.print(text[i]);
  }
}


// ============================================================
// DISPLAY CURRENT STATION
// ============================================================

void displayCurrentStation()
{
  scrollText =
    stations[currentStation].name;

  scrollPosition = 0;

  // ----------------------------------------------------------
  // Short station name
  // ----------------------------------------------------------

  if (scrollText.length() <= 16)
  {
    scrolling = false;
    scrollWaiting = false;

    lcd.setCursor(0, 0);

    lcd.print("                ");

    lcd.setCursor(0, 0);

    lcd.print(scrollText);
  }

  // ----------------------------------------------------------
  // Long station name
  // ----------------------------------------------------------

  else
  {
    scrolling = false;

    scrollWaiting = true;

    scrollPosition = 0;

    scrollTimer = millis();

    lcd.setCursor(0, 0);

    lcd.print("                ");

    lcd.setCursor(0, 0);

    // Initially show first 16 characters
    for (
      int i = 0;
      i < 16;
      i++
    )
    {
      if (
        i < (int)scrollText.length()
      )
      {
        lcd.print(
          scrollText[i]
        );
      }
      else
      {
        lcd.print(" ");
      }
    }
  }

  // ----------------------------------------------------------
  // Bottom line is always blank
  // ----------------------------------------------------------

  printLCDLine(
    1,
    ""
  );
}


// ============================================================
// DISPLAY STATION PREVIEW
// ============================================================

void displayStationPreview()
{
  // IMPORTANT:
  // The top line remains the CURRENTLY PLAYING station.
  // It must not change while browsing.

  scrolling = false;

  scrollWaiting = false;

  printLCDLine(
    0,
    stations[currentStation].name
  );

  // Only bottom line changes during browsing
  printLCDLine(
    1,
    stations[previewStation].name
  );
}


// ============================================================
// UPDATE TOP LINE SCROLL
// ============================================================

void updateTopLineScroll()
{
  int textLength = scrollText.length();

  // No scrolling required
  if (textLength <= 16)
  {
    return;
  }

  // Initial pause before scrolling starts
  if (scrollWaiting)
  {
    if (millis() - scrollTimer < SCROLL_START_PAUSE)
    {
      return;
    }

    scrollWaiting = false;
    scrolling = true;
    scrollPosition = 0;
  }

  // Scroll timing
  if (millis() - scrollTimer < SCROLL_INTERVAL)
  {
    return;
  }

  scrollTimer = millis();

  // Move one character to the left
  scrollPosition++;

  // Wrap straight back to the start, no blank time
  int cycleLength = textLength + SCROLL_GAP;

  if (scrollPosition >= cycleLength)
  {
    scrollPosition = 0;
  }

  // Draw ONLY line 0
  lcd.setCursor(0, 0);

  for (int lcdPosition = 0; lcdPosition < 16; lcdPosition++)
  {
    int textIndex = (scrollPosition + lcdPosition) % cycleLength;

    if (textIndex < textLength)
    {
      lcd.print(scrollText[textIndex]);
    }
    else
    {
      lcd.print(" ");
    }
  }
}


// ============================================================
// CONNECT TO STATION
// ============================================================

void connectToStation(
  int stationNumber
)
{
  Serial.println();

  Serial.print(
    "Connecting to: "
  );

  Serial.println(
    stations[stationNumber].name
  );

  Serial.print(
    "URL: "
  );

  Serial.println(
    stations[stationNumber].url
  );

  audio.connecttohost(
    stations[stationNumber].url
  );
}


// ============================================================
// SAVE LAST STATION
// ============================================================

void saveLastStation()
{
  preferences.begin(
    "radio",
    false
  );

  preferences.putInt(
    "station",
    currentStation
  );

  preferences.end();


  Serial.print(
    "Saved station: "
  );

  Serial.print(
    currentStation + 1
  );

  Serial.print(" ");

  Serial.println(
    stations[currentStation].name
  );
}


// ============================================================
// LOAD LAST STATION
// ============================================================

int loadLastStation()
{
  preferences.begin(
    "radio",
    true
  );

  int savedStation =
    preferences.getInt(
      "station",
      0
    );

  preferences.end();


  // Safety check
  if (
    savedStation < 0 ||
    savedStation >= NUMBER_OF_STATIONS
  )
  {
    savedStation = 0;
  }


  Serial.print(
    "Last station: "
  );

  Serial.print(
    savedStation + 1
  );

  Serial.print(" ");

  Serial.println(
    stations[savedStation].name
  );


  return savedStation;
}


// ============================================================
// ============================================================
//
//                  WIFI SETUP FUNCTIONS
//
// ============================================================
// ============================================================


// ============================================================
// ENCODER SETTINGS FOR THE RADIO (STATION BROWSING)
// ============================================================

void configureEncoderForRadio()
{
  // The encoder only reports direction (clockwise or not).
  // The station number is wrapped by the code in loop(),
  // so the encoder itself must NOT wrap: a wrap would make
  // the library report a big jump in the wrong direction.
  rotaryEncoder.setBoundaries(
    0,
    100000,
    false
  );

  rotaryEncoder.setEncoderValue(
    50000
  );

  // Discard any leftover movement
  rotaryEncoder.encoderChanged();
}


// ============================================================
// ENCODER SETTINGS FOR THE WIFI SETUP
// A huge non-wrapping range, so direction is always clear.
// ============================================================

void configureEncoderForSetup()
{
  rotaryEncoder.setBoundaries(
    0,
    100000,
    false
  );

  rotaryEncoder.setEncoderValue(
    50000
  );

  // Discard any leftover movement
  rotaryEncoder.encoderChanged();
}


// ============================================================
// ENCODER STEP: +1 clockwise, -1 counter-clockwise, 0 none
// ============================================================

int encoderStep()
{
  int change =
    rotaryEncoder.encoderChanged();

  if (change > 0)
  {
    return 1;
  }

  if (change < 0)
  {
    return -1;
  }

  return 0;
}


// ============================================================
// BUTTON: SHORT AND LONG PRESS
// A long press is reported while the button is still held.
// ============================================================

#define BTN_NONE   0
#define BTN_SHORT  1
#define BTN_LONG   2


int readButton()
{
  static bool wasDown = false;
  static bool longFired = false;
  static unsigned long lastEdge = 0;
  static unsigned long downSince = 0;

  bool down =
    (digitalRead(ROTARY_ENCODER_BUTTON_PIN) == LOW);

  // Debounced press / release
  if (
    down != wasDown &&
    millis() - lastEdge > 30
  )
  {
    lastEdge = millis();
    wasDown = down;

    if (down)
    {
      downSince = millis();
      longFired = false;
    }
    else if (!longFired)
    {
      return BTN_SHORT;
    }
  }

  // Held long enough
  if (
    wasDown &&
    !longFired &&
    millis() - downSince >= LONG_PRESS_MS
  )
  {
    longFired = true;
    return BTN_LONG;
  }

  return BTN_NONE;
}


void waitForButtonRelease()
{
  while (digitalRead(ROTARY_ENCODER_BUTTON_PIN) == LOW)
  {
    delay(10);
  }

  delay(50);
}


// ============================================================
// GUEST WIFI STORAGE (FLASH)
// ============================================================

bool loadGuestWiFi(
  String &guestSsid,
  String &guestPassword
)
{
  preferences.begin(
    "wifi",
    true
  );

  guestSsid =
    preferences.getString("ssid", "");

  guestPassword =
    preferences.getString("pass", "");

  preferences.end();

  return guestSsid.length() > 0;
}


void saveGuestWiFi(
  const String &guestSsid,
  const String &guestPassword
)
{
  preferences.begin(
    "wifi",
    false
  );

  preferences.putString("ssid", guestSsid);
  preferences.putString("pass", guestPassword);

  preferences.end();

  Serial.print("Saved guest WiFi: ");
  Serial.println(guestSsid);
}


// ============================================================
// CONNECT TO A WIFI NETWORK (WITH TIMEOUT)
// ============================================================

bool connectWiFi(
  const String &networkName,
  const String &networkPassword,
  unsigned long timeoutMs
)
{
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(networkName);

  printLCDLine(0, "Connecting WiFi");
  printLCDLine(1, networkName.c_str());

  WiFi.disconnect();
  WiFi.mode(WIFI_STA);

  if (networkPassword.length() > 0)
  {
    WiFi.begin(
      networkName.c_str(),
      networkPassword.c_str()
    );
  }
  else
  {
    // Open network
    WiFi.begin(
      networkName.c_str()
    );
  }

  unsigned long startTime = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startTime < timeoutMs
  )
  {
    delay(250);

    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    return true;
  }

  // Stop further background retries
  WiFi.disconnect();

  return false;
}


// ============================================================
// IS THE MASTER NETWORK IN RANGE?
// ============================================================

bool masterNetworkVisible()
{
  printLCDLine(0, "Looking for WiFi");
  printLCDLine(1, "Please wait...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  int found = WiFi.scanNetworks();

  bool visible = false;

  for (int i = 0; i < found; i++)
  {
    if (WiFi.SSID(i) == MASTER_SSID)
    {
      visible = true;
      break;
    }
  }

  WiFi.scanDelete();

  return visible;
}


// ============================================================
// NETWORK LIST (FOR THE SETUP SCREEN)
// ============================================================

struct WifiNetwork
{
  String ssid;
  bool open;
};

std::vector<WifiNetwork> networks;


void scanForNetworks()
{
  printLCDLine(0, "Scanning WiFi...");
  printLCDLine(1, "");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  int found = WiFi.scanNetworks();

  networks.clear();

  for (
    int i = 0;
    i < found && (int)networks.size() < MAX_NETWORKS;
    i++
  )
  {
    String name = WiFi.SSID(i);

    // Skip hidden networks (no name)
    if (name.length() == 0)
    {
      continue;
    }

    // Skip duplicates (several access points, same name)
    bool duplicate = false;

    for (size_t k = 0; k < networks.size(); k++)
    {
      if (networks[k].ssid == name)
      {
        duplicate = true;
        break;
      }
    }

    if (duplicate)
    {
      continue;
    }

    WifiNetwork n;
    n.ssid = name;
    n.open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

    networks.push_back(n);
  }

  WiFi.scanDelete();
}


// Entry text: networks first, then "[Rescan]" at the end
String networkLabel(int index)
{
  if (index < (int)networks.size())
  {
    return networks[index].ssid;
  }

  if (index == (int)networks.size())
  {
    return "[Rescan]";
  }

  return "";
}


void drawNetworkList(int selected)
{
  String line0 = ">" + networkLabel(selected);
  String line1 = " " + networkLabel(selected + 1);

  printLCDLine(0, line0.c_str());
  printLCDLine(1, line1.c_str());
}


// Returns the chosen network index, or -1 for "rescan"
int pickNetwork()
{
  int total = networks.size() + 1;

  int selected = 0;

  drawNetworkList(selected);

  while (true)
  {
    int step = encoderStep();

    if (step != 0)
    {
      selected = (selected + step + total) % total;

      drawNetworkList(selected);
    }

    int button = readButton();

    if (button == BTN_SHORT)
    {
      if (selected < (int)networks.size())
      {
        return selected;
      }

      return -1;
    }

    // Long press = rescan
    if (button == BTN_LONG)
    {
      return -1;
    }

    delay(2);
  }
}


// ============================================================
// PASSWORD ENTRY
// ============================================================

char passwordCharFor(int index)
{
  if (index == 0)
  {
    return BACKSPACE_CHAR;
  }

  return PASSWORD_CHARS[index - 1];
}


void drawPassword(
  const String &typed,
  int charIndex
)
{
  // Typed text + the character currently being chosen
  String shown = typed;

  shown += passwordCharFor(charIndex);

  int length = shown.length();

  // Only the last 16 characters fit on the LCD
  int start = (length > 16) ? length - 16 : 0;

  printLCDLine(
    0,
    shown.substring(start).c_str()
  );

  // Underline cursor under the character being chosen
  lcd.setCursor(length - 1 - start, 0);
}


// Returns true = password entered, false = go back
bool enterPassword(
  const String &networkName,
  String &result
)
{
  printLCDLine(0, "Password for:");
  printLCDLine(1, networkName.c_str());

  delay(1500);

  String typed = "";

  // Start on "a" (index 0 is backspace)
  int charIndex = 1;

  printLCDLine(1, "Click=OK Hold=Go");

  lcd.cursor();

  drawPassword(typed, charIndex);

  while (true)
  {
    int step = encoderStep();

    if (step != 0)
    {
      charIndex =
        (charIndex + step + PASSWORD_ENTRIES) % PASSWORD_ENTRIES;

      drawPassword(typed, charIndex);
    }

    int button = readButton();

    // ------------------------------------------------------
    // SHORT PRESS: accept character and move on
    // ------------------------------------------------------

    if (button == BTN_SHORT)
    {
      if (charIndex == 0)
      {
        // Backspace
        if (typed.length() == 0)
        {
          // Nothing left to delete = go back to network list
          lcd.noCursor();

          return false;
        }

        typed.remove(typed.length() - 1);
      }
      else if (typed.length() < MAX_PASSWORD_LENGTH)
      {
        typed += passwordCharFor(charIndex);

        // Next position starts on "a" again
        charIndex = 1;
      }

      drawPassword(typed, charIndex);
    }

    // ------------------------------------------------------
    // LONG PRESS: finished, save and connect
    // ------------------------------------------------------

    if (button == BTN_LONG)
    {
      if (typed.length() < MIN_PASSWORD_LENGTH)
      {
        printLCDLine(1, "Min 8 characters");

        delay(1500);

        printLCDLine(1, "Click=OK Hold=Go");

        drawPassword(typed, charIndex);
      }
      else
      {
        result = typed;

        lcd.noCursor();

        return true;
      }
    }

    delay(2);
  }
}


// ============================================================
// WIFI SETUP (RUNS UNTIL A NETWORK IS CONNECTED)
// ============================================================

void runWiFiSetup()
{
  Serial.println("Starting WiFi setup");

  waitForButtonRelease();

  configureEncoderForSetup();

  while (true)
  {
    scanForNetworks();

    if (networks.empty())
    {
      printLCDLine(0, "No networks");
      printLCDLine(1, "found. Retrying");

      delay(2000);

      continue;
    }

    int choice = pickNetwork();

    // Rescan requested
    if (choice < 0)
    {
      continue;
    }

    String chosenSsid = networks[choice].ssid;

    String chosenPassword = "";

    // Open networks need no password
    if (!networks[choice].open)
    {
      if (!enterPassword(chosenSsid, chosenPassword))
      {
        // Backspace on an empty password = back to the list
        continue;
      }
    }

    if (
      connectWiFi(
        chosenSsid,
        chosenPassword,
        20000
      )
    )
    {
      // Only saved after a successful connection
      saveGuestWiFi(chosenSsid, chosenPassword);

      printLCDLine(0, "WiFi connected");
      printLCDLine(1, "Saved");

      delay(1500);

      break;
    }

    printLCDLine(0, "Connection failed");
    printLCDLine(1, "Try again");

    delay(2000);
  }

  configureEncoderForRadio();
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  delay(500);


  Serial.println();

  Serial.println(
    "=============================="
  );

  Serial.println(
    "ESP32 INTERNET RADIO"
  );

  Serial.println(
    "=============================="
  );


  // ----------------------------------------------------------
  // LCD
  // ----------------------------------------------------------

  Wire.begin(
    LCD_SDA,
    LCD_SCL
  );

  lcd.init();

  lcd.backlight();

  lcd.clear();


  printLCDLine(
    0,
    "    Waterval"
  );

  printLCDLine(
    1,
    "   WiFi Radio"
  );


  // ----------------------------------------------------------
  // SPLASH SCREEN
  // Holding the encoder button during the splash screen
  // forces the WiFi setup (for visiting a new place).
  // ----------------------------------------------------------

  pinMode(
    ROTARY_ENCODER_BUTTON_PIN,
    INPUT_PULLUP
  );

  bool forceSetup = false;

  unsigned long splashStart = millis();

  while (millis() - splashStart < 1500)
  {
    if (digitalRead(ROTARY_ENCODER_BUTTON_PIN) == LOW)
    {
      forceSetup = true;
    }

    delay(10);
  }


  // ----------------------------------------------------------
  // LOAD LAST STATION
  // ----------------------------------------------------------

  currentStation =
    loadLastStation();

  previewStation =
    currentStation;


  // ----------------------------------------------------------
  // ROTARY ENCODER
  // ----------------------------------------------------------

  rotaryEncoder.begin();

  rotaryEncoder.setup(
    readEncoderISR
  );

  configureEncoderForRadio();


  // ----------------------------------------------------------
  // WIFI
  // 1. Master (home) network if in range
  // 2. Saved guest network
  // 3. On-device setup
  // ----------------------------------------------------------

  lcd.clear();

  bool connected = false;

  if (forceSetup)
  {
    printLCDLine(0, "WiFi setup mode");
    printLCDLine(1, "Release button");

    waitForButtonRelease();
  }
  else
  {
    if (masterNetworkVisible())
    {
      connected = connectWiFi(
        MASTER_SSID,
        MASTER_PASSWORD,
        WIFI_CONNECT_TIMEOUT
      );
    }

    if (!connected)
    {
      String guestSsid;
      String guestPassword;

      if (loadGuestWiFi(guestSsid, guestPassword))
      {
        connected = connectWiFi(
          guestSsid,
          guestPassword,
          WIFI_CONNECT_TIMEOUT
        );
      }
    }
  }

  if (!connected)
  {
    runWiFiSetup();
  }


  Serial.println();

  Serial.println(
    "WiFi connected"
  );

  Serial.print(
    "IP address: "
  );

  Serial.println(
    WiFi.localIP()
  );


  // ----------------------------------------------------------
  // AUDIO
  // ----------------------------------------------------------

  audio.setPinout(
    I2S_BCLK,
    I2S_LRC,
    I2S_DOUT
  );


  // Fixed internal volume
  audio.setVolume(
    RADIO_VOLUME
  );


  // ----------------------------------------------------------
  // START LAST USED STATION
  // ----------------------------------------------------------

  connectToStation(
    currentStation
  );


  // ----------------------------------------------------------
  // DISPLAY CURRENT STATION
  // ----------------------------------------------------------

  displayCurrentStation();
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
  // ----------------------------------------------------------
  // AUDIO
  // ----------------------------------------------------------

  audio.loop();


  // ----------------------------------------------------------
  // ROTARY ENCODER
  // ----------------------------------------------------------

  int encoderChange =
    rotaryEncoder.encoderChanged();


  if (encoderChange != 0)
  {
    /*
       YOUR WORKING ENCODER DIRECTION:

       Clockwise:
           station + 1

       Counter-clockwise:
           station - 1

       We are leaving this exactly as
       you tested successfully.
    */


    if (encoderChange > 0)
    {
      // Clockwise
      previewStation++;
    }
    else
    {
      // Counter-clockwise
      previewStation--;
    }


    // --------------------------------------------------------
    // Wrap around
    // --------------------------------------------------------

    if (
      previewStation >=
      NUMBER_OF_STATIONS
    )
    {
      previewStation = 0;
    }


    if (
      previewStation < 0
    )
    {
      previewStation =
        NUMBER_OF_STATIONS - 1;
    }


    Serial.print(
      "Preview station: "
    );

    Serial.print(
      previewStation + 1
    );

    Serial.print(" ");

    Serial.println(
      stations[previewStation].name
    );


    // --------------------------------------------------------
    // SHOW PREVIEW
    // --------------------------------------------------------

    displayStationPreview();


    // Restart five-second timeout
    lastEncoderActivity =
      millis();

    previewActive = true;
  }


  // ----------------------------------------------------------
  // ROTARY BUTTON
  // ----------------------------------------------------------

  if (
    rotaryEncoder.isEncoderButtonClicked()
  )
  {
    if (previewActive)
    {
      // Select preview station
      currentStation =
        previewStation;


      Serial.print(
        "Selected station: "
      );

      Serial.print(
        currentStation + 1
      );

      Serial.print(" ");

      Serial.println(
        stations[currentStation].name
      );


      // ------------------------------------------------------
      // SAVE STATION
      // ------------------------------------------------------

      saveLastStation();


      // ------------------------------------------------------
      // START STATION
      // ------------------------------------------------------

      connectToStation(
        currentStation
      );


      // ------------------------------------------------------
      // DISPLAY NEW CURRENT STATION
      // ------------------------------------------------------

      displayCurrentStation();


      previewActive = false;
    }
  }


  // ----------------------------------------------------------
  // PREVIEW TIMEOUT
  // ----------------------------------------------------------

  if (
    previewActive &&
    millis() - lastEncoderActivity >=
    STATION_TIMEOUT
  )
  {
    Serial.println(
      "Preview cancelled"
    );


    previewStation =
      currentStation;


    // Restore current station display
    displayCurrentStation();


    previewActive = false;
  }


  // ----------------------------------------------------------
  // UPDATE TOP LINE SCROLL
  // ----------------------------------------------------------

  // This ONLY changes LCD line 0.
  // It does not touch the bottom line.
  //
  // During station preview, scrolling is stopped so
  // the current station remains clearly visible.
  //
  if (!previewActive)
  {
    updateTopLineScroll();
  }


  // ----------------------------------------------------------
  // BACKGROUND TASKS
  // ----------------------------------------------------------

  yield();
}
