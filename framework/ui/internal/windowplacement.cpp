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

#include "windowplacement.h"

#include <algorithm>
#include <memory>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include "log.h"

using namespace muse::ui;

namespace {
QString rectToString(const QRect& rect)
{
    return QString("%1,%2 %3x%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}

//! Reasonably visible: at least half the window's width and half its height (or of the screen's,
//! for a window larger than the screen) are on the screen
bool isReasonablyVisible(const QRect& geometry, const QRect& screen)
{
    const QRect visible = geometry.intersected(screen);
    if (visible.isEmpty()) {
        return false;
    }

    const int neededWidth = std::min(geometry.width(), screen.width()) / 2;
    const int neededHeight = std::min(geometry.height(), screen.height()) / 2;
    return visible.width() >= neededWidth && visible.height() >= neededHeight;
}

qint64 visibleArea(const QRect& geometry, const QRect& screen)
{
    const QRect visible = geometry.intersected(screen);
    return visible.isEmpty() ? 0 : qint64(visible.width()) * visible.height();
}

//! The size bounded by the screen and kept at least the minimum; then moved inside the screen,
//! with the top-left corner kept on the screen when the window can't fit (title bar reachable)
QRect clampInto(const QRect& geometry, const QRect& available, const QSize& minimumSize)
{
    QSize size = geometry.size().boundedTo(available.size());
    if (minimumSize.isValid()) {
        size = size.expandedTo(minimumSize);
    }

    const int x = std::max(available.left(), std::min(geometry.x(), available.x() + available.width() - size.width()));
    const int y = std::max(available.top(), std::min(geometry.y(), available.y() + available.height() - size.height()));
    return QRect(QPoint(x, y), size);
}
}

QRect WindowPlacement::safeGeometry(const QRect& available, const QSize& minimumSize)
{
    QSize size(available.width() * SAFE_SIZE_PERCENT / 100, available.height() * SAFE_SIZE_PERCENT / 100);
    if (minimumSize.isValid()) {
        size = size.expandedTo(minimumSize).boundedTo(available.size());
    }

    const QPoint topLeft(available.x() + (available.width() - size.width()) / SAFE_OFFSET_DIVISOR,
                         available.y() + (available.height() - size.height()) / SAFE_OFFSET_DIVISOR);
    return clampInto(QRect(topLeft, size), available, minimumSize);
}

WindowPlacement::Result WindowPlacement::place(const QRect& frameGeometry, const QList<QRect>& screens, int primaryScreen,
                                               const QSize& minimumSize)
{
    Result result;
    result.geometry = frameGeometry;

    if (screens.isEmpty()) {
        return result;
    }

    const QRect primary = screens.at(primaryScreen >= 0 && primaryScreen < screens.size() ? primaryScreen : 0);

    const int minWidth = std::max(MIN_WIDTH, minimumSize.isValid() ? minimumSize.width() : 0);
    const int minHeight = std::max(MIN_HEIGHT, minimumSize.isValid() ? minimumSize.height() : 0);
    if (frameGeometry.width() < minWidth || frameGeometry.height() < minHeight) {
        result.action = Action::SafePlacement;
        result.geometry = safeGeometry(primary, minimumSize);
        result.reason = "main window smaller than its minimum size";
        return result;
    }

    //! The screen showing most of the window
    const QRect* best = nullptr;
    qint64 bestArea = 0;
    for (const QRect& screen : screens) {
        const qint64 area = visibleArea(frameGeometry, screen);
        if (area > bestArea) {
            bestArea = area;
            best = &screen;
        }
    }

    if (!best || !isReasonablyVisible(frameGeometry, *best)) {
        result.action = Action::SafePlacement;
        result.geometry = safeGeometry(primary, minimumSize);
        result.reason = best ? "main window mostly off the screens" : "main window off every screen";
        return result;
    }

    const QRect clamped = clampInto(frameGeometry, *best, minimumSize);
    if (clamped != frameGeometry) {
        result.action = Action::Clamped;
        result.geometry = clamped;
        result.reason = "main window partly off its screen";
    }

    return result;
}

bool WindowPlacement::ensureOnScreen(QWindow* window, bool onlyIfUnusable)
{
    if (!window) {
        return false;
    }

    //! On Wayland the compositor places windows; their position is not known to the app
    if (QGuiApplication::platformName().startsWith("wayland")) {
        return false;
    }

    const Qt::WindowStates states = window->windowStates();
    if (states & Qt::WindowMinimized) {
        return false;
    }

    QList<QRect> screens;
    int primaryIndex = 0;
    const QScreen* primary = QGuiApplication::primaryScreen();
    for (const QScreen* screen : QGuiApplication::screens()) {
        if (screen == primary) {
            primaryIndex = static_cast<int>(screens.size());
        }
        screens.push_back(screen->availableGeometry());
    }

    //! Before the window is shown its frame margins are not known yet, they count as zero
    const QMargins margins = window->frameMargins();
    const QRect frame = window->geometry().marginsAdded(margins);
    const QSize minimumFrameSize = window->minimumSize().grownBy(margins);

    const Result result = place(frame, screens, primaryIndex, minimumFrameSize);
    if (result.action == Action::None) {
        return false;
    }

    if (onlyIfUnusable && result.action != Action::SafePlacement) {
        return false;
    }

    const bool maximizedOrFullScreen = states & (Qt::WindowMaximized | Qt::WindowFullScreen);
    if (maximizedOrFullScreen) {
        //! A maximized or full-screen window may extend past the available area by design;
        //! only one that is not usable is put back
        if (result.action != Action::SafePlacement) {
            return false;
        }
        window->setWindowStates(Qt::WindowNoState);
    }

    LOGW() << "Moving the main window onto a visible screen (" << result.reason << "): "
           << rectToString(frame) << " -> " << rectToString(result.geometry);

    window->setGeometry(result.geometry.marginsRemoved(margins));
    return true;
}

void WindowPlacement::CorrectionLimit::endPeriodIfOver(qint64 nowMs)
{
    if (m_periodStart < 0 || nowMs - m_periodStart < PERIOD_MS) {
        return;
    }

    //! A period that reached the limit counts towards stopping, unless a quiet period followed it
    const bool capped = m_corrections >= MAX_CORRECTIONS;
    const bool followedByQuietPeriod = nowMs - m_periodStart >= 2 * PERIOD_MS;
    m_cappedPeriods = capped && !followedByQuietPeriod ? m_cappedPeriods + 1 : 0;

    m_periodStart = -1;
    m_corrections = 0;

    if (m_cappedPeriods >= MAX_CAPPED_PERIODS) {
        m_stopped = true;
    }
}

bool WindowPlacement::CorrectionLimit::allowed(qint64 nowMs)
{
    endPeriodIfOver(nowMs);
    return !m_stopped && m_corrections < MAX_CORRECTIONS;
}

void WindowPlacement::CorrectionLimit::corrected(qint64 nowMs)
{
    endPeriodIfOver(nowMs);
    if (m_periodStart < 0) {
        m_periodStart = nowMs;
    }
    ++m_corrections;
}

bool WindowPlacement::CorrectionLimit::stopped() const
{
    return m_stopped;
}

void WindowPlacement::watch(QWindow* window)
{
    if (!window) {
        return;
    }

    struct State {
        QElapsedTimer sinceStart;
        bool pending = false;
        bool stopLogged = false;
        CorrectionLimit limit;
    };

    auto state = std::make_shared<State>();
    state->sinceStart.start();

    auto scheduleCheck = [window, state]() {
        if (state->pending) {
            return;
        }

        if (!state->limit.allowed(state->sinceStart.elapsed())) {
            if (state->limit.stopped() && !state->stopLogged) {
                state->stopLogged = true;
                LOGW() << "The main window kept being moved; not checking it again in this run";
            }
            return;
        }

        //! After start-up the user places the window; only a size below its minimum is corrected
        const bool startingUp = state->sinceStart.elapsed() < STARTUP_WATCH_MS;
        if (!startingUp && window->width() >= window->minimumWidth() && window->height() >= window->minimumHeight()) {
            return;
        }

        state->pending = true;
        QTimer::singleShot(0, window, [window, state]() {
            state->pending = false;
            if (ensureOnScreen(window)) {
                state->limit.corrected(state->sinceStart.elapsed());
                if (!state->limit.allowed(state->sinceStart.elapsed())) {
                    LOGW() << "The main window was moved " << CorrectionLimit::MAX_CORRECTIONS << " times within "
                           << CorrectionLimit::PERIOD_MS << " ms; not checking it again for a while";
                }
            }
        });
    };

    QObject::connect(window, &QWindow::xChanged, window, scheduleCheck);
    QObject::connect(window, &QWindow::yChanged, window, scheduleCheck);
    QObject::connect(window, &QWindow::widthChanged, window, scheduleCheck);
    QObject::connect(window, &QWindow::heightChanged, window, scheduleCheck);
    QObject::connect(window, &QWindow::screenChanged, window, scheduleCheck);
    QObject::connect(window, &QWindow::visibleChanged, window, scheduleCheck);

    //! Once when the event loop has run, also if nothing changes
    scheduleCheck();
}
