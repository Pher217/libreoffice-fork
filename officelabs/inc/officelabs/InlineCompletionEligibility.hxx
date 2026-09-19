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

/// The caret's paragraph-local context, as read by DocumentController.
struct CursorContext
{
    OUString textBefore;
    OUString textAfter;
    bool hasSelection = false;
    bool readOnly = false;
    /// Text FOLLOWING the caret, reaching across paragraphs -- what the model
    /// conditions on. Read only by buildCompletionRequest(); the gate never
    /// sees it.
    ///
    /// This exists because the two questions asked after the caret are not the
    /// same question. "May I complete here?" is paragraph-local and is
    /// @p textAfter. "What follows, for the model?" is not, and putting it in
    /// @p textAfter is what disabled ghost text everywhere but the document's
    /// last non-blank paragraph (fork#75, fixed by fork#78 -- whose comment in
    /// DocumentController.cxx asks for exactly this separate field).
    ///
    /// Declared LAST on purpose. Every existing 4-element aggregate initialiser
    /// still compiles and leaves this empty, which costs the model context.
    /// Ordering it before the bools would leave the *gate* empty instead and
    /// fire ghost text mid-paragraph -- so the forgetful caller fails safe.
    OUString textAfterContext;
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

/// Interprets the per-user inline-completion toggle file: ON by default
/// (file missing), OFF only when the trimmed ASCII content equals "off"
/// case-insensitively, and ON for any other readable content.
OFFICELABS_DLLPUBLIC bool isInlineCompletionEnabledValue(std::string_view aFileContent, bool bFileExists);

/// Builds the JSON body for POST /completions/: text_before (capped to its
/// last 2000 characters), text_after, mode and max_suggestions.
///
/// text_after comes from @p textAfterContext, not @p textAfter. Reading the
/// gate field here would always put a whitespace-only string on the wire,
/// because isEligible() has just required exactly that of it -- which is why
/// the agent never saw trailing context before this field existed
/// (officelabs-agent#336).
OFFICELABS_DLLPUBLIC OString buildCompletionRequest(const CursorContext& rContext);

/// Reads suggestions[0].text from a completion response body. Returns an
/// empty string on malformed JSON, a missing/empty suggestions array, or a
/// missing text field.
OFFICELABS_DLLPUBLIC OUString parseFirstSuggestion(const std::string& rBody);

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_INLINECOMPLETIONELIGIBILITY_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
