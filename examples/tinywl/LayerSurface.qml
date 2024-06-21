// Copyright (C) 2023 rewine <luhongxu@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

import QtQuick
import Waylib.Server
import QtQuick.Particles
import Tinywl

Item {
    property alias waylandSurface: surfaceItem.shellSurface
    property alias surfaceItem: surfaceItem
    property bool anchorWidth: false
    property bool anchorHeight: false
    // From Helper
    required property DynamicCreatorComponent creator
    property OutputItem output
    property CoordMapper outputCoordMapper
    property bool mapped: waylandSurface.surface && waylandSurface.surface.mapped && waylandSurface.WaylandSocket.rootSocket.enabled
    property bool pendingDestroy: false

    id: root
    z: zValueFormLayer((waylandSurface as WaylandLayerSurface).layer)

    LayerSurfaceItem {
        anchors.centerIn: parent

        id: surfaceItem

        onHeightChanged: {
            if (!anchorHeight)
                parent.height = height
        }

        onWidthChanged: {
            if (!anchorWidth)
                parent.width = width
        }

        onEffectiveVisibleChanged: {
            if (effectiveVisible && surface.isActivated)
                forceActiveFocus()
        }
    }

    OutputLayoutItem {
        anchors.fill: parent
        layout: QmlHelper.layout

        onEnterOutput: function(output) {
            waylandSurface.surface.enterOutput(output)
            Helper.onSurfaceEnterOutput(waylandSurface, surfaceItem, output)
            Helper.registerExclusiveZone(waylandSurface)
        }
        onLeaveOutput: function(output) {
            Helper.unregisterExclusiveZone(waylandSurface)
            waylandSurface.surface.leaveOutput(output)
            Helper.onSurfaceLeaveOutput(waylandSurface, surfaceItem, output)
            waylandSurface.closed()
        }
    }

    Loader {
        id: closeAnimation
    }

    Component {
        id: closeAnimationComponent

        CloseAnimation {
            onStopped: {
                if (pendingDestroy)
                    creator.destroyObject(root)
            }
        }
    }

    onMappedChanged: {
        // When Socket is enabled and mapped becomes false, set visible
        // after closeAnimation complete， Otherwise set visible directly.
        if (mapped) {
            Helper.registerExclusiveZone(waylandSurface)
            refreshMargin()
            visible = true
            if (surfaceItem.effectiveVisible)
                Helper.activatedSurface = waylandSurface
        } else { // if not mapped
            Helper.unregisterExclusiveZone(waylandSurface)
            if (!waylandSurface.WaylandSocket.rootSocket.enabled) {
                visible = false
            } else {
                // do animation for window close
                closeAnimation.parent = root.parent
                closeAnimation.anchors.fill = root
                closeAnimation.sourceComponent = closeAnimationComponent
                closeAnimation.item.start(root)
            }
        }
    }

    function doDestroy() {
        pendingDestroy = true

        if (!surfaceItem.visible || !closeAnimation.active) {
            //Helper.unregisterExclusiveZone(waylandSurface)
            creator.destroyObject(root)
            return
        }

        // unbind some properties
        mapped = false
    }

    function getPrimaryOutputItem() {
        let output = waylandSurface.surface.primaryOutput
        if (!output)
            return null
        return output.OutputItem.item
    }

    function updateOutputCoordMapper() {
        let output = getPrimaryOutputItem()
        if (!output)
            return
        root.output = output
        root.outputCoordMapper = surface.CoordMapper.helper.get(output)
    }

    function zValueFormLayer(layer) {
        switch (layer) {
        case WaylandLayerSurface.LayerType.Background:
            return -100
        case WaylandLayerSurface.LayerType.Bottom:
            return -50
        case WaylandLayerSurface.LayerType.Top:
            return 50
        case WaylandLayerSurface.LayerType.Overlay:
            return 100
        }
        // Should not be reachable
        return -50;
    }

    function refreshAnchors() {
        var top = waylandSurface.ancher & WaylandLayerSurface.AnchorType.Top
        var bottom = waylandSurface.ancher & WaylandLayerSurface.AnchorType.Bottom
        var left = waylandSurface.ancher & WaylandLayerSurface.AnchorType.Left
        var right = waylandSurface.ancher & WaylandLayerSurface.AnchorType.Right

        anchorWidth = left && right
        anchorHeight = top && bottom

        anchors.top = top ? parent.top : undefined
        anchors.bottom = bottom ? parent.bottom : undefined
        anchors.verticalCenter = (top || bottom) ? undefined : parent.verticalCenter;
        anchors.left = left ? parent.left : undefined
        anchors.right = right ? parent.right : undefined
        anchors.horizontalCenter = (left || right) ? undefined : parent.horizontalCenter;
        // Setting anchors may change the container size which should keep same with surfaceItem
        if (!anchorWidth)
            width = surfaceItem.width
        if (!anchorHeight)
            height = surfaceItem.height
    }

    function refreshExclusiveZone() {
        Helper.unregisterExclusiveZone(waylandSurface)
        Helper.registerExclusiveZone(waylandSurface)
    }

    function refreshMargin() {
        var accpectExclusive = waylandSurface.exclusiveZone >= 0 ? 1 : 0;

        var exclusiveMargin = Helper.getExclusiveMargins(waylandSurface)
        var topMargin = waylandSurface.topMargin + accpectExclusive * exclusiveMargin.top;
        var bottomMargin = waylandSurface.bottomMargin + accpectExclusive * exclusiveMargin.bottom;
        var leftMargin = waylandSurface.leftMargin + accpectExclusive * exclusiveMargin.left;
        var rightMargin = waylandSurface.rightMargin + accpectExclusive * exclusiveMargin.right;

        anchors.topMargin = topMargin;
        anchors.bottomMargin = bottomMargin;
        anchors.leftMargin = leftMargin;
        anchors.rightMargin = rightMargin;
    }

    function configureSurfaceSize() {
        var surfaceWidth = waylandSurface.desiredSize.width
        var surfaceHeight = waylandSurface.desiredSize.height

        if (surfaceWidth === 0)
            surfaceWidth = width
        if (surfaceHeight === 0)
            surfaceHeight = height

        if (surfaceWidth && surfaceHeight)
            waylandSurface.configureSize(Qt.size(surfaceWidth, surfaceHeight))
    }

    onHeightChanged: {
        if (waylandSurface.desiredSize.height === 0 && height != 0) {
            configureSurfaceSize()
        }
    }

    onWidthChanged: {
        if (waylandSurface.desiredSize.width === 0 && width != 0) {
            configureSurfaceSize()
        }
    }

    Component.onCompleted: {
        refreshAnchors()
        refreshMargin()
        configureSurfaceSize()
    }

    Connections {
        target: waylandSurface

        function onLayerPropertiesChanged() {
            Helper.unregisterExclusiveZone(waylandSurface)
            Helper.registerExclusiveZone(waylandSurface)
            refreshAnchors()
            refreshMargin()
            configureSurfaceSize()
        }

        function onActivateChanged() {
            if (waylandSurface.isActivated && surfaceItem.effectiveVisible) {
                surfaceItem.forceActiveFocus()
            } else {
                surfaceItem.focus = false
            }
        }
    }
    Connections {
        target: Helper

        function onTopExclusiveMarginChanged() {
            refreshMargin()
        }

        function onBottomExclusiveMarginChanged() {
            refreshMargin()
        }

        function onLeftExclusiveMarginChanged() {
            refreshMargin()
        }

        function onRightExclusiveMarginChanged() {
            refreshMargin()
        }
    }
}
