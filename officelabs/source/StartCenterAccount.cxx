/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * See officelabs/inc/officelabs/StartCenterAccount.hxx.
 */

#include <officelabs/StartCenterAccount.hxx>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <cctype>
#include <sstream>

namespace officelabs {

namespace {

StartCenterAccountView unreachable()
{
    StartCenterAccountView aView;
    aView.eState = StartCenterAccountState::Unreachable;
    aView.aLabel = "OfficeLabs is starting\xE2\x80\xA6";
    return aView;
}

StartCenterAccountView signedOut()
{
    StartCenterAccountView aView;
    aView.eState = StartCenterAccountState::SignedOut;
    aView.aLabel = "Sign in to OfficeLabs";
    aView.aButton = "Sign in";
    aView.eAction = StartCenterAccountAction::SignIn;
    return aView;
}

/// A string field, or empty when absent, not a string, or a nested object.
std::string field(const boost::property_tree::ptree& rTree, const char* pKey)
{
    const auto oChild = rTree.get_child_optional(pKey);
    if (!oChild || !oChild->empty())
        return std::string();
    return oChild->data();
}

/// The agent sends the plan as an enum value ("FREE", "pro"); show "Free", "Pro".
std::string displayPlan(std::string aPlan)
{
    for (std::size_t i = 0; i < aPlan.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(aPlan[i]);
        aPlan[i] = static_cast<char>(i == 0 ? std::toupper(c) : std::tolower(c));
    }
    return aPlan;
}

} // namespace

StartCenterAccountView mapAuthStatus(long nHttpStatus, std::string_view aBody)
{
    if (nHttpStatus != 200)
        return unreachable();

    boost::property_tree::ptree aTree;
    try
    {
        std::istringstream aStream{ std::string(aBody) };
        boost::property_tree::read_json(aStream, aTree);
    }
    catch (const boost::property_tree::ptree_error&)
    {
        return unreachable();
    }

    const std::string aState = field(aTree, "state");

    if (aState == "signed_out" || aState == "error")
        return signedOut();

    if (aState == "pending")
    {
        StartCenterAccountView aView;
        aView.eState = StartCenterAccountState::Pending;
        aView.aLabel = "Confirm in your browser";
        aView.aSublabel = field(aTree, "user_code");
        aView.aButton = "Cancel";
        aView.eAction = StartCenterAccountAction::Cancel;
        aView.aVerificationUrl = field(aTree, "verification_uri_complete");
        return aView;
    }

    // offline = the agent still holds the session but cannot reach the cloud.
    if (aState == "signed_in" || aState == "offline")
    {
        StartCenterAccountView aView;
        aView.eState = StartCenterAccountState::SignedIn;
        const std::string aEmail = field(aTree, "email");
        aView.aLabel = aEmail.empty() ? std::string("Signed in") : aEmail;
        const std::string aPlan = field(aTree, "plan_tier");
        if (!aPlan.empty())
            aView.aSublabel = "Plan: " + displayPlan(aPlan);
        aView.aButton = "Sign out";
        aView.eAction = StartCenterAccountAction::SignOut;
        return aView;
    }

    // not_configured, a missing state, or a state this build does not know.
    return unreachable();
}

} // namespace officelabs

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
