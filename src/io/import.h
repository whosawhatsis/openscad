#pragma once

#include <boost/optional.hpp>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "core/AST.h"
#include "core/CurveDiscretizer.h"

std::unique_ptr<class PolySet> import_stl(const std::string& filename, const Location& loc);
std::unique_ptr<class PolySet> import_obj(const std::string& filename, const Location& loc);
std::unique_ptr<class PolySet> import_off(const std::string& filename, const Location& loc);
// An optional parts output bypasses mesh-kernel unions for native BREP import.
std::unique_ptr<class PolySet> import_amf(const std::string&, const Location& loc,
                                          std::vector<std::unique_ptr<class PolySet>> *parts = nullptr);
std::unique_ptr<class PolySet> import_3mf(const std::string&, const Location& loc,
                                          std::vector<std::unique_ptr<class PolySet>> *parts = nullptr);

using SvgBezierContours = std::vector<std::vector<std::vector<std::array<double, 3>>>>;
std::unique_ptr<class Polygon2d> import_svg(CurveDiscretizer discretizer, const std::string& filename,
                                            const boost::optional<std::string>& id,
                                            const boost::optional<std::string>& layer, const double dpi,
                                            const bool center, const Location& loc,
                                            std::vector<SvgBezierContours> *curves = nullptr);

#ifdef ENABLE_CGAL
std::unique_ptr<class CGALNefGeometry> import_nef3(const std::string& filename, const Location& loc);
#endif

class Value import_json(const std::string& filename, class EvaluationSession *session,
                        const Location& loc);
