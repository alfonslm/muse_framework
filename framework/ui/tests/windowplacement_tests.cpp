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

#include "ui/internal/windowplacement.h"

using namespace muse::ui;

using Action = WindowPlacement::Action;

namespace {
//! A laptop-like primary screen (menu bar at the top) and a secondary display to its left,
//! which has negative coordinates
const QRect PRIMARY(0, 25, 1440, 875);
const QRect LEFT_DISPLAY(-1920, 0, 1920, 1080);
const QRect RIGHT_DISPLAY(1440, 0, 2560, 1440);
}

class Ui_WindowPlacementTests : public ::testing::Test
{
};

TEST_F(Ui_WindowPlacementTests, FullyOnPrimary_Unchanged)
{
    const QRect geometry(100, 50, 1150, 800);
    const WindowPlacement::Result result = WindowPlacement::place(geometry, { PRIMARY, LEFT_DISPLAY }, 0);
    EXPECT_EQ(result.action, Action::None);
    EXPECT_EQ(result.geometry, geometry);
}

TEST_F(Ui_WindowPlacementTests, FullyOnNegativeCoordinateDisplay_Unchanged)
{
    const QRect geometry(-1800, 100, 1150, 800);
    const WindowPlacement::Result result = WindowPlacement::place(geometry, { PRIMARY, LEFT_DISPLAY }, 0);
    EXPECT_EQ(result.action, Action::None);
    EXPECT_EQ(result.geometry, geometry);
}

TEST_F(Ui_WindowPlacementTests, MostlyOnNegativeCoordinateDisplay_ClampedIntoIt)
{
    //! 1000 px on the left display, 150 px on the primary one
    const WindowPlacement::Result result = WindowPlacement::place(QRect(-1000, 100, 1150, 800), { PRIMARY, LEFT_DISPLAY }, 0);
    EXPECT_EQ(result.action, Action::Clamped);
    EXPECT_EQ(result.geometry, QRect(-1150, 100, 1150, 800));
}

TEST_F(Ui_WindowPlacementTests, OffEveryScreen_SafePlacementOnPrimary)
{
    const WindowPlacement::Result result = WindowPlacement::place(QRect(5000, 3000, 1150, 800), { LEFT_DISPLAY, PRIMARY }, 1);
    EXPECT_EQ(result.action, Action::SafePlacement);
    EXPECT_EQ(result.geometry, WindowPlacement::safeGeometry(PRIMARY));
    EXPECT_FALSE(result.reason.isEmpty());
}

TEST_F(Ui_WindowPlacementTests, DisplayThatIsGone_SafePlacementOnPrimary)
{
    //! The window was on the right-hand display, which is no longer connected
    const QRect geometry(2000, 200, 1150, 800);
    ASSERT_EQ(WindowPlacement::place(geometry, { PRIMARY, RIGHT_DISPLAY }, 0).action, Action::None);

    const WindowPlacement::Result result = WindowPlacement::place(geometry, { PRIMARY }, 0);
    EXPECT_EQ(result.action, Action::SafePlacement);
    EXPECT_EQ(result.geometry, WindowPlacement::safeGeometry(PRIMARY));
}

TEST_F(Ui_WindowPlacementTests, PartlyVisible_ClampedIntoScreen)
{
    //! 640 of 1150 px wide on the screen: moved left and up so all of it is on the screen
    const WindowPlacement::Result result = WindowPlacement::place(QRect(800, 200, 1150, 800), { PRIMARY }, 0);
    EXPECT_EQ(result.action, Action::Clamped);
    EXPECT_EQ(result.geometry, QRect(290, 100, 1150, 800));
}

TEST_F(Ui_WindowPlacementTests, TitleBarAboveScreen_ClampedDown)
{
    const WindowPlacement::Result result = WindowPlacement::place(QRect(100, -300, 1150, 800), { PRIMARY }, 0);
    EXPECT_EQ(result.action, Action::Clamped);
    EXPECT_EQ(result.geometry, QRect(100, 25, 1150, 800));
}

