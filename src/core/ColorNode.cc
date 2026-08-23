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

#include "core/ColorNode.h"

#include <algorithm>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/assign/std/vector.hpp>
#include <cctype>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "core/Builtins.h"
#include "core/Children.h"
#include "core/ColorUtil.h"
#include "core/ModuleInstantiation.h"
#include "Feature.h"
#include "core/Material.h"
#include "core/Parameters.h"
#include "core/module.h"
#include "geometry/linalg.h"
#include "utils/printutils.h"

using namespace boost::assign;  // bring 'operator+=()' into scope

static std::shared_ptr<AbstractNode> builtin_color_impl(const ModuleInstantiation *inst,
                                                        Arguments arguments, const Children& children,
                                                        bool isMaterial)
{
  auto node = std::make_shared<ColorNode>(inst);
  node->isMaterial = isMaterial;

  Parameters parameters =
    Parameters::parse(std::move(arguments), inst->location(),
                      isMaterial ? std::vector<std::string>{"name", "c", "alpha", "roughness"}
                                 : std::vector<std::string>{"c", "alpha", "roughness"});
  if (parameters["c"].type() == Value::Type::VECTOR) {
    const auto& vec = parameters["c"].toVector();
    Vector4f color;
    for (size_t i = 0; i < 4; ++i) {
      color[i] = i < vec.size() ? (float)vec[i].toDouble() : 1.0f;
      if (color[i] > 1 || color[i] < 0) {
        LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
            "color() expects numbers between 0.0 and 1.0. Value of %1$.1f is out of range", color[i]);
      }
    }
    node->color = color;
  } else if (parameters["c"].type() == Value::Type::STRING) {
    auto colorname = parameters["c"].toString();
    const auto parsed_color = OpenSCAD::parse_color(colorname);
    if (parsed_color) {
      node->color = *parsed_color;
    } else {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "Unable to parse color \"%1$s\"", colorname);
      LOG(message_group::HtmlLink,
          "For a list of valid color names, see the <a href=\"open-window://colorlist\"><b>Color "
          "List</b></a> window.");
    }
  }
  if (parameters["alpha"].type() == Value::Type::NUMBER) {
    node->color.setAlpha(parameters["alpha"].toDouble());
    if (node->color.a() < 0.0f || node->color.a() > 1.0f) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "color() expects alpha between 0.0 and 1.0. Value of %1$.1f is out of range", node->color.a());
    }
  }
  const char *const moduleName = isMaterial ? "material" : "color";

  if (parameters["finish"].isDefined()) {
    const auto& value = parameters["finish"];
    if (value.type() == Value::Type::NUMBER) {
      // A bare scalar is the scale; strength defaults to 1 and seed to 0. This
      // spelling is only safe because the attribute is not called "roughness" --
      // there, a scalar means the PBR microfacet value instead.
      const double scale = value.toDouble();
      if (scale <= 0.0) {
        LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
            "%1$s() finish scale must be greater than 0, got %2$.6g", moduleName, scale);
      } else {
        node->finish = Vector3d{scale, 1.0, 0.0};
        node->hasFinish = true;
      }
    } else if (value.type() != Value::Type::VECTOR) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "%1$s() finish must be a scale, or a vector [scale, strength] or "
          "[scale, strength, seed]",
          moduleName);
    } else {
      const auto& vec = value.toVector();
      if (vec.size() < 2 || vec.size() > 3) {
        LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
            "%1$s() finish expects 2 or 3 values [scale, strength, seed], got %2$d", moduleName,
            (int)vec.size());
      } else {
        Vector3d finish{0.0, 1.0, 0.0};
        bool ok = true;
        for (size_t i = 0; i < vec.size(); ++i) {
          if (vec[i].type() != Value::Type::NUMBER) {
            LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
                "%1$s() finish values must be numbers", moduleName);
            ok = false;
            break;
          }
          finish[i] = vec[i].toDouble();
        }
        if (ok && finish[0] <= 0.0) {
          LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
              "%1$s() finish scale must be greater than 0, got %2$.6g", moduleName, finish[0]);
          ok = false;
        }
        if (ok) {
          node->finish = finish;
          node->hasFinish = true;
        }
      }
    }
  }

  // Conventional scalar PBR attributes. Deliberately scalars: the vector spelling
  // belongs to finish, so the two can never be confused for one another.
  struct PbrParam {
    const char *name;
    double *target;
    bool *flag;
  };
  const PbrParam pbrParams[] = {
    {"roughness", &node->pbrRoughness, &node->hasPbrRoughness},
    {"metallic", &node->metallic, &node->hasMetallic},
  };
  for (const auto& param : pbrParams) {
    if (!parameters[param.name].isDefined()) continue;
    const auto& value = parameters[param.name];
    if (value.type() != Value::Type::NUMBER) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "%1$s() %2$s must be a number between 0.0 and 1.0", moduleName, param.name);
      continue;
    }
    double v = value.toDouble();
    if (v < 0.0 || v > 1.0) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "%1$s() %2$s expects a number between 0.0 and 1.0. Value of %3$.1f is out of range",
          moduleName, param.name, v);
      v = std::clamp(v, 0.0, 1.0);
    }
    *param.target = v;
    *param.flag = true;
  }

  if (isMaterial && parameters["name"].type() == Value::Type::STRING) {
    const auto& name = parameters["name"].toString();
    if (Material::isValidName(name)) {
      node->materialName = name;
    } else {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "material() name must start and end with an ASCII letter or digit and contain only "
          "letters, digits, '.', '-' or '_'");
    }
  }

  return children.instantiate(node);
}

