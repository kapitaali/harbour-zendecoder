import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    id: settingsPage

    allowedOrientations: Orientation.Portrait

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: settingsColumn.height + Theme.paddingLarge

        Column {
            id: settingsColumn
            width: parent.width - Theme.horizontalPageMargin * 2
            x: Theme.horizontalPageMargin
            spacing: Theme.paddingMedium

            PageHeader {
                title: "Settings"
                leftMargin: 0
            }

            SectionHeader { text: "Scanning" }

            TextSwitch {
                width: parent.width
                text: "Product lookup (Open Food Facts)"
                description: "Look up EAN/UPC codes online. Decoding itself stays on-device."
                checked: settings.productLookupEnabled
                onClicked: settings.productLookupEnabled = !settings.productLookupEnabled
            }

            SectionHeader { text: "Formats" }

            Label {
                width: parent.width
                text: "Which codes the scanner recognizes. All groups are on by default; "
                      + "if you turn every group off, nothing will be decoded."
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            TextSwitch {
                width: parent.width
                text: "Retail"
                description: "EAN-8, EAN-13, UPC-A, UPC-E, ISBN, ITF"
                checked: settings.formatRetail
                onClicked: settings.formatRetail = !settings.formatRetail
            }

            TextSwitch {
                width: parent.width
                text: "Linear codes"
                description: "Code 39, Code 93, Code 128, Codabar, DataBar, DX film edge"
                checked: settings.formatLinear
                onClicked: settings.formatLinear = !settings.formatLinear
            }

            TextSwitch {
                width: parent.width
                text: "2D codes"
                description: "QR (incl. Micro QR and rMQR), Aztec, Data Matrix, MaxiCode"
                checked: settings.formatMatrix
                onClicked: settings.formatMatrix = !settings.formatMatrix
            }

            TextSwitch {
                width: parent.width
                text: "PDF417"
                description: "PDF417, Compact and Micro PDF417"
                checked: settings.formatPdf417
                onClicked: settings.formatPdf417 = !settings.formatPdf417
            }

            SectionHeader { text: "Feedback" }

            TextSwitch {
                width: parent.width
                text: "Sound"
                checked: settings.soundEnabled
                onClicked: settings.soundEnabled = !settings.soundEnabled
            }

            TextSwitch {
                width: parent.width
                text: "Vibration"
                checked: settings.vibrationEnabled
                onClicked: settings.vibrationEnabled = !settings.vibrationEnabled
            }

            SectionHeader { text: "Pro" }

            Label {
                width: parent.width
                text: trial.mode === "SeedPro" ? "Pro preview — all features unlocked"
                      : trial.mode === "ProUnlocked" ? "Pro unlocked"
                      : trial.mode === "Trial" ? "Trial: " + trial.trialDaysLeft + " days left"
                      : "Free mode — trial ended"
                color: Theme.primaryColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            TextField {
                id: keyField
                width: parent.width
                label: "License key"
                placeholderText: "ZEN-PRO-…"
                visible: trial.mode !== "SeedPro"
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: trial.mode !== "SeedPro" && trial.mode !== "ProUnlocked"
                text: "Unlock Pro"
                onClicked: {
                    if (!trial.submitKey(keyField.text))
                        toast.show("Invalid key")
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: trial.kofiUrl.length > 0 && trial.mode !== "SeedPro" && trial.mode !== "ProUnlocked"
                text: "Get Pro on Ko-fi"
                onClicked: Qt.openUrlExternally(trial.kofiUrl)
            }
        }
    }

    Toast { id: toast }
}
