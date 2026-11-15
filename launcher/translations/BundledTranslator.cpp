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

#include "BundledTranslator.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// The catalogs are in a static library's resources: they are there only once registered. Outside any namespace, as
// Q_INIT_RESOURCE has to be.
static void registerCatalogs()
{
    static bool s_registered = false;
    if (!s_registered) {
        s_registered = true;
        Q_INIT_RESOURCE(horizon);
    }
}

BundledTranslator::BundledTranslator(const QString& language, QObject* parent) : QTranslator(parent)
{
    // Languages are picked while the application starts, before main() registers the other resources
    registerCatalogs();

    QStringList candidates{ language };
    if (const auto underscore = language.indexOf('_'); underscore > 0) {
        candidates.append(language.left(underscore));
    }
    for (const auto& candidate : candidates) {
        QFile file(QString(":/horizon/translations/horizon_%1.json").arg(candidate));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonParseError error{};
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError) {
            qWarning() << "The bundled translations" << file.fileName() << "could not be read:" << error.errorString();
            continue;
        }
        m_language = candidate;
        const auto messages = document.object().value("messages").toObject();
        for (auto it = messages.constBegin(); it != messages.constEnd(); ++it) {
            QStringList forms;
            if (it.value().isArray()) {
                for (const auto& form : it.value().toArray()) {
                    forms.append(form.toString());
                }
            } else {
                forms.append(it.value().toString());
            }
            m_messages.insert(it.key().toUtf8(), forms);
        }
        qDebug() << "Loaded" << m_messages.size() << "bundled translations for" << candidate;
        break;
    }
}

bool BundledTranslator::isEmpty() const
{
    return m_messages.isEmpty();
}

int BundledTranslator::pluralForm(int n) const
{
    // https://www.unicode.org/cldr/charts/latest/supplemental/language_plural_rules.html
    if (m_language == "ru" || m_language == "uk" || m_language == "be") {
        const int mod10 = n % 10;
        const int mod100 = n % 100;
        if (mod10 == 1 && mod100 != 11) {
            return 0;
        }
        if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) {
            return 1;
        }
        return 2;
    }
    return n == 1 ? 0 : 1;
}

QString BundledTranslator::translate([[maybe_unused]] const char* context,
                                     const char* sourceText,
                                     [[maybe_unused]] const char* disambiguation,
                                     int n) const
{
    const auto found = m_messages.constFind(QByteArray(sourceText));
    if (found == m_messages.constEnd() || found->isEmpty()) {
        // A null string: the next catalog is asked
        return {};
    }
    if (n < 0 || found->size() == 1) {
        return found->first();
    }
    return found->at(qBound(0, pluralForm(n), static_cast<int>(found->size()) - 1));
}
