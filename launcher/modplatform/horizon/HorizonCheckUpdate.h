// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "modplatform/CheckUpdateTask.h"

/// Looks for newer versions of resources installed from Horizon's registry, in its index.
class HorizonCheckUpdate : public CheckUpdateTask {
    Q_OBJECT

   public:
    HorizonCheckUpdate(QList<Resource*>& resources,
                       std::vector<Version>& mcVersions,
                       QList<ModPlatform::ModLoaderType> loadersList,
                       ResourceFolderModel* resourceModel)
        : CheckUpdateTask(resources, mcVersions, std::move(loadersList), resourceModel)
    {}

   public slots:
    bool abort() override;

   protected slots:
    void executeTask() override;

   private:
    Task::Ptr m_job;
};
