/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * OfficeLabsJob -- the module's first UNO component.
 *
 * officelabs/ has one inbound surface today: cefQuery() from the CEF
 * sidebar, handled entirely inside the process. The Python agent talks to
 * LibreOffice over the URP bridge, and until now there was nothing on the
 * fork side for it to call that wasn't already exposed by stock UNO
 * services -- this component is the seam that lets a future operation be
 * "a new Operation string", not "a new .idl and an offapi change".
 *
 * It implements the stock com.sun.star.task.XJob rather than a bespoke
 * interface for exactly that reason: XJob::execute() already takes an
 * open-ended sequence<NamedValue>, so adding a capability later is a
 * dispatch-table entry here, nothing in offapi.
 */

#include <com/sun/star/task/XJob.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/beans/NamedValue.hpp>
#include <com/sun/star/document/UndoFailedException.hpp>
#include <com/sun/star/document/XUndoManager.hpp>
#include <com/sun/star/document/XUndoManagerSupplier.hpp>
#include <com/sun/star/frame/XModel.hpp>

#include <cppuhelper/implbase.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <cppuhelper/weak.hxx>
#include <vcl/svapp.hxx>
#include <vcl/threadex.hxx>

#include <utility>

namespace officelabs {

namespace {

class OfficeLabsJob final : public ::cppu::WeakImplHelper<css::task::XJob, css::lang::XServiceInfo>
{
public:
    explicit OfficeLabsJob(css::uno::Reference<css::uno::XComponentContext> xContext)
        : m_xContext(std::move(xContext))
    {
    }

    // css.lang.XServiceInfo
    virtual OUString SAL_CALL getImplementationName() override;
    virtual sal_Bool SAL_CALL supportsService(const OUString& rServiceName) override;
    virtual css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override;

    // css.task.XJob
    virtual css::uno::Any SAL_CALL
    execute(const css::uno::Sequence<css::beans::NamedValue>& rArguments) override;

private:
    css::uno::Any executeUndoAgentTop(const css::uno::Sequence<css::beans::NamedValue>& rArguments);

