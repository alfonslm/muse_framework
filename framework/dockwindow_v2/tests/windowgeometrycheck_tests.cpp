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

//! A main window in the given KDDockWidgets::WindowState (2 = maximized, 4 = full screen)
QByteArray layoutWithStateAndNormalGeometry(int windowState, const QRect& geometry, const QRect& normalGeometry)
{
    QJsonObject root = QJsonDocument::fromJson(layoutWithMainWindow(geometry)).object();
    QJsonArray mainWindows = root["mainWindows"].toArray();
    QJsonObject mainWindow = mainWindows[0].toObject();

    QJsonObject normal;
    normal["x"] = normalGeometry.x();
    normal["y"] = normalGeometry.y();
    normal["width"] = normalGeometry.width();
    normal["height"] = normalGeometry.height();
    mainWindow["normalGeometry"] = normal;
    mainWindow["windowState"] = windowState;

    mainWindows[0] = mainWindow;
    root["mainWindows"] = mainWindows;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QRect normalGeometryOf(const QByteArray& layout)
{
    const QJsonObject rect = QJsonDocument::fromJson(layout).object()["mainWindows"].toArray()[0].toObject()["normalGeometry"].toObject();
    return QRect(rect["x"].toInt(), rect["y"].toInt(), rect["width"].toInt(), rect["height"].toInt());
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

TEST(Dock_WindowGeometryCheckTests, MaximizedWithEmptyNormalGeometry_NotUsable)
{
    //! The restore of a maximized or full-screen window applies its normal geometry first
    const QByteArray layout = layoutWithStateAndNormalGeometry(4, QRect(0, 0, 1920, 1080), QRect());
    const WindowGeometryCheck::Result result = WindowGeometryCheck::check(layout, ONE_SCREEN);
    EXPECT_FALSE(result.usable);
    EXPECT_TRUE(result.reason.contains("normal geometry"));

    //! In the normal state the normal geometry is not used
    EXPECT_TRUE(WindowGeometryCheck::check(layoutWithStateAndNormalGeometry(0, QRect(0, 0, 1920, 1080), QRect()), ONE_SCREEN).usable);
}

TEST(Dock_WindowGeometryCheckTests, RepairNormalGeometry_UsesGeometry)
{
    const QRect geometry(0, 0, 1920, 1080);
    for (int state : { 2, 4 }) {
        QString note;
        const QByteArray repaired = WindowGeometryCheck::repairNormalGeometry(
            layoutWithStateAndNormalGeometry(state, geometry, QRect(40, 62, 0, 0)), &note);
        EXPECT_EQ(normalGeometryOf(repaired), geometry);
        EXPECT_FALSE(note.isEmpty());
        EXPECT_TRUE(WindowGeometryCheck::check(repaired, ONE_SCREEN).usable);
    }
}

TEST(Dock_WindowGeometryCheckTests, RepairNormalGeometry_LeavesOthersAlone)
{
    //! A good normal geometry, the normal state, a bad geometry too, and a layout that can't be read
    const QByteArray good = layoutWithStateAndNormalGeometry(2, QRect(0, 0, 1920, 1080), QRect(100, 100, 1280, 800));
    const QByteArray normalState = layoutWithStateAndNormalGeometry(0, QRect(100, 100, 1280, 800), QRect());
    const QByteArray bothBad = layoutWithStateAndNormalGeometry(2, QRect(0, 0, 87, 21), QRect());
    const QByteArray unreadable("not a layout");

    for (const QByteArray& layout : { good, normalState, bothBad, unreadable }) {
        QString note;
        EXPECT_EQ(WindowGeometryCheck::repairNormalGeometry(layout, &note), layout);
        EXPECT_TRUE(note.isEmpty());
    }

    EXPECT_FALSE(WindowGeometryCheck::check(bothBad, ONE_SCREEN).usable);
}
