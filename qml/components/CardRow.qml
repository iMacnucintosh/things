import QtQuick

// Horizontal row of cards. page(±1) scrolls one screenful; canLeft/canRight drive the arrow buttons
// that DiscoverPage puts in each row header (drag and trackpad scrolling still work).
Item {
    id: root
    property alias model: view.model
    property alias delegate: view.delegate
    readonly property alias view: view

    implicitHeight: 204

    readonly property bool canLeft: view.contentX > 1
    readonly property bool canRight: view.contentX + view.width < view.contentWidth - 1

    // One "page" = as many whole cards as fit (card 176 + spacing 12).
    function page(dir) {
        const unit = 188
        const step = Math.max(unit, Math.floor(view.width / unit) * unit)
        const max = Math.max(0, view.contentWidth - view.width)
        slide.stop()
        slide.from = view.contentX
        slide.to = Math.min(max, Math.max(0, view.contentX + dir * step))
        slide.start()
    }

    ListView {
        id: view
        anchors.fill: parent
        orientation: ListView.Horizontal
        spacing: 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        topMargin: 2
        bottomMargin: 2

        NumberAnimation {
            id: slide
            target: view
            property: "contentX"
            duration: 340
            easing.type: Easing.OutCubic
        }
    }
}
