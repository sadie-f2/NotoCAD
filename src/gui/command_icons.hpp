// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Toolbar icons, drawn rather than shipped.
//
// Same reasoning as the Hershey font, and it lands in the same place: an icon
// for a drawing command IS a little line drawing, so describing it as strokes
// costs less than a PNG and answers every question a bitmap raises. There is no
// asset directory to install beside the binary, no second copy at 2x for a
// retina panel, no licence to carry for somebody's icon set, and the ink colour
// follows the palette instead of being baked in at design time.
//
// The vocabulary is deliberately R12-flat: strokes of one weight, no fills
// except where a filled shape IS the meaning (SOLID), no colour, no perspective
// and no gloss. It matches what the viewport draws, which is the same argument
// the font makes -- screen text and DXF text from one source.
#pragma once

#include <QIcon>
#include <QString>

class QColor;

namespace ncad {

// The icon for a command name -- "LINE", "OFFSET" -- inked in `ink`. A name
// with no drawing of its own falls back to its first two letters set in the
// same stroke weight, so a toolbar entry can be added before its icon is and
// still reads as itself rather than as a blank.
QIcon command_icon(const QString& command, const QColor& ink);

// Whether `command` has a drawing rather than the lettered fallback. The
// toolbars do not consult it -- the fallback is what makes that unnecessary --
// so this exists for whoever wants to ask, and to make the fallback's
// existence visible rather than a surprise found by adding a button.
bool has_command_icon(const QString& command);

}  // namespace ncad
