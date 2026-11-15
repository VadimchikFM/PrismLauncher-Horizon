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

#include "HorizonRegistryBrowser.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMap>
#include <QPointer>
#include <QScrollArea>
#include <QSet>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

#include "Application.h"
#include "FileSystem.h"
#include "modplatform/horizon/HorizonAPI.h"
#include "settings/SettingsObject.h"

namespace {

/// How many of the most used tags get a checkbox.
constexpr int ShownTags = 40;

enum Order { ByRelevance, ByDownloads, ByUpdate, ByName };

QStringList strings(const QJsonValue& array)
{
    QStringList list;
    const auto values = array.toArray();
    for (const auto& value : values) {
        list.append(value.toString());
    }
    return list;
}

/// `game-mechanics` as a label: `Game mechanics`.
QString label(QString slug)
{
    slug.replace('-', ' ');
    if (!slug.isEmpty()) {
        slug[0] = slug[0].toUpper();
    }
    return slug;
}

/// `text`, safe inside the HTML of a project's page.
QString plain(const QString& text)
{
    return text.toHtmlEscaped();
}

}  // namespace

HorizonRegistryBrowser::HorizonRegistryBrowser(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_query = new QLineEdit(this);
    m_query->setPlaceholderText(tr("Search Horizon's registry..."));
    m_query->setClearButtonEnabled(true);
    layout->addWidget(m_query);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(splitter, 1);

    // Left: the filters
    auto* filters = new QWidget(this);
    auto* filterLayout = new QVBoxLayout(filters);
    filterLayout->setContentsMargins(0, 0, 6, 0);

    const auto game = APPLICATION->settings()->get("HorizonMinecraftVersion").toString();
    m_onlyGame = new QCheckBox(tr("Only made for Minecraft %1").arg(game), filters);
    m_onlyGame->setProperty("game", game);
    m_onlyGame->setChecked(true);
    filterLayout->addWidget(m_onlyGame);

    auto* categories = new QGroupBox(tr("Categories"), filters);
    m_categoryLayout = new QVBoxLayout(categories);
    filterLayout->addWidget(categories);
    auto* tags = new QGroupBox(tr("Tags"), filters);
    m_tagLayout = new QVBoxLayout(tags);
    filterLayout->addWidget(tags);
    filterLayout->addStretch(1);

    auto* filterScroll = new QScrollArea(splitter);
    filterScroll->setWidgetResizable(true);
    filterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filterScroll->setWidget(filters);
    splitter->addWidget(filterScroll);

    // Middle: the projects
    m_list = new QListWidget(splitter);
    m_list->setIconSize(QSize(48, 48));
    m_list->setWordWrap(true);
    m_list->setSpacing(1);
    m_list->setAlternatingRowColors(true);
    splitter->addWidget(m_list);

    // Right: the selected project's page
    m_details = new QTextBrowser(splitter);
    m_details->setOpenExternalLinks(true);
    splitter->addWidget(m_details);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 2);
    splitter->setSizes({ 240, 480, 360 });

    auto* bottom = new QHBoxLayout();
    m_sort = new QComboBox(this);
    m_sort->addItem(tr("Sort by relevance"), ByRelevance);
    m_sort->addItem(tr("Sort by downloads"), ByDownloads);
    m_sort->addItem(tr("Sort by date updated"), ByUpdate);
    m_sort->addItem(tr("Sort by name"), ByName);
    bottom->addWidget(m_sort);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    bottom->addWidget(m_status, 1);
    layout->addLayout(bottom);

    connect(m_query, &QLineEdit::textChanged, this, &HorizonRegistryBrowser::applyFilters);
    connect(m_sort, &QComboBox::currentIndexChanged, this, &HorizonRegistryBrowser::applyFilters);
    connect(m_onlyGame, &QCheckBox::toggled, this, &HorizonRegistryBrowser::applyFilters);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &HorizonRegistryBrowser::showSelected);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &HorizonRegistryBrowser::activated);

    // Once the widget shows
    QTimer::singleShot(0, this, &HorizonRegistryBrowser::reload);
}

void HorizonRegistryBrowser::setStatus(const QString& text)
{
    m_status->setText(text);
}

