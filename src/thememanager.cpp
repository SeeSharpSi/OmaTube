#include "thememanager.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>
#include <QTimer>

namespace {
constexpr auto themeSetting = "appearance/theme";
constexpr auto defaultThemeId = "default";
constexpr auto rosePineThemeId = "rose-pine";
constexpr auto nordThemeId = "nord";
constexpr auto omarchyThemeId = "omarchy";

bool isThemeId(const QString &themeId)
{
    return themeId == QString::fromLatin1(defaultThemeId)
        || themeId == QString::fromLatin1(rosePineThemeId)
        || themeId == QString::fromLatin1(nordThemeId)
        || themeId == QString::fromLatin1(omarchyThemeId);
}

QString builtInThemePath(const QString &themeId)
{
    const QString builtInId = themeId == QString::fromLatin1(rosePineThemeId)
        ? QString::fromLatin1(rosePineThemeId)
        : themeId == QString::fromLatin1(nordThemeId)
            ? QString::fromLatin1(nordThemeId)
            : QString::fromLatin1(defaultThemeId);
    return QStringLiteral(":/themes/%1/colors.toml").arg(builtInId);
}

QColor mixColors(const QColor &first, const QColor &second, qreal amount)
{
    const qreal firstAmount = 1.0 - amount;
    return QColor::fromRgbF(
        first.redF() * firstAmount + second.redF() * amount,
        first.greenF() * firstAmount + second.greenF() * amount,
        first.blueF() * firstAmount + second.blueF() * amount);
}

QStringList omarchyColorsCandidates()
{
#ifdef Q_OS_LINUX
    const QString homePath = QDir::homePath();
    const QString stateHome = qEnvironmentVariable("XDG_STATE_HOME").trimmed();
    const QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME").trimmed();
    QStringList candidates = {
        homePath + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml"),
        homePath + QStringLiteral("/.config/omarchy/current/theme/colors.toml")
    };
    const auto appendCandidate = [&candidates](const QString &basePath) {
        if (basePath.isEmpty())
            return;
        const QString candidate = QDir(basePath).filePath(
            QStringLiteral("omarchy/current/theme/colors.toml"));
        if (!candidates.contains(candidate))
            candidates.append(candidate);
    };
    appendCandidate(stateHome);
    appendCandidate(configHome);
    return candidates;
#else
    return {};
#endif
}
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    connect(
        &m_omarchyWatcher,
        &QFileSystemWatcher::fileChanged,
        this,
        [this](const QString &) { scheduleOmarchyReload(); });
    connect(
        &m_omarchyWatcher,
        &QFileSystemWatcher::directoryChanged,
        this,
        [this](const QString &) { scheduleOmarchyReload(); });

    QSettings settings;
    m_selectedThemeId = settings.value(
        QString::fromLatin1(themeSetting),
        QString::fromLatin1(defaultThemeId)).toString();
    if (!isThemeId(m_selectedThemeId))
        m_selectedThemeId = QString::fromLatin1(defaultThemeId);
    applySelectedTheme();
}

QString ThemeManager::selectedThemeId() const
{
    return m_selectedThemeId;
}

QVariantMap ThemeManager::colors() const
{
    return m_colors;
}

bool ThemeManager::omarchyThemeAvailable() const
{
    return m_omarchyThemeAvailable;
}

QString ThemeManager::omarchyThemeName() const
{
    return m_omarchyThemeName;
}

void ThemeManager::setSelectedThemeId(const QString &themeId)
{
    if (!isThemeId(themeId) || m_selectedThemeId == themeId)
        return;

    m_selectedThemeId = themeId;
    QSettings settings;
    settings.setValue(QString::fromLatin1(themeSetting), themeId);
    settings.sync();
    applySelectedTheme();
}

