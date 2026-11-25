/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * FontDaw
 * The notation-first DAW
 *
 * Copyright (C) 2026 FontDaw contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "windowgeometrycheck.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QStringList>

using namespace muse::dock;

namespace {
QRect rectFromJson(const QJsonValue& value)
{
    const QJsonObject obj = value.toObject();
    return QRect(obj.value("x").toInt(), obj.value("y").toInt(), obj.value("width").toInt(), obj.value("height").toInt());
}

QString rectToString(const QRect& rect)
{
    return QString("%1,%2 %3x%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}

QJsonObject rectToJson(const QRect& rect)
{
    QJsonObject obj;
    obj["x"] = rect.x();
    obj["y"] = rect.y();
    obj["width"] = rect.width();
    obj["height"] = rect.height();
    return obj;
}

//! KDDockWidgets::WindowState: Maximized = 2, FullScreen = 4. The restore applies the normal
//! geometry of a window in one of these states, not its geometry.
bool isMaximizedOrFullScreen(const QJsonObject& window)
{
    return window.value("windowState").toInt() & (2 | 4);
}

bool isTooSmall(const QRect& rect)
{
    return rect.width() < WindowGeometryCheck::MIN_WIDTH || rect.height() < WindowGeometryCheck::MIN_HEIGHT;
}
}

QByteArray WindowGeometryCheck::repairNormalGeometry(const QByteArray& layout, QString* note, bool leaveFullScreen)
{
    if (layout.isEmpty()) {
        return layout;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(layout, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return layout;
    }

    QJsonObject root = doc.object();
    QJsonArray mainWindows = root.value("mainWindows").toArray();
    bool repaired = false;
    QStringList notes;

    for (qsizetype i = 0; i < mainWindows.size(); ++i) {
        QJsonObject mainWindow = mainWindows.at(i).toObject();
        if (!isMaximizedOrFullScreen(mainWindow) || !mainWindow.contains("geometry")) {
            continue;
        }

        QRect normalGeometry = rectFromJson(mainWindow.value("normalGeometry"));
        const QRect geometry = rectFromJson(mainWindow.value("geometry"));
        if (isTooSmall(normalGeometry) && !isTooSmall(geometry)) {
            notes << QString("normal geometry of the maximized or full-screen main window was %1, using %2")
                .arg(rectToString(normalGeometry), rectToString(geometry));
            normalGeometry = geometry;
            mainWindow["normalGeometry"] = rectToJson(normalGeometry);
            repaired = true;
        }

        //! KDDockWidgets::WindowState::FullScreen = 4
        const int windowState = mainWindow.value("windowState").toInt();
        if (leaveFullScreen && (windowState & 4) && !isTooSmall(normalGeometry)) {
            notes << QString("full-screen main window, restored as a normal window at %1").arg(rectToString(normalGeometry));
            mainWindow["windowState"] = windowState & ~4;
            mainWindow["geometry"] = rectToJson(normalGeometry);
            repaired = true;
        }

        mainWindows[i] = mainWindow;
    }

    if (note && !notes.isEmpty()) {
        *note = notes.join("; ");
    }

    if (!repaired) {
        return layout;
    }

    root["mainWindows"] = mainWindows;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

WindowGeometryCheck::Result WindowGeometryCheck::check(const QByteArray& layout, const QList<QRect>& screens, Kind kind)
{
    if (layout.isEmpty()) {
        return {};
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(layout, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }

    const QJsonArray mainWindows = doc.object().value("mainWindows").toArray();
    for (const QJsonValue& value : mainWindows) {
        const QJsonObject mainWindow = value.toObject();
        if (!mainWindow.contains("geometry")) {
            continue;
        }

        const QRect geometry = rectFromJson(mainWindow.value("geometry"));
        if (isTooSmall(geometry)) {
            return { false, QString("main window too small: %1").arg(rectToString(geometry)) };
        }

        //! The restore applies this one first; see repairNormalGeometry()
        if (kind == Kind::MainWindow && isMaximizedOrFullScreen(mainWindow)) {
            const QRect normalGeometry = rectFromJson(mainWindow.value("normalGeometry"));
            if (isTooSmall(normalGeometry)) {
                return { false, QString("normal geometry of the maximized or full-screen main window too small: %1")
                         .arg(rectToString(normalGeometry)) };
            }
        }

        if (screens.isEmpty()) {
            continue;
        }

        bool reachable = false;
        for (const QRect& screen : screens) {
            const QRect visible = geometry.intersected(screen);
            if (visible.width() >= MIN_VISIBLE_WIDTH && visible.height() >= MIN_VISIBLE_HEIGHT) {
                reachable = true;
                break;
            }
        }

        if (!reachable) {
            return { false, QString("main window off every screen: %1").arg(rectToString(geometry)) };
        }
    }

    return {};
}

QList<QRect> WindowGeometryCheck::availableScreenGeometries()
{
    QList<QRect> result;
    for (const QScreen* screen : QGuiApplication::screens()) {
        result.push_back(screen->availableGeometry());
    }
    return result;
}
