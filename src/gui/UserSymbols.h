#pragma once

#include <QList>

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
