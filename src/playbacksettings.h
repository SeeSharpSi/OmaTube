#pragma once

#include <QString>

namespace PlaybackSettings {

QString normalizeBackend(const QString &backend);
int normalizeMaximumVideoHeight(int height);
QString ytDlpFormatForMaximumHeight(int height);

} // namespace PlaybackSettings
