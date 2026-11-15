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

#include <QHash>
#include <QStringList>
#include <QTranslator>

/**
 * Translations that come with the launcher, for the strings the launcher's downloaded catalogs don't have yet: the
 * ones it added for Horizon. A catalog is a JSON file in the launcher's resources,
 * `:/horizon/translations/horizon_<language>.json`, keyed by the English text alone (whatever class says it); a list
 * holds a text's plural forms.
 *
 * It is installed before the downloaded catalog, so a translation that catalog has is the one shown.
 */
class BundledTranslator : public QTranslator {
    Q_OBJECT
   public:
    /// The catalog of `language` ("ru", "pt_BR"...), or of its language alone when there is none for the region.
    explicit BundledTranslator(const QString& language, QObject* parent = nullptr);

    bool isEmpty() const override;
    QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override;

   private:
    /// Which of a text's plural forms goes with `n`, in this language.
    int pluralForm(int n) const;

    QString m_language;
    QHash<QByteArray, QStringList> m_messages;
};
