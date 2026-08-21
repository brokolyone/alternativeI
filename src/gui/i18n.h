#pragma once

#include <QString>

namespace gui::i18n {

// The whole UI is authored bilingually inline (see t() below) rather than
// via Qt Linguist .ts/.qm files - there's no lupdate/lrelease step in this
// project's build, so keeping English/Russian pairs at each call site is
// simpler to keep in sync than a separate translation catalog would be.
enum class Language { English, Russian };

// Persisted in QSettings under "language". On first call (no saved
// preference yet) defaults to Russian if the OS locale is Russian,
// English otherwise. Cached for the process's lifetime - see setLanguage().
Language currentLanguage();

// Persists the new language for future launches. Does NOT change what
// currentLanguage()/t() return in this already-running process (widgets
// built with the old language stay as they are) - callers should tell the
// user the change takes effect after a restart.
void setLanguage(Language language);

// Returns `ru` when the current language is Russian, `en` otherwise. Pass
// plain UTF-8 literals; %1/%2/... placeholders for QString::arg() work as
// usual in either string.
QString t(const char *en, const char *ru);

} // namespace gui::i18n
