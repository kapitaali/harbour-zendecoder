import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: aboutPage

    property string sourceLink: "https://github.com/kapitaali/harbour-zendecoder"
    property string tipLink: "https://ko-fi.com/kapitaali"

    allowedOrientations: Orientation.Portrait

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: aboutColumn.height + Theme.paddingLarge

        Column {
            id: aboutColumn
            width: parent.width - Theme.horizontalPageMargin * 2
            x: Theme.horizontalPageMargin
            spacing: Theme.paddingMedium

            PageHeader {
                title: "About"
                leftMargin: 0
            }

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: Qt.resolvedUrl("../img/harbour-zendecoder.png")
                width: 86
                height: 86
            }

            Label {
                width: parent.width
                text: "ZenDecoder"
                color: Theme.primaryColor
                font.pixelSize: Theme.fontSizeExtraLarge
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                width: parent.width
                // The real build version, handed over by the RPM build from
                // the git tag; hand-built binaries report "dev". The build
                // id (git hash or timestamp) tells test builds apart when
                // the version hasn't moved.
                text: "Version " + appVersion + " · build " + buildId
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                width: parent.width
                text: "A native barcode and QR code reader for Sailfish OS.\n\n" +
                      "• Live camera scanning\n" +
                      "• Gallery image import + manual entry\n" +
                      "• History with CSV/JSON export\n" +
                      "• Optional product lookup\n\n" +
                      "Decoding runs on-device via the system zxing service. " +
                      "Network is only used for opt-in product lookup."
                color: Theme.primaryColor
                font.pixelSize: Theme.fontSizeSmall
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            SectionHeader {
                text: "Support"
            }

            Label {
                width: parent.width
                text: "ZenDecoder is free and open source. If you want to " +
                      "support development, Pro licenses are available here:"
                color: Theme.primaryColor
                font.pixelSize: Theme.fontSizeSmall
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: trial.kofiUrl.length > 0
                text: "Ko-fi"
                onClicked: Qt.openUrlExternally(aboutPage.tipLink)
            }

            SectionHeader {
                text: "Source code"
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "View on GitHub"
                onClicked: Qt.openUrlExternally(aboutPage.sourceLink)
            }

            Label {
                width: parent.width
                text: "GPL-3.0-only. zxing decoding by the system service (zxing-cpp)."
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
    }
}
