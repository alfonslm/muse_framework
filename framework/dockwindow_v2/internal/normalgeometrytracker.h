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

namespace muse::dock {
//! Remembers the main window's real normal geometry: its geometry the last time it was in the
//! normal state (not maximized, full screen or minimized) at a usable size. It is written as the
//! normal geometry of a saved layout, so that a window zoomed or in full screen at quit comes back
//! at the size the user had. The platform does not always know it: Qt reports no normal geometry
//! for a maximized or full-screen window on macOS or Linux (xcb), and a run that is never in the
//! normal state would otherwise save an empty one (FD-148).
//!
//! A geometry change in the normal state becomes a candidate, and is committed when the window has
//! settled (the caller's timer, SETTLE_MS). Entering zoom or full screen drops a candidate, so the
//! frames of the transition are not taken. A geometry that covers a whole screen, or fills a
//! screen's available area (zoomed), is never taken.
//! Pure: geometry in, geometry out, for tests.
class NormalGeometryTracker
{
public:
    static constexpr int SETTLE_MS = 300;

    //! A window whose frame is within this many pixels of a screen's available geometry on the
    //! left, right and bottom edges, and within ZOOM_TOP_TOLERANCE at the top, counts as zoomed: on
    //! macOS a zoomed window can be reported in the normal state. The top is looser because before
    //! the window is shown its frame is not known yet, and its geometry starts below the title bar.
    static constexpr int ZOOM_TOLERANCE = 8;
    static constexpr int ZOOM_TOP_TOLERANCE = 64;

    struct Screen {
        QRect geometry;          // the whole screen
        QRect availableGeometry; // without the menu bar, the Dock or a task bar
    };

    enum class Change {
        Candidate, // taken: start (or restart) the settle timer
        Rejected   // not a normal size: any pending candidate is dropped, stop the settle timer
    };

    //! \p geometry is the window's geometry without its frame (what is saved and restored),
    //! \p frameGeometry with it (what is compared with the screens)

    //! The normal geometry read from a restored layout whose main window was maximized or in full
    //! screen: the size the user had, which the window itself can't show while it is not normal
    void seedRestored(const QRect& normalGeometry);

    //! The window's geometry right after the layout is restored, and again once it is shown (when
    //! its frame is known): committed if it is a normal size; otherwise the restored normal geometry
    //! (if any) is kept
    void seed(const QRect& geometry, const QRect& frameGeometry, bool normalState, const QList<Screen>& screens);

    Change onGeometryChanged(const QRect& geometry, const QRect& frameGeometry, bool normalState, const QList<Screen>& screens);

    void onStateChanged(bool normalState);

    //! The settle timer fired: the candidate becomes the normal geometry if the window is still normal
    void commit(bool normalState);

    //! The last committed normal geometry; empty if there is none
    QRect normalGeometry() const;

    static bool isZoomed(const QRect& frameGeometry, const QList<Screen>& screens);

private:
    static bool isUsable(const QRect& geometry, const QRect& frameGeometry, const QList<Screen>& screens);

    QRect m_restored;
    QRect m_committed;
    QRect m_candidate;
};
}
