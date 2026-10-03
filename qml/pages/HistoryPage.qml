import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    id: historyPage

    allowedOrientations: Orientation.Portrait

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: history
        header: PageHeader { title: "History" }

        PullDownMenu {
            MenuItem {
                text: "Export CSV"
                onClicked: {
                    var path = history.exportCsv()
                    toast.show(path.length > 0 ? "Exported to Documents" : "Export failed")
                }
            }
            MenuItem {
                text: "Export JSON"
                onClicked: {
                    var path = history.exportJson()
                    toast.show(path.length > 0 ? "Exported to Documents" : "Export failed")
                }
            }
            MenuItem {
                text: "Clear all"
                onClicked: history.clearAll()
            }
        }

        delegate: ListItem {
            id: delegate
            width: listView.width
            contentHeight: Theme.itemSizeMedium

            Column {
                x: Theme.horizontalPageMargin
                width: parent.width - Theme.horizontalPageMargin * 2
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.paddingSmall

                Label {
                    width: parent.width
                    text: model.value
                    color: Theme.primaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    truncationMode: TruncationMode.Fade
                }
                Label {
                    width: parent.width
                    text: (model.format.length > 0 ? model.format + " — " : "")
                          + Qt.formatDateTime(model.timestamp, "dd.MM.yyyy hh:mm")
                          + (model.productName.length > 0 ? " — " + model.productName : "")
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                    truncationMode: TruncationMode.Fade
                }
            }

            menu: ContextMenu {
                MenuItem {
                    text: "Open"
                    onClicked: pageStack.push(Qt.resolvedUrl("ResultPage.qml"),
                                              { entry: { scanId: model.scanId,
                                                         format: model.format,
                                                         value: model.value } })
                }
                MenuItem {
                    text: "Copy"
                    onClicked: {
                        Clipboard.text = model.value
                        toast.show("Copied")
                    }
                }
                MenuItem {
                    text: "Delete"
                    onClicked: history.deleteScan(model.scanId)
                }
            }

            onClicked: pageStack.push(Qt.resolvedUrl("ResultPage.qml"),
                                      { entry: { scanId: model.scanId,
                                                 format: model.format,
                                                 value: model.value } })
        }

        ViewPlaceholder {
            enabled: history.count === 0
            text: "No scans yet"
            hintText: "Point the camera at a code to start"
        }
    }

    Toast { id: toast }
}
