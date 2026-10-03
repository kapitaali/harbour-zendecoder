import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0
import QtMultimedia 5.6
import "../components"

/*
 * Live barcode viewfinder (default page).
 *
 * Decoding is hybrid (see src/decoder.h): the vendored zxing-cpp on a
 * worker thread decodes each still first — every symbology, symbology
 * name included — and the system zxing D-Bus service answers when that
 * finds nothing (QR only, format comes back empty from that path).
 * Still captures rather than viewfinder frames because the overlay
 * backend runs no filter chain and QVideoProbe fails (see decoder.h).
 * The page takes still captures into a private cache file and hands each
 * written file to decoder.submitImageFile(); a decoded value is stored in
 * history and pushed to ResultPage. Gallery imports arrive through
 * decoder.decodeFile() and report misses via onNotFound.
 *
 * ResultPage shows the raw value + symbology + product lookup for GTINs.
 */
Page {
    id: scannerPage

    allowedOrientations: Orientation.Portrait

    property bool succeeded: false
    property int captureFailures: 0

    function noteCaptureFailure() {
        if (++captureFailures === 3) {
            toast.show("Camera capture failed")
        }
    }

    function succeed(text, format) {
        if (succeeded)
            return
        succeeded = true
        pendingGalleryOp = null // live result wins; drop any straggler op
        captureTimer.stop()
        camera.stop()
        if (settings.soundEnabled) {
            // Short system beep via Alarm? Keep to vibration + visual in
            // v0.1; sound hook lands with the static decoder beat.
        }
        var id = history.addScan(format, text)
        var entry = { "scanId": id, "format": format, "value": text }
        successTimer.entry = entry
        successTimer.restart()
    }

    function reset() {
        succeeded = false
        captureFailures = 0
        camera.start()
    }

    // Gallery one-shot results arrive asynchronously: usually while the
    // file picker is popping itself closed (status Activating) or just
    // after. Results wait in pendingGalleryOp; a settle-timer delivers
    // them once this page is Active, the stack is not mid-transition and
    // we are still the current page. (Delivering straight from
    // onStatusChanged re-entered Silica's status bookkeeping mid-update —
    // "Binding loop detected for property status" — and the toast never
    // appeared; the timer retries every 250 ms until the op is consumed,
    // and never pops anything, so it cannot eat pages the user opened.)
    property var pendingGalleryOp: null

    function deliverGalleryOp() {
        var op = pendingGalleryOp
        if (op === null || op === undefined)
            return
        pendingGalleryOp = null
        var hasEntry = (op.entry !== null && op.entry !== undefined)
        decoder.logMessage("deliver gallery op, hasEntry=" + hasEntry)
        if (hasEntry) {
            pageStack.push(Qt.resolvedUrl("ResultPage.qml"),
                           { entry: op.entry })
        } else {
            toast.show("No code found in that image")
        }
    }

    Timer {
        id: galleryDeliverTimer
        interval: 250
        repeat: true
        running: scannerPage.pendingGalleryOp !== null
        onTriggered: {
            if (scannerPage.status === PageStatus.Active
                    && pageStack.currentPage === scannerPage
                    && !pageStack.busy) {
                scannerPage.deliverGalleryOp()
            }
        }
    }

    Camera {
        id: camera
        position: Camera.BackFace

        focus {
            focusMode: Camera.FocusContinuous
        }

        flash.mode: settings.torchOn ? Camera.FlashTorch : Camera.FlashOff

        onError: {
            decoder.logMessage("camera error " + error + ": " + errorString)
            toast.show("Camera error: " + errorString)
        }

        imageCapture {
            onImageSaved: function (id, fileName) {
                scannerPage.captureFailures = 0
                decoder.submitImageFile(fileName)
            }
            onCaptureFailed: function (id, message) {
                scannerPage.noteCaptureFailure()
            }
            onReadyForCaptureChanged: {
                if (camera.imageCapture.ready && !scannerPage.succeeded) {
                    decoder.requestCapture()
                }
            }
        }

        Component.onCompleted: decoder.attachCamera(camera)
    }

    VideoOutput {
        id: viewfinder
        anchors.fill: parent
        source: camera
    }

    property int unloadedTicks: 0
    property int cameraRestarts: 0
    Timer {
        id: captureTimer
        interval: 700
        repeat: true
        running: scannerPage.status === PageStatus.Active && !scannerPage.succeeded
        onTriggered: {
            if (camera.status === Camera.UnloadedStatus) {
                if (++scannerPage.unloadedTicks >= 3 && scannerPage.cameraRestarts < 5) {
                    scannerPage.unloadedTicks = 0
                    scannerPage.cameraRestarts++
                    camera.start()
                }
            } else {
                scannerPage.unloadedTicks = 0
            }
            decoder.requestCapture()
        }
    }

    Timer {
        id: successTimer
        property var entry
        interval: 900
        onTriggered: {
            if (pageStack.currentPage === scannerPage) {
                pageStack.push(Qt.resolvedUrl("ResultPage.qml"), { entry: entry })
                scannerPage.reset()
            }
        }
    }

    onStatusChanged: {
        if (status === PageStatus.Active) {
            captureFailures = 0
            camera.start()
            // NOTE: gallery results are NOT delivered here — pushing a
            // page while Silica is still updating status causes the
            // binding-loop warning and a swallowed toast. The settle-timer
            // above handles delivery once everything is quiet.
        } else if (status === PageStatus.Inactive && !succeeded) {
            camera.stop()
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: parent.height

        PullDownMenu {
            MenuItem {
                text: "History"
                onClicked: pageStack.push(Qt.resolvedUrl("HistoryPage.qml"))
            }
            MenuItem {
                text: "Settings"
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: "About"
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
        }

        Rectangle {
            id: reticle
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height) * 0.68
            height: width * 0.62
            radius: Theme.paddingSmall
            color: "transparent"
            border.color: scannerPage.succeeded ? "#33cc33" : Theme.highlightColor
            border.width: 3
            opacity: scannerPage.succeeded ? 1.0 : 0.55
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.paddingLarge
            anchors.horizontalCenter: parent.horizontalCenter
            width: scanHint.width + Theme.paddingLarge * 2
            height: scanHint.height + Theme.paddingMedium
            radius: Theme.paddingSmall
            color: Theme.highlightBackgroundColor
            opacity: 0.85

            Label {
                id: scanHint
                anchors.centerIn: parent
                text: scannerPage.succeeded ? "Code scanned"
                                            : "Point the camera at a code"
                color: Theme.primaryColor
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        Column {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.paddingLarge * 4
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.paddingMedium

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: settings.torchOn ? "Torch off" : "Torch on"
                onClicked: settings.torchOn = !settings.torchOn
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Import from gallery"
                onClicked: pageStack.push(imagePickerPage)
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Enter manually"
                onClicked: {
                    var dlg = pageStack.push(Qt.resolvedUrl("ResultPage.qml"),
                                             { entry: { scanId: -1, format: "TEXT", value: "" },
                                               manual: true })
                }
            }
        }
    }

    Component {
        id: imagePickerPage
        // NOTE: ImagePickerPage needs the Tracker document gallery, which
        // is broken/unreachable on this device (DocumentGalleryModel cannot
        // connect) — it opens empty. FilePickerPage browses the filesystem
        // directly and is proven on this phone.
        FilePickerPage {
            nameFilters: [ '*.png', '*.jpg', '*.jpeg', '*.JPG', '*.PNG' ]
            onSelectedContentPropertiesChanged: {
                if (selectedContentProperties) {
                    decoder.decodeFile(selectedContentProperties.filePath)
                }
            }
        }
    }

    Connections {
        target: decoder
        onDecoded: {
            if (scannerPage.status === PageStatus.Active && !scannerPage.succeeded) {
                scannerPage.succeed(text, format)
            } else {
                // Gallery one-shot (or a straggler from the live loop):
                // queue for delivery on next Active, when navigation is safe.
                decoder.logMessage("decoded queued, status=" + scannerPage.status)
                var id = history.addScan(format, text)
                scannerPage.pendingGalleryOp = { entry: { scanId: id, format: format, value: text } }
                if (scannerPage.status === PageStatus.Active && !scannerPage.succeeded)
                    scannerPage.deliverGalleryOp()
            }
        }
        onNotFound: {
            decoder.logMessage("notFound queued, status=" + scannerPage.status)
            scannerPage.pendingGalleryOp = { entry: null }
            if (scannerPage.status === PageStatus.Active && !scannerPage.succeeded)
                scannerPage.deliverGalleryOp()
        }
    }

    Toast { id: toast }
}