void HorizonRegistryBrowser::reload()
{
    if (m_indexJob && m_indexJob->isRunning()) {
        return;
    }
    setStatus(tr("Reading Horizon's registry..."));
    // The index the download pages read too: it is read once for both
    auto job = HorizonAPI::withIndex([self = QPointer<HorizonRegistryBrowser>(this)](const QJsonObject& index) {
        if (self) {
            self->indexLoaded(index);
        }
    });
    connect(job.get(), &Task::failed, this, [this](const QString& reason) {
        setStatus(tr("<span style=\"color:#e01b24;\">The registry could not be read:</span> %1").arg(reason.toHtmlEscaped()));
    });
    m_indexJob = job;
    job->start();
}

void HorizonRegistryBrowser::indexLoaded(const QJsonObject& index)
{
    // Every version of a project taken down (an entry with no sha256) is not offered
    QSet<QString> revoked;
    const auto revocations = index.value("revoked").toArray();
    for (const auto& value : revocations) {
        const auto entry = value.toObject();
        if (entry.value("sha256").toString().isEmpty()) {
            revoked.insert(entry.value("id").toString());
        }
    }

    m_projects.clear();
    m_names.clear();
    const auto projects = index.value("projects").toArray();
    for (const auto& value : projects) {
        const auto object = value.toObject();
        Project project;
        project.id = object.value("id").toString();
        m_names.insert(project.id, object.value("name").toString());
        project.versions = object.value("versions").toArray();
        if (object.value("kind").toString() != "modpack" || project.id.isEmpty() || project.versions.isEmpty() ||
            revoked.contains(project.id)) {
            continue;
        }
        project.name = object.value("name").toString();
        project.summary = object.value("summary").toString();
        project.description = object.value("description").toString();
        project.license = object.value("license").toString();
        project.source = object.value("source").toString();
        project.icon = object.value("icon").toString();
        project.authors = strings(object.value("authors"));
        project.categories = strings(object.value("categories"));
        project.tags = strings(object.value("tags"));
        project.downloads = object.value("downloads").toInteger();
        project.stars = object.value("stars").toInt();

        const auto newest = project.versions.first().toObject();
        project.version = newest.value("version").toString();
        project.published = newest.value("published").toString();
        project.game = newest.value("game").toString();
        project.mods = static_cast<int>(newest.value("requires").toArray().size());
        m_projects.append(project);
    }

    buildFilters();
    applyFilters();
    fetchIcons();
}

void HorizonRegistryBrowser::buildFilters()
{
    // The categories and tags the modpacks use, the most used first. What was ticked stays ticked.
    const auto rebuild = [this](QVBoxLayout* layout, QList<QCheckBox*>& boxes, bool tags, int limit) {
        QSet<QString> ticked;
        for (auto* box : boxes) {
            if (box->isChecked()) {
                ticked.insert(box->property("slug").toString());
            }
            box->deleteLater();
        }
        boxes.clear();

        QMap<QString, int> uses;
        for (const auto& project : m_projects) {
            for (const auto& slug : tags ? project.tags : project.categories) {
                uses[slug]++;
            }
        }
        auto slugs = uses.keys();
        std::sort(slugs.begin(), slugs.end(), [&uses](const QString& a, const QString& b) {
            return uses[a] != uses[b] ? uses[a] > uses[b] : a < b;
        });
        for (const auto& slug : slugs.mid(0, limit)) {
            auto* box = new QCheckBox(QString("%1 (%2)").arg(tags ? "#" + slug : label(slug)).arg(uses[slug]), this);
            box->setProperty("slug", slug);
            box->setChecked(ticked.contains(slug));
            connect(box, &QCheckBox::toggled, this, &HorizonRegistryBrowser::applyFilters);
            layout->addWidget(box);
            boxes.append(box);
        }
        // When none is tagged, there is no box for tags
        if (auto* box = qobject_cast<QWidget*>(layout->parent())) {
            box->setVisible(!boxes.isEmpty());
        }
    };
    rebuild(m_categoryLayout, m_categoryBoxes, false, 100);
    rebuild(m_tagLayout, m_tagBoxes, true, ShownTags);
}

