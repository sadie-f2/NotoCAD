// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Filename handling for the file commands.
//
// In the core rather than in `ncad`, because the commands that need it are here
// and both front ends must agree about what a name means. A path typed into the
// Qt command line and the same path typed at the terminal have to open the same
// file.
#pragma once

#include <string>

namespace ncad {

// Expands a leading `~`, using $HOME.
//
// Only the bare `~` and `~/...` forms. `~user/...` needs getpwnam to resolve
// another account's home directory, and half-supporting it -- silently treating
// `~bob/x` as a relative path called "~bob/x" -- is worse than not supporting
// it, so that form is left ALONE and reaches the filesystem verbatim, where it
// will fail with a name the user can recognise as the one they typed.
//
// If $HOME is unset or empty the string is returned unchanged: there is nothing
// to expand to, and inventing a home directory would be a guess.
std::string expand_user_path(const std::string& path);

// Appends `ext` (with its dot, e.g. ".dxf") when the name has no extension of
// its own. A name that already ends in `ext`, in any case, is left alone.
std::string ensure_extension(const std::string& path, const std::string& ext);

// True when something already exists at this path. Used to warn before an
// overwrite; a directory counts, since writing to one fails either way.
bool path_exists(const std::string& path);

// True when two names denote the same file, whether or not they are spelled the
// same. "~/x", "./x" and "/home/me/../me/x" are one file, and comparing the
// strings says otherwise -- which would make QSAVE warn about overwriting the
// drawing it just wrote. Neither path need exist; the comparison is on the
// normalised form, so a save to a new name still answers correctly.
bool same_file(const std::string& a, const std::string& b);

// The same name with `.`, `..` and symbolic links resolved as far as they can
// be. Used for what is RECORDED, so DWGPREFIX names a directory that can be
// used rather than whatever mixture of dots and tildes was typed.
std::string normalised_path(const std::string& path);

// The parts a drawing name is reported in: DWGPREFIX is the directory with its
// trailing separator, DWGNAME the file within it. R12 splits them the same way,
// and keeping the split means neither has to be re-derived by whoever asks.
std::string path_directory(const std::string& path);
std::string path_filename(const std::string& path);

// --- drawing locks ------------------------------------------------------------
//
// AutoCAD writes `plan.dwl` and `plan.dwl2` BESIDE `plan.dwg` while it has the
// drawing open. Beside it rather than in a system directory is what makes them
// work over a shared network folder -- and also what makes them stale when a
// process dies, since nothing cleans up after a crash.
//
// They are ADVISORY. Nothing in the OS enforces them and AutoCAD itself will let
// you past with a warning, so this reads them and says what it found; it does
// not make anything impossible. Sadie's framing: a warning before clobbering a
// working session is the point, not a mechanism nobody can override.
//
// **We do not write our own yet.** That needs deciding what a stale lock is and
// who may clear one, and this program has segfaulted twice this month -- a lock
// left behind by a dead process is exactly the case that has to be answered
// before writing one is an improvement. See SF_todo.

// The two files AutoCAD writes, MEASURED from a real pair rather than assumed.
// Specimens and the full notes are in `examples/acad-locks/`. Three details
// here are not guessable and one of them is not even valid:
//
//   - `.dwl` is three lines -- user, machine, datetime -- in **CP1252**. The
//     apostrophe in "Sadie's MacBook Pro" is the single byte 0x92.
//   - `.dwl2` carries the same fields as tags, in **UTF-8**, where that same
//     character is three bytes. Two files, two encodings.
//   - `.dwl2` is NOT well-formed XML: its declaration is
//     `<?xml version="1.0" encoding="UTF-8">` with no closing `?`, so a parser
//     rejects the file outright. Scan for tags instead.
//
// Neither file ends in a newline, and `.dwl`'s machine line has a trailing
// space. Everything is read defensively anyway: a lock we cannot make sense of
// is still a lock, and its EXISTENCE is the fact that matters.
struct DrawingLock {
    bool present{false};

    // Who holds it. Empty when nothing legible could be read -- the lock is
    // still reported in that case.
    std::string owner;

    // Which machine holds it, which is the field that matters over a shared
    // folder: "sadie" on your own box and "sadie" on somebody else's are very
    // different situations, and the user name alone cannot tell them apart.
    std::string machine;

    // Which file was found, so the message can name something the user can go
    // and look at (or delete, when they know the session is gone).
    std::string lock_path;

    // When the lock was made, from the filesystem rather than parsed out of the
    // file. `.dwl2` is said to carry a timestamp, but its layout is not
    // documented anywhere we can rely on, and a modification time is both
    // certain and the same answer. Empty if it could not be determined.
    std::string since;
};

// The lock file AutoCAD would write for this drawing: the same name with the
// extension replaced. `plan.dxf` locks as `plan.dwl`, exactly as `plan.dwg`
// does -- the lock is named for the DRAWING, not for the format it is in.
std::string lock_path_for(const std::string& drawing, const std::string& ext);

// Looks for `.dwl`, then `.dwl2`. Both are reported the same way, since either
// one present means somebody has the drawing open.
DrawingLock read_drawing_lock(const std::string& drawing);

// One line naming who holds it and since when, for a prompt or a message.
// Empty when the lock is not present, so a caller can use it as the test.
std::string describe_lock(const DrawingLock& lock);

}  // namespace ncad
