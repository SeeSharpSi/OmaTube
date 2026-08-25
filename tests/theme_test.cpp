#include "thememanager.h"

#include <QColor>
#include <QSettings>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

class ThemeTest final : public QObject
{
    Q_OBJECT

private slots:
    void loadsSemanticPalette();
    void normalizesLegacyPalette();
    void loadsAnsiOnlyPalette();
    void rejectsPaletteWithoutRequiredColors();
    void switchesBuiltInThemes();
};

void ThemeTest::loadsSemanticPalette()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(R"(
mode = "dark"
accent = "#7aa2f7"
selection = "#292e42"
muted = "#414868"
background = "#1a1b26"
lighter_background = "#24283b"
foreground = "#a9b1d6"
dark_foreground = "#565f89"
red = "#f7768e"
bright_red = "#ff7a93"
)");
    QVERIFY(file.flush());

    QString error;
    const std::optional<QVariantMap> colors = ThemeManager::loadColors(file.fileName(), &error);

    QVERIFY2(colors.has_value(), qPrintable(error));
    QCOMPARE(colors->value(QStringLiteral("mode")).toString(), QStringLiteral("dark"));
    QCOMPARE(
        colors->value(QStringLiteral("accent")).value<QColor>(),
        QColor(QStringLiteral("#7aa2f7")));
    QCOMPARE(
        colors->value(QStringLiteral("lighter_background")).value<QColor>(),
        QColor(QStringLiteral("#24283b")));
    QCOMPARE(
        colors->value(QStringLiteral("bright_red")).value<QColor>(),
        QColor(QStringLiteral("#ff7a93")));
}

void ThemeTest::normalizesLegacyPalette()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(R"(
accent = "#81a1c1"
foreground = "#d8dee9"
background = "#2e3440"
selection_background = "#4c566a"
color0 = "#3b4252"
color1 = "#bf616a"
color4 = "#81a1c1"
color8 = "#4c566a"
color9 = "#d08770"
color15 = "#eceff4"
)");
    QVERIFY(file.flush());

    QString error;
    const std::optional<QVariantMap> colors = ThemeManager::loadColors(file.fileName(), &error);

    QVERIFY2(colors.has_value(), qPrintable(error));
    QCOMPARE(colors->value(QStringLiteral("mode")).toString(), QStringLiteral("dark"));
    QCOMPARE(
        colors->value(QStringLiteral("selection")).value<QColor>(),
        QColor(QStringLiteral("#4c566a")));
    QCOMPARE(
        colors->value(QStringLiteral("lighter_background")).value<QColor>(),
        QColor(QStringLiteral("#3b4252")));
    QCOMPARE(
        colors->value(QStringLiteral("red")).value<QColor>(),
        QColor(QStringLiteral("#bf616a")));
    QCOMPARE(
        colors->value(QStringLiteral("bright_red")).value<QColor>(),
        QColor(QStringLiteral("#d08770")));
}

void ThemeTest::loadsAnsiOnlyPalette()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(R"(
color0 = "#2e3440"
color1 = "#bf616a"
color4 = "#81a1c1"
color7 = "#d8dee9"
color8 = "#4c566a"
color15 = "#eceff4"
)");
    QVERIFY(file.flush());

    QString error;
    const std::optional<QVariantMap> colors = ThemeManager::loadColors(file.fileName(), &error);

    QVERIFY2(colors.has_value(), qPrintable(error));
    QCOMPARE(
        colors->value(QStringLiteral("background")).value<QColor>(),
        QColor(QStringLiteral("#2e3440")));
    QCOMPARE(
        colors->value(QStringLiteral("foreground")).value<QColor>(),
        QColor(QStringLiteral("#d8dee9")));
    QCOMPARE(
        colors->value(QStringLiteral("accent")).value<QColor>(),
        QColor(QStringLiteral("#81a1c1")));
}

void ThemeTest::rejectsPaletteWithoutRequiredColors()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write("accent = \"#81a1c1\"\n");
    QVERIFY(file.flush());

    QString error;
    const std::optional<QVariantMap> colors = ThemeManager::loadColors(file.fileName(), &error);

    QVERIFY(!colors.has_value());
    QVERIFY(error.contains(QStringLiteral("background and foreground")));
}

void ThemeTest::switchesBuiltInThemes()
{
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("OmaTube Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("Theme Test"));

    ThemeManager manager;
    QCOMPARE(manager.selectedThemeId(), QStringLiteral("default"));
    QCOMPARE(
        manager.colors().value(QStringLiteral("background")).value<QColor>(),
        QColor(QStringLiteral("#f7f4ed")));

    manager.setSelectedThemeId(QStringLiteral("rose-pine"));
    QCOMPARE(manager.selectedThemeId(), QStringLiteral("rose-pine"));
    QCOMPARE(
        manager.colors().value(QStringLiteral("accent")).value<QColor>(),
        QColor(QStringLiteral("#56949f")));

    manager.setSelectedThemeId(QStringLiteral("nord"));
    QCOMPARE(manager.selectedThemeId(), QStringLiteral("nord"));
    QCOMPARE(
        manager.colors().value(QStringLiteral("background")).value<QColor>(),
        QColor(QStringLiteral("#2e3440")));

    QSettings settings;
    QCOMPARE(settings.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("nord"));
}

QTEST_GUILESS_MAIN(ThemeTest)

#include "theme_test.moc"