QString HorizonRegistryBrowser::madeFor(const Project& project) const
{
    return tr("Modpack %1 for Minecraft %2: %n mod(s)", nullptr, project.mods).arg(project.version, project.game);
}

QString HorizonRegistryBrowser::iconPath(const Project& project) const
{
    return FS::PathCombine(APPLICATION->metacache()->getBasePath("general"), "horizon-icons", project.id + ".png");
}

void HorizonRegistryBrowser::applyFilters()
{
    const auto ticked = [](const QList<QCheckBox*>& boxes) {
        QStringList slugs;
        for (const auto* box : boxes) {
            if (box->isChecked()) {
                slugs.append(box->property("slug").toString());
            }
        }
        return slugs;
    };
    const auto categories = ticked(m_categoryBoxes);
    const auto tags = ticked(m_tagBoxes);
    const auto words = m_query->text().toLower().split(' ', Qt::SkipEmptyParts);
    const auto game = m_onlyGame->isChecked() ? m_onlyGame->property("game").toString() : QString();

    // How well a project answers the search: 0 when a word is nowhere in it
    const auto relevance = [&words](const Project& project) {
        int score = 1;
        for (const auto& word : words) {
            if (project.name.toLower().startsWith(word)) {
                score += 8;
            } else if (project.name.toLower().contains(word) || project.id.contains(word)) {
                score += 4;
            } else if (project.tags.contains(word) || project.categories.contains(word)) {
                score += 2;
            } else if (project.summary.toLower().contains(word) || project.authors.join(' ').toLower().contains(word)) {
                score += 1;
            } else {
                return 0;
            }
        }
        return score;
    };

    QList<QPair<int, int>> shown;  // (relevance, index)
    for (int i = 0; i < m_projects.size(); i++) {
        const auto& project = m_projects.at(i);
        if (!game.isEmpty() && project.game != game) {
            continue;
        }
        const auto has = [](const QStringList& of, const QStringList& all) {
            return std::all_of(all.begin(), all.end(), [&of](const QString& one) { return of.contains(one); });
        };
        if (!has(project.categories, categories) || !has(project.tags, tags)) {
            continue;
        }
        if (const auto score = relevance(project); score > 0) {
            shown.append({ score, i });
        }
    }

    const auto order = m_sort->currentData().toInt();
    std::stable_sort(shown.begin(), shown.end(), [this, order](const QPair<int, int>& a, const QPair<int, int>& b) {
        const auto& x = m_projects.at(a.second);
        const auto& y = m_projects.at(b.second);
        switch (order) {
            case ByUpdate:
                return x.published > y.published;
            case ByName:
                return x.name.localeAwareCompare(y.name) < 0;
            case ByRelevance:
                if (a.first != b.first) {
                    return a.first > b.first;
                }
                [[fallthrough]];
            default:
                return x.downloads > y.downloads;
        }
    });

    const auto* current = selected();
    const auto keep = current != nullptr ? current->id : QString();
    m_list->clear();
    for (const auto& [score, index] : shown) {
        const auto& project = m_projects.at(index);
        auto* item = new QListWidgetItem(m_list);
        item->setText(QString("%1  -  %2\n%3\n%4")
                          .arg(project.name, project.authors.join(", "), project.summary,
                               tr("%1  -  %2 downloads").arg(madeFor(project), QLocale().toString(project.downloads))));
        item->setData(Qt::UserRole, index);
        if (QFileInfo(iconPath(project)).isFile()) {
            item->setIcon(QIcon(iconPath(project)));
        }
        if (project.id == keep) {
            m_list->setCurrentItem(item);
        }
    }
    setStatus(tr("%n project(s)", nullptr, static_cast<int>(shown.size())));
    showSelected();
}

