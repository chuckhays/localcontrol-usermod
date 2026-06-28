#include "wled.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include "KY040Encoder.h"

// #define XPT2046_IRQ 38
// #define XPT2046_MOSI 45
// #define XPT2046_MISO 39
// #define XPT2046_CLK 47
// #define XPT2046_CS 48

/*
 * Usermods allow you to add own functionality to WLED without touching core source files.
 * See the WLED docs: https://kno.wled.ge/advanced/custom-features/
 *
 * This is an example usermod. It demonstrates:
 *   - persistent settings via addToConfig() / readFromConfig()
 *   - JSON state read/write via addToJsonState() / readFromJsonState()
 *   - MQTT subscribe and message handling (guarded by WLED_DISABLE_MQTT)
 *   - button event handling
 *   - the Usermod Settings page via appendConfigData()
 *
 * To create your own usermod:
 *   1. Click "Use this template" on https://github.com/wled/wled-usermod-example to create your own repo.
 *   2. Rename the class and file to something descriptive.
 *   3. Reference your new repo in platformio_override.ini via custom_usermods.
 *
 * REGISTER_USERMOD() at the bottom self-registers the instance — no other
 * file edits are needed.
 */

class LocalControlUsermod : public Usermod {

  private:

    enum class EncoderId  { Rotary1, Rotary2 };
    enum class PanelItem  { Brightness = 0, Speed = 1, Intensity = 2, COUNT = 3 };

    PanelItem panelSelected = PanelItem::Brightness;

    // Cached values — panel column is redrawn only when its value changes
    uint8_t lastPanelBri       = 255;
    uint8_t lastPanelSpeed     = 255;
    uint8_t lastPanelIntensity = 255;

    void onEncoderRotate(EncoderId id, int delta) {
        Serial.printf("Encoder %d: %+d\n", (int)id, delta);
        if (id == EncoderId::Rotary1) {
            switch (panelSelected) {
                case PanelItem::Brightness:
                    bri = (uint8_t)constrain((int)bri + delta * 5, 0, 255);
                    stateUpdated(CALL_MODE_BUTTON);
                    break;
                case PanelItem::Speed:
                    effectSpeed = (uint8_t)constrain((int)effectSpeed + delta * 5, 0, 255);
                    strip.getMainSegment().speed = effectSpeed;
                    stateUpdated(CALL_MODE_BUTTON);
                    break;
                case PanelItem::Intensity:
                    effectIntensity = (uint8_t)constrain((int)effectIntensity + delta * 5, 0, 255);
                    strip.getMainSegment().intensity = effectIntensity;
                    stateUpdated(CALL_MODE_BUTTON);
                    break;
                default: break;
            }
        }
    }

    void onEncoderClick(EncoderId id) {
        Serial.printf("Encoder %d clicked\n", (int)id);
        if (id == EncoderId::Rotary1) {
            int next = ((int)panelSelected + 1) % (int)PanelItem::COUNT;
            PanelItem prev = panelSelected;
            panelSelected = (PanelItem)next;
            // Redraw both old and new column to update selection indicator
            drawPanelItem((int)prev, false);
            drawPanelItem((int)panelSelected, true);
        }
    }

    void onEncoderLongPress(EncoderId id) {
        Serial.printf("Encoder %d long press\n", (int)id);
    }

    TFT_eSPI tft = TFT_eSPI(TFT_WIDTH, TFT_HEIGHT);
    KY040Encoder rotary1{5, 6, 7,
        [this](int delta) { onEncoderRotate(EncoderId::Rotary1, delta); },
        [this]()          { onEncoderClick(EncoderId::Rotary1); },
        [this]()          { onEncoderLongPress(EncoderId::Rotary1); }};
    KY040Encoder rotary2{8, 18, 17,
        [this](int delta) { onEncoderRotate(EncoderId::Rotary2, delta); },
        [this]()          { onEncoderClick(EncoderId::Rotary2); },
        [this]()          { onEncoderLongPress(EncoderId::Rotary2); }};


    // Private class members. You can declare variables and functions only accessible to your usermod here
    bool enabled = false;
    bool initDone = false;
    unsigned long lastTime = 0;

    // config variables — boot defaults can be set here or inside readFromConfig()
    bool testBool = false;
    unsigned long testULong = 42424242;
    float testFloat = 42.42;
    String testString = "Forty-Two";
    uint16_t greatValue = 0;  // example persistent value exposed in JSON state

