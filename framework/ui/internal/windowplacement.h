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

#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

class QWindow;

namespace muse::ui {
//! Keeps the main window usable and on a screen the user can see. The geometry the window gets,
//! from any source (a saved layout, the platform's own placement, a display that is gone or
//! arranged differently, a size of zero applied by a restore), is checked before the window is
//! shown, after it is shown, and on every change for a short while after start-up:
//! - a window of a usable size that is reasonably visible on a screen is clamped into that
//!   screen's available area;
//! - one that is not (off every screen, mostly off, or smaller than its minimum size) is placed
//!   at a fixed safe place on the primary screen.
//! After start-up only a size below the window's minimum is corrected.
//! All geometries are frame geometries (the title bar included), in global screen coordinates.
class WindowPlacement
{
public:
    //! Smaller windows are always unusable, also when the window sets no minimum size
    static constexpr int MIN_WIDTH = 200;
    static constexpr int MIN_HEIGHT = 150;

    //! The safe placement: this percentage of the primary screen's available width and height...
    static constexpr int SAFE_SIZE_PERCENT = 80;
    //! ...with the top-left corner this fraction of the remaining free space in from the top-left
    static constexpr int SAFE_OFFSET_DIVISOR = 3;

    //! How long after the window is shown every change of its geometry is checked
    static constexpr int STARTUP_WATCH_MS = 5000;
    //! At most this many corrections by the watch, so a platform that keeps refusing a geometry
    //! can't make it loop
    static constexpr int MAX_WATCH_CORRECTIONS = 10;

    enum class Action {
        None,     // already fully on a screen
        Clamped,  // reasonably visible: moved and/or shrunk into the screen it is mostly on
        SafePlacement // not reasonably visible, or too small: placed on the primary screen
    };

    struct Result {
        Action action = Action::None;
        QRect geometry; // the frame geometry to use
        QString reason; // why it was changed, for the log
    };

    //! `screens`: the available geometry of each connected screen; `primaryScreen`: an index into
    //! it. `minimumSize`: the window's minimum frame size, if any; a window smaller than it (or than
    //! MIN_WIDTH x MIN_HEIGHT) gets the safe placement. Pure, for tests.
    static Result place(const QRect& frameGeometry, const QList<QRect>& screens, int primaryScreen,
                        const QSize& minimumSize = QSize());

    //! The safe placement on a screen with this available geometry
    static QRect safeGeometry(const QRect& available, const QSize& minimumSize = QSize());

    //! Applies place() to the window and logs one line when it is changed. Returns whether it was
    //! changed. With \p onlyIfUnusable (before the window is shown) a window that would only be
    //! clamped is left to the platform's own placement, which happens when it is shown.
    static bool ensureOnScreen(QWindow* window, bool onlyIfUnusable = false);

    //! Checks the window again after each change of its geometry: every change during
    //! STARTUP_WATCH_MS from now, afterwards only a size below its minimum
    static void watch(QWindow* window);
};
}
