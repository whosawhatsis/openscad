#pragma once

#include <QList>
#include <QPair>
#include <QString>

/// Offset and length of one editable field within a shape's text.
using SnippetField = QPair<int, int>;

/**
 * @brief The editable fields of an inserted call shape, in source order.
 *
 * A field is a value written at argument position - immediately after `(`, `[`,
 * `,` or `=`, ignoring spaces. Requiring a delimiter is what keeps the `1` in
 * `cylinder(h=1,r1=1)`'s parameter name `r1` from being mistaken for a value.
 *
 * Only numbers, `true`/`false` and string literals count. That is enough for the
 * curated shapes in core/ArgumentShapes.cc, which is the only text this is ever
 * asked about - it is not a general OpenSCAD expression scanner.
 */
QList<SnippetField> shapeFieldRanges(const QString& shape);

/**
 * @brief The same call with a space after every comma.
 *
 * Autocompletion menu entries cannot contain spaces - Scintilla treats the first
 * one as the end of the inserted word - so shapes are stored and displayed
 * space-free and spaced out again when they land in the document. Commas inside
 * string literals are left alone.
 */
QString spacedAfterCommas(const QString& call);
