#include "qt/WatchedValuesModel.h"

WatchedValuesModel::WatchedValuesModel(const std::vector<WatchRow>& rows, QObject* parent)
    : QAbstractListModel(parent), m_rows(rows) {}

int WatchedValuesModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_rows.size());
}

QVariant WatchedValuesModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= static_cast<int>(m_rows.size()))
        return {};
    const WatchRow& w = m_rows[static_cast<std::size_t>(idx.row())];
    switch (role) {
        case LogicalNameRole: return QString::fromStdString(w.logicalName);
        case NodeIdRole: return QString::fromStdString(w.nodeId);
        case ValueRole: return w.hasValue ? QString::fromStdString(w.value)
                                           : QStringLiteral("(bekleniyor)");
        case GoodRole: return w.good;
        case HasModbusMappingRole: return w.hasModbusMapping;
        case ModbusPreviewRole: return QString::fromStdString(w.modbusPreview);
        default: return {};
    }
}

QHash<int, QByteArray> WatchedValuesModel::roleNames() const {
    return {
        {LogicalNameRole, "logicalName"},
        {NodeIdRole, "nodeId"},
        {ValueRole, "value"},
        {GoodRole, "good"},
        {HasModbusMappingRole, "hasModbusMapping"},
        {ModbusPreviewRole, "modbusPreview"},
    };
}

void WatchedValuesModel::notifyRowChanged(int row) {
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx);
}
