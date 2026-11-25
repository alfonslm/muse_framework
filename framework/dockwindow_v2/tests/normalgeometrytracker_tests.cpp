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
using Screen = NormalGeometryTracker::Screen;

namespace {
//! A laptop screen: the whole screen, and the area without the menu bar and the Dock
const QList<Screen> SCREENS { { QRect(0, 0, 1470, 956), QRect(0, 33, 1470, 850) } };
constexpr int TITLE_BAR = 28;

//! The frame of a window with this geometry (a title bar on top, as on macOS)
QRect frameOf(const QRect& geometry)
{
    return geometry.adjusted(0, -TITLE_BAR, 0, 0);
}

//! A window whose frame is this rect
QRect geometryInFrame(const QRect& frame)
{
    return frame.adjusted(0, TITLE_BAR, 0, 0);
}

const QRect NORMAL(100, 80, 1200, 750);
const QRect RESIZED(120, 90, 1300, 700);
const QRect FULL_SCREEN = SCREENS.first().geometry;
const QRect ZOOMED = geometryInFrame(SCREENS.first().availableGeometry);

bool change(NormalGeometryTracker& tracker, const QRect& geometry, bool normalState, const QList<Screen>& screens = SCREENS)
{
    return tracker.onGeometryChanged(geometry, frameOf(geometry), normalState, screens)
           == NormalGeometryTracker::Change::Candidate;
}

NormalGeometryTracker seeded(const QRect& geometry = NORMAL)
{
    NormalGeometryTracker tracker;
    tracker.seed(geometry, frameOf(geometry), true, SCREENS);
    return tracker;
}
}

TEST(Dock_NormalGeometryTrackerTests, NoGeometry_Empty)
{
    NormalGeometryTracker tracker;
    EXPECT_TRUE(tracker.normalGeometry().isEmpty());
}

TEST(Dock_NormalGeometryTrackerTests, Seed_InNormalState)
{
    EXPECT_EQ(seeded().normalGeometry(), NORMAL);

    NormalGeometryTracker fullScreen;
    fullScreen.seed(FULL_SCREEN, FULL_SCREEN, false, SCREENS);
    EXPECT_TRUE(fullScreen.normalGeometry().isEmpty());
}

TEST(Dock_NormalGeometryTrackerTests, ResizeThenFullScreen_KeepsLastNormalSize)
{
    //! The case of FD-148: the window is resized, then put in full screen; the size to save is the
    //! last normal one, not the full-screen one and not an empty one
    NormalGeometryTracker tracker = seeded();

    EXPECT_TRUE(change(tracker, RESIZED, true));
    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), RESIZED);

    tracker.onStateChanged(false);
    EXPECT_EQ(tracker.onGeometryChanged(FULL_SCREEN, FULL_SCREEN, false, SCREENS), NormalGeometryTracker::Change::Rejected);
    EXPECT_EQ(tracker.normalGeometry(), RESIZED);
}

TEST(Dock_NormalGeometryTrackerTests, TransitionFrames_NotTaken)
{
    NormalGeometryTracker tracker = seeded();

    //! Still reported as normal while growing to full screen: a frame between is a candidate, but
    //! the state changes before it settles, so it is dropped
    EXPECT_TRUE(change(tracker, QRect(50, 60, 1350, 800), true));
    tracker.onStateChanged(false);
    tracker.commit(false);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);

    //! The full-screen frame itself is never taken, even when reported in the normal state
    EXPECT_EQ(tracker.onGeometryChanged(FULL_SCREEN, FULL_SCREEN, true, SCREENS), NormalGeometryTracker::Change::Rejected);
    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);
}

TEST(Dock_NormalGeometryTrackerTests, Zoomed_NotTakenEvenInNormalState)
{
    //! On macOS a zoomed window (green button with Option, or a double-click on the title bar) can
    //! be reported in the normal state; its frame fills the available area
    NormalGeometryTracker tracker = seeded();

    EXPECT_FALSE(change(tracker, ZOOMED, true));
    EXPECT_FALSE(change(tracker, ZOOMED, false));

    //! Within a few pixels of the available area counts as zoomed too
    EXPECT_FALSE(change(tracker, ZOOMED.adjusted(2, 3, -4, -1), true));

    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);

    //! A window clearly smaller than the available area is a normal size
    const QRect large = ZOOMED.adjusted(40, 30, -40, -30);
    EXPECT_TRUE(change(tracker, large, true));
    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), large);

    //! A zoomed window at start is not taken as the seed
    NormalGeometryTracker zoomedAtStart;
    zoomedAtStart.seed(ZOOMED, frameOf(ZOOMED), true, SCREENS);
    EXPECT_TRUE(zoomedAtStart.normalGeometry().isEmpty());
}

TEST(Dock_NormalGeometryTrackerTests, CommitWhenNoLongerNormal_Ignored)
{
    NormalGeometryTracker tracker = seeded();

    EXPECT_TRUE(change(tracker, RESIZED, true));
    tracker.commit(false);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);
}

