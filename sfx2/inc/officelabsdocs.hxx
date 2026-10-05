/* OfficeLabs docs URL — where Help (F1) points, per release channel */
#pragma once

#include <string>

namespace sfx2::officelabs {

/// OFFICELABS_DOCS_URL (full override) > channel "dev" > production.
/// Hosts follow officelabs-master CLAUDE.md: docs.dev.officelabs.tech for dev,
/// docs.officelabs.tech for prod (the dash form dev-docs is stale).
inline std::string ResolveDocsUrl(const char* pOverride, const char* pChannel)
{
    if (pOverride && *pOverride)
        return pOverride;
    if (pChannel && std::string(pChannel) == "dev")
        return "https://docs.dev.officelabs.tech";
    return "https://docs.officelabs.tech";
}

}
