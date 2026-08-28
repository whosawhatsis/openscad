/*
 *  OpenSCAD (www.openscad.org)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "geometry/PolySet.h"
#include "geometry/PolySetUtils.h"
#include "geometry/linalg.h"
#include "io/export.h"
#include "platform/PlatformUtils.h"
#include "utils/printutils.h"

namespace {

// ponytail: patch a frozen, dependency-closed Blender scene instead of reimplementing Blender's
// scene defaults. Blender authored every ID; the writer only clones owner-adjacent mesh data.
constexpr std::string_view BLEND_HEADER = "BLENDER17-01v0500";
constexpr uint64_t GENERATED_ADDRESS_BASE = 0x0100000000000000ULL;
constexpr size_t MAX_BLEND_FRAMES = 256;
constexpr size_t MAX_BLEND_OBJECTS = 1024;
constexpr size_t MAX_BLEND_MATERIALS = 1024;

template <typename T>
T readScalar(const std::vector<uint8_t>& bytes, size_t offset)
{
  if (offset + sizeof(T) > bytes.size()) throw std::runtime_error("Truncated Blender template");
  T value;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

template <typename T>
void writeScalar(std::vector<uint8_t>& bytes, size_t offset, T value)
{
  if (offset + sizeof(T) > bytes.size()) throw std::runtime_error("Invalid Blender field offset");
  std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

template <typename T>
void appendScalar(std::vector<uint8_t>& bytes, T value)
{
  const auto *begin = reinterpret_cast<const uint8_t *>(&value);
  bytes.insert(bytes.end(), begin, begin + sizeof(value));
}

std::string codeString(const std::array<char, 4>& code)
{
  return std::string(code.data(), std::find(code.begin(), code.end(), '\0'));
}

struct Field {
  uint16_t type;
  uint16_t name;
};

struct Structure {
  uint16_t type;
  std::vector<Field> fields;
};

class Dna
{
public:
  explicit Dna(const std::vector<uint8_t>& bytes)
  {
    size_t offset = 0;
    expect(bytes, offset, "SDNA");
    expect(bytes, offset, "NAME");
    names_ = readStrings(bytes, offset, readU32(bytes, offset));
    align(offset);
    expect(bytes, offset, "TYPE");
    types_ = readStrings(bytes, offset, readU32(bytes, offset));
    align(offset);
    expect(bytes, offset, "TLEN");
    lengths_.reserve(types_.size());
    for (size_t i = 0; i < types_.size(); ++i) {
      lengths_.push_back(readU16(bytes, offset));
    }
    align(offset);
    expect(bytes, offset, "STRC");
    const uint32_t count = readU32(bytes, offset);
    structures_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      Structure structure;
      structure.type = readU16(bytes, offset);
      const uint16_t fieldCount = readU16(bytes, offset);
      structure.fields.reserve(fieldCount);
      for (uint16_t field = 0; field < fieldCount; ++field) {
        structure.fields.push_back({readU16(bytes, offset), readU16(bytes, offset)});
      }
      structureByType_[types_.at(structure.type)] = structures_.size();
      structures_.push_back(std::move(structure));
    }
  }

  std::string_view typeName(int structure) const { return types_.at(structures_.at(structure).type); }

  size_t structureSize(int structure) const { return lengths_.at(structures_.at(structure).type); }

  int structureIndex(std::string_view type) const { return structureByType_.at(std::string(type)); }

  size_t fieldOffset(int structure, std::string_view path) const
  {
    const auto result = findField(structure, path, 0);
    if (!result) throw std::runtime_error("Missing Blender field: " + std::string(path));
    return *result;
  }

  std::vector<size_t> pointerOffsets(int structure) const
  {
    std::vector<size_t> offsets;
    collectPointers(structure, 0, offsets);
    return offsets;
  }

private:
  static void expect(const std::vector<uint8_t>& bytes, size_t& offset, std::string_view text)
  {
    if (offset + text.size() > bytes.size() ||
        std::memcmp(bytes.data() + offset, text.data(), text.size()) != 0) {
      throw std::runtime_error("Invalid Blender DNA");
    }
    offset += text.size();
  }

  static uint16_t readU16(const std::vector<uint8_t>& bytes, size_t& offset)
  {
    const auto value = readScalar<uint16_t>(bytes, offset);
    offset += sizeof(value);
    return value;
  }

  static uint32_t readU32(const std::vector<uint8_t>& bytes, size_t& offset)
  {
    const auto value = readScalar<uint32_t>(bytes, offset);
    offset += sizeof(value);
    return value;
  }

  static std::vector<std::string> readStrings(const std::vector<uint8_t>& bytes, size_t& offset,
                                              uint32_t count)
  {
    std::vector<std::string> strings;
    strings.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      const auto begin = offset;
      while (offset < bytes.size() && bytes[offset]) ++offset;
      if (offset == bytes.size()) throw std::runtime_error("Invalid Blender DNA string");
      strings.emplace_back(reinterpret_cast<const char *>(bytes.data() + begin), offset - begin);
      ++offset;
    }
    return strings;
  }

  static void align(size_t& offset) { offset = (offset + 3) & ~size_t(3); }

  static bool isPointer(std::string_view name) { return name.find('*') != std::string_view::npos; }

  static size_t arrayCount(std::string_view name)
  {
    size_t count = 1;
    for (size_t open = name.find('['); open != std::string_view::npos; open = name.find('[', open)) {
      const size_t close = name.find(']', open);
      if (close == std::string_view::npos) break;
      count *= std::stoul(std::string(name.substr(open + 1, close - open - 1)));
      open = close + 1;
    }
    return count;
  }

  static std::string cleanName(std::string_view name)
  {
    std::string result;
    bool inArray = false;
    for (const char c : name) {
      if (c == '*') continue;
      if (c == '[') inArray = true;
      if (!inArray) result += c;
      if (c == ']') inArray = false;
    }
    return result;
  }

  size_t fieldSize(const Field& field) const
  {
    const auto& name = names_.at(field.name);
    return (isPointer(name) ? size_t(8) : size_t(lengths_.at(field.type))) * arrayCount(name);
  }

  std::optional<size_t> findField(int structure, std::string_view path, size_t base) const
  {
    size_t offset = base;
    for (const auto& field : structures_.at(structure).fields) {
      const auto& encodedName = names_.at(field.name);
      const std::string name = cleanName(encodedName);
      if (path == name) return offset;
      const std::string prefix = name + ".";
      if (path.substr(0, prefix.size()) == prefix && !isPointer(encodedName) &&
          arrayCount(encodedName) == 1) {
        const auto nested = structureByType_.find(types_.at(field.type));
        if (nested != structureByType_.end()) {
          return findField(nested->second, path.substr(name.size() + 1), offset);
        }
      }
      offset += fieldSize(field);
    }
    return std::nullopt;
  }

  void collectPointers(int structure, size_t base, std::vector<size_t>& result) const
  {
    size_t offset = base;
    for (const auto& field : structures_.at(structure).fields) {
      const auto& name = names_.at(field.name);
      if (isPointer(name)) {
        for (size_t i = 0; i < arrayCount(name); ++i) result.push_back(offset + i * 8);
      } else if (arrayCount(name) == 1) {
        const auto nested = structureByType_.find(types_.at(field.type));
        if (nested != structureByType_.end()) collectPointers(nested->second, offset, result);
      }
      offset += fieldSize(field);
    }
  }

  std::vector<std::string> names_;
  std::vector<std::string> types_;
  std::vector<uint16_t> lengths_;
  std::vector<Structure> structures_;
  std::unordered_map<std::string, int> structureByType_;
};

struct Block {
  std::array<char, 4> code{};
  int32_t dna = 0;
  uint64_t old = 0;
  int64_t count = 0;
  std::vector<uint8_t> data;
};

class BlendScene
{
public:
  BlendScene(const fs::path& templatePath, const Color4f& defaultColor) : defaultColor_(defaultColor)
  {
    std::ifstream input(templatePath, std::ios::binary);
    if (!input) throw std::runtime_error("Can't open Blender export template: " + templatePath.string());
    bytes_ = std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
    if (bytes_.size() < BLEND_HEADER.size() ||
        std::memcmp(bytes_.data(), BLEND_HEADER.data(), BLEND_HEADER.size()) != 0) {
      throw std::runtime_error("Unsupported Blender export template");
    }

    size_t offset = BLEND_HEADER.size();
    while (offset + 32 <= bytes_.size()) {
      Block block;
      std::memcpy(block.code.data(), bytes_.data() + offset, 4);
      block.dna = readScalar<int32_t>(bytes_, offset + 4);
      block.old = readScalar<uint64_t>(bytes_, offset + 8);
      const int64_t length = readScalar<int64_t>(bytes_, offset + 16);
      block.count = readScalar<int64_t>(bytes_, offset + 24);
      offset += 32;
      if (length < 0 || offset + size_t(length) > bytes_.size()) {
        throw std::runtime_error("Invalid Blender block length");
      }
      block.data.assign(bytes_.begin() + offset, bytes_.begin() + offset + length);
      offset += length;
      blocks_.push_back(std::move(block));
      if (codeString(blocks_.back().code) == "ENDB") break;
    }

    rebuildIndex();
    const auto& dnaBlock = blockByCode("DNA1");
    dna_ = std::make_unique<Dna>(dnaBlock.data);
  }

  void setFrames(const std::vector<std::shared_ptr<const Geometry>>& frames, unsigned fps,
                 size_t maxSamples)
  {
    if (frames.empty()) throw std::runtime_error("Blender animation has no frames");
    setScene(frames.size(), fps);

    const size_t samples = std::min(frames.size(), maxSamples);
    for (size_t sample = 0; sample < samples; ++sample) {
      const size_t frame =
        samples == 1 ? 0 : (sample * (frames.size() - 1) + (samples - 1) / 2) / (samples - 1);
      const size_t nextFrame =
        sample + 1 == samples ? frames.size()
                              : ((sample + 1) * (frames.size() - 1) + (samples - 1) / 2) / (samples - 1);
      char objectName[32];
      std::snprintf(objectName, sizeof(objectName), "OBFrame %04zu", sample + 1);
      Block& object = blockByIdName(objectName);
      const uint64_t objectAddress = object.old;
      const uint64_t meshAddress =
        readScalar<uint64_t>(object.data, dna_->fieldOffset(object.dna, "data"));
      detachOwnedData(meshAddress);
      patchPackage(ownedPackage(objectAddress), frames[frame], std::nullopt);
      std::snprintf(objectName, sizeof(objectName), "ACFrame %04zu", sample + 1);
      patchVisibility(ownedPackage(blockByIdName(objectName).old), frame + 1, nextFrame + 1);
    }
  }

  void setObjectAnimation(const std::vector<UsdAnimationFrame>& frames, unsigned fps, size_t maxSamples)
  {
    std::vector<bool> stable(frames[0].objects.size(), true);
    for (size_t object = 0; object < stable.size(); ++object) {
      for (const auto& frame : frames) {
        stable[object] = stable[object] && frame.objects[object].geometry.get() ==
                                             frames[0].objects[object].geometry.get();
      }
    }

    size_t stableSlot = 0;
    for (size_t object = 0; object < stable.size(); ++object) {
      if (!stable[object]) continue;
      patchStableObject(++stableSlot, frames, object);
    }

    if (std::all_of(stable.begin(), stable.end(), [](bool value) { return value; })) {
      setScene(frames.size(), fps);
      hideFrameObjects();
      return;
    }

    std::vector<std::shared_ptr<const Geometry>> remeshed;
    remeshed.reserve(frames.size());
    for (const auto& frame : frames) {
      auto mesh = std::make_shared<PolySet>(3);
      for (size_t object = 0; object < stable.size(); ++object) {
        if (stable[object]) continue;
        const auto& source = *frame.objects[object].geometry;
        const int offset = mesh->vertices.size();
        for (const auto& vertex : source.vertices) {
          mesh->vertices.push_back(frame.objects[object].transform * vertex);
        }
        const int colorIndex = mesh->colors.size();
        mesh->colors.push_back(frame.objects[object].color);
        for (const auto& face : source.indices) {
          auto& destination = mesh->indices.emplace_back();
          for (const auto index : face) destination.push_back(offset + index);
          mesh->color_indices.push_back(colorIndex);
        }
      }
      remeshed.push_back(std::move(mesh));
    }
    setFrames(remeshed, fps, maxSamples);
  }

  void setCamera(const std::vector<UsdAnimationFrame>& frames)
  {
    if (frames.empty() || !frames[0].camera) return;
    std::vector<Vector3d> locations;
    std::vector<Eigen::Quaterniond> orientations;
    locations.reserve(frames.size());
    orientations.reserve(frames.size());
    for (const auto& frame : frames) {
      const Camera& camera = frame.camera.value_or(*frames[0].camera);
      const double radians = std::acos(-1.0) / 180.0;
      const Eigen::Matrix3d modelRotation =
        (Eigen::AngleAxisd(camera.object_rot.x() * radians, Vector3d::UnitX()) *
         Eigen::AngleAxisd(camera.object_rot.y() * radians, Vector3d::UnitY()) *
         Eigen::AngleAxisd(camera.object_rot.z() * radians, Vector3d::UnitZ()))
          .toRotationMatrix();
      Eigen::Matrix3d base;
      base.col(0) = Vector3d::UnitX();
      base.col(1) = Vector3d::UnitZ();
      base.col(2) = -Vector3d::UnitY();
      locations.push_back(camera.getVpt() +
                          modelRotation.transpose() * Vector3d(0, -camera.zoomValue(), 0));
      Eigen::Quaterniond orientation(modelRotation.transpose() * base);
      if (!orientations.empty() && orientations.back().dot(orientation) < 0) {
        orientation.coeffs() *= -1;
      }
      orientations.push_back(orientation);
    }

    Block& object = blockByIdName("OBOpenSCAD Camera");
    const size_t location = dna_->fieldOffset(object.dna, "loc");
    const size_t rotation = dna_->fieldOffset(object.dna, "quat");
    for (size_t component = 0; component < 3; ++component) {
      writeScalar<float>(object.data, location + component * sizeof(float), locations[0][component]);
    }
    writeScalar<float>(object.data, rotation, orientations[0].w());
    for (size_t component = 0; component < 3; ++component) {
      writeScalar<float>(object.data, rotation + (component + 1) * sizeof(float),
                         orientations[0].coeffs()[component]);
    }

    const uint64_t cameraAddress =
      readScalar<uint64_t>(object.data, dna_->fieldOffset(object.dna, "data"));
    Block& cameraData = blockByAddress(cameraAddress);
    const Camera& camera = *frames[0].camera;
    Block& scene = blockByCode("SC");
    writeScalar<int32_t>(scene.data, dna_->fieldOffset(scene.dna, "r.xsch"), camera.pixel_width);
    writeScalar<int32_t>(scene.data, dna_->fieldOffset(scene.dna, "r.ysch"), camera.pixel_height);
    writeScalar<int16_t>(scene.data, dna_->fieldOffset(scene.dna, "r.size"), 100);
    const float sensor = 36.0f;
    const float verticalFov = camera.fovValue() * std::acos(-1.0) / 180.0;
    writeScalar<int16_t>(cameraData.data, dna_->fieldOffset(cameraData.dna, "type"),
                         camera.projection == Camera::ProjectionType::ORTHOGONAL ? 1 : 0);
    writeScalar<int8_t>(cameraData.data, dna_->fieldOffset(cameraData.dna, "sensor_fit"), 2);
    writeScalar<float>(cameraData.data, dna_->fieldOffset(cameraData.dna, "sensor_y"), sensor);
    writeScalar<float>(cameraData.data, dna_->fieldOffset(cameraData.dna, "lens"),
                       sensor / (2 * std::tan(verticalFov / 2)));
    writeScalar<float>(cameraData.data, dna_->fieldOffset(cameraData.dna, "ortho_scale"),
                       2 * camera.zoomValue() * std::tan(verticalFov / 2));

    const auto package = ownedPackage(blockByIdName("ACOpenSCAD Camera").old);
    std::vector<uint64_t> curves;
    for (const uint64_t address : package) {
      if (dna_->typeName(blockByAddress(address).dna) == "FCurve") curves.push_back(address);
    }
    for (const uint64_t address : curves) {
      detachCurveKeys(address);
      Block& curve = blockByAddress(address);
      const uint64_t pathAddress =
        readScalar<uint64_t>(curve.data, dna_->fieldOffset(curve.dna, "rna_path"));
      const std::string path = rawString(blockByAddress(pathAddress));
      const int32_t component =
        readScalar<int32_t>(curve.data, dna_->fieldOffset(curve.dna, "array_index"));
      std::vector<float> values;
      if (path == "location") {
        for (const auto& value : locations) values.push_back(value[component]);
      } else if (path == "rotation_quaternion") {
        for (const auto& value : orientations) {
          values.push_back(component == 0 ? value.w() : value.coeffs()[component - 1]);
        }
      }
      if (!values.empty()) patchCurve(curve, values, 1);
    }
  }

  void write(std::ostream& output) const
  {
    output.write(BLEND_HEADER.data(), BLEND_HEADER.size());
    for (const auto& block : blocks_) {
      output.write(block.code.data(), block.code.size());
      output.write(reinterpret_cast<const char *>(&block.dna), sizeof(block.dna));
      output.write(reinterpret_cast<const char *>(&block.old), sizeof(block.old));
      const int64_t length = block.data.size();
      output.write(reinterpret_cast<const char *>(&length), sizeof(length));
      output.write(reinterpret_cast<const char *>(&block.count), sizeof(block.count));
      output.write(reinterpret_cast<const char *>(block.data.data()), block.data.size());
    }
  }

private:
  void setScene(size_t frames, unsigned fps)
  {
    Block& scene = blockByCode("SC");
    writeScalar<int32_t>(scene.data, dna_->fieldOffset(scene.dna, "r.sfra"), 1);
    writeScalar<int32_t>(scene.data, dna_->fieldOffset(scene.dna, "r.efra"), int32_t(frames));
    writeScalar<int16_t>(scene.data, dna_->fieldOffset(scene.dna, "r.frs_sec"), int16_t(fps));
  }

  Block& blockByCode(std::string_view code)
  {
    return *std::find_if(blocks_.begin(), blocks_.end(),
                         [&](const auto& block) { return codeString(block.code) == code; });
  }

  Block& blockByAddress(uint64_t address)
  {
    const auto found = indexByAddress_.find(address);
    if (found == indexByAddress_.end()) {
      throw std::runtime_error("Missing Blender block address " + std::to_string(address));
    }
    return blocks_.at(found->second);
  }

  const Block& blockByAddress(uint64_t address) const
  {
    const auto found = indexByAddress_.find(address);
    if (found == indexByAddress_.end()) {
      throw std::runtime_error("Missing Blender block address " + std::to_string(address));
    }
    return blocks_.at(found->second);
  }

  std::string idName(const Block& block) const
  {
    if (codeString(block.code).size() != 2) return {};
    const size_t offset = dna_->fieldOffset(block.dna, "id.name");
    const char *name = reinterpret_cast<const char *>(block.data.data() + offset);
    const char *end = std::find(name, name + std::min<size_t>(66, block.data.size() - offset), '\0');
    return std::string(name, end);
  }

  Block& blockByIdName(std::string_view name)
  {
    const auto found = std::find_if(blocks_.begin(), blocks_.end(),
                                    [&](const auto& block) { return idName(block) == name; });
    if (found == blocks_.end()) throw std::runtime_error("Missing Blender ID: " + std::string(name));
    return *found;
  }

  void rebuildIndex()
  {
    indexByAddress_.clear();
    for (size_t i = 0; i < blocks_.size(); ++i) {
      if (blocks_[i].old) indexByAddress_[blocks_[i].old] = i;
    }
  }

  std::vector<uint64_t> pointers(const Block& block) const
  {
    std::vector<uint64_t> values;
    if (block.data.empty()) return values;
    if (codeString(block.code) == "DATA" && block.dna == 0) {
      for (size_t offset = 0; offset + 8 <= block.data.size(); offset += 8) {
        const uint64_t value = readScalar<uint64_t>(block.data, offset);
        if (indexByAddress_.find(value) != indexByAddress_.end()) values.push_back(value);
      }
      return values;
    }
    const size_t structSize = dna_->structureSize(block.dna);
    if (!structSize) return values;
    for (int64_t item = 0; item < block.count; ++item) {
      for (const size_t relative : dna_->pointerOffsets(block.dna)) {
        const size_t offset = size_t(item) * structSize + relative;
        if (offset + 8 > block.data.size()) continue;
        const uint64_t value = readScalar<uint64_t>(block.data, offset);
        if (indexByAddress_.find(value) != indexByAddress_.end()) values.push_back(value);
      }
    }
    return values;
  }

  std::set<uint64_t> ownedPackage(uint64_t root) const
  {
    std::set<uint64_t> result;
    std::queue<uint64_t> pending;
    pending.push(root);
    while (!pending.empty()) {
      const uint64_t address = pending.front();
      pending.pop();
      if (!result.insert(address).second) continue;
      for (const uint64_t target : pointers(blockByAddress(address))) {
        const std::string code = codeString(blockByAddress(target).code);
        if (code == "DATA" || code == "ME") pending.push(target);
      }
    }
    return result;
  }

  void detachOwnedData(uint64_t ownerAddress)
  {
    auto package = ownedPackage(ownerAddress);
    package.erase(ownerAddress);
    std::unordered_map<uint64_t, uint64_t> remap;
    for (const uint64_t address : package) remap[address] = nextAddress();

    std::vector<Block> clones;
    clones.reserve(package.size());
    for (const uint64_t address : package) {
      Block clone = blockByAddress(address);
      clone.old = remap.at(address);
      patchPointers(clone, remap);
      clones.push_back(std::move(clone));
    }
    patchPointers(blockByAddress(ownerAddress), remap);
    const auto owner = std::find_if(blocks_.begin(), blocks_.end(),
                                    [&](const auto& block) { return block.old == ownerAddress; });
    blocks_.insert(std::next(owner), std::make_move_iterator(clones.begin()),
                   std::make_move_iterator(clones.end()));
    rebuildIndex();
  }

  void patchPointers(Block& block, const std::unordered_map<uint64_t, uint64_t>& remap)
  {
    std::vector<size_t> offsets;
    if (codeString(block.code) == "DATA" && block.dna == 0) {
      for (size_t offset = 0; offset + 8 <= block.data.size(); offset += 8) offsets.push_back(offset);
    } else {
      const size_t structSize = dna_->structureSize(block.dna);
      for (int64_t item = 0; item < block.count; ++item) {
        for (const size_t relative : dna_->pointerOffsets(block.dna)) {
          offsets.push_back(size_t(item) * structSize + relative);
        }
      }
    }
    for (const size_t offset : offsets) {
      if (offset + 8 > block.data.size()) continue;
      const auto found = remap.find(readScalar<uint64_t>(block.data, offset));
      if (found != remap.end()) writeScalar<uint64_t>(block.data, offset, found->second);
    }
  }

  uint64_t nextAddress()
  {
    while (indexByAddress_.find(GENERATED_ADDRESS_BASE + nextGeneratedAddress_ * 16) !=
           indexByAddress_.end()) {
      ++nextGeneratedAddress_;
    }
    return GENERATED_ADDRESS_BASE + nextGeneratedAddress_++ * 16;
  }

  void patchPackage(const std::set<uint64_t>& package, const std::shared_ptr<const Geometry>& geometry,
                    const std::optional<Color4f>& color)
  {
    for (const uint64_t address : package) {
      Block& block = blockByAddress(address);
      if (codeString(block.code) == "ME") patchMesh(block, geometry, color);
    }
  }

  void patchVisibility(const std::set<uint64_t>& package, size_t frame, size_t nextFrame)
  {
    const std::array<float, 3> times{float(frame - 1), float(frame), float(nextFrame)};
    for (const uint64_t address : package) {
      Block& block = blockByAddress(address);
      if (dna_->typeName(block.dna) != "BezTriple" || block.count != 3) continue;
      const size_t itemSize = dna_->structureSize(block.dna);
      const size_t vec = dna_->fieldOffset(block.dna, "vec");
      const size_t interpolation = dna_->fieldOffset(block.dna, "ipo");
      for (size_t key = 0; key < times.size(); ++key) {
        for (size_t point = 0; point < 3; ++point) {
          writeScalar<float>(block.data, key * itemSize + vec + point * 3 * sizeof(float), times[key]);
        }
        writeScalar<int8_t>(block.data, key * itemSize + interpolation, 0);
      }
    }
  }

  void patchStableObject(size_t slot, const std::vector<UsdAnimationFrame>& frames, size_t objectIndex)
  {
    char name[32];
    std::snprintf(name, sizeof(name), "OBStable %04zu", slot);
    Block& object = blockByIdName(name);
    const uint64_t meshAddress =
      readScalar<uint64_t>(object.data, dna_->fieldOffset(object.dna, "data"));
    detachOwnedData(meshAddress);
    patchPackage(ownedPackage(object.old), frames[0].objects[objectIndex].geometry,
                 frames[0].objects[objectIndex].color);

    std::snprintf(name, sizeof(name), "ACStable %04zu", slot);
    const uint64_t actionAddress = blockByIdName(name).old;
    detachOwnedData(actionAddress);
    patchTransformAction(ownedPackage(actionAddress), frames, objectIndex);
  }

  void patchTransformAction(const std::set<uint64_t>& package,
                            const std::vector<UsdAnimationFrame>& frames, size_t objectIndex)
  {
    std::vector<Eigen::Quaterniond> orientations;
    orientations.reserve(frames.size());
    for (const auto& frame : frames) {
      Eigen::Quaterniond orientation(frame.objects[objectIndex].transform.linear());
      if (!orientations.empty() && orientations.back().dot(orientation) < 0) {
        orientation.coeffs() *= -1;
      }
      orientations.push_back(orientation);
    }

    std::vector<uint64_t> curves;
    for (const uint64_t address : package) {
      if (dna_->typeName(blockByAddress(address).dna) == "FCurve") curves.push_back(address);
    }
    for (const uint64_t address : curves) {
      detachCurveKeys(address);
      Block& curve = blockByAddress(address);
      const uint64_t pathAddress =
        readScalar<uint64_t>(curve.data, dna_->fieldOffset(curve.dna, "rna_path"));
      const std::string path = rawString(blockByAddress(pathAddress));
      const int32_t component =
        readScalar<int32_t>(curve.data, dna_->fieldOffset(curve.dna, "array_index"));
      std::vector<float> values;
      if (path == "hide_render" || path == "hide_viewport") {
        values = {0, 0};
      } else if (path == "location") {
        for (const auto& frame : frames) {
          values.push_back(frame.objects[objectIndex].transform.translation()[component]);
        }
      } else if (path == "rotation_quaternion") {
        for (const auto& orientation : orientations) {
          values.push_back(component == 0 ? orientation.w() : orientation.coeffs()[component - 1]);
        }
      } else {
        continue;
      }
      patchCurve(curve, values, path.rfind("hide_", 0) == 0 ? 0 : 1);
    }
  }

  void detachCurveKeys(uint64_t curveAddress)
  {
    Block& curve = blockByAddress(curveAddress);
    const size_t bezt = dna_->fieldOffset(curve.dna, "bezt");
    Block keys = blockByAddress(readScalar<uint64_t>(curve.data, bezt));
    keys.old = nextAddress();
    writeScalar<uint64_t>(curve.data, bezt, keys.old);
    const auto owner = std::find_if(blocks_.begin(), blocks_.end(),
                                    [&](const auto& block) { return block.old == curveAddress; });
    blocks_.insert(std::next(owner), std::move(keys));
    rebuildIndex();
  }

  void patchCurve(Block& curve, const std::vector<float>& values, int8_t interpolation)
  {
    const uint64_t keysAddress = readScalar<uint64_t>(curve.data, dna_->fieldOffset(curve.dna, "bezt"));
    Block& keys = blockByAddress(keysAddress);
    const size_t itemSize = dna_->structureSize(keys.dna);
    if (keys.data.size() < itemSize) throw std::runtime_error("Blender action has no keyframes");
    const std::vector<uint8_t> exemplar(keys.data.begin(), keys.data.begin() + itemSize);
    keys.data.clear();
    for (size_t key = 0; key < values.size(); ++key) {
      keys.data.insert(keys.data.end(), exemplar.begin(), exemplar.end());
    }
    keys.count = values.size();
    writeScalar<int32_t>(curve.data, dna_->fieldOffset(curve.dna, "totvert"), values.size());
    const size_t vec = dna_->fieldOffset(keys.dna, "vec");
    const size_t ipo = dna_->fieldOffset(keys.dna, "ipo");
    for (size_t key = 0; key < values.size(); ++key) {
      for (size_t point = 0; point < 3; ++point) {
        const size_t offset = key * itemSize + vec + point * 3 * sizeof(float);
        writeScalar<float>(keys.data, offset, float(key + 1));
        writeScalar<float>(keys.data, offset + sizeof(float), values[key]);
      }
      writeScalar<int8_t>(keys.data, key * itemSize + ipo, interpolation);
    }
  }

  void hideFrameObjects()
  {
    for (size_t slot = 1; slot <= MAX_BLEND_FRAMES; ++slot) {
      char name[32];
      std::snprintf(name, sizeof(name), "ACFrame %04zu", slot);
      for (const uint64_t address : ownedPackage(blockByIdName(name).old)) {
        Block& block = blockByAddress(address);
        if (dna_->typeName(block.dna) != "BezTriple") continue;
        const size_t itemSize = dna_->structureSize(block.dna);
        const size_t vec = dna_->fieldOffset(block.dna, "vec");
        for (int64_t key = 0; key < block.count; ++key) {
          for (size_t point = 0; point < 3; ++point) {
            writeScalar<float>(block.data,
                               size_t(key) * itemSize + vec + (point * 3 + 1) * sizeof(float), 1);
          }
        }
      }
    }
  }

  uint64_t materialAddress(const Color4f& color)
  {
    const auto key = std::make_tuple(color.r(), color.g(), color.b(), color.a());
    const auto found = materials_.find(key);
    if (found != materials_.end()) return found->second;
    if (materials_.size() == MAX_BLEND_MATERIALS) {
      throw std::runtime_error("Blender export supports at most 1024 materials");
    }
    char name[48];
    std::snprintf(name, sizeof(name), "MAOpenSCAD Material %04zu", materials_.size() + 1);
    Block& material = blockByIdName(name);
    const std::array<float, 4> rgba{color.r(), color.g(), color.b(), color.a()};
    writeScalar<float>(material.data, dna_->fieldOffset(material.dna, "r"), color.r());
    writeScalar<float>(material.data, dna_->fieldOffset(material.dna, "g"), color.g());
    writeScalar<float>(material.data, dna_->fieldOffset(material.dna, "b"), color.b());
    writeScalar<float>(material.data, dna_->fieldOffset(material.dna, "a"), color.a());

    for (const uint64_t address : ownedPackage(material.old)) {
      Block& socket = blockByAddress(address);
      if (dna_->typeName(socket.dna) != "bNodeSocket") continue;
      const size_t itemSize = dna_->structureSize(socket.dna);
      for (int64_t item = 0; item < socket.count; ++item) {
        const size_t base = size_t(item) * itemSize;
        const size_t nameOffset = base + dna_->fieldOffset(socket.dna, "name");
        const char *socketName = reinterpret_cast<const char *>(socket.data.data() + nameOffset);
        const uint64_t valueAddress =
          readScalar<uint64_t>(socket.data, base + dna_->fieldOffset(socket.dna, "default_value"));
        if (!valueAddress) continue;
        Block& value = blockByAddress(valueAddress);
        if (std::string_view(socketName) == "Base Color") {
          std::memcpy(value.data.data() + dna_->fieldOffset(value.dna, "value"), rgba.data(),
                      sizeof(rgba));
        } else if (std::string_view(socketName) == "Alpha") {
          writeScalar<float>(value.data, dna_->fieldOffset(value.dna, "value"), color.a());
        }
      }
    }
    materials_.emplace(key, material.old);
    return material.old;
  }

  void patchMesh(Block& mesh, const std::shared_ptr<const Geometry>& geometry,
                 const std::optional<Color4f>& overrideColor)
  {
    const auto polyset = PolySetUtils::getGeometryAsPolySet(geometry);
    const std::vector<Vector3d> emptyVertices;
    const PolygonIndices emptyFaces;
    const auto& vertices = polyset ? polyset->vertices : emptyVertices;
    const auto& faces = polyset ? polyset->indices : emptyFaces;

    std::vector<uint64_t> materialSlots;
    std::map<std::tuple<float, float, float, float>, int32_t> materialIndices;
    std::vector<int32_t> faceMaterials;
    for (size_t face = 0; face < faces.size(); ++face) {
      Color4f color = defaultColor_;
      if (overrideColor && overrideColor->isValid()) {
        color = *overrideColor;
      } else if (polyset && face < polyset->color_indices.size()) {
        const int index = polyset->color_indices[face];
        if (index >= 0 && static_cast<size_t>(index) < polyset->colors.size()) {
          color = polyset->colors[index];
        }
      }
      if (!color.isValid()) color = defaultColor_;
      const auto key = std::make_tuple(color.r(), color.g(), color.b(), color.a());
      auto [found, inserted] = materialIndices.emplace(key, materialSlots.size());
      if (inserted) materialSlots.push_back(materialAddress(color));
      faceMaterials.push_back(found->second);
    }
    if (materialSlots.empty()) {
      materialSlots.push_back(materialAddress(Color4f(0.8f, 0.8f, 0.8f, 1.0f)));
    }
    const uint64_t materialArray = readScalar<uint64_t>(mesh.data, dna_->fieldOffset(mesh.dna, "mat"));
    if (!materialArray) throw std::runtime_error("Blender mesh has no material array");
    setRaw(blockByAddress(materialArray), materialSlots);
    writeScalar<int16_t>(mesh.data, dna_->fieldOffset(mesh.dna, "totcol"), materialSlots.size());

    std::vector<std::array<int32_t, 2>> edges;
    std::map<std::pair<int32_t, int32_t>, int32_t> edgeIndex;
    std::vector<int32_t> cornerVertices;
    std::vector<int32_t> cornerEdges;
    std::vector<int32_t> offsets{0};
    for (const auto& face : faces) {
      for (size_t i = 0; i < face.size(); ++i) {
        const int32_t a = face[i];
        const int32_t b = face[(i + 1) % face.size()];
        const std::pair<int32_t, int32_t> key{std::min(a, b), std::max(a, b)};
        auto [found, inserted] = edgeIndex.emplace(key, int32_t(edges.size()));
        if (inserted) edges.push_back({a, b});
        cornerVertices.push_back(a);
        cornerEdges.push_back(found->second);
      }
      offsets.push_back(int32_t(cornerVertices.size()));
    }

    writeScalar<int32_t>(mesh.data, dna_->fieldOffset(mesh.dna, "totvert"), int32_t(vertices.size()));
    writeScalar<int32_t>(mesh.data, dna_->fieldOffset(mesh.dna, "totedge"), int32_t(edges.size()));
    writeScalar<int32_t>(mesh.data, dna_->fieldOffset(mesh.dna, "totpoly"), int32_t(faces.size()));
    writeScalar<int32_t>(mesh.data, dna_->fieldOffset(mesh.dna, "totloop"),
                         int32_t(cornerVertices.size()));

    const uint64_t offsetAddress =
      readScalar<uint64_t>(mesh.data, dna_->fieldOffset(mesh.dna, "poly_offset_indices"));
    if (!offsetAddress) throw std::runtime_error("Blender mesh has no polygon offsets");
    setRaw(blockByAddress(offsetAddress), offsets);

    const uint64_t attributesAddress =
      readScalar<uint64_t>(mesh.data, dna_->fieldOffset(mesh.dna, "attribute_storage.dna_attributes"));
    if (!attributesAddress) throw std::runtime_error("Blender mesh has no attributes");
    Block& attributes = blockByAddress(attributesAddress);
    const size_t attributeSize = dna_->structureSize(attributes.dna);
    for (int64_t item = 0; item < attributes.count; ++item) {
      const size_t base = size_t(item) * attributeSize;
      const uint64_t nameAddress =
        readScalar<uint64_t>(attributes.data, base + dna_->fieldOffset(attributes.dna, "name"));
      if (!nameAddress) throw std::runtime_error("Blender mesh attribute has no name");
      const std::string name = rawString(blockByAddress(nameAddress));
      const uint64_t arrayAddress =
        readScalar<uint64_t>(attributes.data, base + dna_->fieldOffset(attributes.dna, "data"));
      if (!arrayAddress) throw std::runtime_error("Blender mesh attribute has no array");
      Block& array = blockByAddress(arrayAddress);
      const uint64_t payloadAddress =
        readScalar<uint64_t>(array.data, dna_->fieldOffset(array.dna, "data"));
      if (!payloadAddress) throw std::runtime_error("Blender mesh attribute has no payload: " + name);
      Block& payload = blockByAddress(payloadAddress);

      if (name == "position") {
        payload.data.clear();
        for (const auto& vertex : vertices) {
          appendScalar(payload.data, float(vertex.x()));
          appendScalar(payload.data, float(vertex.y()));
          appendScalar(payload.data, float(vertex.z()));
        }
        setArraySize(array, vertices.size());
      } else if (name == ".edge_verts") {
        setRaw(payload, edges);
        setArraySize(array, edges.size());
      } else if (name == ".corner_vert") {
        setRaw(payload, cornerVertices);
        setArraySize(array, cornerVertices.size());
      } else if (name == ".corner_edge") {
        setRaw(payload, cornerEdges);
        setArraySize(array, cornerEdges.size());
      } else if (name == ".select_vert") {
        payload.data.assign(vertices.size(), 0);
        setArraySize(array, vertices.size());
      } else if (name == ".select_edge") {
        payload.data.assign(edges.size() * 8, 0);
        setArraySize(array, edges.size());
      } else if (name == ".select_poly" || name == "sharp_face") {
        payload.data.assign(faces.size(), 0);
        setArraySize(array, faces.size());
      } else if (name == "material_index" || name == ".material_index") {
        setRaw(payload, faceMaterials);
        setArraySize(array, faceMaterials.size());
      }
    }
  }

  template <typename T>
  static void setRaw(Block& block, const std::vector<T>& values)
  {
    block.data.resize(values.size() * sizeof(T));
    if (!values.empty()) std::memcpy(block.data.data(), values.data(), block.data.size());
  }

  void setArraySize(Block& array, size_t size)
  {
    writeScalar<int64_t>(array.data, dna_->fieldOffset(array.dna, "size"), size);
  }

  static std::string rawString(const Block& block)
  {
    const char *data = reinterpret_cast<const char *>(block.data.data());
    return std::string(data, std::find(data, data + block.data.size(), '\0'));
  }

  std::vector<uint8_t> bytes_;
  std::vector<Block> blocks_;
  std::unordered_map<uint64_t, size_t> indexByAddress_;
  std::map<std::tuple<float, float, float, float>, uint64_t> materials_;
  Color4f defaultColor_;
  std::unique_ptr<Dna> dna_;
  uint64_t nextGeneratedAddress_ = 1;
};

}  // namespace

void export_blend_animation(const std::vector<UsdAnimationFrame>& frames, unsigned fps,
                            std::ostream& output, const BlendExportOptions& options)
{
  if (options.remeshSamples == 0 || options.remeshSamples > MAX_BLEND_FRAMES) {
    throw std::runtime_error("Blender remesh samples must be in range 1..256");
  }
  const fs::path templatePath = PlatformUtils::resourcePath("templates") / "blender-5.0.1.blend";
  BlendScene scene(templatePath, options.defaultColor);
  scene.setCamera(frames);
  if (!frames.empty() && frames[0].objects.size() <= MAX_BLEND_OBJECTS &&
      canExportObjectAnimation(frames)) {
    const bool remeshes =
      std::any_of(frames[0].objects.begin(), frames[0].objects.end(), [&](const auto& object) {
        const size_t index = &object - frames[0].objects.data();
        return std::any_of(frames.begin(), frames.end(), [&](const auto& frame) {
          return frame.objects[index].geometry.get() != object.geometry.get();
        });
      });
    if (remeshes && frames.size() > options.remeshSamples) {
      LOG(message_group::Warning,
          "Blender export samples changing geometry at %1$d evenly spaced frames out of %2$d; "
          "brief changes between samples may be missed.",
          options.remeshSamples, frames.size());
    }
    scene.setObjectAnimation(frames, fps, options.remeshSamples);
    scene.write(output);
    return;
  }
  if (frames.size() > options.remeshSamples) {
    LOG(message_group::Warning,
        "Blender export samples changing geometry at %1$d evenly spaced frames out of %2$d; brief "
        "changes between samples may be missed.",
        options.remeshSamples, frames.size());
  }
  std::vector<std::shared_ptr<const Geometry>> geometry;
  geometry.reserve(frames.size());
  for (const auto& frame : frames) geometry.push_back(frame.geometry);
  scene.setFrames(geometry, fps, options.remeshSamples);
  scene.write(output);
}
