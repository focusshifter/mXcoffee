#pragma once

#ifdef SIMULATOR

#include <OpenFontRender.h>
#include "simulator_backend.h"

// SimFontRender extends OpenFontRender to provide a simulator-specific implementation
class SimFontRender : public OpenFontRender {
private:
    SimulatorCanvas* simCanvas;
    uint16_t fontColor;

public:
    SimFontRender() : simCanvas(nullptr), fontColor(0xFFFF) {}
    
    void setSimCanvas(SimulatorCanvas* canvas) {
        simCanvas = canvas;
    }
    
    // Override setDrawer to capture the SimulatorCanvas
    void setDrawer(void* drawer) override {
        simCanvas = static_cast<SimulatorCanvas*>(drawer);
        OpenFontRender::setDrawer(drawer);
    }
    
    // Override loadFont to skip actual font loading and just return success
    int loadFont(const char* path) override {
        printf("SimFontRender::loadFont called with path %s (simulator mock)\n", path);
        return 0; // Return success
    }
    
    // Override setFontColor to store the color
    void setFontColor(uint16_t color) override {
        fontColor = color;
        if (simCanvas) {
            simCanvas->setTextColor(color, 0);
        }
    }
    
    // Override setFontSize to call the SimulatorCanvas method
    void setFontSize(float size) override {
        if (simCanvas) {
            simCanvas->setFontSize(size);
        }
    }
    
    // Override drawString to use SimulatorCanvas methods
    void drawString(const char* text, int x, int y) override {
        if (simCanvas) {
            simCanvas->drawString(text, x, y);
        }
    }
};

#endif // SIMULATOR