#pragma once

#include <QAbstractListModel>
#include <vector>

#include "qt/WatchRow.h"

class WatchedValuesModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        LogicalNameRole = Qt::UserRole + 1,
        NodeIdRole,
        ValueRole,
        GoodRole,
        HasModbusMappingRole,
        ModbusPreviewRole,
    };

    explicit WatchedValuesModel(const std::vector<WatchRow>& rows, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    template <typename Func>
    void insertRows(int first, int last, Func&& mutate) {
        beginInsertRows(QModelIndex(), first, last);
        mutate();
        endInsertRows();
    }

    template <typename Func>
    void removeRow(int row, Func&& mutate) {
        beginRemoveRows(QModelIndex(), row, row);
        mutate();
        endRemoveRows();
    }

    template <typename Func>
    void reset(Func&& mutate) {
        beginResetModel();
        mutate();
        endResetModel();
    }

    void notifyRowChanged(int row);

private:
    const std::vector<WatchRow>& m_rows;
};
