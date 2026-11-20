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

#include <QByteArray>
#include <QList>
#include <QRect>
#include <QString>

namespace muse::dock {
//! Checks a saved dock layout (as written by KDDockWidgets::LayoutSaver) before it is restored or
//! saved: a main window that is empty, a few pixels in size or off every screen would leave the
//! app without a visible window. Such a layout can be saved by a window that is being taken down
//! (e.g. a run ended by force), and restoring it brings the problem back at every start.
class WindowGeometryCheck
{
public:
    //! The smallest main window that counts as usable
    static constexpr int MIN_WIDTH = 200;
    static constexpr int MIN_HEIGHT = 150;

    //! How much of the main window must be on one screen to count as reachable (e.g. its title bar)
    static constexpr int MIN_VISIBLE_WIDTH = 100;
    static constexpr int MIN_VISIBLE_HEIGHT = 50;

    struct Result {
        bool usable = true;
        QString reason; // why not, for the log
    };

    //! `screens`: the available geometry of each screen. When empty, the position is not checked
    //! (a page layout is restored relative to the main window, so only its size matters).
    //! A layout that can't be read, or that has no main window, is left to the library to judge.
    static Result check(const QByteArray& layout, const QList<QRect>& screens);

    //! A maximized or full-screen main window is restored by first applying its saved normal
    //! geometry, then the state. When the window was never in the normal state during the run
    //! (e.g. on macOS, zoomed or full screen from the start), that normal geometry is saved as
    //! empty, and the restore gives a window of size zero. Such a normal geometry is replaced by
    //! the window's geometry. Returns the layout unchanged when there is nothing to repair.
    static QByteArray repairNormalGeometry(const QByteArray& layout, QString* note = nullptr);

    //! The available geometry of each screen of the application
    static QList<QRect> availableScreenGeometries();
};
}
