#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

namespace gui::theme {

namespace {
constexpr const char *kThemeKey = "theme";

QPalette darkPalette() {
    QPalette palette;
    const QColor windowColor(53, 53, 53);
    const QColor baseColor(35, 35, 35);
    const QColor textColor(220, 220, 220);
    palette.setColor(QPalette::Window, windowColor);
    palette.setColor(QPalette::WindowText, textColor);
    palette.setColor(QPalette::Base, baseColor);
    palette.setColor(QPalette::AlternateBase, windowColor);
    palette.setColor(QPalette::ToolTipBase, textColor);
    palette.setColor(QPalette::ToolTipText, textColor);
    palette.setColor(QPalette::Text, textColor);
    palette.setColor(QPalette::Button, windowColor);
    palette.setColor(QPalette::ButtonText, textColor);
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, QColor(100, 170, 255));
    palette.setColor(QPalette::Highlight, QColor(70, 120, 190));
    palette.setColor(QPalette::HighlightedText, Qt::black);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
    return palette;
}
} // namespace

Mode currentMode() {
    // Unlike i18n::currentLanguage(), this is *not* cached: theme changes
    // apply live (see apply()) rather than requiring a restart, so a
    // freshly-opened SettingsDialog must see whatever was set last, even
    // within the same process run.
    const QSettings settings;
    const int stored = settings.value(QLatin1String(kThemeKey), -1).toInt();
    if (stored == static_cast<int>(Mode::Light)) return Mode::Light;
    if (stored == static_cast<int>(Mode::Dark)) return Mode::Dark;
    return Mode::System;
}

void setMode(Mode mode) {
    QSettings settings;
    settings.setValue(QLatin1String(kThemeKey), static_cast<int>(mode));
}

void apply() {
    const Mode mode = currentMode();

    if (mode == Mode::System) {
        qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        qApp->setPalette(qApp->style()->standardPalette());
        return;
    }

    qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    qApp->setPalette(mode == Mode::Dark ? darkPalette() : qApp->style()->standardPalette());
}

} // namespace gui::theme
