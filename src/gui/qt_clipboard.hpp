// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The system clipboard behind the core's Clipboard interface.
//
// This adapter is the whole reason the interface exists: the core cannot see
// Qt, and this file is in src/gui where Qt is allowed. Plain text on the
// system clipboard is deliberate -- the payload is a DXF document, so a copy
// crosses into another NotoCAD window through the same door it would cross
// into a text editor, and DXF text arriving from anywhere else pastes as
// geometry.
#pragma once

#include "ncad/clipboard.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QString>

namespace ncad {

class QtClipboard final : public Clipboard {
public:
    bool set_text(const std::string& text) override {
        QClipboard* cb = QGuiApplication::clipboard();
        if (!cb) return false;
        cb->setText(QString::fromStdString(text));
        return true;
    }

    bool get_text(std::string& out) const override {
        const QClipboard* cb = QGuiApplication::clipboard();
        if (!cb) return false;
        const QString text = cb->text();
        if (text.isEmpty()) return false;
        out = text.toStdString();
        return true;
    }
};

}  // namespace ncad