    // These config variables have defaults set inside readFromConfig()
    int testInt;
    long testLong;
    int8_t testPins[2];

    // string that are used multiple time (this will save some flash memory)
    static const char _name[];
    static const char _enabled[];


    // any private methods should go here (non-inline method should be defined out of class)
    void publishMqtt(const char* state, bool retain = false); // example for publishing MQTT message

    // ── Icon drawing helpers ─────────────────────────────────────────────────
    // Each draws a 20×20 icon with its top-left corner at (x, y).

    enum class ArrowDir { Right, Left, Up, Down };

    // Filled triangle arrow — menu position indicator
    void drawIconArrow(int x, int y, uint16_t color, ArrowDir dir = ArrowDir::Right) {
      switch (dir) {
        case ArrowDir::Right:
          tft.fillTriangle(x,      y+10, x+18, y+1,  x+18, y+18, color); break;
        case ArrowDir::Left:
          tft.fillTriangle(x+19,   y+10, x+1,  y+1,  x+1,  y+18, color); break;
        case ArrowDir::Up:
          tft.fillTriangle(x+10,   y,    x+1,  y+18, x+18, y+18, color); break;
        case ArrowDir::Down:
          tft.fillTriangle(x+10,   y+19, x+1,  y+1,  x+18, y+1,  color); break;
      }
    }

    // Circle with 8 tick-marks around it — brightness / sun
    void drawIconSun(int x, int y, uint16_t color) {
      int cx = x + 10, cy = y + 10;
      tft.fillCircle(cx, cy, 4, color);
      // 8 rays evenly spaced
      for (int i = 0; i < 8; i++) {
        float angle = i * (M_PI / 4.0f);
        int x1 = cx + (int)(cosf(angle) * 6);
        int y1 = cy + (int)(sinf(angle) * 6);
        int x2 = cx + (int)(cosf(angle) * 9);
        int y2 = cy + (int)(sinf(angle) * 9);
        tft.drawLine(x1, y1, x2, y2, color);
      }
    }

    // Two left-pointing triangles side-by-side — speed (180° flipped fast-forward)
    void drawIconSpeed(int x, int y, uint16_t color) {
      // left triangle
      tft.fillTriangle(
        x,      y + 1,
        x,      y + 18,
        x + 9,  y + 10,
        color
      );
      // right triangle
      tft.fillTriangle(
        x + 10, y + 1,
        x + 10, y + 18,
        x + 19, y + 10,
        color
      );
    }

    // Layered flame with round base and yellow teardrop core
    void drawIconFire(int x, int y, uint16_t /*color*/) {
      uint16_t red    = tft.color565(220, 40,  0);
      uint16_t orange = tft.color565(255, 120, 0);
      uint16_t yellow = tft.color565(255, 230, 0);

      // Outer red-orange: pointed top + round base
      tft.fillTriangle(x+10, y+1,  x+2,  y+14, x+18, y+14, red);
      tft.fillCircle  (x+10, y+14, 6,                        red);

      // Mid orange layer: narrower triangle + slightly smaller circle
      tft.fillTriangle(x+10, y+5,  x+5,  y+14, x+15, y+14, orange);
      tft.fillCircle  (x+10, y+14, 4,                        orange);

      // Yellow teardrop core: circle at base + triangle pointing up
      tft.fillCircle  (x+10, y+15, 3,                        yellow);
      tft.fillTriangle(x+10, y+10, x+8,  y+15, x+12, y+15, yellow);
    }
    // ── Control panel (top 240×100 px, three 80-px columns) ─────────────────
    //
    //  col 0            col 1            col 2
    //  ┌──────────┐    ┌──────────┐    ┌──────────┐
    //  │  [icon]  │    │  [icon]  │    │  [icon]  │   y=10 icon (20×20)
    //  │   128    │    │    50    │    │    75    │   y=36 value (font 2)
    //  │ ──────── │    │          │    │          │   y=56 underline if selected
    //  └──────────┘    └──────────┘    └──────────┘

    static constexpr int PANEL_COL_W   = 80;
    static constexpr int PANEL_ICON_Y  = 8;
    static constexpr int PANEL_VAL_Y   = 34;
    static constexpr int PANEL_LINE_Y  = 54;

