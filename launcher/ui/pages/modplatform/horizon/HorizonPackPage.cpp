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

#include "HorizonPackPage.h"

#include <QFileInfo>
#include <QVBoxLayout>

#include "minecraft/HorizonPackCreationTask.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/widgets/HorizonRegistryBrowser.h"

HorizonPackPage::HorizonPackPage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), m_dialog(dialog)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    // No game folder: nothing is "installed" before the instance exists
    m_browser = new HorizonRegistryBrowser(this);
    layout->addWidget(m_browser);

    connect(m_browser, &HorizonRegistryBrowser::selectionChanged, this, &HorizonPackPage::suggestCurrent);
}

void HorizonPackPage::openedImpl()
{
    suggestCurrent();
}

void HorizonPackPage::suggestCurrent()
{
    if (!isOpened) {
        return;
    }
    const auto* pack = m_browser->selected();
    if (pack == nullptr) {
        m_dialog->setSuggestedPack();
        return;
    }
    m_dialog->setSuggestedPack(pack->name, pack->version, new HorizonPackCreationTask(pack->id, pack->game));
    // The pack's icon, once the browser has fetched it
    if (const auto icon = m_browser->iconPath(*pack); QFileInfo(icon).isFile()) {
        m_dialog->setSuggestedIconFromFile(icon, "horizon_" + pack->id);
    } else {
        m_dialog->setSuggestedIcon("default");
    }
}
