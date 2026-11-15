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

#include "BaseVersionList.h"

/// A build of Horizon an instance can play: a release of horizon-rust/horizon on GitHub, or "the newest".
class HorizonVersion : public BaseVersion {
   public:
    /// What an instance set to follow the newest Nightly keeps instead of a tag.
    static const QString Latest;

    HorizonVersion(QString tag, QString name, QString type) : m_tag(std::move(tag)), m_name(std::move(name)), m_type(std::move(type)) {}

    /// The release's tag (`nightly-2026-11-08`, `v0.2.97`), or [Latest].
    QString descriptor() const override { return m_tag; }
    QString name() const override { return m_name; }
    QString typeString() const override { return m_type; }

    /// Whether `tag` may name a release: it becomes part of an address and of a folder's name.
    static bool isValidTag(const QString& tag);
    /// What the window calls the version an instance is set to.
    static QString displayName(const QString& tag);

   private:
    QString m_tag;
    QString m_name;
    QString m_type;
};

/**
 * The builds of Horizon there are to play: every release of the game in horizon-rust/horizon that has an archive
 * for this system, newest first, after "the newest Nightly", which follows updates. (The same repository holds the
 * releases of Horizon's own launcher; those are not the game.)
 *
 * It is what a mod loader's list of versions is to its loader: the new-instance dialog, the Install Loader dialog
 * and the Version page's Change Version pick from it.
 */
class HorizonVersionList : public BaseVersionList {
    Q_OBJECT
   public:
    /// The one list of the program: it is loaded once, then again only when asked to.
    static HorizonVersionList* shared();

    Task::Ptr getLoadTask(bool forceReload = false) override;
    bool isLoaded() override { return m_loaded; }
    const BaseVersion::Ptr at(int i) const override;
    int count() const override;
    void sortVersions() override {}

    QVariant data(const QModelIndex& index, int role) const override;
    RoleList providesRoles() const override;

   public slots:
    void updateListData(QList<BaseVersion::Ptr> versions) override;

   private:
    explicit HorizonVersionList(QObject* parent = nullptr);

    QList<BaseVersion::Ptr> m_versions;
    bool m_loaded = false;
    Task::Ptr m_loadTask;
};