void HorizonRegistryBrowser::fetchIcons()
{
    // Each icon once: it is kept beside the launcher's other caches
    auto job = makeShared<NetJob>(QString("Horizon::RegistryIcons"), APPLICATION->network());
    job->setAskRetry(false);
    for (const auto& project : m_projects) {
        const auto path = iconPath(project);
        if (!project.icon.startsWith("https://") || QFileInfo(path).isFile()) {
            continue;
        }
        auto download = Net::Download::makeFile(QUrl(project.icon), path);
        const auto id = project.id;
        connect(download.get(), &Task::succeeded, this, [this, id, path] {
            for (int row = 0; row < m_list->count(); row++) {
                auto* item = m_list->item(row);
                if (m_projects.at(item->data(Qt::UserRole).toInt()).id == id) {
                    item->setIcon(QIcon(path));
                }
            }
            if (const auto* project = selected(); project != nullptr && project->id == id) {
                emit selectionChanged();
            }
        });
        job->addNetAction(download);
    }
    if (job->size() > 0) {
        m_iconJob = job;
        job->start();
    }
}

const HorizonRegistryBrowser::Project* HorizonRegistryBrowser::selected() const
{
    const auto* item = m_list->currentItem();
    if (item == nullptr) {
        return nullptr;
    }
    const auto index = item->data(Qt::UserRole).toInt();
    return index >= 0 && index < m_projects.size() ? &m_projects.at(index) : nullptr;
}

void HorizonRegistryBrowser::showSelected()
{
    const auto* project = selected();
    emit selectionChanged();
    if (project == nullptr) {
        m_details->clear();
        return;
    }

    QString page;
    page += QString("<h2>%1</h2>").arg(plain(project->name));
    QStringList facts{ tr("by %1").arg(plain(project->authors.join(", "))), plain(project->license),
                       tr("%1 downloads").arg(QLocale().toString(project->downloads)) };
    if (project->stars > 0) {
        facts.append(tr("%1 stars").arg(project->stars));
    }
    page += QString("<p>%1</p>").arg(facts.join(" &middot; "));
    page += QString("<p>%1</p>").arg(plain(project->summary));
    page += QString("<p><b>%1</b></p>").arg(plain(madeFor(*project)));

    QStringList links;
    links.append(QString("<a href=\"https://playhorizon.dev/mods/%1\">%2</a>").arg(project->id, tr("Its page")));
    if (project->source.startsWith("https://")) {
        links.append(QString("<a href=\"%1\">%2</a>").arg(plain(project->source), tr("Source code")));
    }
    page += QString("<p>%1</p>").arg(links.join(" &middot; "));

    if (!project->categories.isEmpty() || !project->tags.isEmpty()) {
        QStringList words;
        for (const auto& category : project->categories) {
            words.append(plain(label(category)));
        }
        for (const auto& tag : project->tags) {
            words.append("#" + plain(tag));
        }
        page += QString("<p>%1</p>").arg(words.join(", "));
    }

    // What it installs: the mods of its newest version, by the names the registry knows them under
    QStringList mods;
    const auto requires_ = project->versions.first().toObject().value("requires").toArray();
    for (const auto& value : requires_) {
        const auto mod = value.toObject();
        const auto id = mod.value("id").toString();
        mods.append(QString("%1 %2").arg(plain(m_names.value(id, id)), plain(mod.value("version").toString())));
    }
    page += QString("<h3>%1</h3><p>%2</p>").arg(tr("Mods in the pack"), mods.join(", "));

    // Every version, with the Minecraft it is for
    page += QString("<h3>%1</h3><ul>").arg(tr("Versions"));
    for (const auto& value : project->versions) {
        const auto version = value.toObject();
        auto line = QString("<b>%1</b> (%2), Minecraft %3")
                        .arg(plain(version.value("version").toString()), plain(version.value("published").toString()),
                             plain(version.value("game").toString()));
        if (const auto changelog = version.value("changelog").toString(); !changelog.isEmpty()) {
            line += "<br>" + plain(changelog);
        }
        page += QString("<li>%1</li>").arg(line);
    }
    page += "</ul>";

    m_details->setHtml(page);
    // Its own words, which are Markdown, after what the registry says about it
    if (!project->description.isEmpty()) {
        QTextDocument description;
        description.setMarkdown(project->description);
        m_details->append(QString("<h3>%1</h3>").arg(tr("About")));
        m_details->append(description.toHtml());
    }
    m_details->moveCursor(QTextCursor::Start);
}
