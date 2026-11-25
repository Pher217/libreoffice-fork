/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * The Start Center sign-in entry (project#418), as data.
 *
 * mapAuthStatus() turns the agent's GET /auth/status answer into what the entry
 * shows: a label, a sublabel, one button and the action behind it. It is pure
 * (no VCL, no network) so cppunit covers every state; the VCL widget in
 * StartCenterAccountWidget.cxx only renders what this returns.
 */

#ifndef INCLUDED_OFFICELABS_STARTCENTERACCOUNT_HXX
#define INCLUDED_OFFICELABS_STARTCENTERACCOUNT_HXX

#include <officelabs/officelabsdllapi.h>

#include <string>
#include <string_view>

namespace officelabs {

enum class StartCenterAccountState
{
    SignedOut,
    Pending,
    SignedIn,
    Unreachable
};

enum class StartCenterAccountAction
{
    None,
    SignIn,
    Cancel,
    SignOut
};

/// All text is UTF-8.
struct StartCenterAccountView
{
    StartCenterAccountState eState = StartCenterAccountState::Unreachable;
    std::string aLabel;
    std::string aSublabel;
    /// Empty when there is nothing to press.
    std::string aButton;
    StartCenterAccountAction eAction = StartCenterAccountAction::None;
    /// Only set while pending: the page the user confirms the grant on.
    std::string aVerificationUrl;
};

/// Maps an agent response to the entry. Anything that is not a well-formed
/// HTTP 200 JSON object with a known state -- no connection (status 0), a 401,
/// a proxy's HTML, truncated JSON -- is the Unreachable state, so the entry
/// never shows a raw error.
OFFICELABS_DLLPUBLIC StartCenterAccountView mapAuthStatus(long nHttpStatus,
                                                          std::string_view aBody);

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_STARTCENTERACCOUNT_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
