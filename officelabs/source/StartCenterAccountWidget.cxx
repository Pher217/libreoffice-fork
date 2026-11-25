/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * The Start Center sign-in entry (project#418). See StartCenterAccount.hxx for
 * the state function this renders and StartCenterAccountWidget.hxx for how
 * BackingWindow reaches it.
 *
 * It talks to the agent on :8766 through AgentHttp, exactly as the sidebar's
 * account view does, so there is one auth path. Every call runs on a worker
 * thread (a hung agent must not freeze the Start Center) and its result is
 * posted back to the VCL thread.
 */

#ifdef HAVE_FEATURE_CEF

#include <officelabs/StartCenterAccountWidget.hxx>

#include <officelabs/AgentHttp.hxx>
#include <officelabs/AgentIdentity.hxx>
#include <officelabs/StartCenterAccount.hxx>
#include <officelabs/WebViewMessageHandler.hxx>

#include <sfx2/startcenteraccount.hxx>

#include <sal/log.hxx>
#include <tools/link.hxx>
#include <vcl/font.hxx>
#include <vcl/officelabstheme.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>
#include <vcl/timer.hxx>
#include <vcl/vclenum.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/weld.hxx>

#include <functional>
#include <memory>
#include <thread>

namespace officelabs {

namespace {

// While a grant is pending the agent polls the cloud on its own schedule;
// this re-reads the agent (a loopback call) so the entry notices the approval.
// Mirrors AccountPanel.tsx PENDING_POLL_MS.
const sal_uInt64 PENDING_POLL_MS = 2000;

// Mirrors useAuthStatus.ts UNREACHABLE_RETRY_MS: a cold-starting agent recovers
// without waiting for a focus event.
const sal_uInt64 UNREACHABLE_RETRY_MS = 5000;

const long STATUS_TIMEOUT_SECONDS = 2;
const long CANCEL_TIMEOUT_SECONDS = 5;
// Start and sign-out make a cloud call on the agent side (authClient.ts uses 15 s).
const long CLOUD_TIMEOUT_SECONDS = 15;

enum class Call
{
    Status,
    Start,
    Cancel,
    SignOut
};

void runOnVclThread(void* pData, void*)
{
    std::unique_ptr<std::function<void()>> pFn(static_cast<std::function<void()>*>(pData));
    (*pFn)();
}

class StartCenterAccountWidget final : public sfx2::StartCenterAccountEntry
{
public:
    explicit StartCenterAccountWidget(weld::Container& rParent);
    ~StartCenterAccountWidget() override;

    void refresh() override;

private:
    DECL_LINK(ClickHdl, weld::Button&, void);
    DECL_LINK(TimerHdl, Timer*, void);

    void send(Call eCall);
    void onResponse(Call eCall, sal_uInt32 nEpoch, long nStatus, const std::string& rBody);
    void apply(const StartCenterAccountView& rView);

    std::unique_ptr<weld::Builder> m_xBuilder;
    std::unique_ptr<weld::Container> m_xContainer;
    std::unique_ptr<weld::Label> m_xLabel;
    std::unique_ptr<weld::Label> m_xSublabel;
    std::unique_ptr<weld::Button> m_xButton;
    Timer m_aTimer;

    StartCenterAccountAction m_eAction = StartCenterAccountAction::None;
    // A worker's result is applied only if nothing the user did has happened
    // since it was issued; a poll answered after a click would show stale state.
    sal_uInt32 m_nEpoch = 0;
    bool m_bStatusInFlight = false;
    // Worker results arrive as posted events that may outlive this object. They
    // check this flag (shared, VCL-thread only) before touching it.
    std::shared_ptr<StartCenterAccountWidget*> m_pSelf;
};

StartCenterAccountWidget::StartCenterAccountWidget(weld::Container& rParent)
    : m_xBuilder(Application::CreateBuilder(&rParent, u"officelabs/ui/startcenteraccount.ui"_ustr))
    , m_xContainer(m_xBuilder->weld_container(u"StartCenterAccount"_ustr))
    , m_xLabel(m_xBuilder->weld_label(u"account_label"_ustr))
    , m_xSublabel(m_xBuilder->weld_label(u"account_sublabel"_ustr))
    , m_xButton(m_xBuilder->weld_button(u"account_button"_ustr))
    , m_aTimer("officelabs StartCenterAccount")
    , m_pSelf(std::make_shared<StartCenterAccountWidget*>(this))
{
    // The OfficeLabs theme colours, as the rest of the module applies them.
    const vcl::officelabs::OLColors aColors
        = vcl::officelabs::GetOLColors(vcl::officelabs::GetOLTheme());
    m_xLabel->set_font_color(aColors.text);
    m_xSublabel->set_font_color(aColors.subtext);

    vcl::Font aBold = Application::GetSettings().GetStyleSettings().GetLabelFont();
    aBold.SetWeight(WEIGHT_BOLD);
    m_xLabel->set_font(aBold);

    m_xButton->connect_clicked(LINK(this, StartCenterAccountWidget, ClickHdl));
    m_aTimer.SetInvokeHandler(LINK(this, StartCenterAccountWidget, TimerHdl));

    // BackingWindow sizes the Start Center once, from what is shown when it
    // lays out, and the entry changes height with its state. Reserve the tallest
    // state's height up front so a later state is never squeezed to nothing.
    StartCenterAccountView aTallest;
    aTallest.aLabel = "Sign in to OfficeLabs";
    aTallest.aSublabel = "Plan";
    aTallest.aButton = "Sign out";
    apply(aTallest);
    m_xContainer->set_size_request(-1, m_xContainer->get_preferred_size().Height());

    apply(mapAuthStatus(0, std::string()));
    refresh();
}

StartCenterAccountWidget::~StartCenterAccountWidget()
{
    m_aTimer.Stop();
    *m_pSelf = nullptr;
}

void StartCenterAccountWidget::refresh()
{
    if (m_bStatusInFlight)
        return;
    m_bStatusInFlight = true;
    send(Call::Status);
}

IMPL_LINK_NOARG(StartCenterAccountWidget, TimerHdl, Timer*, void) { refresh(); }

IMPL_LINK_NOARG(StartCenterAccountWidget, ClickHdl, weld::Button&, void)
{
    // Nothing the agent says now may overwrite the answer to this click.
    ++m_nEpoch;
    m_xButton->set_sensitive(false);
    switch (m_eAction)
    {
        case StartCenterAccountAction::SignIn:
            send(Call::Start);
            break;
        case StartCenterAccountAction::Cancel:
            send(Call::Cancel);
            break;
        case StartCenterAccountAction::SignOut:
            send(Call::SignOut);
            break;
        case StartCenterAccountAction::None:
            m_xButton->set_sensitive(true);
            break;
    }
}

void StartCenterAccountWidget::send(Call eCall)
{
    OString aMethod = "POST"_ostr;
    OString aPath;
    long nTimeout = CLOUD_TIMEOUT_SECONDS;
    switch (eCall)
    {
        case Call::Status:
            aMethod = "GET"_ostr;
            aPath = "/auth/status"_ostr;
            nTimeout = STATUS_TIMEOUT_SECONDS;
            break;
        case Call::Start:
            aPath = "/auth/device/start"_ostr;
            break;
        case Call::Cancel:
            aPath = "/auth/device/cancel"_ostr;
            nTimeout = CANCEL_TIMEOUT_SECONDS;
            break;
        case Call::SignOut:
            aPath = "/auth/sign-out"_ostr;
            break;
    }

    const sal_uInt32 nEpoch = m_nEpoch;
    std::shared_ptr<StartCenterAccountWidget*> pSelf = m_pSelf;
    std::thread([=]() {
        const AgentResponse aResp
            = httpRequest(aMethod, aPath, OString(), nTimeout, readSessionToken());
        auto* pFn = new std::function<void()>([=]() {
            if (StartCenterAccountWidget* pWidget = *pSelf)
                pWidget->onResponse(eCall, nEpoch, aResp.nStatus, aResp.aBody);
        });
        Application::PostUserEvent(LINK_NONMEMBER(pFn, runOnVclThread));
    }).detach();
}

void StartCenterAccountWidget::onResponse(Call eCall, sal_uInt32 nEpoch, long nStatus,
                                          const std::string& rBody)
{
    switch (eCall)
    {
        case Call::Status:
            m_bStatusInFlight = false;
            if (nEpoch != m_nEpoch)
            {
                // A click happened while this was in flight: ask again.
                refresh();
                return;
            }
            apply(mapAuthStatus(nStatus, rBody));
            return;

        case Call::Start:
        {
            const StartCenterAccountView aView = mapAuthStatus(nStatus, rBody);
            apply(aView);
            if (aView.eState == StartCenterAccountState::Pending && !aView.aVerificationUrl.empty())
            {
                const OUString aUrl = OUString::fromUtf8(aView.aVerificationUrl);
                if (openExternalUrlValidated(aUrl) != ExternalUrlOpen::Opened)
                    SAL_WARN("officelabs.cef", "Start Center sign-in: could not open the browser");
            }
            return;
        }

        case Call::Cancel:
        case Call::SignOut:
            // The response only says whether it worked; what the entry shows
            // comes from the agent's state.
            refresh();
            return;
    }
}

void StartCenterAccountWidget::apply(const StartCenterAccountView& rView)
{
    m_xLabel->set_label(OUString::fromUtf8(rView.aLabel));
    m_xSublabel->set_label(OUString::fromUtf8(rView.aSublabel));
    m_xSublabel->set_visible(!rView.aSublabel.empty());
    m_xButton->set_label(OUString::fromUtf8(rView.aButton));
    m_xButton->set_visible(!rView.aButton.empty());
    m_xButton->set_sensitive(true);
    m_eAction = rView.eAction;

    switch (rView.eState)
    {
        case StartCenterAccountState::Pending:
            m_aTimer.SetTimeout(PENDING_POLL_MS);
            m_aTimer.Start();
            break;
        case StartCenterAccountState::Unreachable:
            m_aTimer.SetTimeout(UNREACHABLE_RETRY_MS);
            m_aTimer.Start();
            break;
        case StartCenterAccountState::SignedOut:
        case StartCenterAccountState::SignedIn:
            m_aTimer.Stop();
            break;
    }
}

std::unique_ptr<sfx2::StartCenterAccountEntry> createStartCenterAccount(weld::Container& rParent)
{
    return std::make_unique<StartCenterAccountWidget>(rParent);
}

} // namespace

void registerStartCenterAccount()
{
    sfx2::SetStartCenterAccountFactory(&createStartCenterAccount);
}

} // namespace officelabs

#endif // HAVE_FEATURE_CEF

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
