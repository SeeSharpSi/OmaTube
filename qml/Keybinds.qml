import QtQuick

QtObject {
    readonly property var entries: [
        { key: "h", label: qsTr("history"), pages: ["feed"] },
        { key: "s", label: qsTr("settings"), pages: ["feed", "history"] },
        { key: "j/k", label: qsTr("scroll"), pages: ["feed", "history"] },
        { key: "r", label: qsTr("refresh"), pages: ["feed"] },
        { key: "q", label: qsTr("quit"), pages: ["feed", "history"] },
        { key: "esc", label: qsTr("feed"), pages: ["history"] },
        { key: "right-click", label: qsTr("delete"), pages: ["history"] }
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
