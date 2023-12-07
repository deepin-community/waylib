// Copyright (C) 2023 JiDe Zhang <zhangjide@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#pragma once

#include <WSeat>
#include <WCursor>
#include <WSurfaceItem>
#include <WOutput>
#include <wtoplevelsurface.h>

Q_DECLARE_OPAQUE_POINTER(QWindow*)

struct wlr_output_event_request_state;
QW_USE_NAMESPACE
WAYLIB_SERVER_USE_NAMESPACE

class Helper : public WSeatEventFilter {
    Q_OBJECT
    Q_PROPERTY(WToplevelSurface* activatedSurface READ activatedSurface WRITE setActivateSurface NOTIFY activatedSurfaceChanged FINAL)
    Q_PROPERTY(WSurfaceItem* resizingItem READ resizingItem NOTIFY resizingItemChanged FINAL)
    Q_PROPERTY(WSurfaceItem* movingItem READ movingItem NOTIFY movingItemChanged FINAL)
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Helper(QObject *parent = nullptr);
    void stopMoveResize();

    WToplevelSurface *activatedSurface() const;
    WSurfaceItem *resizingItem() const;
    WSurfaceItem *movingItem() const;

public Q_SLOTS:
    void startMove(WToplevelSurface *surface, WSurfaceItem *shell, WSeat *seat, int serial);
    void startResize(WToplevelSurface *surface, WSurfaceItem *shell, WSeat *seat, Qt::Edges edge, int serial);
    void cancelMoveResize(WSurfaceItem *shell);
    bool startDemoClient(const QString &socket);
    WSurface *getFocusSurfaceFrom(QObject *object);

    void allowNonDrmOutputAutoChangeMode(WOutput *output);

signals:
    void activatedSurfaceChanged();
    void resizingItemChanged();
    void movingItemChanged();

private:
    bool beforeDisposeEvent(WSeat *seat, QWindow *watched, QInputEvent *event) override;
    bool afterHandleEvent(WSeat *seat, WSurface *watched, QObject *surfaceItem, QObject *, QInputEvent *event) override;
    bool unacceptedEvent(WSeat *seat, QWindow *watched, QInputEvent *event) override;

    void setActivateSurface(WToplevelSurface *newActivate);
    void setResizingItem(WSurfaceItem *newResizingItem);
    void setMovingItem(WSurfaceItem *newMovingItem);
    void onOutputRequeseState(wlr_output_event_request_state *newState);

    QPointer<WToplevelSurface> m_activateSurface;

    // for move resize
    QPointer<WToplevelSurface> surface;
    QPointer<WSurfaceItem> surfaceItem;
    WSeat *seat = nullptr;
    QPointF surfacePosOfStartMoveResize;
    QSizeF surfaceSizeOfStartMoveResize;
    Qt::Edges resizeEdgets;
    WSurfaceItem *m_resizingItem = nullptr;
    WSurfaceItem *m_movingItem = nullptr;
};
