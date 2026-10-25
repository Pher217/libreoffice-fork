/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Rolling-window rate limiter for openExternalUrl (core#150).
 *
 * Header-only and free of CEF types so it is unit-testable without a
 * CefBrowser. The clock is injected as a millisecond count, never read here.
 */

#ifndef INCLUDED_OFFICELABS_EXTERNALURLRATELIMITER_HXX
#define INCLUDED_OFFICELABS_EXTERNALURLRATELIMITER_HXX

#include <sal/types.h>

#include <cstddef>
#include <deque>

namespace officelabs {

/// At most kMaxExternalUrlOpens opens per kExternalUrlOpenWindowMs.
constexpr std::size_t kMaxExternalUrlOpens = 3;
constexpr sal_uInt64 kExternalUrlOpenWindowMs = 10000;

class ExternalUrlRateLimiter
{
public:
    ExternalUrlRateLimiter(std::size_t nMax = kMaxExternalUrlOpens,
                           sal_uInt64 nWindowMs = kExternalUrlOpenWindowMs)
        : m_nMax(nMax)
        , m_nWindowMs(nWindowMs)
    {
    }

    /// True and the attempt is recorded when fewer than nMax opens happened
    /// in the last nWindowMs; false otherwise. A rejected attempt is NOT
    /// recorded, so hammering the limiter cannot extend its own lockout.
    /// |nNowMs| must be monotonic.
    bool tryAcquire(sal_uInt64 nNowMs)
    {
        while (!m_aStamps.empty() && nNowMs - m_aStamps.front() >= m_nWindowMs)
            m_aStamps.pop_front();

        if (m_aStamps.size() >= m_nMax)
            return false;

        m_aStamps.push_back(nNowMs);
        return true;
    }

private:
    std::size_t m_nMax;
    sal_uInt64 m_nWindowMs;
    std::deque<sal_uInt64> m_aStamps;
};

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_EXTERNALURLRATELIMITER_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
