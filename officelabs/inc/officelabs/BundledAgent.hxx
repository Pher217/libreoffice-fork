/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Starts and stops the agent shipped inside OfficeLabs.app (macOS).
 *
 * A Finder launch runs no launcher script, so nothing would start the agent
 * the sidebar talks to on 127.0.0.1:8766. When the bundle carries
 * Contents/Resources/officelabs-services, the office process spawns that agent
 * itself and stops it again on quit. Dev instdir builds have no such directory
 * and keep using scripts/launch/officelabs_mac_launcher.sh.
 */

#ifndef INCLUDED_OFFICELABS_BUNDLEDAGENT_HXX
#define INCLUDED_OFFICELABS_BUNDLEDAGENT_HXX

#include <officelabs/officelabsdllapi.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace officelabs {

/// Parses officelabs.config text into io. Same rules as ol_service_env in
/// officelabs_mac_launcher.sh: lines are trimmed; blank lines, '#' comments and
/// lines without '=' are skipped; KEY must match [A-Za-z_][A-Za-z0-9_]*; later
/// assignments win; a trailing '\r' is stripped.
OFFICELABS_DLLPUBLIC void parseConfig(std::string_view text, std::map<std::string, std::string>& io);

enum class PortDecision
{
    Spawn,   ///< port is free: start our own agent
    Reuse,   ///< port held by an OfficeLabs agent: use it, never kill it
    Blocked, ///< port held by something else: leave it alone, start nothing
};

OFFICELABS_DLLPUBLIC PortDecision decide(bool portInUse, bool healthIsOfficeLabsAgent);

/// KEY=VALUE strings for the agent process: defaults, overridden by cfg, plus
/// HOME (when set) and a minimal PATH.
OFFICELABS_DLLPUBLIC std::vector<std::string> buildEnv(const std::map<std::string, std::string>& cfg);

namespace BundledAgent {

/// Once per process; no-op unless running from a bundle that ships the agent.
OFFICELABS_DLLPUBLIC void ensureStarted();

/// Stops the agent only if ensureStarted() spawned it. Idempotent.
OFFICELABS_DLLPUBLIC void stop();

} // namespace BundledAgent

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_BUNDLEDAGENT_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
