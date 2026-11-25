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
}

WindowGeometryCheck::Result WindowGeometryCheck::check(const QByteArray& layout, const QList<QRect>& screens)
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
        if (geometry.width() < MIN_WIDTH || geometry.height() < MIN_HEIGHT) {
            return { false, QString("main window too small: %1").arg(rectToString(geometry)) };
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
