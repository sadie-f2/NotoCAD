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

namespace noto {

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

}  // namespace noto
