/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <officelabs/ConsentBridge.hxx>
#include <officelabs/AgentHttp.hxx>
#include <officelabs/AgentIdentity.hxx>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <rtl/strbuf.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <tools/link.hxx>
#include <vcl/svapp.hxx>
#include <vcl/timer.hxx>
#include <vcl/vclenum.hxx>
#include <vcl/weld/MessageDialog.hxx>

#include <memory>
#include <sstream>
#include <thread>

namespace officelabs {

namespace {

void VclDispatchCb(void* pData, void*)
{
    auto* pFn = static_cast<std::function<void()>*>(pData);
    (*pFn)();
    delete pFn;
}

void postToVclThread(std::function<void()> fn)
{
    auto* pData = new std::function<void()>(std::move(fn));
    Application::PostUserEvent(LINK_NONMEMBER(pData, VclDispatchCb));
}

// The challenge lives 90 s and the UNO bridge read-timeout kills the
// subprocess at 110 s. Deny well before both so a user who wandered off
// cannot take the bridge down with them.
const int DIALOG_TIMEOUT_SECONDS = 45;
const long HTTP_TIMEOUT_SECONDS = 10;

boost::property_tree::ptree parseJson(const std::string& rBody)
{
    boost::property_tree::ptree aTree;
    try
    {
        std::istringstream aStream(rBody);
        boost::property_tree::read_json(aStream, aTree);
    }
    catch (const std::exception& e)
    {
        SAL_WARN("officelabs.cef", "consent response is not JSON: " << e.what());
    }
    return aTree;
}

/// Escape a string for embedding in the one JSON body we send.
OString jsonEscape(const OString& rValue)
{
    OStringBuffer aOut(rValue.getLength() + 8);
    for (sal_Int32 i = 0; i < rValue.getLength(); ++i)
    {
        const char c = rValue[i];
        switch (c)
        {
            case '"':  aOut.append("\\\""); break;
            case '\\': aOut.append("\\\\"); break;
            case '\n': aOut.append("\\n"); break;
            case '\r': aOut.append("\\r"); break;
            case '\t': aOut.append("\\t"); break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                    aOut.append("\\u00").append(OString::number(c, 16));
                else
                    aOut.append(c);
        }
    }
    return aOut.makeStringAndClear();
}

OUString describeRequest(const boost::property_tree::ptree& rChallenge)
{
    // What the user reads must bound what the grant authorises.
    //
    // macro_id is validated against a stored macro, and the digest the grant is
    // keyed on is resolved by the agent, so the macro named here is the macro
    // that runs. Capabilities are *proposed* by the page, but only from a fixed
    // five-value allowlist -- never free text -- and consume_grant refuses any
    // run needing a capability the grant does not carry. So the page can make
    // this prompt scarier than necessary; it cannot make it tamer than the truth,
    // which is the direction that matters. (Deriving them agent-side would drop
    // even the over-statement; tracked as follow-up, not a gate here.)
    //
    // document_identity is deliberately NOT shown. The agent copies it verbatim
    // from the page's challenge request and never verifies it (its own
    // consume_grant docstring says so), so printing it would put attacker-chosen
    // text on the line a user reads to decide whether to trust this. Labelling
    // it "unverified" would not help: the reassuring string is still the
    // attacker's. It can return when D12 supplies a verified identity.
    const std::string aMacro = rChallenge.get<std::string>("macro_id", "(unknown macro)");

    OUStringBuffer aCaps;
    if (const auto aNode = rChallenge.get_child_optional("capabilities"))
    {
        for (const auto& rItem : *aNode)
        {
            if (!aCaps.isEmpty())
                aCaps.append(", ");
            aCaps.appendAscii(rItem.second.data().c_str());
        }
    }
    if (aCaps.isEmpty())
        aCaps.append("(none declared)");

    OUStringBuffer aText;
    aText.append("A macro is asking permission to run with elevated powers.\n\n");
    aText.append("Macro: ").appendAscii(aMacro.c_str()).append("\n");
    aText.append("Requesting: ").append(aCaps.makeStringAndClear()).append("\n");
    aText.append("\nThis authorises ONE run. Allow only if you started this.");
    return aText.makeStringAndClear();
}

/// The dialog result, plus everything the follow-up HTTP needs.
struct PendingConsent
{
    OString sChallengeId;
    OString sPayload;
    OString sSessionToken;
    OString sInstallProof;
    std::vector<unsigned char> aSecret;
    OUString sPrompt;
    std::function<void(ConsentOutcome)> fnDone;
};

void finishOnWorker(std::shared_ptr<PendingConsent> pPending, bool bApproved)
{
    // Network work must not run on the VCL main thread: it holds SolarMutex,
    // and a stalled request there blocks every concurrent bridge UNO call.
    std::thread([pPending, bApproved]() {
        ConsentOutcome aOutcome;

        if (!bApproved)
        {
            httpRequest("POST", "/consent/" + pPending->sChallengeId + "/deny", "{}",
                        HTTP_TIMEOUT_SECONDS, pPending->sSessionToken, pPending->sInstallProof);
            aOutcome.sError = "declined";
            pPending->fnDone(aOutcome);
            return;
        }

        const OString sSignature
            = hmacSha256Hex(pPending->aSecret,
                            std::string_view(pPending->sPayload.getStr(),
                                             pPending->sPayload.getLength()));
        const OString sBody = "{\"signature\":\"" + jsonEscape(sSignature) + "\"}";
        const AgentResponse aApprove
            = httpRequest("POST", "/consent/" + pPending->sChallengeId + "/approve", sBody,
                          HTTP_TIMEOUT_SECONDS, pPending->sSessionToken, OString());

        if (aApprove.nStatus != 200)
        {
            SAL_WARN("officelabs.cef", "consent approve rejected: " << aApprove.nStatus);
            aOutcome.sError = "agent refused the approval";
            pPending->fnDone(aOutcome);
            return;
        }

        const auto aTree = parseJson(aApprove.aBody);
        const std::string aGrant = aTree.get<std::string>("consent_id", "");
        if (aGrant.empty())
        {
            aOutcome.sError = "agent returned no grant";
            pPending->fnDone(aOutcome);
            return;
        }

        aOutcome.bGranted = true;
        aOutcome.sConsentId = OString(aGrant.c_str());
        pPending->fnDone(aOutcome);
    }).detach();
}

/// Auto-deny timer.
///
/// A modal left open past the UNO bridge's 110 s read-timeout gets the bridge
/// subprocess killed and respawned, so the dialog must answer itself long
/// before that if the user has walked away. Denial is the safe default.
class ConsentTimeout : public Timer
{
    std::shared_ptr<weld::MessageDialog> mxDialog;

public:
    explicit ConsentTimeout(std::shared_ptr<weld::MessageDialog> xDialog)
        : Timer("officelabs consent timeout")
        , mxDialog(std::move(xDialog))
    {
        SetTimeout(DIALOG_TIMEOUT_SECONDS * 1000);
    }

