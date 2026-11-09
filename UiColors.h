#pragma once

#include <QColor>

// ============================================================
// UI 颜色常量（支持深色模式切换）
// ============================================================

namespace UiColors {
    // 浅色模式（默认）
    inline QColor PRIMARY() { return QColor(26, 115, 232); }
    inline QColor PRIMARY_LIGHT() { return QColor(232, 240, 254); }
    inline QColor TEXT_PRIMARY() { return QColor(32, 33, 36); }
    inline QColor TEXT_SECONDARY() { return QColor(95, 99, 104); }
    inline QColor TEXT_DISABLED() { return QColor(189, 193, 198); }
    inline QColor BG_TOOLBAR() { return QColor(255, 255, 255); }
    inline QColor BG_HOVER() { return QColor(232, 234, 237); }
    inline QColor BG_PRESSED() { return QColor(218, 220, 224); }
    inline QColor BORDER() { return QColor(218, 220, 224); }
    inline QColor BORDER_BOTTOM() { return QColor(232, 234, 237); }
    inline QColor MODIFIED_INDICATOR() { return QColor(249, 171, 0); }
    inline QColor EDGE_COLOR() { return QColor(0, 0, 139); }
    inline QColor NODE_FILL() { return QColor(211, 234, 253); }
    inline QColor NODE_BORDER() { return QColor(211, 234, 253); }
    inline QColor NODE_TEXT() { return QColor(5, 56, 96); }
    inline QColor HIGHLIGHT() { return QColor(0, 120, 255); }
    inline QColor HOVER() { return QColor(180, 210, 240); }
    inline QColor SCROLLBAR_BG() { return QColor(240, 240, 240); }
    inline QColor SCROLLBAR_HANDLE() { return QColor(192, 192, 192); }
    inline QColor SCROLLBAR_HANDLE_HOVER() { return QColor(160, 160, 160); }

    // 深色模式（VSCode风格）
    inline QColor DARK_BG_TOOLBAR() { return QColor(51, 51, 51); }
    inline QColor DARK_BG_CANVAS() { return QColor(30, 30, 30); }
    inline QColor DARK_TEXT_PRIMARY() { return QColor(204, 204, 204); }
    inline QColor DARK_TEXT_SECONDARY() { return QColor(154, 160, 166); }
    inline QColor DARK_BORDER() { return QColor(30, 30, 30); }
    inline QColor DARK_BORDER_BOTTOM() { return QColor(30, 30, 30); }
    inline QColor DARK_BG_HOVER() { return QColor(74, 74, 74); }
    inline QColor DARK_BG_PRESSED() { return QColor(90, 90, 90); }
    inline QColor DARK_EDGE_COLOR() { return QColor(100, 160, 220); }
    inline QColor DARK_NODE_FILL() { return QColor(37, 37, 38); }
    inline QColor DARK_NODE_BORDER() { return QColor(60, 60, 60); }
    inline QColor DARK_NODE_TEXT() { return QColor(204, 204, 204); }
    inline QColor DARK_HIGHLIGHT() { return QColor(0, 122, 204); }
    inline QColor DARK_HOVER() { return QColor(74, 74, 74); }
    inline QColor DARK_SCROLLBAR_BG() { return QColor(45, 45, 45); }
    inline QColor DARK_SCROLLBAR_HANDLE() { return QColor(75, 75, 75); }
    inline QColor DARK_SCROLLBAR_HANDLE_HOVER() { return QColor(100, 100, 100); }

    extern bool s_isDark;
    inline void setDarkMode(bool dark) { s_isDark = dark; }
    inline bool isDarkMode() { return s_isDark; }

    inline QColor edgeColor() { return s_isDark ? DARK_EDGE_COLOR() : EDGE_COLOR(); }
    inline QColor nodeFill() { return s_isDark ? DARK_NODE_FILL() : NODE_FILL(); }
    inline QColor nodeBorder() { return s_isDark ? DARK_NODE_BORDER() : NODE_BORDER(); }
    inline QColor nodeText() { return s_isDark ? DARK_NODE_TEXT() : NODE_TEXT(); }
    inline QColor highlight() { return s_isDark ? DARK_HIGHLIGHT() : HIGHLIGHT(); }
    inline QColor hover() { return s_isDark ? DARK_HOVER() : HOVER(); }
}
