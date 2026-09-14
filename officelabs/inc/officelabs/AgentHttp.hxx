/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * OfficeLabs agent HTTP helpers -- loopback calls to the local agent.
 *
 * Extracted from ConsentBridge.cxx so the inline-completion path (a second,
 * lower-latency caller) does not copy the URL-building and response-capping
 * logic. Behaviour is unchanged from ConsentBridge's original private helpers:
 * a PORT-only URL (see agentBase() below for why), CURLOPT_NOSIGNAL because
 * these run on detached worker threads, no CURLOPT_FOLLOWLOCATION, and a
 * capped response body.
 */

#ifndef INCLUDED_OFFICELABS_AGENTHTTP_HXX
#define INCLUDED_OFFICELABS_AGENTHTTP_HXX

#include <officelabs/officelabsdllapi.h>
#include <rtl/string.hxx>

#include <string>
#include <utility>
#include <vector>

namespace officelabs {

struct AgentResponse
{
    long nStatus = 0;
    std::string aBody;
};

/// An extra header as (name, value); joined as "name: value".
using AgentHttpHeader = std::pair<OString, OString>;

/// Where the agent listens, built from a PORT only -- never a full URL. See
/// AgentHttp.cxx for why a URL was rejected.
OFFICELABS_DLLPUBLIC OString agentBase();

/// One loopback request to the agent.
///
/// @p nTimeoutSeconds is the whole-request curl timeout (callers pick their
/// own budget: ConsentBridge's dialog flow can afford longer than an inline
/// completion poll). @p rSessionToken and @p rInstallProof are sent as
/// X-OfficeLabs-Session / X-OfficeLabs-Install-Proof when non-empty.
/// @p rExtraHeaders are appended after those two, in order.
OFFICELABS_DLLPUBLIC AgentResponse httpRequest(const OString& rMethod, const OString& rPath,
                                               const OString& rBody, long nTimeoutSeconds,
                                               const OString& rSessionToken = OString(),
                                               const OString& rInstallProof = OString(),
                                               const std::vector<AgentHttpHeader>& rExtraHeaders
                                                   = {});

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_AGENTHTTP_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
