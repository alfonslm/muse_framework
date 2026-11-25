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

#include "internal/normalgeometrytracker.h"

using namespace muse::dock;

namespace {
//! A laptop screen (full geometry, the menu bar included)
const QList<QRect> SCREENS { QRect(0, 0, 1470, 956) };
const QRect NORMAL(100, 80, 1200, 750);
const QRect RESIZED(120, 90, 1300, 800);
}

TEST(Dock_NormalGeometryTrackerTests, NoGeometry_Empty)
{
    NormalGeometryTracker tracker;
    EXPECT_TRUE(tracker.normalGeometry().isEmpty());
}

TEST(Dock_NormalGeometryTrackerTests, Seed_InNormalState)
{
    NormalGeometryTracker tracker;
    tracker.seed(NORMAL, true, SCREENS);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);

    NormalGeometryTracker fullScreen;
    fullScreen.seed(SCREENS.first(), false, SCREENS);
    EXPECT_TRUE(fullScreen.normalGeometry().isEmpty());
}

TEST(Dock_NormalGeometryTrackerTests, ResizeThenFullScreen_KeepsLastNormalSize)
{
    //! The case of FD-148: the window is resized, then put in full screen; the size to save is the
    //! last normal one, not the full-screen one and not an empty one
    NormalGeometryTracker tracker;
    tracker.seed(NORMAL, true, SCREENS);

    EXPECT_TRUE(tracker.onGeometryChanged(RESIZED, true, SCREENS));
    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), RESIZED);

    tracker.onStateChanged(false);
    EXPECT_FALSE(tracker.onGeometryChanged(SCREENS.first(), false, SCREENS));
    EXPECT_EQ(tracker.normalGeometry(), RESIZED);
}

TEST(Dock_NormalGeometryTrackerTests, TransitionFrames_NotTaken)
{
    NormalGeometryTracker tracker;
    tracker.seed(NORMAL, true, SCREENS);

    //! Still reported as normal while growing to full screen: a frame between is a candidate, but
    //! the state changes before it settles, so it is dropped
    EXPECT_TRUE(tracker.onGeometryChanged(QRect(50, 40, 1350, 900), true, SCREENS));
    tracker.onStateChanged(false);
    tracker.commit(false);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);

    //! The full-screen frame itself is never taken, even when reported in the normal state
    EXPECT_FALSE(tracker.onGeometryChanged(SCREENS.first(), true, SCREENS));
    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);
}

TEST(Dock_NormalGeometryTrackerTests, CommitWhenNoLongerNormal_Ignored)
{
    NormalGeometryTracker tracker;
    tracker.seed(NORMAL, true, SCREENS);

    EXPECT_TRUE(tracker.onGeometryChanged(RESIZED, true, SCREENS));
    tracker.commit(false);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);
}

TEST(Dock_NormalGeometryTrackerTests, TooSmall_NotTaken)
{
    NormalGeometryTracker tracker;
    tracker.seed(NORMAL, true, SCREENS);

    EXPECT_FALSE(tracker.onGeometryChanged(QRect(40, 62, 0, 0), true, SCREENS));
    EXPECT_FALSE(tracker.onGeometryChanged(QRect(40, 62, 87, 21), true, SCREENS));
    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);

    NormalGeometryTracker empty;
    empty.seed(QRect(0, 0, 0, 0), true, SCREENS);
    EXPECT_TRUE(empty.normalGeometry().isEmpty());
}

TEST(Dock_NormalGeometryTrackerTests, SecondScreen_FullScreenThereNotTaken)
{
    const QList<QRect> twoScreens { QRect(0, 0, 1470, 956), QRect(-1920, 0, 1920, 1080) };
    NormalGeometryTracker tracker;
    tracker.seed(QRect(-1800, 100, 1200, 750), true, twoScreens);

    EXPECT_FALSE(tracker.onGeometryChanged(QRect(-1920, 0, 1920, 1080), true, twoScreens));
    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), QRect(-1800, 100, 1200, 750));
}
