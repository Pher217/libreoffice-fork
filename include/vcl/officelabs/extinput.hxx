/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_VCL_OFFICELABS_EXTINPUT_HXX
#define INCLUDED_VCL_OFFICELABS_EXTINPUT_HXX

#include <vcl/dllapi.h>

namespace vcl {
class Window;
}

/// True if the window is currently in an extended text input (IME) session.
/// Provided for the OfficeLabs inline-completion feature so it can test
/// composition state without exposing WindowImpl internals.
VCL_DLLPUBLIC bool OfficeLabsIsExtTextInputActive(const vcl::Window* pWindow);

#endif // INCLUDED_VCL_OFFICELABS_EXTINPUT_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
