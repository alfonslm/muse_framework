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

bool isLargeEnough(const QRect& geometry)
{
    return geometry.width() >= WindowGeometryCheck::MIN_WIDTH && geometry.height() >= WindowGeometryCheck::MIN_HEIGHT;
}
}

bool NormalGeometryTracker::isZoomed(const QRect& frame, const QList<Screen>& screens)
{
    for (const Screen& screen : screens) {
        const QRect& area = screen.availableGeometry;
        if (isWithin(frame.left(), area.left(), ZOOM_TOLERANCE) && isWithin(frame.right(), area.right(), ZOOM_TOLERANCE)
            && isWithin(frame.bottom(), area.bottom(), ZOOM_TOLERANCE)
            && frame.top() >= area.top() - ZOOM_TOLERANCE && frame.top() <= area.top() + ZOOM_TOP_TOLERANCE) {
            return true;
        }
    }
    return false;
}

bool NormalGeometryTracker::isUsable(const QRect& geometry, const QRect& frameGeometry, const QList<Screen>& screens)
{
    if (!isLargeEnough(geometry)) {
        return false;
    }

    //! Full screen (or on its way there), or zoomed: not the size the user gave the window. On a
    //! display with a notch a full-screen frame starts below the notch, so a frame spanning the
    //! screen's whole width down to its bottom edge counts as full screen, whatever its top.
    for (const Screen& screen : screens) {
        const QRect& whole = screen.geometry;
        if (frameGeometry.contains(whole)) {
            return false;
        }
        if (isWithin(frameGeometry.left(), whole.left(), ZOOM_TOLERANCE) && isWithin(frameGeometry.right(), whole.right(), ZOOM_TOLERANCE)
            && isWithin(frameGeometry.bottom(), whole.bottom(), ZOOM_TOLERANCE)) {
            return false;
        }
    }

    return !isZoomed(frameGeometry, screens);
}

void NormalGeometryTracker::seedRestored(const QRect& normalGeometry)
{
    if (isLargeEnough(normalGeometry)) {
        m_restored = normalGeometry;
        m_committed = normalGeometry;
    }
}

void NormalGeometryTracker::seed(const QRect& geometry, const QRect& frameGeometry, bool normalState, const QList<Screen>& screens)
{
    m_committed = normalState && isUsable(geometry, frameGeometry, screens) ? geometry : m_restored;
    m_candidate = QRect();
}

NormalGeometryTracker::Change NormalGeometryTracker::onGeometryChanged(const QRect& geometry, const QRect& frameGeometry,
                                                                       bool normalState, const QList<Screen>& screens)
{
    if (!normalState || !isUsable(geometry, frameGeometry, screens)) {
        m_candidate = QRect();
        return Change::Rejected;
    }

    m_candidate = geometry;
    return Change::Candidate;
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
