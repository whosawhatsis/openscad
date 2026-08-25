#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "gui/CompletionItem.h"

class SourceFile;
class UserModule;

/**
 * @brief Whether a user-defined module can take children.
 *
 * True when its own body directly invokes children()/child(), or references
 * $children. Uses inside conditionals and nested scopes count; nested module
 * declarations and transitive calls to other modules do not.
 */
bool moduleAcceptsChildren(const UserModule& module);

/**
 * @brief Typed completion items for the symbols a source file declares.
 */
QList<CompletionItem> userCompletionItems(const SourceFile& file);

/**
 * @brief Parameter names documented by a builtin's calltips.
 *
 * Calltips are documentation, not signatures, so this only takes what is
 * unambiguous: a bare identifier at argument level, or the name half of
 * "name = value". A bracketed argument such as "[width, depth, height]"
 * describes the contents of a vector and yields nothing - harvesting those
 * words would offer arguments that do not exist. Overloads contribute their
 * union, in first-seen order.
 */
QStringList parameterNamesFromCalltips(const QStringList& calltips);

/**
 * @brief Parameter names of a module or function declared in this file.
 */
QStringList parameterNamesOf(const SourceFile& file, const QString& callable);
