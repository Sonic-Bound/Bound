#pragma once

// The wallet app's look: Qt's Fusion style with BOUND's colours, light or dark to match the
// system, and the logo drawn in code (no image files to ship).

#include <QColor>
#include <QFont>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QString>

class QApplication;

namespace bound::gui {

struct Colors {
    bool dark = false;
    QColor window, card, text, muted, border, accent, accent_text, positive, negative, warning_bg, warning_border, sidebar,
        sidebar_text, sidebar_selected;
};

// Sets the style, palette and style sheet; call once, before any window is made.
void apply_style(QApplication& app);
const Colors& colors();

QPixmap logo(int size);  // the round BOUND mark
QIcon app_icon();
QFont mono_font(int point_size = 0);  // 0: the application's size

// Building blocks of the pages.
QFrame* card(QWidget* parent = nullptr);          // a rounded panel
QLabel* page_title(const QString& text, QWidget* parent = nullptr);
QLabel* muted_label(const QString& text, QWidget* parent = nullptr);
QLabel* warning_banner(const QString& text, QWidget* parent = nullptr);

}  // namespace bound::gui
