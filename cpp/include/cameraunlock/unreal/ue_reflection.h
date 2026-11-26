// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <cameraunlock/unreal/ue_objects.h>

// The engine's own reflection data, with its layout measured on engine types
// whose shape every build agrees on, rather than taken from a table per version.
//
//   FVector / FRotator / FQuat  three or four reals named X Y Z (W) / Pitch Yaw
//                               Roll, so they fix the property chain, the field
//                               names, ArrayDim, ElementSize and the offsets,
//                               and whether reals are float (UE4) or double (UE5).
//   Actor -> Object, PlayerController -> Controller   fix SuperStruct.
//   Transform.Rotation is a StructProperty of Quat     fixes where a property's
//                               subclass data begins.
//   Native BlueprintCallable functions                 fix FunctionFlags and Func.
//
// Properties are FFields on their own chain from 4.25 and UObjects on the
// Children chain before. Both are measured; whichever the engine types agree
// with is the model.
namespace cameraunlock::unreal {

enum class PropertyModel { FField, UProperty };

struct ReflectionLayout {
    PropertyModel model = PropertyModel::FField;
    std::size_t superStruct = 0;
    std::size_t childProperties = 0;  // ChildProperties (FField) or Children (UProperty)
    std::size_t propertiesSize = 0;
    std::size_t fieldNext = 0;
    std::size_t fieldName = 0;        // FField::NamePrivate, or UObject NamePrivate
    std::size_t fieldClassName = 0;   // FFieldClass::Name: 0, or 8 from UE 5.7, where a pointer comes first
    std::size_t arrayDim = 0;
    std::size_t elementSize = 0;
    std::size_t offset = 0;
    std::size_t propertyExtra = 0;    // first byte past FProperty/UProperty
    std::size_t functionFlags = 0;
    std::size_t functionFunc = 0;
    std::size_t realSize = 4;         // 4 for UE4's float vectors, 8 for UE5's doubles
};

// Whether `name` fits `pattern`, ignoring case as FNames compare. A leading or
// trailing '*' matches any run, so a Blueprint member with a GUID suffix
// ("FOV_12_ABCD...") is named by "FOV_*".
bool NameMatches(const std::string& pattern, const std::string& name);

struct Field {
    std::uintptr_t addr = 0;
    std::string name;
    std::string type;          // "FloatProperty", "StructProperty", ...
    std::int32_t offset = 0;
    std::int32_t size = 0;     // ElementSize * ArrayDim
};

class Reflection {
public:
    // Holds `objects` by reference: it, and the Memory it reads through, must
    // stay where they are while the Reflection is used.
    Reflection(const Objects& objects, const ReflectionLayout& layout)
        : objects_(objects), layout_(layout) {}

    const ReflectionLayout& Layout() const { return layout_; }
    const Objects& Objs() const { return objects_; }

    std::uintptr_t Super(std::uintptr_t ustruct) const;
    std::int32_t StructSize(std::uintptr_t ustruct) const;

    // The struct's own properties, in declaration order.
    std::vector<Field> Properties(std::uintptr_t ustruct) const;

    // A property of the struct or of any struct it derives from, by NameMatches.
    std::optional<Field> Find(std::uintptr_t ustruct, const std::string& name) const;

    // StructProperty: the UScriptStruct. ObjectProperty: the property class.
    std::uintptr_t PropertyStruct(const Field& f) const;

    // BoolProperty: the byte the bit lives in (relative to the container) and its mask.
    bool BoolBit(const Field& f, std::int32_t& byteOffset, std::uint8_t& mask) const;

    // UFunction fields.
    std::uint32_t FunctionFlags(std::uintptr_t function) const;
    std::uintptr_t NativeFunc(std::uintptr_t function) const;

    // True when `cls` is `base` or derives from it.
    bool IsA(std::uintptr_t cls, std::uintptr_t base) const;

    // One property's name and type ("IntProperty"), for an FField or UProperty
    // reached other than through Properties (an array's inner property).
    std::string FieldName(std::uintptr_t field) const;
    std::string FieldType(std::uintptr_t field) const;

private:
    const Objects& objects_;
    ReflectionLayout layout_;
};

// The FunctionFlags bits that tell a remote function, read off the flags of
// PlayerController's and Actor's functions: `net` (FUNC_Net) is carried by
// every remote function, and each of those carries exactly one of two bits
// that nothing else carries, FUNC_NetServer and `client` (FUNC_NetClient).
// Names only tell the two apart: no function with the server bit is named
// Client... and no function with the client bit Server..., and each bit has
// one function named its way. Names alone are no guide: PlayerController
// holds local Client... and Server... functions (ClientTravel, ServerExec) and
// a client one named OnServerStartedVisualLogger. A server function carries
// `net`, not `client`, and one of `server` (FUNC_NetServer, and NetValidate
// where every remote Server... function is validated), which a multicast
// function does not.
struct NetFunctionFlags {
    std::uint32_t net = 0, client = 0, server = 0;
};
std::optional<NetFunctionFlags> FindNetFunctionFlags(const Reflection& refl, std::string& why);

// The value of `entry` in the UEnum at `path` ("/Script/Engine.EPhysBodyOp",
// "PBO_None"), matched with or without the "Enum::" a scoped entry's name
// leads with. The enum's Names is the one array in it whose entries, as
// TPair<FName, int64>, TPair<FName, uint8> or bare FNames, end with the "_MAX"
// the engine adds to every enum.
// Nothing, with `why`, when the enum or the entry is not found.
std::optional<std::int64_t> EnumValue(const Objects& objects, const std::string& path, const std::string& entry,
                                      std::string& why);
// EnumValue as a byte, for a parameter of the enum's type. Throws when it is
// not found or does not fit a byte.
std::uint8_t EnumByte(const Objects& objects, const std::string& path, const std::string& entry);

// The engine types calibration measures against, by path.
struct CalibrationTypes {
    std::uintptr_t vector = 0, rotator = 0, quat = 0, transform = 0;
    std::uintptr_t object = 0, actor = 0, controller = 0, playerController = 0;
    std::vector<std::uintptr_t> nativeCallables;
    std::vector<std::string> missingCallables;
};

CalibrationTypes FindCalibrationTypes(const Objects& objects);

// The measured layout, or nullopt with `why` naming the first measurement that
// did not hold; `notLoaded` is set when a type it measures is not created yet.
std::optional<ReflectionLayout> CalibrateReflection(const Objects& objects, const memory::ModuleMap& module,
                                                    const CalibrationTypes& types, std::string& why, bool& notLoaded);

}  // namespace cameraunlock::unreal
