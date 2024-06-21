// Copyright (C) 2023 JiDe Zhang <zhangjide@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#pragma once

#include <wglobal.h>
#include <wtypes.h>
#include <qwglobal.h>

#include <QObject>
#include <QRect>
#include <QQmlEngine>

#include <any>

struct wlr_surface;

QW_BEGIN_NAMESPACE
class QWTexture;
class QWSurface;
class QWBuffer;
QW_END_NAMESPACE
WAYLIB_SERVER_BEGIN_NAMESPACE

class WServer;
class WOutput;
class WSurfacePrivate;
class WAYLIB_SERVER_EXPORT WSurface : public WWrapObject
{
    Q_OBJECT
    W_DECLARE_PRIVATE(WSurface)
    Q_PROPERTY(bool mapped READ mapped NOTIFY mappedChanged)
    Q_PROPERTY(bool isSubsurface READ isSubsurface NOTIFY isSubsurfaceChanged)
    Q_PROPERTY(bool hasSubsurface READ hasSubsurface NOTIFY hasSubsurfaceChanged)
    Q_PROPERTY(QList<WSurface*> subsurfaces READ subsurfaces NOTIFY newSubsurface)
    Q_PROPERTY(WOutput* primaryOutput READ primaryOutput NOTIFY primaryOutputChanged)
    Q_PROPERTY(uint32_t preferredBufferScale READ preferredBufferScale WRITE setPreferredBufferScale RESET resetPreferredBufferScale NOTIFY preferredBufferScaleChanged FINAL)
    QML_NAMED_ELEMENT(WaylandSurface)
    QML_UNCREATABLE("Only create in C++")

public:
    explicit WSurface(QW_NAMESPACE::QWSurface *handle, QObject *parent = nullptr);

    QW_NAMESPACE::QWSurface *handle() const;

    static WSurface *fromHandle(QW_NAMESPACE::QWSurface *handle);
    static WSurface *fromHandle(wlr_surface *handle);

    // for current state
    bool mapped() const;
    QSize size() const;
    QSize bufferSize() const;
    WLR::Transform orientation() const;
    int bufferScale() const;
    QPoint bufferOffset() const;
    QW_NAMESPACE::QWBuffer *buffer() const;

    void notifyFrameDone();
    WOutput *primaryOutput() const;

    bool isSubsurface() const;
    bool hasSubsurface() const;
    QList<WSurface*> subsurfaces() const;

    uint32_t preferredBufferScale() const;
    void setPreferredBufferScale(uint32_t newPreferredBufferScale);
    void resetPreferredBufferScale();

public Q_SLOTS:
    void enterOutput(WOutput *output);
    void leaveOutput(WOutput *output);
    QVector<WOutput*> outputs() const;
    bool inputRegionContains(const QPointF &localPos) const;

    void map();
    void unmap();

Q_SIGNALS:
    void primaryOutputChanged();
    void mappedChanged();
    void bufferChanged();
    void isSubsurfaceChanged();
    void hasSubsurfaceChanged();
    void newSubsurface(WSurface *subsurface);
    void preferredBufferScaleChanged();
    void outputEntered(WOutput *output);
    void outputLeft(WOutput *output);

protected:
    WSurface(WSurfacePrivate &dd, QObject *parent);
};

WAYLIB_SERVER_END_NAMESPACE