    void drawPanelItem(int col, bool selected) {
        int cx = col * PANEL_COL_W;          // left edge
        int iconX = cx + 30;                 // centre the 20-px icon in 80-px column

        // Clear column
        tft.fillRect(cx, 0, PANEL_COL_W, 100, TFT_BLACK);

        // Icon
        switch ((PanelItem)col) {
            case PanelItem::Brightness:
                drawIconSun(iconX, PANEL_ICON_Y, selected ? TFT_YELLOW : tft.color565(180, 180, 0));
                break;
            case PanelItem::Speed:
                drawIconSpeed(iconX, PANEL_ICON_Y, selected ? TFT_GREEN : tft.color565(0, 160, 0));
                break;
            case PanelItem::Intensity:
                drawIconFire(iconX, PANEL_ICON_Y, 0);
                if (!selected) {
                    // Dim the fire icon by overlaying a semi-transparent-ish dark rect
                    // (TFT has no alpha — approximate with a dark tint fill at low opacity)
                    // Instead just recolour: redraw with grey tones
                    uint16_t dimRed = tft.color565(180, 30, 0);
                    uint16_t dimOrg = tft.color565(210, 100, 0);
                    uint16_t dimYel = tft.color565(210, 180, 0);
                    tft.fillTriangle(iconX+10, PANEL_ICON_Y+1,  iconX+2,  PANEL_ICON_Y+14, iconX+18, PANEL_ICON_Y+14, dimRed);
                    tft.fillCircle  (iconX+10, PANEL_ICON_Y+14, 6,                                                      dimRed);
                    tft.fillTriangle(iconX+10, PANEL_ICON_Y+5,  iconX+5,  PANEL_ICON_Y+14, iconX+15, PANEL_ICON_Y+14, dimOrg);
                    tft.fillCircle  (iconX+10, PANEL_ICON_Y+14, 4,                                                      dimOrg);
                    tft.fillCircle  (iconX+10, PANEL_ICON_Y+15, 3,                                                      dimYel);
                    tft.fillTriangle(iconX+10, PANEL_ICON_Y+10, iconX+8,  PANEL_ICON_Y+15, iconX+12, PANEL_ICON_Y+15, dimYel);
                }
                break;
            default: break;
        }

        // Value
        uint8_t val = 0;
        switch ((PanelItem)col) {
            case PanelItem::Brightness: val = bri;             break;
            case PanelItem::Speed:      val = effectSpeed;     break;
            case PanelItem::Intensity:  val = effectIntensity; break;
            default: break;
        }
        tft.setTextColor(selected ? TFT_WHITE : tft.color565(200, 200, 200), TFT_BLACK);
        tft.drawCentreString(String(val), cx + 40, PANEL_VAL_Y, 2);

        // Selection underline
        if (selected) {
            tft.drawLine(cx + 8, PANEL_LINE_Y, cx + PANEL_COL_W - 8, PANEL_LINE_Y, TFT_WHITE);
        }
    }

    void drawPanel() {
        for (int i = 0; i < (int)PanelItem::COUNT; i++) {
            drawPanelItem(i, (PanelItem)i == panelSelected);
        }
    }
    // ────────────────────────────────────────────────────────────────────────


  public:

    // non WLED related methods, may be used for data exchange between usermods (non-inline methods should be defined out of class)

    /**
     * Enable/Disable the usermod
     */
    inline void enable(bool enable) { enabled = enable; }

    /**
     * Get usermod enabled/disabled state
     */
    inline bool isEnabled() { return enabled; }

    // To access this usermod from another usermod, cast the result of UsermodManager::lookup():
    //   LocalControlUsermod* um = (LocalControlUsermod*) UsermodManager::lookup(USERMOD_ID_LOCALCONTROL);
    // Make sure to assign a unique ID in getId()!


    /*
     * setup() is called once at boot. WiFi is not yet connected at this point.
     * readFromConfig() is called prior to setup()
     * You can use it to initialize variables, sensors or similar.
     */
    void setup() override {
      // do your set-up here
      Serial.println("Setting up localcontrol");

      // Use this calibration code in setup():
      // uint16_t calData[5] = { 398, 3374, 400, 3280, 7 };
      // tft.setTouch(calData);

      rotary1.begin();
      rotary2.begin();

      tft.init();
      tft.setRotation(2);
      tft.fillScreen(TFT_BLACK);

      drawPanel();

      initDone = true;
    }

