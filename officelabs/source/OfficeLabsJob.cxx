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
        catch (const css::uno::Exception& rEx)
        {
            // Letting this propagate would replace the count with an exception,
            // and the count is the part the caller cannot reconstruct: after a
            // throw on step 3 of 5 the document has moved and the agent has no
            // way to learn how far. Report the partial count and the reason
            // instead -- a half-done undo the caller knows about beats a
            // half-done undo it does not.
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
// read the top title, then undo -- leave a window on the VCL thread in which
// the stack can change between them; one call closes that window.
//
// WHY THIS HOPS TO THE MAIN THREAD
// The call always arrives over URP, on a thread that is not the solar thread.
// Undo is not a data-structure operation: reverting a Writer action runs view
// code -- invalidation, cursor and selection movement, scrolling -- which ends
// in VCL and, on macOS, in AppKit. AppKit off the main thread is undefined,
// which is why framework marshals its own dispatches the same way when a
// caller asks for it (framework/source/services/dispatchhelper.cxx:115-119,
// frame.cxx:579-590, both keyed on the OnMainThread descriptor property).
// Holding the SolarMutex from a foreign thread is necessary for that work but
// it is not sufficient, and this component had only the mutex.
//
// Second, the undo helper documents a precondition the mutex-only version
// cannot honour. impl_doUndoRedo() opens with
//
//     ::osl::Guard< ::framework::IMutex > aExternalGuard( i_externalLock.getGuardedMutex() );
//         // note that this assumes that the mutex has been released in the
//         // thread which added the Undo/Redo request, so we can successfully
//         // acquire it
//
// (framework/source/fwe/helper/undomanagerhelper.cxx:622-626). undo() does not
// run the work inline: impl_processRequest() queues it, calls
// i_instanceLock.clear() -- which drops DocumentUndoManager's own
// UndoManagerGuard but not an outer guard of ours, the SolarMutex being
// recursive -- and, if another thread is already draining the queue, blocks in
// pRequest->wait() (ibid.:479) with our guard still held. The draining thread
// then wants the mutex we are sitting on.
//
// Be precise about that second one: it needs contention to bite. With no other
// thread in the queue we drain it ourselves and the recursive mutex makes it
// work, which is why every main-thread cppunit test below passes either way.
// Running on the solar thread does not make the contended case impossible --
// the main thread can wait on the queue holding the mutex too. What it does is
// stop us being a special case: the sequence now executes in exactly the
// position Edit > Undo executes in, so it inherits whatever guarantees ship
// with that path instead of resting on an untested assumption about a foreign
// thread's recursive lock.
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
