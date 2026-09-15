/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <officelabs/CefDebugPort.hxx>

#include <o3tl/string_view.hxx>

#include <string_view>

namespace officelabs {

int parseRemoteDebuggingPort(const char* pValue)
{
    if (!pValue)
        return 0;

    const std::string_view aValue = o3tl::trim(std::string_view(pValue));
    if (aValue.empty() || aValue.size() > 5)
        return 0;

    int nPort = 0;
    for (char c : aValue)
    {
        if (c < '0' || c > '9')
            return 0;
        nPort = nPort * 10 + (c - '0');
    }

    return (nPort >= 1024 && nPort <= 65535) ? nPort : 0;
}

} // namespace officelabs

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