TEST_F(Ui_WindowPlacementTests, MostlyOffScreen_SafePlacement)
{
    //! Only 140 px of the width on the screen: not reasonably visible
    const WindowPlacement::Result result = WindowPlacement::place(QRect(1300, 200, 1150, 800), { PRIMARY }, 0);
    EXPECT_EQ(result.action, Action::SafePlacement);
    EXPECT_EQ(result.geometry, WindowPlacement::safeGeometry(PRIMARY));
}

TEST_F(Ui_WindowPlacementTests, TooLarge_ShrunkToScreen)
{
    const WindowPlacement::Result result = WindowPlacement::place(QRect(0, 0, 3000, 2000), { PRIMARY }, 0);
    EXPECT_EQ(result.action, Action::Clamped);
    EXPECT_EQ(result.geometry, PRIMARY);
}

TEST_F(Ui_WindowPlacementTests, TooSmall_SafePlacement)
{
    //! The size seen in the log of FD-148
    const WindowPlacement::Result result = WindowPlacement::place(QRect(0, 0, 87, 21), { PRIMARY }, 0);
    EXPECT_EQ(result.action, Action::SafePlacement);
    EXPECT_EQ(result.geometry, WindowPlacement::safeGeometry(PRIMARY));
}

TEST_F(Ui_WindowPlacementTests, SafeGeometry_ThirdOfTheFreeSpaceIn)
{
    //! 80 % of 1440x875 is 1152x700; the free space 288x175, a third of it 96x58
    EXPECT_EQ(WindowPlacement::safeGeometry(PRIMARY), QRect(96, 83, 1152, 700));
    EXPECT_EQ(WindowPlacement::safeGeometry(LEFT_DISPLAY), QRect(-1792, 72, 1536, 864));
}

TEST_F(Ui_WindowPlacementTests, SafeGeometry_KeepsMinimumSize)
{
    const QRect small(0, 0, 1024, 700);
    const QRect geometry = WindowPlacement::safeGeometry(small, QSize(1050, 530));
    EXPECT_EQ(geometry.width(), 1050);
    EXPECT_EQ(geometry.height(), 560);
    EXPECT_EQ(geometry.topLeft(), QPoint(0, 46));
}

TEST_F(Ui_WindowPlacementTests, Clamp_KeepsMinimumSizeAndTopLeftOnScreen)
{
    const QRect small(0, 0, 1024, 700);
    const WindowPlacement::Result result = WindowPlacement::place(QRect(200, 100, 1100, 650), { small }, 0, QSize(1050, 530));
    EXPECT_EQ(result.action, Action::Clamped);
    EXPECT_EQ(result.geometry, QRect(0, 50, 1050, 650));
}

TEST_F(Ui_WindowPlacementTests, InvalidPrimaryIndex_UsesFirstScreen)
{
    const WindowPlacement::Result result = WindowPlacement::place(QRect(9000, 9000, 1150, 800), { PRIMARY, LEFT_DISPLAY }, 7);
    EXPECT_EQ(result.geometry, WindowPlacement::safeGeometry(PRIMARY));
}

TEST_F(Ui_WindowPlacementTests, NoScreens_Unchanged)
{
    const QRect geometry(9000, 9000, 1150, 800);
    const WindowPlacement::Result result = WindowPlacement::place(geometry, {}, 0);
    EXPECT_EQ(result.action, Action::None);
    EXPECT_EQ(result.geometry, geometry);
}

TEST_F(Ui_WindowPlacementTests, ZeroSizeOnScreen_SafePlacement)
{
    //! The state seen in the log of the 0.1.1 test build: on the screen, but of size zero
    //! (with the title bar of the frame only)
    const QSize minimum(1050, 628);
    const WindowPlacement::Result result = WindowPlacement::place(QRect(40, 62, 0, 28), { PRIMARY }, 0, minimum);
    EXPECT_EQ(result.action, Action::SafePlacement);
    EXPECT_EQ(result.geometry, WindowPlacement::safeGeometry(PRIMARY, minimum));
    EXPECT_GE(result.geometry.width(), minimum.width());
    EXPECT_GE(result.geometry.height(), minimum.height());
}

