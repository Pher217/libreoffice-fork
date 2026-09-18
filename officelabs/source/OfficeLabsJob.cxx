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

#include <cppuhelper/implbase.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <cppuhelper/weak.hxx>

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

    throw css::lang::IllegalArgumentException(
        "OfficeLabsJob: unknown Operation \"" + sOperation + "\"",
        getXWeak(), 0);
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