    /*
     * connected() is called every time the WiFi is (re)connected
     * Use it to initialize network interfaces
     */
    void connected() override {
    }

    /*
     * loop() is called continuously. Here you can check for events, read sensors, etc.
     * 
     * Tips:
     * 1. You can use "if (WLED_CONNECTED)" to check for a successful network connection.
     *    Additionally, "if (WLED_MQTT_CONNECTED)" is available to check for a connection to an MQTT broker.
     * 
     * 2. Try to avoid using the delay() function. NEVER use delays longer than 10 milliseconds.
     *    Instead, use a timer check as shown here.
     */
    void loop() override {
      // if usermod is disabled or called during strip updating just exit
      // NOTE: on very long strips strip.isUpdating() may always return true so update accordingly
      // if (!enabled || strip.isUpdating()) return;
      if (strip.isUpdating()) return;

      rotary1.loop();
      rotary2.loop();

      // Redraw panel columns whose values changed (encoder or external WLED change)
      if (bri != lastPanelBri) {
        lastPanelBri = bri;
        drawPanelItem((int)PanelItem::Brightness, panelSelected == PanelItem::Brightness);
      }
      if (effectSpeed != lastPanelSpeed) {
        lastPanelSpeed = effectSpeed;
        drawPanelItem((int)PanelItem::Speed, panelSelected == PanelItem::Speed);
      }
      if (effectIntensity != lastPanelIntensity) {
        lastPanelIntensity = effectIntensity;
        drawPanelItem((int)PanelItem::Intensity, panelSelected == PanelItem::Intensity);
      }

      bool show = (millis() - lastTime > 1000);
      if (show) {
        lastTime = millis();
      }

      if (show) {
        static uint8_t lastEffect = 255;
        if (effectCurrent != lastEffect) {
          lastEffect = effectCurrent;
          char effectName[32];
          strncpy_P(effectName, strip.getModeData(effectCurrent), sizeof(effectName) - 1);
          effectName[sizeof(effectName) - 1] = '\0';
          char* at = strchr(effectName, '@');
          if (at) *at = '\0';
          tft.fillRect(0, TFT_HEIGHT / 2 - 10, TFT_WIDTH, 20, TFT_BLACK);
          tft.drawCentreString(effectName, TFT_WIDTH / 2, TFT_HEIGHT / 2 - 8, 2);
        }
      }

        uint16_t x, y;
  bool touched = tft.getTouch(&x, &y);
  if (touched) {
    Serial.printf("Touch detected at x: %i, y: %i\n", x, y);
  }
    }


    /*
     * addToJsonInfo() can be used to add custom entries to the /json/info part of the JSON API.
     * Creating an "u" object allows you to add custom key/value pairs to the Info section of the WLED web UI.
     * Below it is shown how this could be used for e.g. a light sensor
     */
    void addToJsonInfo(JsonObject& root) override
    {
      // if "u" object does not exist yet wee need to create it
      JsonObject user = root["u"];
      if (user.isNull()) user = root.createNestedObject("u");

      //this code adds "u":{"ExampleUsermod":[20," lux"]} to the info object
      //int reading = 20;
      //JsonArray lightArr = user.createNestedArray(FPSTR(_name))); //name
      //lightArr.add(reading); //value
      //lightArr.add(F(" lux")); //unit

      // if you are implementing a sensor usermod, you may publish sensor data
      //JsonObject sensor = root[F("sensor")];
      //if (sensor.isNull()) sensor = root.createNestedObject(F("sensor"));
      //temp = sensor.createNestedArray(F("light"));
      //temp.add(reading);
      //temp.add(F("lux"));
    }


    /*
     * addToJsonState() adds entries to the /json/state response. Clients can read and write these.
     * Use this to expose runtime state that should be controllable via the API.
     * addToJsonState() is NOT called for presets — use addToConfig() for persistent values.
     */
    void addToJsonState(JsonObject& root) override
    {
      if (!initDone || !enabled) return;  // prevent crash on boot applyPreset()

      JsonObject usermod = root[FPSTR(_name)];
      if (usermod.isNull()) usermod = root.createNestedObject(FPSTR(_name));

      usermod["greatValue"] = greatValue;
    }


