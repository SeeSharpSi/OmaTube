import QtQuick

QtObject {
    readonly property var entries: [
        { key: "h", label: qsTr("history"), pages: ["feed", "history", "watchnext"] },
        { key: "w", label: qsTr("watch next"), pages: ["feed", "history", "watchnext"] },
        { key: "c", label: qsTr("config"), pages: ["feed", "history", "watchnext"] },
        { key: "r", label: qsTr("refresh"), pages: ["feed"] },
        { key: "q", label: qsTr("quit"), pages: ["feed", "history", "watchnext"] },
        { key: "j/k", label: qsTr("scroll"), pages: ["feed", "history", "watchnext"] },
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

    function escapeHtml(value) {
        return value.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    // Single-line rich text for the top-middle header label. When the key
    // is the action's first letter (history, watch next, config, refresh,
    // quit), only that letter is highlighted; otherwise the key is shown
    // highlighted ahead of the action (j/k, esc, right-click).
    // highlight is expected to be a color (e.g. the theme foreground).
    function headerText(page, highlight) {
        var parts = []
        for (var i = 0; i < entries.length; ++i) {
            var e = entries[i]
            if (e.pages.indexOf(page) === -1)
                continue
            var open = "<b><font color=\"" + highlight + "\">"
            var close = "</font></b>"
            if (e.key.length === 1 && e.label.charAt(0).toLowerCase() === e.key.toLowerCase())
                parts.push(open + escapeHtml(e.label.charAt(0)) + close
                    + escapeHtml(e.label.slice(1)))
            else
                parts.push(open + escapeHtml(e.key) + close + " " + escapeHtml(e.label))
        }
        return parts.join("  ")
    }
}