std::optional<QVariantMap> ThemeManager::loadColors(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Could not open theme colors: %1").arg(path);
        return std::nullopt;
    }

    static const QRegularExpression assignmentExpression(
        QStringLiteral("^\\s*([A-Za-z][A-Za-z0-9_]*)\\s*=\\s*([\\\"'])([^\\\"']*)\\2\\s*(?:#.*)?$"));
    static const QRegularExpression colorExpression(
        QStringLiteral("^#[0-9A-Fa-f]{6}$"));

    QHash<QString, QString> values;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QRegularExpressionMatch match = assignmentExpression.match(stream.readLine());
        if (match.hasMatch())
            values.insert(match.captured(1), match.captured(3));
    }
    if (file.error() != QFileDevice::NoError) {
        if (error)
            *error = QStringLiteral("Could not read theme colors: %1").arg(path);
        return std::nullopt;
    }

    auto colorFor = [&values](const QStringList &keys) -> QColor {
        for (const QString &key : keys) {
            const QString value = values.value(key);
            if (colorExpression.match(value).hasMatch())
                return QColor(value);
        }
        return {};
    };

    const QColor background = colorFor(
        {QStringLiteral("background"), QStringLiteral("bg"), QStringLiteral("color0")});
    const QColor foreground = colorFor(
        {QStringLiteral("foreground"), QStringLiteral("fg"), QStringLiteral("color7"),
         QStringLiteral("color15")});
    if (!background.isValid() || !foreground.isValid()) {
        if (error) {
            *error = QStringLiteral(
                "Theme colors must define valid background and foreground colors: %1")
                         .arg(path);
        }
        return std::nullopt;
    }

    QVariantMap colors;
    for (auto iterator = values.cbegin(); iterator != values.cend(); ++iterator) {
        if (colorExpression.match(iterator.value()).hasMatch())
            colors.insert(iterator.key(), QColor(iterator.value()));
    }

    auto insertColor = [&colors](const QString &key, const QColor &color) {
        colors.insert(key, color);
    };
    auto resolvedColor = [&colorFor](
                             const QStringList &keys,
                             const QColor &fallback) -> QColor {
        const QColor color = colorFor(keys);
        return color.isValid() ? color : fallback;
    };

    const QColor black(QStringLiteral("#000000"));
    const QColor accent = resolvedColor(
        {QStringLiteral("accent"), QStringLiteral("color4"), QStringLiteral("blue")},
        foreground);
    const QColor red = resolvedColor(
        {QStringLiteral("red"), QStringLiteral("color1")},
        foreground);
    const QColor yellow = resolvedColor(
        {QStringLiteral("yellow"), QStringLiteral("color3")},
        foreground);
    const QColor green = resolvedColor(
        {QStringLiteral("green"), QStringLiteral("color2")},
        foreground);
    const QColor cyan = resolvedColor(
        {QStringLiteral("cyan"), QStringLiteral("color6")},
        accent);
    const QColor blue = resolvedColor(
        {QStringLiteral("blue"), QStringLiteral("color4")},
        accent);
    const QColor magenta = resolvedColor(
        {QStringLiteral("magenta"), QStringLiteral("color5")},
        accent);

    insertColor(QStringLiteral("accent"), accent);
    insertColor(
        QStringLiteral("selection"),
        resolvedColor(
            {QStringLiteral("selection"), QStringLiteral("selection_background")},
            mixColors(background, accent, 0.22)));
    insertColor(
        QStringLiteral("muted"),
        resolvedColor(
            {QStringLiteral("muted"), QStringLiteral("color8")},
            mixColors(background, foreground, 0.25)));
    insertColor(QStringLiteral("background"), background);
    insertColor(
        QStringLiteral("dark_background"),
        resolvedColor(
            {QStringLiteral("dark_background"), QStringLiteral("dark_bg")},
            mixColors(background, black, 0.12)));
    insertColor(
        QStringLiteral("darker_background"),
        resolvedColor(
            {QStringLiteral("darker_background"), QStringLiteral("darker_bg")},
            mixColors(background, black, 0.24)));
    insertColor(
        QStringLiteral("lighter_background"),
        resolvedColor(
            {QStringLiteral("lighter_background"), QStringLiteral("lighter_bg"),
             QStringLiteral("color0")},
            mixColors(background, foreground, 0.10)));
    insertColor(QStringLiteral("foreground"), foreground);
    insertColor(
        QStringLiteral("dark_foreground"),
        resolvedColor(
            {QStringLiteral("dark_foreground"), QStringLiteral("dark_fg"),
             QStringLiteral("color8")},
            mixColors(foreground, background, 0.45)));
    insertColor(
        QStringLiteral("light_foreground"),
        resolvedColor(
            {QStringLiteral("light_foreground"), QStringLiteral("light_fg"),
             QStringLiteral("color7")},
            foreground));
    insertColor(
        QStringLiteral("bright_foreground"),
        resolvedColor(
            {QStringLiteral("bright_foreground"), QStringLiteral("bright_fg"),
             QStringLiteral("color15"), QStringLiteral("cursor")},
            foreground));
    insertColor(QStringLiteral("red"), red);
    insertColor(QStringLiteral("yellow"), yellow);
    insertColor(
        QStringLiteral("orange"),
        resolvedColor({QStringLiteral("orange")}, mixColors(red, yellow, 0.5)));
    insertColor(QStringLiteral("green"), green);
    insertColor(QStringLiteral("cyan"), cyan);
    insertColor(QStringLiteral("blue"), blue);
    insertColor(QStringLiteral("magenta"), magenta);
    insertColor(
        QStringLiteral("brown"),
        resolvedColor({QStringLiteral("brown")}, mixColors(red, background, 0.45)));
    insertColor(
        QStringLiteral("bright_red"),
        resolvedColor({QStringLiteral("bright_red"), QStringLiteral("color9")}, red));
    insertColor(
        QStringLiteral("bright_yellow"),
        resolvedColor({QStringLiteral("bright_yellow"), QStringLiteral("color11")}, yellow));
    insertColor(
        QStringLiteral("bright_green"),
        resolvedColor({QStringLiteral("bright_green"), QStringLiteral("color10")}, green));
    insertColor(
        QStringLiteral("bright_cyan"),
        resolvedColor({QStringLiteral("bright_cyan"), QStringLiteral("color14")}, cyan));
    insertColor(
        QStringLiteral("bright_blue"),
        resolvedColor({QStringLiteral("bright_blue"), QStringLiteral("color12")}, blue));
    insertColor(
        QStringLiteral("bright_magenta"),
        resolvedColor({QStringLiteral("bright_magenta"), QStringLiteral("color13")}, magenta));

    const QString configuredMode = values.value(QStringLiteral("mode"));
    colors.insert(
        QStringLiteral("mode"),
        configuredMode == QStringLiteral("light") || configuredMode == QStringLiteral("dark")
            ? configuredMode
            : background.lightnessF() > foreground.lightnessF()
                ? QStringLiteral("light")
                : QStringLiteral("dark"));
    if (error)
        error->clear();
    return colors;
}

