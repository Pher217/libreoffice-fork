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
    // compare-and-undo: that operation must take a single SolarMutexGuard
    // for the whole check-and-act sequence, not one guard per step. Separate
    // UNO round trips (one call to check, another to act) cannot close the
    // window between the two -- the document can change on the VCL thread
    // in between -- which is the entire reason this exists as one execute()
    // call instead of two.
    if (sOperation == "ping")
        return css::uno::Any(u"ok"_ustr);

    if (sOperation == "undo_agent_top")
        return executeUndoAgentTop(rArguments);

    throw css::lang::IllegalArgumentException(
        "OfficeLabsJob: unknown Operation \"" + sOperation + "\"",
        getXWeak(), 0);
}

// Undoes entries at the top of the agent's undo stack that belong to a named
// undo context, in a single execute() call. DocumentUndoManager::undo() takes
// its own UndoManagerGuard per call (sfx2/source/doc/docundomanager.cxx), so
// two separate UNO round trips -- read the top title, then undo -- leave a
// window on the VCL thread in which the stack can change between them. One
// SolarMutexGuard held for the whole check-and-act loop closes that window.
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
    // Reject rather than default. MaxSteps left at 0 makes the loop below a
    // silent no-op -- the caller asks to undo and nothing happens, with a
    // success result. A dispatcher whose failure mode is "quietly did nothing"
    // is the thing this design set out not to be.
    if (nMaxSteps <= 0)
        throw css::lang::IllegalArgumentException(
            "OfficeLabsJob: undo_agent_top requires MaxSteps >= 1", getXWeak(), 0);

    css::uno::Reference<css::document::XUndoManagerSupplier> xSupplier(xModel,
                                                                        css::uno::UNO_QUERY);
    if (!xSupplier.is())
        throw css::lang::IllegalArgumentException(
            "OfficeLabsJob: Model does not support XUndoManagerSupplier", getXWeak(), 0);
    css::uno::Reference<css::document::XUndoManager> xUndoManager = xSupplier->getUndoManager();

    SolarMutexGuard aGuard;

    sal_Int32 nUndone = 0;
    bool bRefused = false;
    OUString sReason;

    while (nUndone < nMaxSteps)
    {
        // isUndoPossible() is false for both an empty stack and an open undo
        // context; either way there is nothing left here for us to undo.
        if (!xUndoManager->isUndoPossible())
            break;

        const OUString sTitle = xUndoManager->getCurrentUndoActionTitle();
        if (sTitle != sContextTitle)
        {
            // Only the very first entry not matching is a refusal. Once we
            // have undone at least one of ours, running into someone else's
            // entry is just the natural end of our run, not an error.
            if (nUndone == 0)
            {
                bRefused = true;
                sReason = "top undo entry \"" + sTitle + "\" does not match ContextTitle \""
                          + sContextTitle + "\"";
            }
            break;
        }

        xUndoManager->undo();
        ++nUndone;
    }

    const css::uno::Sequence<css::beans::NamedValue> aResult{
        { u"Undone"_ustr, css::uno::Any(nUndone) },
        { u"Refused"_ustr, css::uno::Any(bRefused) },
        { u"Reason"_ustr, css::uno::Any(sReason) },
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
