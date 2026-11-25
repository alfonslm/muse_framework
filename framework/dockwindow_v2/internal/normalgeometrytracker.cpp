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

#include "windowgeometrycheck.h"

using namespace muse::dock;

bool NormalGeometryTracker::isUsable(const QRect& geometry, const QList<QRect>& screenGeometries)
{
    if (geometry.width() < WindowGeometryCheck::MIN_WIDTH || geometry.height() < WindowGeometryCheck::MIN_HEIGHT) {
        return false;
    }

    //! A window covering a whole screen is full screen (or on its way there), not a normal window
    for (const QRect& screen : screenGeometries) {
        if (geometry.contains(screen)) {
            return false;
        }
    }

    return true;
}

void NormalGeometryTracker::seed(const QRect& geometry, bool normalState, const QList<QRect>& screenGeometries)
{
    if (normalState && isUsable(geometry, screenGeometries)) {
        m_committed = geometry;
    }
    m_candidate = QRect();
}

bool NormalGeometryTracker::onGeometryChanged(const QRect& geometry, bool normalState, const QList<QRect>& screenGeometries)
{
    if (!normalState || !isUsable(geometry, screenGeometries)) {
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