    /*
     * readFromJsonState() receives values a client POSTs to /json/state.
     * The JSON key nesting matches what addToJsonState() writes — clients send back the same structure.
     */
    void readFromJsonState(JsonObject& root) override
    {
      if (!initDone) return;  // prevent crash on boot applyPreset()

      JsonObject usermod = root[FPSTR(_name)];
      if (!usermod.isNull()) {
        // getJsonValue copies the value if present and returns true; leaves the variable unchanged if missing
        getJsonValue(usermod["greatValue"], greatValue);
      }
    }


    /*
     * addToConfig() saves settings to cfg.json under the "um" object. WLED calls this whenever settings are saved.
     * The Usermod Settings page in the UI is generated automatically from the keys you write here.
     *
     * Usermod Settings Overview:
     * - Numeric values are treated as floats in the browser.
     *   - If the numeric value entered into the browser contains a decimal point, it will be parsed as a C float
     *     before being returned to the Usermod.  The float data type has only 6-7 decimal digits of precision, and
     *     doubles are not supported, numbers will be rounded to the nearest float value when being parsed.
     *     The range accepted by the input field is +/- 1.175494351e-38 to +/- 3.402823466e+38.
     *   - If the numeric value entered into the browser doesn't contain a decimal point, it will be parsed as a
     *     C int32_t (range: -2147483648 to 2147483647) before being returned to the usermod.
     *     Overflows or underflows are truncated to the max/min value for an int32_t, and again truncated to the type
     *     used in the Usermod when reading the value from ArduinoJson.
     * - Pin values can be treated differently from an integer value by using the key name "pin"
     *   - "pin" can contain a single or array of integer values
     *   - On the Usermod Settings page there is simple checking for pin conflicts and warnings for special pins
     *     - Red color indicates a conflict.  Yellow color indicates a pin with a warning (e.g. an input-only pin)
     *   - Tip: use int8_t to store the pin value in the Usermod, so a -1 value (pin not set) can be used
     *
     * To force a config write from loop(), call serializeConfig() — but use it sparingly (flash wear,
     * possible LED stutter). Never call it from a network callback.
     */
    void addToConfig(JsonObject& root) override
    {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top[FPSTR(_enabled)] = enabled;
      top["great"] = greatValue;
      top["testBool"] = testBool;
      top["testInt"] = testInt;
      top["testLong"] = testLong;
      top["testULong"] = testULong;
      top["testFloat"] = testFloat;
      top["testString"] = testString;
      JsonArray pinArray = top.createNestedArray("pin");
      pinArray.add(testPins[0]);
      pinArray.add(testPins[1]); 
    }


    /*
     * readFromConfig() is called before setup() and again after settings are saved.
     * Return false if any expected keys were missing — WLED will then call addToConfig() to write the defaults.
     * getJsonValue(src, dest) copies the value if present and returns true; leaves dest unchanged if missing.
     * getJsonValue(src, dest, default) also assigns a default when the key is absent.
     */
    bool readFromConfig(JsonObject& root) override
    {
      JsonObject top = root[FPSTR(_name)];

      bool configComplete = !top.isNull();

      configComplete &= getJsonValue(top["great"], greatValue);
      configComplete &= getJsonValue(top["testBool"], testBool);
      configComplete &= getJsonValue(top["testULong"], testULong);
      configComplete &= getJsonValue(top["testFloat"], testFloat);
      configComplete &= getJsonValue(top["testString"], testString);

      // A 3-argument getJsonValue() assigns the 3rd argument as a default value if the Json value is missing
      configComplete &= getJsonValue(top["testInt"], testInt, 42);  
      configComplete &= getJsonValue(top["testLong"], testLong, -42424242);

      // "pin" fields have special handling in settings page (or some_pin as well)
      configComplete &= getJsonValue(top["pin"][0], testPins[0], -1);
      configComplete &= getJsonValue(top["pin"][1], testPins[1], -1);

      return configComplete;
    }


    /*
     * appendConfigData() is called when the Usermod Settings page renders.
     * Write JavaScript snippets to settingsScript to add helper text or dropdowns for your config fields.
     * addInfo('<ModName>:<key>', 1, '<html>') adds a tooltip/label next to the field.
     * addDropdown / addOption replace a plain text input with a <select>.
     */
    void appendConfigData(Print& settingsScript) override
    {
      settingsScript.print(F("addInfo('")); settingsScript.print(FPSTR(_name)); settingsScript.print(F(":great',1,'<i>(this is a great config value)</i>');"));
      settingsScript.print(F("addInfo('")); settingsScript.print(FPSTR(_name)); settingsScript.print(F(":testString',1,'enter any string you want');"));
      settingsScript.print(F("dd=addDropdown('")); settingsScript.print(FPSTR(_name)); settingsScript.print(F("','testInt');"));
      settingsScript.print(F("addOption(dd,'Nothing',0);"));
      settingsScript.print(F("addOption(dd,'Everything',42);"));
    }


