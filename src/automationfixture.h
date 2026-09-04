#pragma once

#include <QString>

class AutomationFixture
{
public:
    static bool seed(const QString &databasePath, QString *error = nullptr);
};
