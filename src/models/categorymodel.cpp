#include "models/categorymodel.h"

CategoryModel::CategoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CategoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_categories.size();
}

QVariant CategoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_categories.size())
        return {};

    const Category &category = m_categories.at(index.row());
    switch (role) {
    case CategoryIdRole:
        return category.id;
    case NameRole:
        return category.name;
    default:
        return {};
    }
}

QHash<int, QByteArray> CategoryModel::roleNames() const
{
    return {
        {CategoryIdRole, "categoryId"},
        {NameRole, "name"},
    };
}

void CategoryModel::setCategories(QList<Category> categories)
{
    beginResetModel();
    m_categories = std::move(categories);
    endResetModel();
}
