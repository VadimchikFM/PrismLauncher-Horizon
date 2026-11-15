#include "ShaderPackFolderModel.h"
#include "FileSystem.h"
#include "modplatform/ResourceRemoteInfo.h"

ShaderPackFolderModel::ShaderPackFolderModel(const QDir& dir, BaseInstance* instance, bool is_indexed, bool create_dir, QObject* parent)
    : ResourceFolderModel(dir, instance, is_indexed, create_dir, parent)
{
    m_column_names = QStringList({ "Enable", "Image", "Name", "Last Modified", "Provider", "Size" });
    m_column_names_translated = QStringList({ tr("Enable"), tr("Image"), tr("Name"), tr("Last Modified"), tr("Provider"), tr("Size") });
    m_column_sort_keys = { SortType::ENABLED, SortType::NAME, SortType::NAME, SortType::DATE, SortType::PROVIDER, SortType::SIZE };
    m_column_resize_modes = { QHeaderView::Interactive, QHeaderView::Interactive, QHeaderView::Stretch,
                              QHeaderView::Interactive, QHeaderView::Interactive, QHeaderView::Interactive };
    m_columnsHideable = { false, true, false, true, true, true };

    // Shader packs mostly say nothing of themselves: what the platform they came from says of them fills in
    new ResourceRemoteInfo(
        this,
        [](Resource& resource, const ResourceRemoteInfo::Info& info) {
            auto& pack = static_cast<ShaderPack&>(resource);
            auto details = pack.details();
            if (details.description.isEmpty()) {
                details.description = info.summary;
            }
            if (details.authors.isEmpty()) {
                details.authors = info.authors;
            }
            if (details.license.isEmpty()) {
                details.license = info.license;
            }
            pack.setDetails(details);
        },
        [](Resource& resource, const QImage& image, const QString& path) {
            auto& pack = static_cast<ShaderPack&>(resource);
            // The pack's own icon comes first
            if (!pack.hasImage() || (!pack.imageSource().isEmpty() && pack.imageSource() != path)) {
                pack.setImage(image, path);
            }
        });
}

QVariant ShaderPackFolderModel::data(const QModelIndex& index, int role) const
{
    if (!validateIndex(index)) {
        return {};
    }

    const int row = index.row();
    const int column = index.column();
    switch (role) {
        case Qt::DecorationRole:
            if (column == ImageColumn) {
                return at(row).image({ 32, 32 }, Qt::AspectRatioMode::KeepAspectRatioByExpanding);
            }
            break;
        case Qt::SizeHintRole:
            if (column == ImageColumn) {
                return QSize(32, 32);
            }
            break;
        default:
            break;
    }

    // the other columns are the base model's
    QModelIndex mapped;
    switch (column) {
        case ActiveColumn:
            mapped = index.siblingAtColumn(ResourceFolderModel::ActiveColumn);
            break;
        case NameColumn:
            mapped = index.siblingAtColumn(ResourceFolderModel::NameColumn);
            break;
        case DateColumn:
            mapped = index.siblingAtColumn(ResourceFolderModel::DateColumn);
            break;
        case ProviderColumn:
            mapped = index.siblingAtColumn(ResourceFolderModel::ProviderColumn);
            break;
        case SizeColumn:
            mapped = index.siblingAtColumn(ResourceFolderModel::SizeColumn);
            break;
        default:
            break;
    }
    return mapped.isValid() ? ResourceFolderModel::data(mapped, role) : QVariant();
}

QVariant ShaderPackFolderModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    switch (role) {
        case Qt::DisplayRole:
            return section >= 0 && section < NUM_COLUMNS ? QVariant(columnNames().at(section)) : QVariant();
        case Qt::ToolTipRole:
            switch (section) {
                case ActiveColumn:
                    return tr("Is the shader pack enabled?");
                case NameColumn:
                    return tr("The name of the shader pack.");
                case DateColumn:
                    return tr("The date and time this shader pack was last changed (or added).");
                case ProviderColumn:
                    return tr("The source provider of the shader pack.");
                case SizeColumn:
                    return tr("The size of the shader pack.");
                default:
                    return {};
            }
        case Qt::SizeHintRole:
            if (section == ImageColumn) {
                return QSize(64, 0);
            }
            return {};
        default:
            return ResourceFolderModel::headerData(section, orientation, role);
    }
}

int ShaderPackFolderModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : NUM_COLUMNS;
}

namespace {
class ShaderPackIndexMigrateTask : public Task {
    Q_OBJECT
   public:
    ShaderPackIndexMigrateTask(QDir resourceDir, QDir indexDir) : m_resourceDir(std::move(resourceDir)), m_indexDir(std::move(indexDir)) {}

    void executeTask() override
    {
        if (!m_indexDir.exists()) {
            qDebug() << m_indexDir.absolutePath() << "does not exist; nothing to migrate";
            emitSucceeded();
            return;
        }

        QStringList pwFiles = m_indexDir.entryList({ "*.pw.toml" }, QDir::Files);
        bool movedAll = true;

        for (const auto& file : pwFiles) {
            QString src = m_indexDir.filePath(file);
            QString dest = m_resourceDir.filePath(file);

            if (FS::move(src, dest)) {
                qDebug() << "Moved" << src << "to" << dest;
            } else {
                movedAll = false;
            }
        }

        if (!movedAll) {
            // FIXME: not shown in the UI
            emitFailed(tr("Failed to migrate shaderpack metadata from .index"));
            return;
        }

        if (!FS::deletePath(m_indexDir.absolutePath())) {
            emitFailed(tr("Failed to remove old .index dir"));
            return;
        }

        emitSucceeded();
    }

   private:
    QDir m_resourceDir, m_indexDir;
};
}  // namespace

Task* ShaderPackFolderModel::createPreUpdateTask()
{
    return new ShaderPackIndexMigrateTask(m_dir, ResourceFolderModel::indexDir());
}

#include "ShaderPackFolderModel.moc"
