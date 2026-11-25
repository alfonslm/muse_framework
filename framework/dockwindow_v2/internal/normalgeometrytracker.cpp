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

#include "normalgeometrytracker.h"

#include <cstdlib>

#include "windowgeometrycheck.h"

using namespace muse::dock;

namespace {
bool isWithin(int a, int b, int tolerance)
{
    return std::abs(a - b) <= tolerance;
}

bool fills(const QRect& frame, const QRect& area, int tolerance)
{
    return isWithin(frame.left(), area.left(), tolerance) && isWithin(frame.top(), area.top(), tolerance)
           && isWithin(frame.right(), area.right(), tolerance) && isWithin(frame.bottom(), area.bottom(), tolerance);
}
}

bool NormalGeometryTracker::isUsable(const QRect& geometry, const QRect& frameGeometry, const QList<Screen>& screens)
{
    if (geometry.width() < WindowGeometryCheck::MIN_WIDTH || geometry.height() < WindowGeometryCheck::MIN_HEIGHT) {
        return false;
    }

    for (const Screen& screen : screens) {
        //! Full screen (or on its way there), or zoomed: not the size the user gave the window
        if (frameGeometry.contains(screen.geometry) || fills(frameGeometry, screen.availableGeometry, ZOOM_TOLERANCE)) {
            return false;
        }
    }

    return true;
}

void NormalGeometryTracker::seed(const QRect& geometry, const QRect& frameGeometry, bool normalState, const QList<Screen>& screens)
{
    if (normalState && isUsable(geometry, frameGeometry, screens)) {
        m_committed = geometry;
    }
    m_candidate = QRect();
}

bool NormalGeometryTracker::onGeometryChanged(const QRect& geometry, const QRect& frameGeometry, bool normalState,
                                              const QList<Screen>& screens)
{
    if (!normalState || !isUsable(geometry, frameGeometry, screens)) {
        return false;
    }

    m_candidate = geometry;
    return true;
}

void NormalGeometryTracker::onStateChanged(bool normalState)
{
    if (!normalState) {
        m_candidate = QRect();
    }
}

void NormalGeometryTracker::commit(bool normalState)
{
    if (normalState && m_candidate.isValid()) {
        m_committed = m_candidate;
    }
    m_candidate = QRect();
}

QRect NormalGeometryTracker::normalGeometry() const
{
    return m_committed;
}
