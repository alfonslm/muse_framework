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

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "internal/windowgeometrycheck.h"

using namespace muse::dock;

namespace {
//! A layout in the shape KDDockWidgets::LayoutSaver writes: only the main window's geometry matters
QByteArray layoutWithMainWindow(const QRect& geometry)
{
    QJsonObject rect;
    rect["x"] = geometry.x();
    rect["y"] = geometry.y();
    rect["width"] = geometry.width();
    rect["height"] = geometry.height();

    QJsonObject mainWindow;
    mainWindow["uniqueName"] = "mainWindow";
    mainWindow["geometry"] = rect;
    mainWindow["normalGeometry"] = rect;
    mainWindow["isVisible"] = true;

    QJsonObject layout;
    layout["serializationVersion"] = 3;
    layout["mainWindows"] = QJsonArray { mainWindow };
    layout["floatingWindows"] = QJsonArray {};

    return QJsonDocument(layout).toJson(QJsonDocument::Compact);
}

const QList<QRect> ONE_SCREEN { QRect(0, 0, 1920, 1080) };
const QList<QRect> TWO_SCREENS { QRect(0, 0, 1920, 1080), QRect(1920, 0, 2560, 1440) };
}

TEST(Dock_WindowGeometryCheckTests, NormalWindowIsUsable)
{
    EXPECT_TRUE(WindowGeometryCheck::check(layoutWithMainWindow(QRect(100, 100, 1280, 800)), ONE_SCREEN).usable);
}

TEST(Dock_WindowGeometryCheckTests, EmptyWindowIsNotUsable)
{
    const WindowGeometryCheck::Result result = WindowGeometryCheck::check(layoutWithMainWindow(QRect(0, 0, 0, 0)), ONE_SCREEN);
    EXPECT_FALSE(result.usable);
    EXPECT_FALSE(result.reason.isEmpty());
}

TEST(Dock_WindowGeometryCheckTests, TinyWindowIsNotUsable)
{
    //! A main window of a few pixels, as saved by a window that was being taken down
    EXPECT_FALSE(WindowGeometryCheck::check(layoutWithMainWindow(QRect(0, 0, 87, 21)), ONE_SCREEN).usable);
}

TEST(Dock_WindowGeometryCheckTests, WindowOffEveryScreenIsNotUsable)
{
    EXPECT_FALSE(WindowGeometryCheck::check(layoutWithMainWindow(QRect(5000, 3000, 1280, 800)), ONE_SCREEN).usable);
    EXPECT_FALSE(WindowGeometryCheck::check(layoutWithMainWindow(QRect(-2000, 100, 1280, 800)), ONE_SCREEN).usable);
}

TEST(Dock_WindowGeometryCheckTests, WindowWithOnlyAnEdgeOnScreenIsNotUsable)
{
    //! 30 pixels of the window on the screen: too little to reach its title bar
    EXPECT_FALSE(WindowGeometryCheck::check(layoutWithMainWindow(QRect(1890, 100, 1280, 800)), ONE_SCREEN).usable);
}

TEST(Dock_WindowGeometryCheckTests, WindowPartlyOffScreenIsUsable)
{
    EXPECT_TRUE(WindowGeometryCheck::check(layoutWithMainWindow(QRect(1500, 600, 1280, 800)), ONE_SCREEN).usable);
}

TEST(Dock_WindowGeometryCheckTests, WindowOnASecondScreenIsUsableOnlyWhileItIsThere)
{
    const QByteArray layout = layoutWithMainWindow(QRect(2200, 100, 1280, 800));
    EXPECT_TRUE(WindowGeometryCheck::check(layout, TWO_SCREENS).usable);
    EXPECT_FALSE(WindowGeometryCheck::check(layout, ONE_SCREEN).usable);
}

TEST(Dock_WindowGeometryCheckTests, WithoutScreensOnlyTheSizeIsChecked)
{
    EXPECT_TRUE(WindowGeometryCheck::check(layoutWithMainWindow(QRect(5000, 3000, 1280, 800)), {}).usable);
    EXPECT_FALSE(WindowGeometryCheck::check(layoutWithMainWindow(QRect(0, 0, 87, 21)), {}).usable);
}

TEST(Dock_WindowGeometryCheckTests, UnreadableLayoutsAreLeftToTheLibrary)
{
    EXPECT_TRUE(WindowGeometryCheck::check(QByteArray(), ONE_SCREEN).usable);
    EXPECT_TRUE(WindowGeometryCheck::check(QByteArray("not json"), ONE_SCREEN).usable);
    EXPECT_TRUE(WindowGeometryCheck::check(QByteArray("{\"floatingWindows\":[]}"), ONE_SCREEN).usable);
}
