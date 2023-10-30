// Copyright (C) 2023 JiDe Zhang <zhangjide@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#pragma once

#include <wglobal.h>
#include <woutput.h>
#include <qwglobal.h>

#include <QObject>
#include <QQuickRenderTarget>
#include <QSGRendererInterface>

QT_BEGIN_NAMESPACE
class QOpenGLContext;
class QWindow;
class QQuickRenderControl;
QT_END_NAMESPACE

QW_BEGIN_NAMESPACE
class QWRenderer;
class QWBackend;
class QWBuffer;
QW_END_NAMESPACE

struct wlr_swapchain;
struct pixman_region32;

WAYLIB_SERVER_BEGIN_NAMESPACE

class WOutputHelperPrivate;
class WAYLIB_SERVER_EXPORT WOutputHelper : public QObject, public WObject
{
    Q_OBJECT
    W_DECLARE_PRIVATE(WOutputHelper)
    Q_PROPERTY(bool renderable READ renderable NOTIFY renderableChanged)
    Q_PROPERTY(bool contentIsDirty READ contentIsDirty NOTIFY contentIsDirtyChanged)
    Q_PROPERTY(bool needsFrame READ needsFrame NOTIFY needsFrameChanged FINAL)

public:
    explicit WOutputHelper(WOutput *output, QObject *parent = nullptr);

    WOutput *output() const;
    QWindow *outputWindow() const;

    std::pair<QW_NAMESPACE::QWBuffer*, QQuickRenderTarget> acquireRenderTarget(QQuickRenderControl *rc,
                                                                               int *bufferAge = nullptr,
                                                                               wlr_swapchain **swapchain = nullptr);
    std::pair<QW_NAMESPACE::QWBuffer*, QQuickRenderTarget> lastRenderTarget();

    void setBuffer(QW_NAMESPACE::QWBuffer *buffer);
    void setScale(float scale);
    void setTransform(WOutput::Transform t);
    void setDamage(const pixman_region32 *damage);
    bool commit();

    bool renderable() const;
    bool contentIsDirty() const;
    bool needsFrame() const;

    void resetState();
    void update();

Q_SIGNALS:
    void requestRender();
    void damaged();
    void renderableChanged();
    void contentIsDirtyChanged();
    void needsFrameChanged();
};

WAYLIB_SERVER_END_NAMESPACE
