#pragma once

#include "ResourceFolderModel.h"
#include "minecraft/mod/ShaderPack.h"
#include "minecraft/mod/tasks/LocalShaderPackParseTask.h"

class ShaderPackFolderModel : public ResourceFolderModel {
    Q_OBJECT

   public:
    enum Columns { ActiveColumn = 0, ImageColumn, NameColumn, DateColumn, ProviderColumn, SizeColumn, NUM_COLUMNS };

    explicit ShaderPackFolderModel(const QDir& dir, BaseInstance* instance, bool is_indexed, bool create_dir, QObject* parent = nullptr);

    virtual QString id() const override { return "shaderpacks"; }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    int columnCount(const QModelIndex& parent) const override;

    [[nodiscard]] Resource* createResource(const QFileInfo& info) override { return new ShaderPack(info); }

    [[nodiscard]] Task* createParseTask(Resource& resource) override
    {
        return new LocalShaderPackParseTask(m_next_resolution_ticket, static_cast<ShaderPack&>(resource));
    }

    QDir indexDir() const override { return m_dir; }

    Task* createPreUpdateTask() override;

    // avoid watching twice
    virtual bool startWatching() override { return ResourceFolderModel::startWatching({ m_dir.absolutePath() }); }
    virtual bool stopWatching() override { return ResourceFolderModel::stopWatching({ m_dir.absolutePath() }); }

    RESOURCE_HELPERS(ShaderPack);

   private:
    QMutex m_migrateLock;
};
