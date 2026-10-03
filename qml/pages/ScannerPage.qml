import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0
import QtMultimedia 5.6
import "../components"

/*
 * Live barcode viewfinder (default page).
 *
 * Decoding goes through the system zxing service over D-Bus (see
 * src/decoder.h for why still captures are used instead of viewfinder
 * frames: the overlay backend runs no filter chain, QVideoProbe fails).
 * The page takes still captures into a private cache file and hands each
 * written file to decoder.submitImageFile(); a decoded value is stored in
 * history and pushed to ResultPage.
 *
 * v0.1: the service returns text only (format empty). ResultPage shows the
 * raw value + product lookup for GTINs.
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

    // Gallery one-shot results arrive while the file picker (or its
    // internal pages) is on top. Navigating immediately races the picker's
    // own transitions ("cannot pop/push while transition is in progress"),
    // so results wait in galleryOp and a timer delivers them once the stack
    // settles: pop back to this page, then push the result or toast.
    // galleryPicker bounds the auto-pop to our own picker session so the
    // timer can never eat a page the user opened themselves.
    property var galleryPicker: null
    property var galleryOp: null

    Timer {
        id: galleryOpTimer
        interval: 350
        repeat: true
        running: scannerPage.galleryOp !== null
        onTriggered: {
            if (pageStack.busy)
                return
            if (pageStack.currentPage !== scannerPage) {
                if (scannerPage.galleryPicker !== null) {
                    pageStack.pop()
                    return
                }
                // Picker session over (user backed out manually): drop it.
                scannerPage.galleryOp = null
                return
            }
            var op = scannerPage.galleryOp
            scannerPage.galleryOp = null
            scannerPage.galleryPicker = null
            decoder.logMessage("galleryOp delivering, hasEntry=" + (op.entry !== null))
            if (op.entry) {
                pageStack.push(Qt.resolvedUrl("ResultPage.qml"),
                               { entry: op.entry })
            } else {
                toast.show("No code found in that image")
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
            // Picker session ended with nothing pending (user backed out):
            // release the auto-pop guard so the timer can never touch
            // pages the user opened themselves.
            if (galleryOp === null)
                galleryPicker = null
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
                onClicked: {
                    var pg = pageStack.push(imagePickerPage)
                    scannerPage.galleryPicker = pg
                    decoder.logMessage("import opened, picker null=" + (pg === null))
                }
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
            decoder.logMessage("signal decoded, status=" + scannerPage.status
                               + " pickerNull=" + (scannerPage.galleryPicker === null))
            if (scannerPage.status === PageStatus.Active && !scannerPage.succeeded) {
                scannerPage.succeed(text, format)
            } else if (scannerPage.galleryPicker !== null) {
                // One-shot gallery decode: queue for the settles-timer
                // (direct navigation races the picker's transitions).
                var id = history.addScan(format, text)
                scannerPage.galleryOp = { entry: { scanId: id, format: format, value: text } }
            } else {
                decoder.logMessage("decoded dropped: inactive and no picker session")
            }
        }
        onNotFound: {
            decoder.logMessage("signal notFound, status=" + scannerPage.status
                               + " pickerNull=" + (scannerPage.galleryPicker === null))
            if (scannerPage.galleryPicker !== null) {
                scannerPage.galleryOp = { entry: null }
            } else {
                toast.show("No code found in that image")
            }
        }
    }

    Toast { id: toast }
}
