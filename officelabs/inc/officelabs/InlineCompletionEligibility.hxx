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
#include <string_view>

namespace officelabs {

/// The context around the caret, as read by DocumentController.
///
/// The two sides are not symmetric, deliberately. @p textBefore reaches back
/// across earlier paragraphs because earlier prose is what a completion is
/// predicted from. After the caret there are *two* different questions, and
/// one field cannot answer both:
///
///  - "may I complete here at all?" is paragraph-local -- ghost text is only
///    offered at the end of the caret's own paragraph. That is @p textAfter,
///    and it is what isEligible() and stillValid() read.
///  - "what follows, for the model to condition on?" reaches forward across
///    paragraphs. That is @p textAfterContext, and it only goes on the wire.
///
/// Collapsing the two is a live defect, not a style point: gating on the wide
/// field leaves only the document's last non-blank paragraph eligible, because
/// any following paragraph that has text in it makes the whole forward window
/// non-whitespace
/// ([fork#75](https://github.com/Pher217/libreoffice-fork/pull/75)).
///
/// @p textAfterContext is declared last so that an aggregate initialiser that
/// predates it still compiles and leaves it empty. That direction is safe -- a
/// caller that forgets it sends the model less context. The reverse ordering
/// would leave the *gate* empty and fire ghost text mid-paragraph.
struct CursorContext
{
    OUString textBefore;
    OUString textAfter;
    bool hasSelection = false;
    bool readOnly = false;
    OUString textAfterContext;
};

/// True iff a completion should be requested for this context: no selection,
/// not read-only, the text after the caret *in its own paragraph* is empty or
/// whitespace-only, and there are at least 10 characters before the caret.
///
/// Reads @p textAfter, never @p textAfterContext -- see CursorContext.
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

/// Interprets the per-user inline-completion toggle file: ON by default
/// (file missing), OFF only when the trimmed ASCII content equals "off"
/// case-insensitively, and ON for any other readable content.
OFFICELABS_DLLPUBLIC bool isInlineCompletionEnabledValue(std::string_view aFileContent, bool bFileExists);

/// Builds the JSON body for POST /completions/: text_before (capped to its
/// last 2000 characters), text_after, mode and max_suggestions.
///
/// text_after is taken from @p textAfterContext -- the forward *context*, not
/// the eligibility gate. Sending the gate field instead would always put a
/// whitespace-only string on the wire, since isEligible() has just required
/// exactly that of it.
OFFICELABS_DLLPUBLIC OString buildCompletionRequest(const CursorContext& rContext);

/// Reads suggestions[0].text from a completion response body. Returns an
/// empty string on malformed JSON, a missing/empty suggestions array, or a
/// missing text field.
OFFICELABS_DLLPUBLIC OUString parseFirstSuggestion(const std::string& rBody);

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_INLINECOMPLETIONELIGIBILITY_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
