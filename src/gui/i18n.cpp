#include "i18n.h"

#include <QLocale>
#include <QSettings>

namespace gui::i18n {

namespace {
constexpr const char *kLanguageKey = "language";
} // namespace

Language currentLanguage() {
    // Function-local static: initialized lazily on first use rather than
    // during global/static init, so QSettings only runs after main() has
    // set QCoreApplication's organization/application name (see main.cpp) -
    // constructing it any earlier would read/write the wrong ini location.
    static const Language language = [] {
        const QSettings settings;
        const int stored = settings.value(QLatin1String(kLanguageKey), -1).toInt();
        if (stored == static_cast<int>(Language::Russian)) return Language::Russian;
        if (stored == static_cast<int>(Language::English)) return Language::English;
        return QLocale::system().language() == QLocale::Russian ? Language::Russian : Language::English;
    }();
    return language;
}

void setLanguage(Language language) {
    QSettings settings;
    settings.setValue(QLatin1String(kLanguageKey), static_cast<int>(language));
}

QString t(const char *en, const char *ru) {
    return QString::fromUtf8(currentLanguage() == Language::Russian ? ru : en);
}

} // namespace gui::i18n
