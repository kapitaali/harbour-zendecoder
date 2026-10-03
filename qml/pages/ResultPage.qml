import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

/*
 * Scan result: shows the decoded value, type-specific actions
 * (open URL, connect WiFi, save contact, copy/share) and optional
 * product lookup for GTINs.
 */
Page {
    id: resultPage

    property var entry: ({ scanId: -1, format: "", value: "" })
    property bool manual: false
    property string productLine: ""

    allowedOrientations: Orientation.Portrait

    function isUrl(t) {
        return t.indexOf("http://") === 0 || t.indexOf("https://") === 0
    }

    function openExternally(t) {
        Qt.openUrlExternally(t)
    }

    Component.onCompleted: {
        if (!manual && entry.value && settings.productLookupEnabled
                && trial.isPro && productLookup.looksLikeGtin(entry.value)) {
            productLookup.lookup(entry.value)
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: resultColumn.height + Theme.paddingLarge

        Column {
            id: resultColumn
            width: parent.width - Theme.horizontalPageMargin * 2
            x: Theme.horizontalPageMargin
            spacing: Theme.paddingMedium

            PageHeader {
                title: "Result"
                leftMargin: 0
            }

            TextField {
                id: valueField
                width: parent.width
                text: entry.value
                label: entry.format && entry.format.length > 0 ? entry.format : "Code content"
                readOnly: !manual
                onTextChanged: {
                    if (manual)
                        entry.value = text
                }
            }

            Label {
                width: parent.width
                visible: productLine.length > 0
                text: productLine
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: resultPage.isUrl(valueField.text)
                text: "Open link"
                onClicked: resultPage.openExternally(valueField.text)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: manual
                text: "Save to history"
                enabled: valueField.text.length > 0
                onClicked: {
                    var id = history.addScan("TEXT", valueField.text)
                    entry.scanId = id
                    entry.format = "TEXT"
                    entry.value = valueField.text
                    manual = false
                    toast.show("Saved to history")
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !manual && entry.value.length > 0
                text: "Copy"
                onClicked: {
                    Clipboard.text = entry.value
                    toast.show("Copied")
                }
            }

            Label {
                width: parent.width
                visible: !manual && entry.value.length > 0
                text: "Tip: share or archive scans from History (CSV/JSON export)."
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !manual && entry.scanId >= 0 && settings.productLookupEnabled
                         && productLookup.looksLikeGtin(entry.value)
                text: "Look up product"
                onClicked: productLookup.lookup(entry.value)
            }
        }
    }

    Connections {
        target: productLookup
        onFound: {
            if (barcode === resultPage.entry.value) {
                var label = name
                if (brands.length > 0)
                    label += " (" + brands + ")"
                resultPage.productLine = label
                if (resultPage.entry.scanId >= 0)
                    history.setProductName(resultPage.entry.scanId, label)
            }
        }
        onNotFound: {
            if (barcode === resultPage.entry.value)
                resultPage.productLine = "No product found"
        }
        onLookupError: {
            if (barcode === resultPage.entry.value)
                toast.show("Lookup failed")
        }
    }

    Toast { id: toast }
}