static std::shared_ptr<AbstractNode> builtin_color(const ModuleInstantiation *inst, Arguments arguments,
                                                   const Children& children)
{
  return builtin_color_impl(inst, std::move(arguments), children, false);
}

static std::shared_ptr<AbstractNode> builtin_material(const ModuleInstantiation *inst,
                                                      Arguments arguments, const Children& children)
{
  return builtin_color_impl(inst, std::move(arguments), children, true);
}

std::string ColorNode::toString() const
{
  // Emitted only when set, so dumps of scripts that use none of these stay
  // byte-identical; finish is normalised to its three-element form so that the
  // geometry cache key (which is this string) is canonical.
  std::string attrs;
  if (hasFinish) {
    attrs += STR(", finish = [", this->finish[0], ", ", this->finish[1], ", ", this->finish[2], "]");
  }
  if (hasPbrRoughness) {
    attrs += STR(", roughness = ", this->pbrRoughness);
  }
  if (hasMetallic) {
    attrs += STR(", metallic = ", this->metallic);
  }
  if (isMaterial) {
    return STR("material([", this->color.r(), ", ", this->color.g(), ", ", this->color.b(), ", ",
               this->color.a(), "], name = \"", materialName, "\"", attrs, ")");
  }
  return STR("color([", this->color.r(), ", ", this->color.g(), ", ", this->color.b(), ", ",
             this->color.a(), "]", attrs, ")");
}

std::string ColorNode::name() const
{
  return isMaterial ? "material" : "color";
}

void register_builtin_color()
{
  Builtins::init("color", new BuiltinModule(builtin_color),
                 {
                   "color(c = [r, g, b, a])",
                   "color(c = [r, g, b], alpha = 1.0)",
                   "color(\"#hexvalue\")",
                   "color(\"colorname\", 1.0)",
                   "color(c = [r, g, b], finish = [scale, strength, seed])",
                   "color(c = [r, g, b], roughness = 0.5, metallic = 0.0)",
                 });
  Builtins::init("material", new BuiltinModule(builtin_material, &Feature::ExperimentalMultiMaterial),
                 {
                   "material(\"name\")",
                   "material(\"name\", c = [r, g, b, a])",
                   "material(\"name\", c = [r, g, b], alpha = 1.0)",
                   "material(\"name\", \"colorname\", 1.0)",
                   "material(\"name\", c = [r, g, b], finish = [scale, strength, seed])",
                   "material(\"name\", c = [r, g, b], roughness = 0.5, metallic = 0.0)",
                 });
}
