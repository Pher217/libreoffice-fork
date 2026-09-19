/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <officelabs/InlineCompletionEligibility.hxx>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <rtl/character.hxx>
#include <rtl/strbuf.hxx>

#include <cctype>
#include <cstdio>
#include <sstream>

namespace officelabs {

namespace {

const sal_Int32 MIN_CHARS_BEFORE = 10;
const sal_Int32 MAX_TEXT_BEFORE_CHARS = 2000;

/// Escape an already-UTF-8-encoded byte string for embedding in the one JSON
/// body we send. Non-ASCII bytes (part of a multi-byte UTF-8 sequence) are
/// passed through unescaped, which is valid inside a JSON string.
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
                {
                    char aHex[5];
                    std::snprintf(aHex, sizeof(aHex), "%04x",
                                  static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    aOut.append("\\u").append(aHex);
                }
                else
                    aOut.append(c);
        }
    }
    return aOut.makeStringAndClear();
}

} // namespace

bool isEligible(const CursorContext& rContext)
{
    if (rContext.hasSelection || rContext.readOnly)
        return false;
    if (rContext.textBefore.getLength() < MIN_CHARS_BEFORE)
        return false;
    return rContext.textAfter.trim().isEmpty();
}

bool stillValid(const CursorContext& rRequested, const CursorContext& rCurrent)
{
    return rRequested.textBefore == rCurrent.textBefore
        && rRequested.textAfter == rCurrent.textAfter
        && isEligible(rCurrent);
}

OUString sanitizeSuggestion(const OUString& rSuggestion)
{
    sal_Int32 nCut = rSuggestion.getLength();
    const sal_Int32 nNewline = rSuggestion.indexOf('\n');
    if (nNewline >= 0 && nNewline < nCut)
        nCut = nNewline;
    const sal_Int32 nCarriageReturn = rSuggestion.indexOf('\r');
    if (nCarriageReturn >= 0 && nCarriageReturn < nCut)
        nCut = nCarriageReturn;

    const OUString sCut = rSuggestion.copy(0, nCut);
    if (sCut.trim().isEmpty())
        return OUString();
    return sCut;
}

bool isInlineCompletionEnabledValue(std::string_view aFileContent, bool bFileExists)
{
    if (!bFileExists)
        return true;

    const auto aEnd = aFileContent.end();
    auto it = aFileContent.begin();
    while (it != aEnd && static_cast<unsigned char>(*it) <= ' ')
        ++it;
    auto aTail = aEnd;
    while (aTail != it && static_cast<unsigned char>(aTail[-1]) <= ' ')
        --aTail;

    constexpr std::string_view kOff = "off";
    if (static_cast<std::size_t>(aTail - it) != kOff.size())
        return true;
    for (std::size_t i = 0; i < kOff.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(it[i])) != kOff[i])
            return true;
    }
    return false;
}

OString buildCompletionRequest(const CursorContext& rContext)
{
    OUString sBefore = rContext.textBefore.getLength() > MAX_TEXT_BEFORE_CHARS
        ? rContext.textBefore.copy(rContext.textBefore.getLength() - MAX_TEXT_BEFORE_CHARS)
        : rContext.textBefore;

    if (!sBefore.isEmpty()
        && rtl::isLowSurrogate(static_cast<sal_Unicode>(sBefore[0])))
    {
        sBefore = sBefore.copy(1);
    }

    OStringBuffer aBuf;
    aBuf.append("{\"text_before\":\"").append(jsonEscape(sBefore.toUtf8()));
    aBuf.append("\",\"text_after\":\"").append(jsonEscape(rContext.textAfterContext.toUtf8()));
    aBuf.append("\",\"mode\":\"inline\",\"max_suggestions\":1}");
    return aBuf.makeStringAndClear();
}

OUString parseFirstSuggestion(const std::string& rBody)
{
    boost::property_tree::ptree aTree;
    try
    {
        std::istringstream aStream(rBody);
        boost::property_tree::read_json(aStream, aTree);
    }
    catch (const std::exception&)
    {
        return OUString();
    }

    const auto aSuggestions = aTree.get_child_optional("suggestions");
    if (!aSuggestions || aSuggestions->empty())
        return OUString();

    const std::string aText = aSuggestions->begin()->second.get<std::string>("text", "");
    if (aText.empty())
        return OUString();
    return OUString::fromUtf8(std::string_view(aText.data(), aText.size()));
}

} // namespace officelabs

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