TEST_F(Ui_WindowPlacementTests, BelowMinimumSize_SafePlacement)
{
    const QSize minimum(1050, 628);
    const WindowPlacement::Result result = WindowPlacement::place(QRect(100, 100, 900, 550), { PRIMARY }, 0, minimum);
    EXPECT_EQ(result.action, Action::SafePlacement);
    EXPECT_EQ(result.geometry, WindowPlacement::safeGeometry(PRIMARY, minimum));
}

TEST_F(Ui_WindowPlacementTests, SmallLaptop_MinimumSizeFits)
{
    //! A 1280x800 screen without the menu bar and the Dock; the main window's minimum size with
    //! its title bar
    const QRect laptop(0, 25, 1280, 705);
    const QSize minimum(1050, 628);
    const QRect geometry = WindowPlacement::safeGeometry(laptop, minimum);
    EXPECT_EQ(geometry, QRect(76, 50, 1050, 628));
    EXPECT_TRUE(laptop.contains(geometry));

    const WindowPlacement::Result result = WindowPlacement::place(QRect(500, 300, 1150, 800), { laptop }, 0, minimum);
    EXPECT_EQ(result.action, Action::Clamped);
    EXPECT_TRUE(laptop.contains(result.geometry));
}

TEST_F(Ui_WindowPlacementTests, CorrectionLimit_PerPeriod)
{
    using Limit = WindowPlacement::CorrectionLimit;
    Limit limit;

    for (int i = 0; i < Limit::MAX_CORRECTIONS; ++i) {
        ASSERT_TRUE(limit.allowed(100 + i));
        limit.corrected(100 + i);
    }
    EXPECT_FALSE(limit.allowed(200));

    //! The next period: allowed again, so the minimum-size rule keeps working later in the run
    EXPECT_TRUE(limit.allowed(100 + Limit::PERIOD_MS));
    EXPECT_FALSE(limit.stopped());
}

TEST_F(Ui_WindowPlacementTests, CorrectionLimit_StopsAfterCappedPeriodsInARow)
{
    using Limit = WindowPlacement::CorrectionLimit;
    Limit limit;

    //! A platform that keeps refusing: every period reaches the limit
    qint64 now = 0;
    for (int period = 0; period < Limit::MAX_CAPPED_PERIODS; ++period) {
        now = period * Limit::PERIOD_MS;
        for (int i = 0; i < Limit::MAX_CORRECTIONS; ++i) {
            ASSERT_TRUE(limit.allowed(now + i));
            limit.corrected(now + i);
        }
        EXPECT_FALSE(limit.allowed(now + Limit::MAX_CORRECTIONS));
    }

    EXPECT_FALSE(limit.allowed(Limit::MAX_CAPPED_PERIODS * Limit::PERIOD_MS));
    EXPECT_TRUE(limit.stopped());
    EXPECT_FALSE(limit.allowed(1000 * Limit::PERIOD_MS));
}

TEST_F(Ui_WindowPlacementTests, CorrectionLimit_QuietPeriodResetsTheCount)
{
    using Limit = WindowPlacement::CorrectionLimit;
    Limit limit;

    //! Capped periods with quiet periods between them never stop the watch
    for (int round = 0; round < 2 * Limit::MAX_CAPPED_PERIODS; ++round) {
        const qint64 start = round * 3 * Limit::PERIOD_MS;
        for (int i = 0; i < Limit::MAX_CORRECTIONS; ++i) {
            ASSERT_TRUE(limit.allowed(start + i));
            limit.corrected(start + i);
        }
    }
    EXPECT_TRUE(limit.allowed(100 * Limit::PERIOD_MS));
    EXPECT_FALSE(limit.stopped());

    //! A few corrections a period are always allowed
    Limit few;
    for (int period = 0; period < 10; ++period) {
        EXPECT_TRUE(few.allowed(period * Limit::PERIOD_MS));
        few.corrected(period * Limit::PERIOD_MS);
    }
    EXPECT_FALSE(few.stopped());
}
