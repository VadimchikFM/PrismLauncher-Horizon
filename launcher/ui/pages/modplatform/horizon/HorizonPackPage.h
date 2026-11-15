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

#include <QIcon>
#include <QWidget>

#include "ui/pages/BasePage.h"

class HorizonRegistryBrowser;
class NewInstanceDialog;

/**
 * The modpacks of Horizon's registry, in the New Instance dialog beside the other platforms' packs: the selected one
 * becomes an instance launched with Horizon, with the pack's mods (HorizonPackCreationTask).
 */
class HorizonPackPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit HorizonPackPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);
    ~HorizonPackPage() override = default;

    QString displayName() const override { return "Horizon"; }
    QIcon icon() const override { return QIcon::fromTheme("horizon"); }
    QString id() const override { return "horizon"; }
    QString helpPage() const override { return {}; }

    void openedImpl() override;

   private:
    /// Tells the dialog what OK would make: the selected pack's instance, or nothing.
    void suggestCurrent();

   private:
    NewInstanceDialog* m_dialog;
    HorizonRegistryBrowser* m_browser;
};
