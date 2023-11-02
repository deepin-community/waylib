// Copyright (C) 2023 JiDe Zhang <zhangjide@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "window.h"

QWindow *CustomWindow::parent() const
{
    return m_parent;
}

void CustomWindow::setParent(QWindow *newParent)
{
    if (m_parent == newParent)
        return;
    m_parent = newParent;
    QQuickWindow::setParent(newParent);
    show();

    emit parentChanged();
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "wayland");

    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const QUrl url(u"qrc:/qt/qml/subsurface/Main.qml"_qs);
#else
    const QUrl url(u"qrc:/subsurface/Main.qml"_qs);
#endif
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
