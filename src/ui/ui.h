#ifndef UI_H
#define UI_H

#include "canvas_wrapper.h"
#include "system_wrapper.h"

class UI {
public:
 UI() = default;
 void drawGraph();
};

extern UI ui; // Declaration only

#endif