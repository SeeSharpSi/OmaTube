#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <optional>

class ThemeManager final : public QObject
{
    Q_OBJECT

public:
    explicit ThemeManager(QObject *parent = nullptr);

    [[nodiscard]] QString selectedThemeId() const;
    [[nodiscard]] QVariantMap colors() const;
    [[nodiscard]] bool omarchyThemeAvailable() const;
    [[nodiscard]] QString omarchyThemeName() const;

    void setSelectedThemeId(const QString &themeId);

    static std::optional<QVariantMap> loadColors(
        const QString &path,
        QString *error = nullptr);

signals:
    void themeChanged();

private:
    void applySelectedTheme();
    void scheduleOmarchyReload();
    void updateOmarchyWatcher();
    [[nodiscard]] static QString findOmarchyColorsPath();
    [[nodiscard]] static QString readOmarchyThemeName(const QString &colorsPath);

    QFileSystemWatcher m_omarchyWatcher;
    QString m_selectedThemeId;
    QVariantMap m_colors;
    bool m_omarchyThemeAvailable = false;
    bool m_reloadScheduled = false;
    QString m_omarchyThemeName;
};
