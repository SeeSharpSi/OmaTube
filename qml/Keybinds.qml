import QtQuick

QtObject {
    readonly property var entries: [
        { key: "h", label: qsTr("history"), pages: ["feed", "history", "watchnext"] },
        { key: "w", label: qsTr("watch next"), pages: ["feed", "history", "watchnext"] },
        { key: "s", label: qsTr("config"), pages: ["feed", "history", "watchnext"] },
        { key: "j/k", label: qsTr("scroll"), pages: ["feed", "history", "watchnext"] },
        { key: "r", label: qsTr("refresh"), pages: ["feed"] },
        { key: "q", label: qsTr("quit"), pages: ["feed", "history", "watchnext"] },
        { key: "esc", label: qsTr("feed"), pages: ["history", "watchnext"] },
        { key: "right-click", label: qsTr("watch next"), pages: ["feed"] },
        { key: "right-click", label: qsTr("delete"), pages: ["history"] },
        { key: "right-click", label: qsTr("remove"), pages: ["watchnext"] }
    ]

    function footerText(page) {
        var lines = []
        for (var i = 0; i < entries.length; ++i) {
            var e = entries[i]
            if (e.pages.indexOf(page) !== -1)
                lines.push(e.key + ": " + e.label)
        }
        return lines.join("\n")
    }
}