void ThemeManager::applySelectedTheme()
{
    QString colorsPath;
    m_omarchyThemeAvailable = false;
    m_omarchyThemeName.clear();

    if (m_selectedThemeId == QString::fromLatin1(omarchyThemeId)) {
        colorsPath = findOmarchyColorsPath();
        if (!colorsPath.isEmpty()) {
            const std::optional<QVariantMap> omarchyColors = loadColors(colorsPath);
            if (omarchyColors) {
                m_colors = *omarchyColors;
                m_omarchyThemeAvailable = true;
                m_omarchyThemeName = readOmarchyThemeName(colorsPath);
            }
        }
        if (!m_omarchyThemeAvailable) {
            const std::optional<QVariantMap> nordColors = loadColors(
                builtInThemePath(QString::fromLatin1(nordThemeId)));
            Q_ASSERT(nordColors);
            if (nordColors)
                m_colors = *nordColors;
        }
    } else {
        const std::optional<QVariantMap> builtInColors = loadColors(
            builtInThemePath(m_selectedThemeId));
        Q_ASSERT(builtInColors);
        if (builtInColors)
            m_colors = *builtInColors;
    }
    updateOmarchyWatcher();

    emit themeChanged();
}

void ThemeManager::scheduleOmarchyReload()
{
    if (m_selectedThemeId != QString::fromLatin1(omarchyThemeId) || m_reloadScheduled)
        return;
    m_reloadScheduled = true;
    QTimer::singleShot(50, this, [this] {
        m_reloadScheduled = false;
        if (m_selectedThemeId == QString::fromLatin1(omarchyThemeId))
            applySelectedTheme();
    });
}

void ThemeManager::updateOmarchyWatcher()
{
    const QStringList watchedFiles = m_omarchyWatcher.files();
    const QStringList watchedDirectories = m_omarchyWatcher.directories();
    if (!watchedFiles.isEmpty())
        m_omarchyWatcher.removePaths(watchedFiles);
    if (!watchedDirectories.isEmpty())
        m_omarchyWatcher.removePaths(watchedDirectories);

    if (m_selectedThemeId != QString::fromLatin1(omarchyThemeId))
        return;

    QStringList filesToWatch;
    QStringList directoriesToWatch;
    for (const QString &candidate : omarchyColorsCandidates()) {
        if (QFileInfo(candidate).isFile())
            filesToWatch.append(candidate);

        QString directoryPath = QFileInfo(candidate).absolutePath();
        for (int level = 0; level < 4; ++level) {
            if (QFileInfo(directoryPath).isDir()
                && !directoriesToWatch.contains(directoryPath)) {
                directoriesToWatch.append(directoryPath);
            }
            directoryPath = QFileInfo(directoryPath).dir().absolutePath();
        }
    }
    if (!filesToWatch.isEmpty())
        m_omarchyWatcher.addPaths(filesToWatch);
    if (!directoriesToWatch.isEmpty())
        m_omarchyWatcher.addPaths(directoriesToWatch);
}

QString ThemeManager::findOmarchyColorsPath()
{
    for (const QString &candidate : omarchyColorsCandidates()) {
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
    return {};
}

QString ThemeManager::readOmarchyThemeName(const QString &colorsPath)
{
    QDir currentDirectory = QFileInfo(colorsPath).dir();
    currentDirectory.cdUp();
    QFile nameFile(currentDirectory.filePath(QStringLiteral("theme.name")));
    if (!nameFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(nameFile.readAll()).trimmed();
}