    css::uno::Reference<css::uno::XComponentContext> m_xContext;
};

OUString SAL_CALL OfficeLabsJob::getImplementationName()
{
    return u"ai.officelabs.comp.OfficeLabsJob"_ustr;
}

sal_Bool SAL_CALL OfficeLabsJob::supportsService(const OUString& rServiceName)
{
    return cppu::supportsService(this, rServiceName);
}

css::uno::Sequence<OUString> SAL_CALL OfficeLabsJob::getSupportedServiceNames()
{
    return { u"ai.officelabs.OfficeLabsJob"_ustr };
}

css::uno::Any SAL_CALL
OfficeLabsJob::execute(const css::uno::Sequence<css::beans::NamedValue>& rArguments)
{
    OUString sOperation;
    for (const auto& rArg : rArguments)
    {
        if (rArg.Name == "Operation")
            rArg.Value >>= sOperation;
    }

    // Dispatch on Operation. This is the seam for project#216's atomic
    // compare-and-undo: separate UNO round trips (one call to check, another
    // to act) cannot close the window between the two -- the document can
    // change on the VCL thread in between -- which is the entire reason that
    // operation exists as one execute() call instead of two.
    if (sOperation == "ping")
        return css::uno::Any(u"ok"_ustr);

    if (sOperation == "undo_agent_top")
        return executeUndoAgentTop(rArguments);

    throw css::lang::IllegalArgumentException(
        "OfficeLabsJob: unknown Operation \"" + sOperation + "\"",
        getXWeak(), 0);
}

// The three values undo_agent_top reports back. A struct rather than three
// out-params because it crosses a thread boundary below, and syncExecute
// copy-constructs the functor into free store -- a single returned value is
// the only shape that is obviously safe there.
struct UndoAgentTopOutcome
{
    sal_Int32 nUndone = 0;
    bool bRefused = false;
    bool bStackCleared = false;
    OUString sReason;
};

// The check-and-act itself. Runs ON THE MAIN (solar) THREAD -- see
// executeUndoAgentTop for why that is not optional.
UndoAgentTopOutcome doUndoAgentTop(const css::uno::Reference<css::document::XUndoManager>& xUndoManager,
                                   const OUString& rContextTitle, sal_Int32 nMaxSteps)
{
    SolarMutexGuard aGuard;

    UndoAgentTopOutcome aOut;

    while (aOut.nUndone < nMaxSteps)
    {
        if (!xUndoManager->isUndoPossible())
        {
            // isUndoPossible() is documented to be false in TWO different
            // situations -- "the undo stack is currently empty, OR there is an
            // open and not-yet-closed undo context"
            // (offapi/com/sun/star/document/XUndoManager.idl:213-219). Those
            // are not the same answer to give a caller: an empty stack means
            // there is nothing of yours left, an open context means someone is
            // mid-edit and the same call would succeed later. Reporting both as
            // a silent Undone=0 success told the agent it had undone everything
            // it had when it had undone nothing.
            //
            // getAllUndoActionTitles() does not consult IsInListAction() --
            // lcl_getAllActionTitles() reads GetUndoActionCount(TopLevel)
            // directly (framework/source/fwe/helper/undomanagerhelper.cxx:990),
            // whereas isUndoPossible() returns false outright when
            // IsInListAction() (ibid.:949). So a non-empty stack here can only
            // mean an open context, and that is the discriminator.
            if (xUndoManager->getAllUndoActionTitles().hasElements())
            {
                aOut.bRefused = true;
                aOut.sReason = "an undo context is open on this document; nothing can be "
                               "undone until it is closed";
            }
            break;
        }

        const OUString sTitle = xUndoManager->getCurrentUndoActionTitle();
        if (sTitle != rContextTitle)
        {
            // Only the very first entry not matching is a refusal. Once we
            // have undone at least one of ours, running into someone else's
            // entry is just the natural end of our run, not an error.
            if (aOut.nUndone == 0)
            {
                aOut.bRefused = true;
                aOut.sReason = "top undo entry \"" + sTitle + "\" does not match ContextTitle \""
                               + rContextTitle + "\"";
            }
            break;
        }

        try
        {
            xUndoManager->undo();
        }
        catch (const css::document::UndoFailedException& rEx)
        {
            // THIS IS NOT A PARTIAL UNDO. UndoFailedException carries a
            // documented side effect that dwarfs the failure itself:
            //
            //   "In this case, the undo stack of the undo manager will have
            //    been cleared."
            //   -- offapi/com/sun/star/document/XUndoManager.idl:177-179
            //
            // and SfxUndoManager::Undo does exactly that -- on any throw from
            // the action it calls ImplClearUndo() and rethrows
            // (svl/source/undo/undo.cxx:744-752). So the user's entire undo
            // history is gone, not just ours. Reporting this as "undone N, then
            // it stopped" would be the most misleading result this component
            // could return, so it gets its own flag and says so in words.
            aOut.bStackCleared = true;
            aOut.bRefused = true;
            aOut.sReason = "undo failed after " + OUString::number(aOut.nUndone)
                           + " step(s) AND CLEARED THE WHOLE UNDO STACK, including entries this "
                             "agent did not create -- the document cannot be undone further: "
                           + rEx.Message;
            break;
        }
        catch (const css::uno::Exception& rEx)
        {
            // Everything else -- EmptyUndoStackException and
            // UndoContextNotClosedException from the helper's own re-check
            // (undomanagerhelper.cxx:632-639) -- leaves the stack alone. Report
            // the count, because it is the one thing the caller cannot
            // reconstruct once the document has moved.
            aOut.bRefused = true;
            aOut.sReason = "undo failed after " + OUString::number(aOut.nUndone) + " step(s): "
                           + rEx.Message;
            break;
        }
        ++aOut.nUndone;
    }

    return aOut;
}

// Undoes entries at the top of the agent's undo stack that belong to a named
// undo context, in a single execute() call. Two separate UNO round trips --
// read the top title, then undo -- leave a window in which the stack can change
// between them; one call closes it against DOCUMENT EDITS, which need the
// SolarMutex we hold (SwXText::insertString and friends). It does NOT close it
// against another thread's XUndoManager API calls: enterUndoContext and
// addUndoAction clear their SolarMutex at undomanagerhelper.cxx:474 and then
// mutate under m_aMutex alone. The helper re-checks and throws in that case
// (ibid.:632-639), which surfaces here as a refusal with an approximate
// reason -- bounded, but not the same as serialised.
//
// WHY THIS HOPS TO THE MAIN THREAD -- and what that does NOT buy
// The call always arrives over URP, on a thread that is not the solar thread.
// Undo is not a data-structure operation: reverting a Writer action runs view
// code -- invalidation, cursor and selection movement, scrolling -- which ends
// in VCL and, on macOS, in AppKit.
//
// Two earlier justifications for this hop were WRONG and are recorded here so
// nobody rebuilds an argument on them:
//
//   * "AppKit off the main thread is undefined, so we must marshal." The macOS
//     backend is built for precisely the opposite case: OSX_RUNINMAIN
//     (vcl/inc/osx/runinmain.hxx:67-90) checks !IsMainThread(), asserts the
//     SolarMutex is held, and bounces the AppKit call to the main queue itself.
//     AquaSalFrame::Flush -- the repaint an undo triggers -- is guarded that way
//     (vcl/osx/salframe.cxx:1148). A foreign thread holding the SolarMutex is a
//     supported caller, so this hop is not required for AppKit safety.
//
//   * "It executes in exactly the position Edit > Undo executes in." Also false.
//     Writer's menu Undo never touches this machinery at all: SwWrtShell::Do ->
//     SwEditShell::Undo -> IDocumentUndoRedo::UndoWithOffset ->
//     sw::UndoManager (sw/source/core/undo/docundo.cxx:750-762) -> SfxUndoManager.
//     UndoManagerHelper's request queue is reached only through the UNO
//     XUndoManager API, so there is no shipped path whose guarantees we inherit.
//
// Nor does the hop fix the deadlock in UndoManagerHelper's own precondition --
//
//     ::osl::Guard< ::framework::IMutex > aExternalGuard( i_externalLock.getGuardedMutex() );
//         // note that this assumes that the mutex has been released in the
//         // thread which added the Undo/Redo request, so we can successfully
//         // acquire it
//     (framework/source/fwe/helper/undomanagerhelper.cxx:622-626)
//
// -- because impl_processRequest()'s i_instanceLock.clear() (ibid.:474) drops
// exactly one recursion level, and the main thread dispatching a user event
// already holds one. Under contention the main thread blocks at ibid.:479 still
// owning the mutex, which is the same trap one level up: worse, it freezes the
// UI rather than one agent call. A deterministic test for that exists (block an
// XUndoManagerListener::leftContext on another thread) and is not written yet.
//
// What the hop IS for, stated no more strongly than it deserves: it is the
// shape upstream uses for UNO calls that drive view code -- the OnMainThread
// media-descriptor property routes storeToURL through the same syncExecute
// (sfx2/source/doc/sfxbasemodel.cxx:1813-1815), as do DispatchHelper
// (framework/source/services/dispatchhelper.cxx:115-119) and Frame
// (frame.cxx:579-590). Taking it costs one event hop and removes any need to
// prove that OSX_RUNINMAIN's per-call coverage is complete for every path a
// Writer undo can reach. That is a conservatism argument, not a correctness
// proof, and no test in this module distinguishes the two shapes except
// testUndoRunsOnTheSolarThread, which pins the behaviour rather than
// justifying it.
//
// SolarThreadExecutor::execute() posts a user event and takes a
// SolarMutexReleaser while it waits (vcl/source/helper/threadex.cxx:55-64), so
// the calling thread holds nothing meanwhile. The releaser is safe to
// construct from a thread that owns nothing -- it is conditional on
// GetSolarMutex().IsCurrentThread() (include/vcl/svapp.hxx:1438-1442) -- so no
// outer guard is needed here, and taking one would only add a contention
// window before it was released again.
css::uno::Any
OfficeLabsJob::executeUndoAgentTop(const css::uno::Sequence<css::beans::NamedValue>& rArguments)
{
    css::uno::Reference<css::frame::XModel> xModel;
    OUString sContextTitle;
    sal_Int32 nMaxSteps = 0;
    bool bHasModel = false;

    for (const auto& rArg : rArguments)
    {
        if (rArg.Name == "Model")
            bHasModel = (rArg.Value >>= xModel);
        else if (rArg.Name == "ContextTitle")
            rArg.Value >>= sContextTitle;
        else if (rArg.Name == "MaxSteps")
            rArg.Value >>= nMaxSteps;
    }

    if (!bHasModel || !xModel.is())
        throw css::lang::IllegalArgumentException(
            "OfficeLabsJob: undo_agent_top requires a valid Model", getXWeak(), 0);
    if (sContextTitle.isEmpty())
        throw css::lang::IllegalArgumentException(
            "OfficeLabsJob: undo_agent_top requires a non-empty ContextTitle", getXWeak(), 0);
    // Reject rather than default. MaxSteps left at 0 makes the loop a silent
    // no-op -- the caller asks to undo and nothing happens, with a success
    // result. A dispatcher whose failure mode is "quietly did nothing" is the
    // thing this design set out not to be.
    if (nMaxSteps <= 0)
        throw css::lang::IllegalArgumentException(
            "OfficeLabsJob: undo_agent_top requires MaxSteps >= 1", getXWeak(), 0);

    css::uno::Reference<css::document::XUndoManagerSupplier> xSupplier(xModel, css::uno::UNO_QUERY);
    if (!xSupplier.is())
        throw css::lang::IllegalArgumentException(
            "OfficeLabsJob: Model does not support XUndoManagerSupplier", getXWeak(), 0);
    css::uno::Reference<css::document::XUndoManager> xUndoManager = xSupplier->getUndoManager();

    // Capturing by reference is safe here, and only here, because syncExecute
    // blocks until the functor has returned -- this frame outlives it. The
    // warning in threadex.hxx is about the asynchronous shape.
    const UndoAgentTopOutcome aOutcome = vcl::solarthread::syncExecute(
        [&xUndoManager, &sContextTitle, nMaxSteps] {
            return doUndoAgentTop(xUndoManager, sContextTitle, nMaxSteps);
        });

    const css::uno::Sequence<css::beans::NamedValue> aResult{
        { u"Undone"_ustr, css::uno::Any(aOutcome.nUndone) },
        { u"Refused"_ustr, css::uno::Any(aOutcome.bRefused) },
        { u"UndoStackCleared"_ustr, css::uno::Any(aOutcome.bStackCleared) },
        { u"Reason"_ustr, css::uno::Any(aOutcome.sReason) },
    };
    return css::uno::Any(aResult);
}

} // namespace

} // namespace officelabs

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface*
ai_officelabs_comp_OfficeLabsJob_get_implementation(
    css::uno::XComponentContext* pContext, css::uno::Sequence<css::uno::Any> const&)
{
    return cppu::acquire(new officelabs::OfficeLabsJob(pContext));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
