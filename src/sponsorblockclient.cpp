#include "sponsorblockclient.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QThread>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

namespace {

bool isValidVideoId(const QString &videoId)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    return expression.match(videoId).hasMatch();
}

} // namespace

namespace SponsorBlock {

bool isSupportedCategory(const QString &category)
{
    return kCategories.contains(category);
}

int normalizeAction(int action, int fallback)
{
    if (action == Action::ManualSkip || action == Action::AutoSkip)
        return action;
    return fallback == Action::ManualSkip || fallback == Action::AutoSkip ? fallback : Action::Nothing;
}

QString actionKeySuffix(const QString &category)
{
    return category;
}

QUrl buildSkipSegmentsUrl(const QString &videoId, const QStringList &categories)
{
    if (!::isValidVideoId(videoId))
        return {};

    QUrl url(QStringLiteral("https://sponsor.ajay.app/api/skipSegments"));
    url.setPath(QStringLiteral("/api/skipSegments"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("videoID"), videoId);
    for (const QString &category : categories) {
        if (isSupportedCategory(category))
            query.addQueryItem(QStringLiteral("category"), category);
    }
    query.addQueryItem(QStringLiteral("actionType"), QStringLiteral("skip"));
    url.setQuery(query);
    return url;
}

QVariantList parseSkipSegmentsResponse(const QByteArray &bytes, QString *error)
{
    if (error)
        error->clear();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("SponsorBlock response is not valid JSON.");
        return {};
    }
    if (!document.isArray()) {
        if (error)
            *error = QStringLiteral("SponsorBlock response is not a JSON array.");
        return {};
    }

    struct Segment {
        double start = 0.0;
        double end = 0.0;
        QString category;
        QString uuid;
    };
    QList<Segment> segments;

    const QJsonArray array = document.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString category = object.value(QStringLiteral("category")).toString();
        if (!isSupportedCategory(category))
            continue;

        const QJsonArray segmentArray = object.value(QStringLiteral("segment")).toArray();
        if (segmentArray.size() != 2)
            continue;

        const double start = segmentArray.at(0).toDouble();
        const double end = segmentArray.at(1).toDouble();
        if (!std::isfinite(start) || !std::isfinite(end))
            continue;
        if (start < 0.0 || end <= start || end >= 86400.0)
            continue;

        Segment segment;
        segment.start = start;
        segment.end = end;
        segment.category = category;
        QJsonValue uuidValue = object.value(QStringLiteral("UUID"));
        if (!uuidValue.isString())
            uuidValue = object.value(QStringLiteral("uuid"));
        segment.uuid = uuidValue.isString() ? uuidValue.toString() : QString();
        segments.append(segment);
    }

    std::sort(
        segments.begin(),
        segments.end(),
        [](const Segment &a, const Segment &b) { return a.start < b.start; });

    QVariantList result;
    for (const Segment &segment : segments) {
        QVariantMap map;
        map.insert(QStringLiteral("category"), segment.category);
        map.insert(QStringLiteral("start"), segment.start);
        map.insert(QStringLiteral("end"), segment.end);
        map.insert(QStringLiteral("uuid"), segment.uuid);
        result.append(map);
    }
    return result;
}

} // namespace SponsorBlock

SponsorBlockClient::SponsorBlockClient(QObject *parent)
    : QObject(parent)
{
}

void SponsorBlockClient::fetchSegments(
    const QString &videoId,
    const QStringList &categories,
    SegmentsCallback callback)
{
    QStringList effectiveCategories;
    for (const QString &category : categories) {
        if (SponsorBlock::isSupportedCategory(category))
            effectiveCategories.append(category);
    }

    if (!::isValidVideoId(videoId) || effectiveCategories.isEmpty()) {
        const auto invoke = [callback = std::move(callback)] {
            callback({}, QStringLiteral("invalid"), false);
        };
        if (thread() == QThread::currentThread())
            QMetaObject::invokeMethod(this, invoke, Qt::QueuedConnection);
        else
            callback({}, QStringLiteral("invalid"), false);
        return;
    }

    const QUrl url = SponsorBlock::buildSkipSegmentsUrl(videoId, effectiveCategories);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmaTube/0.1"));
    request.setTransferTimeout(15000);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_network.get(request);
    auto body = std::make_shared<QByteArray>();
    auto exceededSizeLimit = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, body, exceededSizeLimit] {
        const QByteArray chunk = reply->readAll();
        if (body->size() + chunk.size() > kMaxResponseBytes) {
            *exceededSizeLimit = true;
            reply->abort();
            return;
        }
        body->append(chunk);
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, body, exceededSizeLimit, callback = std::move(callback)] {
        if (!*exceededSizeLimit) {
            const QByteArray finalChunk = reply->readAll();
            if (body->size() + finalChunk.size() > kMaxResponseBytes)
                *exceededSizeLimit = true;
            else
                body->append(finalChunk);
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        bool notFound = false;
        if (*exceededSizeLimit) {
            error = QStringLiteral("SponsorBlock response exceeded the size limit.");
        } else if (status == 404) {
            notFound = true;
        } else if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            error = status > 0
                ? QStringLiteral("SponsorBlock request failed with HTTP %1.").arg(status)
                : reply->errorString();
        }
        reply->deleteLater();

        if (notFound) {
            callback({}, {}, true);
        } else if (!error.isEmpty()) {
            callback({}, error, false);
        } else {
            QString parseError;
            const QVariantList segments = SponsorBlock::parseSkipSegmentsResponse(*body, &parseError);
            if (!parseError.isEmpty())
                callback({}, parseError, false);
            else
                callback(segments, {}, false);
        }
    });
}