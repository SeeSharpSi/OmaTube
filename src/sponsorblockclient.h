#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <functional>

namespace SponsorBlock {

inline const QStringList kCategories = {
    QStringLiteral("sponsor"),
    QStringLiteral("selfpromo"),
    QStringLiteral("interaction"),
    QStringLiteral("intro"),
    QStringLiteral("outro"),
    QStringLiteral("preview"),
    QStringLiteral("music_offtopic"),
    QStringLiteral("poi_highlight"),
};

enum Action {
    Nothing = 0,
    ManualSkip = 1,
    AutoSkip = 2,
};

bool isSupportedCategory(const QString &category);
int normalizeAction(int action, int fallback = 0);
QString actionKeySuffix(const QString &category);
QUrl buildSkipSegmentsUrl(const QString &videoId, const QStringList &categories);
QVariantList parseSkipSegmentsResponse(const QByteArray &bytes, QString *error);

} // namespace SponsorBlock

class SponsorBlockClient : public QObject
{
    Q_OBJECT

public:
    explicit SponsorBlockClient(QObject *parent = nullptr);

    using SegmentsCallback = std::function<void(QVariantList segments, QString error, bool notFound)>;
    void fetchSegments(const QString &videoId, const QStringList &categories, SegmentsCallback callback);

private:
    QNetworkAccessManager m_network;

    static constexpr qint64 kMaxResponseBytes = 262144;
};