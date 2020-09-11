void displayInit()
{
    static bool isInit = false;
    if (isInit) {
        return;
    }
    isInit = true;
    display.init();
    display.setRotation(1);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSans9pt7b);
    display.setTextSize(0);
    display.firstPage();
}

void displayMessage(const char *message)
{
    // const char HelloWorld[] = "Hello World!";

    int16_t tbx, tby; uint16_t tbw, tbh;
    display.getTextBounds(message, 0, 0, &tbx, &tby, &tbw, &tbh);
    // center bounding box by transposition of origin:
    uint16_t x = ((display.width() - tbw) / 2) - tbx;
    uint16_t y = ((display.height() - tbh) / 2) - tby;
    display.setFullWindow();
    display.firstPage();
    do
    {
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(x, y);
        display.print(message);
    }
    while (display.nextPage());
    Serial.println("displayMessage() done");
}