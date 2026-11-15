
// SPDX-FileCopyrightText: 2022 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Rachel Powers <508861+Ryex@users.noreply.github.com>
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

#include "ShaderPack.h"

void ShaderPack::setPackFormat(ShaderPackFormat new_format)
{
    QMutexLocker locker(&m_data_lock);

    m_pack_format = new_format;
}

bool ShaderPack::valid() const
{
    return m_pack_format != ShaderPackFormat::INVALID;
}

QString ShaderPack::name() const
{
    if (metadata()) {
        return Resource::name();
    }
    QMutexLocker locker(&m_data_lock);
    return m_details.name.isEmpty() ? Resource::name() : m_details.name;
}

QString ShaderPack::description() const
{
    QMutexLocker locker(&m_data_lock);
    return m_details.description;
}

QStringList ShaderPack::authors() const
{
    QMutexLocker locker(&m_data_lock);
    return m_details.authors;
}

QString ShaderPack::license() const
{
    QMutexLocker locker(&m_data_lock);
    return m_details.license;
}

ShaderPack::Details ShaderPack::details() const
{
    QMutexLocker locker(&m_data_lock);
    return m_details;
}

void ShaderPack::setDetails(const Details& details)
{
    QMutexLocker locker(&m_data_lock);
    m_details = details;
}

void ShaderPack::setImage(const QImage& image, const QString& source)
{
    QMutexLocker locker(&m_data_lock);
    // kept small: a list shows it at 32 pixels and the details at 64
    m_image = image.isNull() ? QImage() : image.scaled({ 64, 64 }, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    m_image_source = source;
    m_pixmap = QPixmap();
}

QString ShaderPack::imageSource() const
{
    QMutexLocker locker(&m_data_lock);
    return m_image_source;
}

bool ShaderPack::hasImage() const
{
    QMutexLocker locker(&m_data_lock);
    return !m_image.isNull();
}

QPixmap ShaderPack::image(QSize size, Qt::AspectRatioMode mode) const
{
    QMutexLocker locker(&m_data_lock);
    if (m_image.isNull()) {
        return {};
    }
    // QPixmap is the GUI thread's: it is made from the image there, once
    if (m_pixmap.isNull()) {
        m_pixmap = QPixmap::fromImage(m_image);
    }
    return size.isNull() ? m_pixmap : m_pixmap.scaled(size, mode, Qt::SmoothTransformation);
}
