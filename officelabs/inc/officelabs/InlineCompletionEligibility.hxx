/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Pure logic for the inline ghost-text completion feature (spec #15).
 *
 * Deliberately free of UNO/VCL: the eligibility rules, request/response JSON
 * shaping and staleness check are cheap to unit-test in isolation, and the
 * key handler that calls into UNO stays a thin caller of these.
 */

#ifndef INCLUDED_OFFICELABS_INLINECOMPLETIONELIGIBILITY_HXX
#define INCLUDED_OFFICELABS_INLINECOMPLETIONELIGIBILITY_HXX

#include <officelabs/officelabsdllapi.h>
#include <rtl/string.hxx>
#include <rtl/ustring.hxx>

#include <string>

namespace officelabs {

/// The caret's paragraph-local context, as read by DocumentController.
struct CursorContext
{
    OUString textBefore;
    OUString textAfter;
    bool hasSelection = false;
    bool readOnly = false;
};

/// True iff a completion should be requested for this context: no selection,
/// not read-only, the paragraph text after the caret is empty or
/// whitespace-only, and there are at least 10 characters before the caret.
OFFICELABS_DLLPUBLIC bool isEligible(const CursorContext& rContext);

/// True iff a suggestion requested against @p rRequested is still safe to
/// show given the cursor is now at @p rCurrent: the text on both sides of the
/// caret is unchanged, and the current context is still eligible.
OFFICELABS_DLLPUBLIC bool stillValid(const CursorContext& rRequested,
                                     const CursorContext& rCurrent);

/// Cuts a raw suggestion at the first newline (the feature is single-line),
/// and collapses a whitespace-only result to empty. A leading space is kept
/// as returned by the agent -- only whitespace-only results are stripped.
OFFICELABS_DLLPUBLIC OUString sanitizeSuggestion(const OUString& rSuggestion);

/// Builds the JSON body for POST /completions/: text_before (capped to its
/// last 2000 characters), text_after, mode and max_suggestions.
OFFICELABS_DLLPUBLIC OString buildCompletionRequest(const CursorContext& rContext);

/// Reads suggestions[0].text from a completion response body. Returns an
/// empty string on malformed JSON, a missing/empty suggestions array, or a
/// missing text field.
OFFICELABS_DLLPUBLIC OUString parseFirstSuggestion(const std::string& rBody);

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_INLINECOMPLETIONELIGIBILITY_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
