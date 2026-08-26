#include "playbacksettings.h"

namespace {

constexpr int autoHeight = 0;

bool isAllowedHeight(int height)
{
    switch (height) {
    case 0:
    case 2160:
    case 1440:
    case 1080:
    case 720:
    case 480:
    case 360:
        return true;
    default:
        return false;
    }
}

} // namespace

namespace PlaybackSettings {

QString normalizeBackend(const QString &backend)
{
    const QString normalized = backend.trimmed().toLower();
    if (normalized == QStringLiteral("mpv"))
        return QStringLiteral("mpv");
    return QStringLiteral("iframe");
}

int normalizeMaximumVideoHeight(int height)
{
    return isAllowedHeight(height) ? height : autoHeight;
}

QString ytDlpFormatForMaximumHeight(int height)
{
    const int normalized = normalizeMaximumVideoHeight(height);
    if (normalized == autoHeight)
        return {};
    return QStringLiteral("bestvideo*[height<=%1]+bestaudio/best[height<=%1]").arg(normalized);
}

} // namespace PlaybackSettings
