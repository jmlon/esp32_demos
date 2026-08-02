#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <SPI.h>

// Notes:
// - Use ESP32C3 Dev Module on /dev/ttyACM0

// Define your ESP32-C3 pins
#define TFT_CS     2
#define TFT_RST    1
#define TFT_DC     7

// Initialize the Adafruit ST7735 instance using Hardware SPI
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  // Force the ESP32-C3 Hardware SPI to use your exact pins:
  // SPI.begin(CLK, MISO, MOSI, SS);
  SPI.begin(4, -1, 6, -1);

  // Initialize the ST7735S chip (use INITR_BLACKTAB or INITR_REDTAB depending on your screen)
  tft.initR(INITR_GREENTAB);         // My display used this one, avoids offset problems
//   tft.initR(INITR_BLACKTAB);
//   tft.initR(INITR_REDTAB);




  // Set orientation (0-3)
  tft.setRotation(1); 
  
  // Fill screen with a background color
  tft.fillScreen(ST7735_BLACK);

  // Print colorful test text
  tft.setCursor(0, 10);
  tft.setTextColor(ST7735_GREEN);
  tft.setTextSize(2);
  tft.println("ESP32-C3");
  
  tft.setTextColor(ST7735_CYAN);
  tft.setTextSize(1);
  tft.println("ST7735 Color TFT");
  tft.println("-------------------");
  
  tft.setTextColor(ST7735_WHITE);
  tft.print("CLK: "); tft.println("GPIO4");
  tft.print("SDA: "); tft.println("GPIO6");
}

void loop() {
  // Simple color cycle animation
  static uint16_t color = ST7735_RED;
  
  tft.fillRect(0, 80, 160, 20, ST7735_BLACK); // Clear local area
  tft.setCursor(0, 80);
  tft.setTextColor(color);
  tft.setTextSize(2);
  tft.println("  ACTIVE");

  // Cycle colors
  if (color == ST7735_RED) color = ST7735_YELLOW;
  else if (color == ST7735_YELLOW) color = ST7735_BLUE;
  else color = ST7735_RED;

  delay(1000);
}