    /*
     * handleOverlayDraw() is called just before every show() (LED strip update frame) after effects have set the colors.
     * Use this to blank out some LEDs or set them to a different color regardless of the set effect mode.
     * Commonly used for custom clocks (Cronixie, 7 segment)
     */
    void handleOverlayDraw() override
    {
      //strip.setPixelColor(0, RGBW32(0,0,0,0)) // set the first pixel to black
    }


    /**
     * handleButton() can be used to override default button behaviour. Returning true
     * will prevent button working in a default way.
     * Replicating button.cpp
     */
    bool handleButton(uint8_t b) override {
      yield();
      // ignore certain button types as they may have other consequences
      if (!enabled
       || buttons[b].type == BTN_TYPE_NONE
       || buttons[b].type == BTN_TYPE_RESERVED
       || buttons[b].type == BTN_TYPE_PIR_SENSOR
       || buttons[b].type == BTN_TYPE_ANALOG
       || buttons[b].type == BTN_TYPE_ANALOG_INVERTED) {
        return false;
      }

      bool handled = false;
      // do your button handling here
      return handled;
    }
  

#ifndef WLED_DISABLE_MQTT
    /**
     * onMqttMessage() is called when a subscribed MQTT topic receives a message.
     * topic only contains stripped topic (part after /wled/MAC).
     * Return true to mark the message handled (prevents other usermods from seeing it).
     * These methods must be inside a #ifndef WLED_DISABLE_MQTT guard — MQTT support is a compile-time option.
     * See usermods/multi_relay for a well-structured subscribe-in-connect / handle-in-message example.
     */
    bool onMqttMessage(char* topic, char* payload) override {
      //if (strlen(topic) == 8 && strncmp_P(topic, PSTR("/command"), 8) == 0) {
      //  String action = payload;
      //  if (action == "on")     { enabled = true;  return true; }
      //  if (action == "off")    { enabled = false; return true; }
      //  if (action == "toggle") { enabled = !enabled; return true; }
      //}
      return false;
    }

    /**
     * onMqttConnect() is called when MQTT connection is established.
     * Subscribe to topics here; mqttDeviceTopic holds the device-specific prefix.
     */
    void onMqttConnect(bool sessionPresent) override {
      //char subuf[64];
      //if (mqttDeviceTopic[0] != 0) {
      //  strcpy(subuf, mqttDeviceTopic);
      //  strcat_P(subuf, PSTR("/command"));
      //  mqtt->subscribe(subuf, 0);
      //}
    }
#endif


    /**
     * onStateChanged() is used to detect WLED state change
     * @mode parameter is CALL_MODE_... parameter used for notifications
     */
    void onStateChange(uint8_t mode) override {
      // do something if WLED state changed (color, brightness, effect, preset, etc)
    }


    /*
     * getId() allows you to optionally give your usermod a unique ID.
     * The base class returns USERMOD_ID_UNSPECIFIED, which is correct for most custom usermods.
     * Override only if you need reliable cross-usermod lookup via UsermodManager::lookup()
     * and have multiple usermods with the same ID registered simultaneously.
     */
    // uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }

   //More methods can be added in the future, this example will then be extended.
   //Your usermod will remain compatible as it does not need to implement all methods from the Usermod base class!
};


// add more strings here to reduce flash memory usage
const char LocalControlUsermod::_name[]    PROGMEM = "LocalControl";
const char LocalControlUsermod::_enabled[] PROGMEM = "enabled";


// implementation of non-inline member methods

void LocalControlUsermod::publishMqtt(const char* state, bool retain)
{
#ifndef WLED_DISABLE_MQTT
  //Check if MQTT Connected, otherwise it will crash the 8266
  if (WLED_MQTT_CONNECTED) {
    char subuf[64];
    strcpy(subuf, mqttDeviceTopic);
    strcat_P(subuf, PSTR("/example"));
    mqtt->publish(subuf, 0, retain, state);
  }
#endif
}

static LocalControlUsermod local_control_usermod;
REGISTER_USERMOD(local_control_usermod);
