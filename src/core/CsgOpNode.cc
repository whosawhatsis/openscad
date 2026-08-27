/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "core/CsgOpNode.h"

#include <cassert>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "core/Builtins.h"
#include "core/Children.h"
#include "core/ModuleInstantiation.h"
#include "core/Parameters.h"
#include "core/enums.h"
#include "core/module.h"
#include "utils/printutils.h"

namespace {

std::shared_ptr<CsgOpNode> createCsgOpNode(const ModuleInstantiation *inst, Arguments arguments,
                                           OpenSCADOperator type)
{
  const Parameters parameters = Parameters::parse(std::move(arguments), inst->location(), {"fillet"});
  const Value& fillet = parameters["fillet"];
  double radius = 0.0;

  if (!fillet.isDefined()) {
    return std::make_shared<CsgOpNode>(inst, type);
  }
  if (!fillet.getFiniteDouble(radius)) {
    LOG(message_group::Error, inst->location(), parameters.documentRoot(),
        "fillet radius must be a finite number");
    radius = 0.0;
  } else if (radius < 0.0) {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
        "negative fillet radius treated as zero");
    radius = 0.0;
  }

  return std::make_shared<CsgOpNode>(inst, type, radius, true);
}

}  // namespace

static std::shared_ptr<AbstractNode> builtin_union(const ModuleInstantiation *inst, Arguments arguments,
                                                   const Children& children)
{
  return children.instantiate(createCsgOpNode(inst, std::move(arguments), OpenSCADOperator::UNION));
}

static std::shared_ptr<AbstractNode> builtin_difference(const ModuleInstantiation *inst,
                                                        Arguments arguments, const Children& children)
{
  return children.instantiate(createCsgOpNode(inst, std::move(arguments), OpenSCADOperator::DIFFERENCE));
}

static std::shared_ptr<AbstractNode> builtin_intersection(const ModuleInstantiation *inst,
                                                          Arguments arguments, const Children& children)
{
  return children.instantiate(
    createCsgOpNode(inst, std::move(arguments), OpenSCADOperator::INTERSECTION));
}

std::string CsgOpNode::toString() const
{
  std::ostringstream stream;
  stream << this->name() << "(";
  if (this->hasFillet) stream << "fillet = " << this->filletRadius;
  stream << ")";
  return stream.str();
}

std::string CsgOpNode::name() const
{
  switch (this->type) {
  case OpenSCADOperator::UNION:        return "union"; break;
  case OpenSCADOperator::DIFFERENCE:   return "difference"; break;
  case OpenSCADOperator::INTERSECTION: return "intersection"; break;
  default:                             assert(false);
  }
  return "internal_error";
}

void register_builtin_csgops()
{
  Builtins::init("union", new BuiltinModule(builtin_union),
                 {
                   "union(fillet = number)",
                 });

  Builtins::init("difference", new BuiltinModule(builtin_difference),
                 {
                   "difference(fillet = number)",
                 });

  Builtins::init("intersection", new BuiltinModule(builtin_intersection),
                 {
                   "intersection(fillet = number)",
                 });
}