    void Invoke() override
    {
        SAL_INFO("officelabs.cef", "consent dialog timed out; denying");
        mxDialog->response(RET_NO);
    }
};

/// Show the dialog on the VCL main thread, without holding it open.
void showDialog(const std::shared_ptr<PendingConsent>& pPending)
{
    std::shared_ptr<weld::MessageDialog> xDialog(Application::CreateMessageDialog(
        nullptr, VclMessageType::Question, VclButtonsType::YesNo, pPending->sPrompt));
    if (!xDialog)
    {
        // Headless, no frame, or out of memory. Deny rather than crash: the
        // caller is waiting on a callback that must fire exactly once.
        SAL_WARN("officelabs.cef", "could not create the consent dialog; denying");
        finishOnWorker(pPending, false);
        return;
    }
    xDialog->set_title(u"OfficeLabs \u2014 allow this macro to run?"_ustr);

    auto pTimer = std::make_shared<ConsentTimeout>(xDialog);
    pTimer->Start();

    xDialog->runAsync(xDialog, [pPending, pTimer, xDialog](sal_Int32 nResult) {
        pTimer->Stop();
        finishOnWorker(pPending, nResult == RET_YES);
    });
}

} // anonymous namespace

int consentDialogTimeoutSeconds() { return DIALOG_TIMEOUT_SECONDS; }

void requestConsentAsync(const OString& rChallengeId, std::function<void(ConsentOutcome)> fnDone)
{
    // Fetch on a worker: this is called from the browser UI thread.
    std::thread([rChallengeId, fnDone]() {
        ConsentOutcome aOutcome;

        const std::vector<unsigned char> aSecret = readInstallSecret();
        const OString sSessionToken = readSessionToken();
        if (aSecret.empty() || sSessionToken.isEmpty())
        {
            aOutcome.sError = "agent identity unavailable";
            fnDone(aOutcome);
            return;
        }

        // Prove we are native before the agent will describe the challenge.
        // The page cannot produce this: it has never seen install.secret.
        const OString sProof = hmacSha256Hex(
            aSecret, std::string_view(rChallengeId.getStr(), rChallengeId.getLength()));

        const AgentResponse aGet = httpRequest("GET", "/consent/" + rChallengeId, OString(),
                                          HTTP_TIMEOUT_SECONDS, sSessionToken, sProof);
        if (aGet.nStatus != 200)
        {
            SAL_WARN("officelabs.cef", "consent fetch failed: " << aGet.nStatus);
            aOutcome.sError = "challenge unavailable";
            fnDone(aOutcome);
            return;
        }

        const auto aTree = parseJson(aGet.aBody);
        const std::string aPayload = aTree.get<std::string>("payload", "");
        if (aPayload.empty())
        {
            aOutcome.sError = "challenge carried no payload";
            fnDone(aOutcome);
            return;
        }

        auto pPending = std::make_shared<PendingConsent>();
        pPending->sChallengeId = rChallengeId;
        pPending->sPayload = OString(aPayload.c_str(), aPayload.size());
        pPending->sSessionToken = sSessionToken;
        pPending->sInstallProof = sProof;
        pPending->aSecret = aSecret;
        // Describe what the AGENT says is being asked, never what the page said.
        pPending->sPrompt = describeRequest(aTree);
        pPending->fnDone = fnDone;

        // The dialog must be created and shown on the VCL main thread.
        postToVclThread([pPending]() { showDialog(pPending); });
    }).detach();
}

} // namespace officelabs

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