TEST(Dock_NormalGeometryTrackerTests, TooSmall_NotTaken)
{
    NormalGeometryTracker tracker = seeded();

    EXPECT_FALSE(change(tracker, QRect(40, 62, 0, 0), true));
    EXPECT_FALSE(change(tracker, QRect(40, 62, 87, 21), true));
    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);

    NormalGeometryTracker empty;
    empty.seed(QRect(0, 0, 0, 0), QRect(0, 0, 0, 0), true, SCREENS);
    EXPECT_TRUE(empty.normalGeometry().isEmpty());
}

TEST(Dock_NormalGeometryTrackerTests, SecondScreen_FullScreenAndZoomThereNotTaken)
{
    const QList<Screen> twoScreens {
        SCREENS.first(),
        { QRect(-1920, 0, 1920, 1080), QRect(-1920, 25, 1920, 1055) }
    };
    const QRect onSecond(-1800, 100, 1200, 750);

    NormalGeometryTracker tracker;
    tracker.seed(onSecond, frameOf(onSecond), true, twoScreens);
    EXPECT_EQ(tracker.normalGeometry(), onSecond);

    EXPECT_EQ(tracker.onGeometryChanged(QRect(-1920, 0, 1920, 1080), QRect(-1920, 0, 1920, 1080), true, twoScreens),
              NormalGeometryTracker::Change::Rejected);
    EXPECT_FALSE(change(tracker, geometryInFrame(QRect(-1920, 25, 1920, 1055)), true, twoScreens));
    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), onSecond);
}

TEST(Dock_NormalGeometryTrackerTests, RejectedFrame_DropsPendingCandidate)
{
    //! A frame between normal and full screen, then the full-screen frame reported as normal, then
    //! the settle timer: the frame between must not be committed
    NormalGeometryTracker tracker = seeded();

    EXPECT_TRUE(change(tracker, QRect(50, 60, 1350, 800), true));
    EXPECT_EQ(tracker.onGeometryChanged(FULL_SCREEN, FULL_SCREEN, true, SCREENS), NormalGeometryTracker::Change::Rejected);
    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);
}

TEST(Dock_NormalGeometryTrackerTests, StartedMaximized_KeepsRestoredNormalSize)
{
    //! A layout saved zoomed or maximized is restored in that state: the window never shows its
    //! normal size during the run, so the restored one is kept for the next save
    NormalGeometryTracker tracker;
    tracker.seedRestored(NORMAL);
    tracker.seed(ZOOMED, frameOf(ZOOMED), false, SCREENS);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);

    EXPECT_FALSE(change(tracker, ZOOMED, false));
    tracker.commit(false);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);

    //! Shown, still zoomed (reported as normal, as macOS may): still the restored size
    tracker.seed(ZOOMED, frameOf(ZOOMED), true, SCREENS);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);

    //! Un-zoomed to a normal size: that one from now on
    EXPECT_TRUE(change(tracker, RESIZED, true));
    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), RESIZED);

    //! An empty or tiny restored normal geometry (the hotfix's case) is not kept
    NormalGeometryTracker empty;
    empty.seedRestored(QRect(0, 0, 0, 0));
    EXPECT_TRUE(empty.normalGeometry().isEmpty());
}

TEST(Dock_NormalGeometryTrackerTests, ZoomedBeforeShow_NotTakenAsSeed)
{
    //! Before the window is shown its frame is not known, so the frame equals the geometry, which
    //! starts below the title bar: a restored zoomed window (0,61 1470x822 on this screen)
    const QRect zoomedClient(0, 61, 1470, 822);
    NormalGeometryTracker tracker;
    tracker.seedRestored(NORMAL);
    tracker.seed(zoomedClient, zoomedClient, true, SCREENS);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);

    NormalGeometryTracker noRestored;
    noRestored.seed(zoomedClient, zoomedClient, true, SCREENS);
    EXPECT_TRUE(noRestored.normalGeometry().isEmpty());

    //! A tall window that does not reach the sides is a normal size
    const QRect tall(200, 61, 1000, 822);
    noRestored.seed(tall, tall, true, SCREENS);
    EXPECT_EQ(noRestored.normalGeometry(), tall);
}

TEST(Dock_NormalGeometryTrackerTests, FullScreenBelowANotch_NotTaken)
{
    //! A MacBook display with a notch: in full screen the frame starts below the notch, and with
    //! the Dock visible it does not fill the available area either
    const QList<Screen> notched { { QRect(0, 0, 1512, 982), QRect(0, 38, 1512, 870) } };
    const QRect frame(0, 38, 1512, 944);

    NormalGeometryTracker tracker;
    tracker.seed(NORMAL, frameOf(NORMAL), true, notched);
    EXPECT_EQ(tracker.onGeometryChanged(frame, frame, true, notched), NormalGeometryTracker::Change::Rejected);
    tracker.commit(true);
    EXPECT_EQ(tracker.normalGeometry(), NORMAL);
}
