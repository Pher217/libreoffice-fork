/* OfficeLabs docs URL — where Help (F1) points, per release channel */
#pragma once

#include <string>

namespace sfx2::officelabs {

/// Last resort only: used when a build carries no OFFICELABS_DOCS_URL_<CHANNEL> key (a dev
/// checkout without an installer-built officelabs.config). The source of truth is
/// officelabs-infra/config/environments.py (docs host per environment); installer/channel_hosts.py
/// derives it at build time and writes both keys into officelabs.config (officelabs-project#419).
/// A literal here can drift from infra, so a release build must never rely on it.
inline constexpr const char* FALLBACK_DOCS_URL_PRODUCTION = "https://docs.officelabs.tech";
inline constexpr const char* FALLBACK_DOCS_URL_DEVELOPMENT = "https://docs.dev.officelabs.tech";

/// OFFICELABS_DOCS_URL (full override) > the OFFICELABS_DOCS_URL_<CHANNEL> key of the active
/// channel > the last-resort literal for that channel. Channel "development" ("dev" accepted)
/// picks the development key; anything else, including unset, picks production.
/// pProductionUrl / pDevelopmentUrl are OFFICELABS_DOCS_URL_PRODUCTION / _DEVELOPMENT from the
/// process environment, like the agent's api_url_for_channel does for the API hosts.
inline std::string ResolveDocsUrl(const char* pOverride, const char* pChannel,
                                  const char* pProductionUrl = nullptr,
                                  const char* pDevelopmentUrl = nullptr)
{
    if (pOverride && *pOverride)
        return pOverride;
    const bool bDevelopment
        = pChannel && (std::string(pChannel) == "development" || std::string(pChannel) == "dev");
    const char* pConfigured = bDevelopment ? pDevelopmentUrl : pProductionUrl;
    if (pConfigured && *pConfigured)
        return pConfigured;
    return bDevelopment ? FALLBACK_DOCS_URL_DEVELOPMENT : FALLBACK_DOCS_URL_PRODUCTION;
}

}
