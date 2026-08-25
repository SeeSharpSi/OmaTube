#pragma once

#include "domain.h"

#include <QAbstractListModel>

class CategoryModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        CategoryIdRole = Qt::UserRole + 1,
        NameRole,
    };
    Q_ENUM(Role)

    explicit CategoryModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setCategories(QList<Category> categories);

private:
    QList<Category> m_categories;
};
