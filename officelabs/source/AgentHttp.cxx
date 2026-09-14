/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <officelabs/AgentHttp.hxx>

// curl/curl.h reaches <winsock2.h>/<windows.h> on Windows, and windows.h still
// defines the legacy Yield() macro. Left alone it erases the declaration of
// Application::Yield() in vcl/svapp.hxx below, failing as
//     svapp.hxx(502): error C2208: 'void': no members defined using this type
// Wrap the offending include -- not the file -- in prewin/postwin: prewin
// defines the IN/OUT SAL annotations winsock2.h needs and pulls in windows.h,
// postwin then undefines Yield (and IN/OUT) before any vcl header is seen.
#ifdef _WIN32
#include <prewin.h>
#endif
#include <curl/curl.h>
#ifdef _WIN32
#include <postwin.h>
#endif

#include <osl/process.h>
#include <rtl/ustring.hxx>
#include <sal/log.hxx>

#include <mutex>

namespace officelabs {

namespace {

// A challenge/completion response is at most a few KB; anything approaching
// this is not one, and an unbounded append would let a wrong endpoint exhaust
// memory.
const size_t MAX_RESPONSE_BYTES = 256 * 1024;

size_t collectBody(void* pContents, size_t nSize, size_t nMemb, void* pUser)
{
    const size_t nTotal = nSize * nMemb;
    auto* pBody = static_cast<std::string*>(pUser);
    if (pBody->size() + nTotal > MAX_RESPONSE_BYTES)
        return 0; // signals an error to libcurl and aborts the transfer
    pBody->append(static_cast<char*>(pContents), nTotal);
    return nTotal;
}

// libcurl's easy API self-inits on first use in practice, but that is not
// guaranteed thread-safe across the several worker threads this code runs
// on (ConsentBridge's dialog flow, and the inline-completion poll). Do it
// exactly once, explicitly.
void ensureCurlGlobalInit()
{
    static std::once_flag aOnceFlag;
    std::call_once(aOnceFlag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

} // namespace

OString agentBase()
{
    OUString sEnv;
    if (osl_getEnvironment(u"OFFICELABS_AGENT_PORT"_ustr.pData, &sEnv.pData) == osl_Process_E_None
        && !sEnv.isEmpty())
    {
        bool bDigits = sEnv.getLength() <= 5;
        for (sal_Int32 i = 0; bDigits && i < sEnv.getLength(); ++i)
            bDigits = sEnv[i] >= '0' && sEnv[i] <= '9';

        const sal_Int32 nPort = bDigits ? sEnv.toInt32() : 0;
        if (nPort > 0 && nPort <= 65535)
            return "http://127.0.0.1:" + OString::number(nPort);

        SAL_WARN("officelabs.cef", "ignoring an invalid OFFICELABS_AGENT_PORT");
    }
    return "http://127.0.0.1:8766"_ostr;
}

AgentResponse httpRequest(const OString& rMethod, const OString& rPath, const OString& rBody,
                          long nTimeoutSeconds, const OString& rSessionToken,
                          const OString& rInstallProof,
                          const std::vector<AgentHttpHeader>& rExtraHeaders)
{
    ensureCurlGlobalInit();

    AgentResponse aResult;
    CURL* pCurl = curl_easy_init();
    if (!pCurl)
    {
        SAL_WARN("officelabs.cef", "curl_easy_init failed");
        return aResult;
    }

    const OString sUrl = agentBase() + rPath;
    curl_slist* pHeaders = nullptr;
    pHeaders = curl_slist_append(pHeaders, "Content-Type: application/json");
    if (!rSessionToken.isEmpty())
    {
        const OString sHeader = "X-OfficeLabs-Session: " + rSessionToken;
        pHeaders = curl_slist_append(pHeaders, sHeader.getStr());
    }
    if (!rInstallProof.isEmpty())
    {
        const OString sHeader = "X-OfficeLabs-Install-Proof: " + rInstallProof;
        pHeaders = curl_slist_append(pHeaders, sHeader.getStr());
    }
    for (const auto& rHeader : rExtraHeaders)
    {
        const OString sHeader = rHeader.first + ": " + rHeader.second;
        pHeaders = curl_slist_append(pHeaders, sHeader.getStr());
    }

    curl_easy_setopt(pCurl, CURLOPT_URL, sUrl.getStr());
    curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, pHeaders);
    curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, nTimeoutSeconds);
    // These run on detached worker threads; without this libcurl may use
    // SIGALRM for resolver timeouts, which is not safe off the main thread.
    curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, collectBody);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &aResult.aBody);
    // Loopback only, and never follow a redirect off it.
    curl_easy_setopt(pCurl, CURLOPT_FOLLOWLOCATION, 0L);
    if (rMethod == "POST")
    {
        curl_easy_setopt(pCurl, CURLOPT_POST, 1L);
        curl_easy_setopt(pCurl, CURLOPT_POSTFIELDS, rBody.getStr());
        curl_easy_setopt(pCurl, CURLOPT_POSTFIELDSIZE, static_cast<long>(rBody.getLength()));
    }

    const CURLcode eResult = curl_easy_perform(pCurl);
    if (eResult == CURLE_OK)
        curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &aResult.nStatus);
    else
        SAL_WARN("officelabs.cef", "agent HTTP failed: " << curl_easy_strerror(eResult));

    curl_slist_free_all(pHeaders);
    curl_easy_cleanup(pCurl);
    return aResult;
}

} // namespace officelabs

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
