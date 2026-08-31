#pragma once

namespace gui::theme {

enum class Mode { System, Light, Dark };

// Persisted in QSettings under "theme". Defaults to System (no saved
// preference yet).
Mode currentMode();
void setMode(Mode mode);

// Applies currentMode() to the running QApplication - safe to call
// repeatedly (e.g. right after setMode() for an immediate live switch, and
// once at startup in main.cpp).
void apply();

} // namespace gui::theme
