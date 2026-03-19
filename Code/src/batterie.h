#pragma once

// Globally accessible battery percentage (0–100, or -1 if not yet measured)
extern int batterie_percent;

// Call once from setup()
void batterie_init();

// Call every loop() iteration
void batterie_update();
