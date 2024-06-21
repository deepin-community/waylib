// Copyright (C) 2023 JiDe Zhang <zhangjide@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#pragma once

#include <wglobal.h>
#include <qwoutputlayout.h>
#include <limits.h>

WAYLIB_SERVER_BEGIN_NAMESPACE

class WOutput;
class WOutputLayoutPrivate;
class WAYLIB_SERVER_EXPORT WOutputLayout : public QW_NAMESPACE::QWOutputLayout, public WObject
{
    Q_OBJECT
    W_DECLARE_PRIVATE(WOutputLayout)
    Q_PROPERTY(int implicitWidth READ implicitWidth NOTIFY implicitWidthChanged)
    Q_PROPERTY(int implicitHeight READ implicitHeight NOTIFY implicitHeightChanged)

public:
    enum class Layer {
        Background = -999,
        Cursor = INT_MAX-1000
        // Ensure the Cursor's z-axis is above all other components
        // DefaultWindowDecoration = 1000000, at least it must be greater than 1000000
        // QQuickItem's z is qreal value, INT_MAX is not the maximum value but big enough
    };
    Q_ENUM(Layer)

    explicit WOutputLayout(QObject *parent = nullptr);

    QList<WOutput*> outputs() const;

    void add(WOutput *output, const QPoint &pos);
    void move(WOutput *output, const QPoint &pos);
    void remove(WOutput *output);

    QList<WOutput*> getIntersectedOutputs(const QRect &geometry) const;

    int implicitWidth() const;
    int implicitHeight() const;

Q_SIGNALS:
    void outputAdded(WOutput *output);
    void outputRemoved(WOutput *output);
    void implicitWidthChanged();
    void implicitHeightChanged();

protected:
    WOutputLayout(WOutputLayoutPrivate &dd, QObject *parent = nullptr);
    ~WOutputLayout() override = default;

protected:
    using QW_NAMESPACE::QWOutputLayout::add;
    using QW_NAMESPACE::QWOutputLayout::addAuto;
    using QW_NAMESPACE::QWOutputLayout::move;
    using QW_NAMESPACE::QWOutputLayout::remove;
};

WAYLIB_SERVER_END_NAMESPACE
