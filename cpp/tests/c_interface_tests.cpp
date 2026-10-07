// CameraUnlockCore.dll through cameraunlock/c/cameraunlock.h and nothing else of core's: real
// OpenTrack datagrams to a loopback port, poses read back a frame at a time, and a config file
// described, rendered, loaded, saved and read again in a scratch folder.

#include "cameraunlock/c/cameraunlock.h"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>

extern "C" int CameraUnlockHeaderIsC(void);

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

bool Near(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

constexpr int kPort = 14311;
constexpr int kHeldPort = 14312;
constexpr float kFrame = 1.0f / 60.0f;

std::string LastError() {
    char text[1024] = {};
    cameraunlock_last_error(text, sizeof(text));
    return text;
}

std::string TakeLog() {
    const std::int32_t length = cameraunlock_log_take(nullptr, 0);
    std::string text(static_cast<std::size_t>(length), '\0');
    cameraunlock_log_take(text.data(), length);
    return text;
}

struct Sender {
    SOCKET socket = INVALID_SOCKET;
    sockaddr_in to = {};

    Sender(const char* address, int port) {
        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        to.sin_family = AF_INET;
        to.sin_port = htons(static_cast<u_short>(port));
        inet_pton(AF_INET, address, &to.sin_addr);
    }
    ~Sender() { closesocket(socket); }

    void Bytes(const char* data, int length) const {
        sendto(socket, data, length, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    }

    // x, y and z in the wire's centimetres, the angles in degrees.
    void Pose(double yaw, double pitch = 0.0, double roll = 0.0, double x = 0.0, double y = 0.0, double z = 0.0) const {
        const double values[6] = {x, y, z, yaw, pitch, roll};
        Bytes(reinterpret_cast<const char*>(values), sizeof(values));
    }
};

// One tracker for the session on kPort: the receiver follows the first sender it hears and
// ignores a second while the first is still sending.
const Sender& Tracker() {
    static const Sender sender("127.0.0.1", kPort);
    return sender;
}

// The host's clock, which the aim transitions run on: a frame's worth on from the last input.
std::uint64_t g_now_ms = 1000;

CameraUnlockFrameInput Input(std::uint32_t flags, float delta = kFrame) {
    CameraUnlockFrameInput input = {};
    input.struct_size = sizeof(input);
    input.flags = flags;
    g_now_ms += 17;
    input.now_ms = g_now_ms;
    input.delta_seconds = delta;
    input.tan_half_fov = 0.5f;
    input.tan_half_fov_base = 0.5f;
    input.forward_stop = std::numeric_limits<float>::infinity();
    input.aim_forward[2] = 1.0f;
    input.tracker_to_world[0] = 1.0f;
    input.tracker_to_world[4] = 1.0f;
    input.tracker_to_world[8] = 1.0f;
    return input;
}

CameraUnlockFrame Frame(const CameraUnlockFrameInput& input) {
    CameraUnlockFrame frame = {};
    frame.struct_size = sizeof(frame);
    if (cameraunlock_session_frame(&input, &frame) != CAMERAUNLOCK_OK) {
        Check(false, "cameraunlock_session_frame: " + LastError());
    }
    return frame;
}

// A second of frames, by which the pipeline is on the newest pose the receiver published.
CameraUnlockFrame Settle(std::uint32_t flags = CAMERAUNLOCK_FRAME_ACTIVE) {
    CameraUnlockFrame frame = {};
    for (int i = 0; i < 60; ++i) frame = Frame(Input(flags));
    return frame;
}

template <class Reached>
bool WaitFor(Reached reached, int timeout_ms = 3000) {
    for (int waited = 0; waited < timeout_ms; waited += 10) {
        if (reached()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return reached();
}

bool WaitYaw(float yaw) {
    return WaitFor([&] { return Near(Settle().head_yaw, yaw); });
}

CameraUnlockSettings Defaults() {
    CameraUnlockSettings settings = {};
    settings.struct_size = sizeof(settings);
    cameraunlock_settings_defaults(&settings);
    return settings;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// An IPv4 address of this machine that is not loopback, or empty.
std::string OwnAddress() {
    char name[256] = {};
    gethostname(name, sizeof(name));
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    addrinfo* found = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &found) != 0) return std::string();
    std::string address;
    for (addrinfo* at = found; at != nullptr && address.empty(); at = at->ai_next) {
        char text[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(at->ai_addr)->sin_addr, text, sizeof(text));
        if (std::strncmp(text, "127.", 4) != 0) address = text;
    }
    freeaddrinfo(found);
    return address;
}

void TestBoundary() {
    std::cout << "\n[c interface: the boundary]\n";
    Check(cameraunlock_abi() == CAMERAUNLOCK_ABI, "the library is the ABI this test was compiled against");
    Check(CameraUnlockHeaderIsC() == 1, "the header compiles as C and calls through");
    Check(cameraunlock_struct_size(CAMERAUNLOCK_STRUCT_SETTINGS) == sizeof(CameraUnlockSettings) &&
              cameraunlock_struct_size(CAMERAUNLOCK_STRUCT_FRAME_INPUT) == sizeof(CameraUnlockFrameInput) &&
              cameraunlock_struct_size(CAMERAUNLOCK_STRUCT_FRAME) == sizeof(CameraUnlockFrame) &&
              cameraunlock_struct_size(CAMERAUNLOCK_STRUCT_OBSTRUCTION) == sizeof(CameraUnlockObstruction) &&
              cameraunlock_struct_size(CAMERAUNLOCK_STRUCT_LEAN) == sizeof(CameraUnlockLean) &&
              cameraunlock_struct_size(CAMERAUNLOCK_STRUCT_CONFIG) == sizeof(CameraUnlockConfig),
          "the library says how large each of its structs is");
    Check(cameraunlock_struct_size(6) == CAMERAUNLOCK_ERROR, "and refuses a number that names none");

    CameraUnlockSettings settings = {};
    settings.struct_size = 4;
    Check(cameraunlock_settings_defaults(&settings) == CAMERAUNLOCK_ERROR, "a struct of another size is refused");
    Check(LastError().find("different builds") != std::string::npos &&
              LastError().find("60 bytes") != std::string::npos,
          "and the reason, as text, names both sizes: " + LastError());
    char small[4] = {};
    Check(cameraunlock_last_error(small, sizeof(small)) > 4 && small[0] == '\0',
          "a buffer too small for the reason is left alone and told the length");
    Check(cameraunlock_session_configure(nullptr) == CAMERAUNLOCK_ERROR, "NULL for a struct is refused, not read");

    settings = Defaults();
    Check(Near(settings.local_smoothing, 0.0f) && Near(settings.remote_smoothing, 0.15f) &&
              Near(settings.limit_x, 0.30f) && Near(settings.limit_y, 0.20f) && Near(settings.limit_z, 0.40f) &&
              Near(settings.limit_z_back, 0.10f) && Near(settings.light_multiplier, 1.5f) &&
              settings.data_freshness_ms == 500,
          "the defaults are core's");
    settings.limit_y_down = -0.1f;
    Check(cameraunlock_session_configure(&settings) == CAMERAUNLOCK_ERROR, "a negative limit is refused");
    settings = Defaults();
    settings.tracking_mode = 7;
    Check(cameraunlock_session_configure(&settings) == CAMERAUNLOCK_ERROR, "a tracking mode that is none is refused");
    Check(cameraunlock_aim_mode_label(9) == nullptr, "an aim mode that is none has no label");
    Check(std::string(cameraunlock_aim_mode_label(CAMERAUNLOCK_AIM_STOCK_SIGHTS)) == "Aim mode: stock sights",
          "an aim mode's label is core's");
    Check(TakeLog().find("different builds") != std::string::npos, "every refusal is in the log as well");
}

void TestPortInUse() {
    std::cout << "\n[c interface: a port another program holds]\n";
    const SOCKET holder = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(kHeldPort);
    address.sin_addr.s_addr = INADDR_ANY;
    Check(bind(holder, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "another program holds the port");

    Check(cameraunlock_session_start(kHeldPort) == CAMERAUNLOCK_OK, "starting on a held port is not an error");
    const CameraUnlockFrame waiting = Frame(Input(CAMERAUNLOCK_FRAME_ACTIVE));
    Check((waiting.flags & CAMERAUNLOCK_STATE_LISTENING) == 0, "the session says it is not listening");
    Check((waiting.flags & CAMERAUNLOCK_STATE_LOG) != 0, "and that the log has lines");
    const std::string log = TakeLog();
    Check(log.find("bind failed with error " + std::to_string(WSAEADDRINUSE)) != std::string::npos,
          "the log carries the system's own reason for the bind");
    Check(log.find("Listening for OpenTrack datagrams") == std::string::npos, "and does not say it is listening");
    Check(cameraunlock_session_start(kHeldPort) == CAMERAUNLOCK_ERROR &&
              LastError().find("already started") != std::string::npos,
          "starting twice is refused: " + LastError());

    closesocket(holder);
    Check(WaitFor([] { return (Frame(Input(0)).flags & CAMERAUNLOCK_STATE_LISTENING) != 0; }),
          "the port is bound soon after the other program lets it go");
    const Sender sender("127.0.0.1", kHeldPort);
    sender.Pose(3.0);
    Check(WaitYaw(3.0f), "and poses arrive on it");
    Check(cameraunlock_session_stop() == CAMERAUNLOCK_OK, "the session stops");
    Check(cameraunlock_session_stop() == CAMERAUNLOCK_OK, "and stopping a stopped session is not an error");
}

void TestReceiving() {
    std::cout << "\n[c interface: poses off the wire]\n";
    CameraUnlockSettings settings = Defaults();
    settings.data_freshness_ms = 200;
    settings.limit_y = 0.25f;
    settings.limit_y_down = 0.05f;
    Check(cameraunlock_session_configure(&settings) == CAMERAUNLOCK_OK, "the settings are taken");
    TakeLog();
    Check(cameraunlock_session_start(kPort) == CAMERAUNLOCK_OK, "the session starts again after a stop");
    Check(TakeLog().find("Listening for OpenTrack datagrams on UDP port " + std::to_string(kPort)) != std::string::npos,
          "and a port bound at once is said so in the log");
    const Sender& sender = Tracker();

    CameraUnlockFrame frame = Settle();
    Check((frame.flags & (CAMERAUNLOCK_STATE_POSE | CAMERAUNLOCK_STATE_FRESH)) == 0,
          "there is no pose before the first packet, the last session's included");

    sender.Bytes("short", 5);
    const double not_a_number[6] = {0.0, 0.0, 0.0, std::nan(""), 0.0, 0.0};
    sender.Bytes(reinterpret_cast<const char*>(not_a_number), sizeof(not_a_number));
    const double too_large[6] = {0.0, 1e300, 0.0, 0.0, 0.0, 0.0};
    sender.Bytes(reinterpret_cast<const char*>(too_large), sizeof(too_large));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Check((Settle().flags & CAMERAUNLOCK_STATE_POSE) == 0,
          "a short datagram, one holding a NaN and one past a float's range give no pose");

    sender.Pose(20.0, -5.0, 2.0, 10.0, 0.0, -20.0);
    Check(WaitYaw(20.0f), "the first pose arrives");
    frame = Settle();
    Check(Near(frame.head_pitch, -5.0f) && Near(frame.head_roll, 2.0f), "pitch and roll in degrees");
    Check(Near(frame.head_x, 0.10f) && Near(frame.head_z, -0.20f), "the wire's centimetres arrive as metres");
    Check((frame.flags & CAMERAUNLOCK_STATE_POSE) != 0 && (frame.flags & CAMERAUNLOCK_STATE_ROTATION) != 0 &&
              (frame.flags & CAMERAUNLOCK_STATE_LISTENING) != 0,
          "the frame says it has a pose, a rotation to apply and a bound port");
    Check((frame.flags & CAMERAUNLOCK_STATE_REMOTE) == 0, "a sender on loopback is local");
    Check(Near(frame.yaw, 20.0f) && Near(frame.pose_share, 1.0f) && Near(frame.zoom_factor, 1.0f),
          "unzoomed and at the hip the view gets the head's own angles");
    Check(Near(frame.light_yaw, 30.0f) && Near(frame.light_pitch, -7.5f), "the light's angles are the view's by light_multiplier");

    // A relay repeating the pose faster than the tracker measures one.
    for (int i = 0; i < 5; ++i) sender.Pose(20.0, -5.0, 2.0, 10.0, 0.0, -20.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Check(Near(Settle().head_yaw, 20.0f), "repeats of the published pose change nothing");

    // The tracker loses the head and repeats centre.
    for (int i = 0; i < 6; ++i) {
        sender.Pose(0.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Check(Near(Settle().head_yaw, 20.0f), "a jump to a pose that then repeats is never followed");
    }
    sender.Pose(20.5, -5.0, 2.0, 10.0, 0.0, -20.0);
    Check(WaitYaw(20.5f), "the head coming back is followed at once");

    // A fast turn: only its first step waits for the packet after it.
    sender.Pose(31.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Check(Near(Settle().head_yaw, 20.5f), "the first large step of a turn is held one packet");
    sender.Pose(42.0);
    Check(WaitYaw(42.0f), "the packet after it differs, and the turn is followed");
    sender.Pose(53.0);
    Check(WaitYaw(53.0f), "the rest of the turn is not held");
    sender.Pose(53.5);
    Check(WaitYaw(53.5f), "the turn ends");

    // The limits, in the tracker's axes: y up and down apart, z forward (negative) and back apart.
    sender.Pose(53.5, 0.0, 0.0, 100.0, 100.0, -100.0);
    Check(WaitFor([] { return Near(Settle().head_x, 0.30f); }), "x stops at limit_x");
    frame = Settle();
    Check(Near(frame.head_y, 0.25f), "y stops at limit_y going up");
    Check(Near(frame.head_z, -0.40f), "z stops at limit_z leaning in");
    sender.Pose(53.5, 0.0, 0.0, -100.0, -100.0, 100.0);
    Check(WaitFor([] { return Near(Settle().head_x, -0.30f); }), "x stops at limit_x the other way");
    frame = Settle();
    Check(Near(frame.head_y, -0.05f), "y stops at limit_y_down going down");
    Check(Near(frame.head_z, 0.10f), "z stops at limit_z_back leaning back");

    // Silence.
    Check((Settle().flags & CAMERAUNLOCK_STATE_FRESH) != 0, "a pose just received is fresh");
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    frame = Settle();
    Check((frame.flags & CAMERAUNLOCK_STATE_FRESH) == 0, "past data_freshness_ms with no packet the tracker is quiet");
    Check((frame.flags & CAMERAUNLOCK_STATE_POSE) != 0 && Near(frame.head_yaw, 53.5f) && Near(frame.head_x, -0.30f),
          "and the view keeps the last pose");

    // A frame tracking does not apply on.
    frame = Frame(Input(0));
    Check((frame.flags & (CAMERAUNLOCK_STATE_POSE | CAMERAUNLOCK_STATE_ROTATION | CAMERAUNLOCK_STATE_LEAN)) == 0 &&
              frame.yaw == 0.0f,
          "an inactive frame carries no pose");
    Check(Near(Frame(Input(CAMERAUNLOCK_FRAME_ACTIVE)).head_yaw, 53.5f),
          "the first active frame after it is at the tracker's pose");
}

void TestModes() {
    std::cout << "\n[c interface: the tracking mode and the aim mode]\n";
    Check(cameraunlock_session_cycle_tracking_mode() == CAMERAUNLOCK_TRACKING_ROTATION_ONLY,
          "the mode after rotation and position is rotation only");
    CameraUnlockFrame frame = Settle(CAMERAUNLOCK_FRAME_ACTIVE | CAMERAUNLOCK_FRAME_LEAN);
    Check(frame.tracking_mode == CAMERAUNLOCK_TRACKING_ROTATION_ONLY &&
              (frame.flags & CAMERAUNLOCK_STATE_ROTATION) != 0 && (frame.flags & CAMERAUNLOCK_STATE_LEAN) == 0 &&
              frame.head_x == 0.0f,
          "rotation only turns the view and asks for no lean");
    Check(cameraunlock_session_cycle_tracking_mode() == CAMERAUNLOCK_TRACKING_POSITION_ONLY, "then position only");
    frame = Settle();
    Check((frame.flags & CAMERAUNLOCK_STATE_ROTATION) == 0 && frame.yaw == 0.0f && Near(frame.head_x, -0.30f),
          "position only has a position and no rotation to apply");
    Check(cameraunlock_session_cycle_tracking_mode() == CAMERAUNLOCK_TRACKING_ROTATION_AND_POSITION, "then both again");

    Check(cameraunlock_session_cycle_aim_mode() == CAMERAUNLOCK_AIM_FREE_LOOK_MARKER &&
              cameraunlock_session_cycle_aim_mode() == CAMERAUNLOCK_AIM_TRUE_FREE_LOOK &&
              cameraunlock_session_cycle_aim_mode() == CAMERAUNLOCK_AIM_STOCK_SIGHTS,
          "the aim mode cycles in core's order");

    // Stock sights: with the sights up, yaw and pitch ease out and roll stays.
    const Sender& sender = Tracker();
    sender.Pose(50.0, 4.0, 6.0);
    Check(WaitYaw(50.0f), "a pose for the sights");
    Frame(Input(CAMERAUNLOCK_FRAME_ACTIVE | CAMERAUNLOCK_FRAME_AIMING));
    g_now_ms += 1000;
    frame = Frame(Input(CAMERAUNLOCK_FRAME_ACTIVE | CAMERAUNLOCK_FRAME_AIMING));
    Check(Near(frame.pose_share, 0.0f) && Near(frame.yaw, 0.0f) && Near(frame.pitch, 0.0f) && Near(frame.roll, 6.0f) &&
              Near(frame.head_yaw, 50.0f),
          "stock sights with the sights up leaves the view the head's roll alone");
    Check(cameraunlock_session_cycle_aim_mode() == CAMERAUNLOCK_AIM_SIGHTS_LOCKED, "and the cycle comes round");
    Check(cameraunlock_session_set_aim_mode(CAMERAUNLOCK_AIM_TRUE_FREE_LOOK) == CAMERAUNLOCK_OK &&
              Frame(Input(CAMERAUNLOCK_FRAME_ACTIVE)).aim_mode == CAMERAUNLOCK_AIM_TRUE_FREE_LOOK,
          "a host with a cycle of its own puts the session in one mode");
    Check(cameraunlock_session_set_aim_mode(4) == CAMERAUNLOCK_ERROR, "which has to be a mode");
    Check(cameraunlock_session_set_aim_mode(CAMERAUNLOCK_AIM_SIGHTS_LOCKED) == CAMERAUNLOCK_OK, "and back");
    g_now_ms += 1000;

    // The zoom: a narrower field of view scales yaw and pitch, never roll.
    for (int i = 0; i < 60; ++i) {
        CameraUnlockFrameInput zoomed = Input(CAMERAUNLOCK_FRAME_ACTIVE);
        zoomed.tan_half_fov = 0.25f;
        frame = Frame(zoomed);
    }
    const float expected = std::atan(std::tan(50.0f * 3.14159265f / 180.0f) * 0.5f) * 180.0f / 3.14159265f;
    Check(Near(frame.zoom_factor, 0.5f) && Near(frame.yaw, expected, 1e-2f) && Near(frame.roll, 6.0f),
          "a zoom of two scales yaw by the tangent and leaves roll");
    sender.Pose(57.0, 4.0, 6.0);
    sender.Pose(64.0, 4.0, 6.0);
    sender.Pose(120.0, 4.0, 6.0);
    sender.Pose(120.5, 4.0, 6.0);
    Check(WaitYaw(120.5f), "a yaw past a right angle, as a tracker's curve gives");
    Check(Near(Settle().yaw, 120.5f, 1e-2f), "stays on its own side of the view unzoomed");
}

void TestLean() {
    std::cout << "\n[c interface: the lean]\n";
    CameraUnlockSettings settings = Defaults();
    settings.collision_enabled = 1;
    settings.collision_margin = 0.10f;
    cameraunlock_session_configure(&settings);
    const Sender& sender = Tracker();
    sender.Pose(1.0, 0.0, 0.0, 20.0, 0.0, 0.0);
    Check(WaitFor([] { return Near(Settle().head_x, 0.20f); }), "a head 20 cm to one side");

    const std::uint32_t leaning = CAMERAUNLOCK_FRAME_ACTIVE | CAMERAUNLOCK_FRAME_LEAN;
    CameraUnlockFrame frame = Frame(Input(leaning));
    Check((frame.flags & CAMERAUNLOCK_STATE_LEAN) != 0 && (frame.flags & CAMERAUNLOCK_STATE_LEAN_QUERY) != 0,
          "the frame asks for the lean, and for the world to be measured first");
    Check(Near(frame.query_direction[0], 1.0f) && Near(frame.query_direction[1], 0.0f) &&
              Near(frame.query_direction[2], 0.0f) && Near(frame.query_reach, 0.30f),
          "along the lean, for its length and the margin");

    CameraUnlockFrame refused = {};
    refused.struct_size = sizeof(refused);
    const CameraUnlockFrameInput next = Input(leaning);
    Check(cameraunlock_session_frame(&next, &refused) == CAMERAUNLOCK_ERROR &&
              LastError().find("cameraunlock_session_lean") != std::string::npos,
          "a frame whose lean was never finished is said so at the next one");

    CameraUnlockLean lean = {};
    lean.struct_size = sizeof(lean);
    Check(cameraunlock_session_lean(nullptr, &lean) == CAMERAUNLOCK_ERROR, "and no lean is waiting after that");

    CameraUnlockObstruction found = {};
    found.struct_size = sizeof(found);
    Frame(Input(leaning));
    Check(cameraunlock_session_lean(&found, &lean) == CAMERAUNLOCK_OK &&
              (lean.flags & CAMERAUNLOCK_LEAN_QUERY_FAILED) != 0 && Near(lean.given, 0.20f) && Near(lean.camera[0], 0.20f),
          "a query that could not run passes the lean and says so");

    found.queried = 1;
    Frame(Input(leaning));
    cameraunlock_session_lean(&found, &lean);
    Check(lean.flags == 0 && Near(lean.asked, 0.20f) && Near(lean.given, 0.20f), "a clear path gives the whole lean");

    found.blocked = 1;
    found.distance = 0.15f;
    Frame(Input(leaning));
    cameraunlock_session_lean(&found, &lean);
    Check((lean.flags & CAMERAUNLOCK_LEAN_CONTACT) != 0 && Near(lean.given, 0.05f) && Near(lean.camera[0], 0.05f) &&
              Near(lean.rig[0], 0.0f),
          "a wall 15 cm off holds the eye the margin short of it, at once");

    found.blocked = 0;
    Frame(Input(leaning));
    cameraunlock_session_lean(&found, &lean);
    Check(lean.given > 0.05f && lean.given < 0.10f, "and the lean opens again slowly once the wall is gone");

    // Sights locked with the sights up: the lean across the aim goes over to the rig.
    const std::uint32_t sights_up = leaning | CAMERAUNLOCK_FRAME_AIMING | CAMERAUNLOCK_FRAME_RIG_AVAILABLE;
    Frame(Input(sights_up));
    cameraunlock_session_lean(&found, &lean);
    g_now_ms += 1000;
    Frame(Input(sights_up));
    cameraunlock_session_lean(&found, &lean);
    Check(Near(lean.camera[0], 0.0f) && Near(lean.rig[0], lean.given) && lean.given > 0.05f,
          "with the sights up the rig carries the lean across the aim");
    frame = Frame(Input(CAMERAUNLOCK_FRAME_ACTIVE));
    Check((frame.flags & CAMERAUNLOCK_STATE_RELEASE_RIG) != 0 && (frame.flags & CAMERAUNLOCK_STATE_LEAN) == 0,
          "and the first frame with no lean says the rig is to be put back");
    Check((Frame(Input(CAMERAUNLOCK_FRAME_ACTIVE)).flags & CAMERAUNLOCK_STATE_RELEASE_RIG) == 0, "once");

    // The host's axes: a matrix that turns the tracker's x into the world's -y.
    CameraUnlockFrameInput turned = Input(leaning);
    std::memset(turned.tracker_to_world, 0, sizeof(turned.tracker_to_world));
    turned.tracker_to_world[3] = -1.0f;
    turned.tracker_to_world[1] = 1.0f;
    turned.tracker_to_world[8] = 1.0f;
    found.blocked = 0;
    g_now_ms += 1000;
    for (int i = 0; i < 120; ++i) {
        g_now_ms += 17;
        turned.now_ms = g_now_ms;
        frame = Frame(turned);
        cameraunlock_session_lean(&found, &lean);
    }
    Check(Near(frame.query_direction[1], -1.0f) && Near(lean.camera[1], -0.20f) && Near(lean.camera[0], 0.0f),
          "the lean comes back in the host's world axes");

    turned.aim_forward[2] = 2.0f;
    CameraUnlockFrame unused = {};
    unused.struct_size = sizeof(unused);
    Check(cameraunlock_session_frame(&turned, &unused) == CAMERAUNLOCK_ERROR, "an aim that is not unit length is refused");
}

void TestAbsurdPose() {
    std::cout << "\n[c interface: a pose no tracker sends]\n";
    const Sender& sender = Tracker();
    sender.Pose(1.0, 2.0, 0.0, 20.0, 0.0, 0.0);
    Check(WaitFor([] { return Near(Settle().head_pitch, 2.0f); }), "a pose to start from");

    // Finite as floats, so the check for NaN and infinity passes them, and far enough apart that
    // the step between them is not finite.
    const double absurd[5][6] = {{1.0, 3e38, 0.0, 20.0, 0.0, 0.0},
                                 {1.0, -3e38, 0.0, 20.0, 0.0, 0.0},
                                 {1.0, 3e38, 0.0, 20.0, 0.0, 0.0},
                                 {1.0, 0.0, 0.0, 3e38, -3e38, 3e38},
                                 {100000.0, 0.0, 0.0, 20.0, 0.0, 0.0}};
    CameraUnlockFrame frame = {};
    for (const double* pose : absurd) {
        sender.Pose(pose[0], pose[1], pose[2], pose[3], pose[4], pose[5]);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        frame = Settle();
    }
    Check(std::isfinite(frame.head_pitch) && std::isfinite(frame.pitch) && std::isfinite(frame.head_yaw),
          "datagrams with angles at the ends of a float's range do not leave the view on a NaN");
    Check(Near(frame.head_pitch, 2.0f) && Near(frame.head_yaw, 1.0f) && Near(frame.head_x, 0.20f),
          "they are refused whole: the pose is the last real one");
    sender.Pose(1.5, 2.5, 0.0, 20.0, 0.0, 0.0);
    Check(WaitFor([] { return Near(Settle().head_pitch, 2.5f); }), "and the tracker's next pose is followed");

    sender.Pose(1.5, 2.5, 0.0, 20.0, 0.0, 0.0);
    sender.Pose(170.5, 80.5, -170.5, -500.0, 500.0, 500.0);
    sender.Pose(170.0, 80.0, -170.0, -500.0, 500.0, 500.0);
    Check(WaitFor([] { return Near(Settle().head_pitch, 80.0f, 0.05f) && Near(Settle().head_yaw, 170.0f, 0.05f); }),
          "a pose at the far end of what a tracker sends is taken");
    sender.Pose(1.0, 0.0, 0.0, 20.0, 0.0, 0.0);
    sender.Pose(1.5, 0.0, 0.0, 20.0, 0.0, 0.0);
    Check(WaitYaw(1.5f), "back to the middle");
}

void TestSmoothingByConnection() {
    std::cout << "\n[c interface: the smoothing the connection selects]\n";
    CameraUnlockSettings settings = Defaults();
    settings.local_smoothing = 0.0f;
    settings.remote_smoothing = 0.95f;
    cameraunlock_session_configure(&settings);

    const auto after_a_fifth_of_a_second = [](const char* address) {
        cameraunlock_session_stop();
        cameraunlock_session_start(kPort);
        const Sender sender(address, kPort);
        sender.Pose(0.0);
        if (!WaitFor([] { return (Frame(Input(CAMERAUNLOCK_FRAME_ACTIVE, 0.0f)).flags & CAMERAUNLOCK_STATE_POSE) != 0; })) {
            Check(false, std::string("a pose arrives from ") + address);
        }
        Settle();
        sender.Pose(6.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CameraUnlockFrame frame = {};
        for (int i = 0; i < 12; ++i) frame = Frame(Input(CAMERAUNLOCK_FRAME_ACTIVE));
        return frame;
    };

    const CameraUnlockFrame local = after_a_fifth_of_a_second("127.0.0.1");
    Check((local.flags & CAMERAUNLOCK_STATE_REMOTE) == 0 && local.head_yaw > 5.5f,
          "a loopback sender gets local_smoothing: 6 degrees is nearly reached in a fifth of a second");

    const std::string own = OwnAddress();
    if (own.empty()) {
        std::cout << "  [SKIP] this machine has no IPv4 address but loopback, so no remote sender\n";
    } else {
        const CameraUnlockFrame remote = after_a_fifth_of_a_second(own.c_str());
        Check((remote.flags & CAMERAUNLOCK_STATE_REMOTE) != 0,
              "a sender on this machine's own network address (" + own + ") is remote");
        Check(remote.head_yaw > 0.5f && remote.head_yaw < 4.0f,
              "and gets remote_smoothing: the same turn is under way and far from done");
    }
    cameraunlock_session_stop();
}

void TestConfig(const fs::path& scratch) {
    std::cout << "\n[c interface: the config]\n";
    const std::string file = (scratch / "CameraUnlock.ini").u8string();
    const std::string defaults = (scratch / "defaults" / "Defaults.ini").u8string();
    const std::string rendered = (scratch / "rendered.ini").u8string();

    CameraUnlockConfig config = {};
    config.struct_size = sizeof(config);
    Check(cameraunlock_config_load(file.c_str(), defaults.c_str(), &config) == CAMERAUNLOCK_ERROR,
          "a load before the rows are described is refused");

    Check(cameraunlock_config_describe("Test Game") == CAMERAUNLOCK_OK, "the description starts");
    const char* plain[] = {"UdpPort", "EnableOnStartup", "DataFreshnessMs", "LocalSmoothing", "RemoteSmoothing",
                           "PositionLimitX", "PositionLimitY", "PositionLimitYDown", "PositionLimitZ", "PositionLimitZBack",
                           "CollisionEnabled", "CollisionReleaseSmoothing", "ToggleKey", "CycleTrackingModeKey",
                           "TrueFreeLookKey", "LightMultiplier"};
    bool added = true;
    for (const char* name : plain) added = cameraunlock_config_concept(name, 0, nullptr, nullptr) == CAMERAUNLOCK_OK && added;
    const char* writable[] = {"WorldSpaceYaw", "RotationEnabled", "PositionEnabled", "TrueFreeLook", "FreeLookMarker",
                              "StockSights"};
    for (const char* name : writable) {
        added = cameraunlock_config_concept(name, CAMERAUNLOCK_ROW_WRITABLE, nullptr, nullptr) == CAMERAUNLOCK_OK && added;
    }
    added = cameraunlock_config_concept("CollisionMargin", 0, "How far the view is held off a wall, in map tiles.",
                                        "0.12") == CAMERAUNLOCK_OK && added;
    Check(added, "the fleet's rows are named by concept: " + LastError());
    Check(cameraunlock_config_concept("FieldOfVision", 0, nullptr, nullptr) == CAMERAUNLOCK_ERROR &&
              LastError().find("not a canonical concept") != std::string::npos,
          "a name that is no concept is refused");
    Check(cameraunlock_config_concept("UdpPort", 0, nullptr, nullptr) == CAMERAUNLOCK_ERROR, "and a concept named twice");
    Check(cameraunlock_config_concept("LocalSmoothing", 0, nullptr, "0.5") == CAMERAUNLOCK_ERROR,
          "a global concept takes no default of the game's own without CAMERAUNLOCK_ROW_PER_GAME");

    const std::int32_t fov = cameraunlock_config_local_float(
        "General", "FieldOfView", "Field of view in degrees.", CAMERAUNLOCK_ROW_LIVE, 65.0f, 40.0f, 110.0f);
    const std::int32_t quality = cameraunlock_config_local_enum(
        "General", "Quality", "The graphics mode.", CAMERAUNLOCK_ROW_WRITABLE, "Automatic,Low,Medium,High", 1);
    const std::int32_t arms = cameraunlock_config_local_bool("Content", "OwnArms", "The mod's own arms.", 0, 1);
    const std::int32_t count = cameraunlock_config_local_int("Content", "Trees", "How many trees.", 0, 12, 0, 100);
    const std::int32_t key =
        cameraunlock_config_local_hotkey("GraphicsKey", "Goes to the next graphics mode.", 0, "f9", 0x100);
    Check(fov == 0 && quality == 1 && arms == 2 && count == 3 && key == 4, "local rows are numbered as they are added: " + LastError());
    Check(cameraunlock_config_local_enum("General", "Style", "The style.", 0, "vivid,Natural", 0) == CAMERAUNLOCK_ERROR,
          "an enum word that is not PascalCase is refused, by core's own codec: " + LastError());
    Check(cameraunlock_config_local_hotkey("StyleKey", "The style.", 0, "F10", 0x100) == CAMERAUNLOCK_ERROR,
          "two key rows cannot share a bit");
    Check(cameraunlock_config_local_hotkey("StyleKey", "The style.", 0, "F10", 0x8) == CAMERAUNLOCK_ERROR,
          "and a local key row cannot take one of the fleet's bits");
    Check(cameraunlock_config_local_float("General", "FieldOfView", "Again.", 0, 65.0f, 40.0f, 110.0f) == CAMERAUNLOCK_ERROR,
          "a key used twice is refused");

    Check(cameraunlock_config_render(rendered.c_str()) == CAMERAUNLOCK_OK, "the description renders: " + LastError());
    const std::string fresh = ReadFile(scratch / "rendered.ini");
    Check(fresh.find("[CameraUnlock]") != std::string::npos && fresh.find("FieldOfView=65") != std::string::npos &&
              fresh.find("Quality=Low") != std::string::npos && fresh.find("GraphicsKey=F9") != std::string::npos &&
              fresh.find("CollisionMargin=0.12") != std::string::npos && fresh.find("LocalSmoothing=default") != std::string::npos,
          "the render holds the stamp, the local rows at their defaults and the global concepts as default");

    fs::create_directories(scratch / "defaults");
    Check(cameraunlock_config_load(file.c_str(), defaults.c_str(), &config) == CAMERAUNLOCK_LOAD_CREATED,
          "a first load creates the file: " + LastError());
    Check(ReadFile(scratch / "CameraUnlock.ini") == fresh, "with the bytes the render wrote");
    Check(fs::exists(scratch / "defaults" / "Defaults.ini"), "and Defaults.ini where the load was told it is");
    Check(config.udp_port == 4242 && config.enable_on_startup == 1 && config.world_space_yaw == 1 &&
              config.settings.tracking_mode == CAMERAUNLOCK_TRACKING_ROTATION_AND_POSITION &&
              config.settings.aim_mode == CAMERAUNLOCK_AIM_SIGHTS_LOCKED && config.settings.collision_enabled == 1 &&
              Near(config.settings.collision_margin, 0.12f) && Near(config.settings.remote_smoothing, 0.15f),
          "the fleet's rows come back as one struct");
    Check(cameraunlock_session_configure(&config.settings) == CAMERAUNLOCK_OK, "whose settings the session takes as they are");
    Check(cameraunlock_config_concept("AimDecoupling", 0, nullptr, nullptr) == CAMERAUNLOCK_ERROR,
          "the rows are settled once the file is loaded");

    float degrees = 0.0f;
    std::int32_t number = -1;
    Check(cameraunlock_config_get_float(fov, &degrees) == CAMERAUNLOCK_OK && degrees == 65.0f, "a float row reads back");
    Check(cameraunlock_config_get_int(quality, &number) == CAMERAUNLOCK_OK && number == 1, "an enum row as its word's place");
    Check(cameraunlock_config_get_int(arms, &number) == CAMERAUNLOCK_OK && number == 1, "a bool row as 1");
    Check(cameraunlock_config_get_int(count, &number) == CAMERAUNLOCK_OK && number == 12, "an int row");
    Check(cameraunlock_config_get_int(fov, &number) == CAMERAUNLOCK_ERROR, "a float row is not read as an int");
    Check(cameraunlock_config_get_int(99, &number) == CAMERAUNLOCK_ERROR, "and a row that is none is refused");

    Check(cameraunlock_config_save_int(quality, 3) == CAMERAUNLOCK_SAVE_SAVED, "a writable row saves: " + LastError());
    Check(ReadFile(scratch / "CameraUnlock.ini").find("Quality=High") != std::string::npos, "as its word");
    Check(cameraunlock_config_get_int(quality, &number) == CAMERAUNLOCK_OK && number == 3, "and reads back as saved");
    Check(cameraunlock_config_save_int(arms, 0) == CAMERAUNLOCK_ERROR &&
              LastError().find("Writable") != std::string::npos,
          "a row not marked writable does not: " + LastError());
    Check(cameraunlock_config_save_int(quality, 9) == CAMERAUNLOCK_ERROR, "nor a value that names no word");

    cameraunlock_session_cycle_tracking_mode();
    cameraunlock_session_cycle_aim_mode();
    Check(cameraunlock_config_save_tracking_mode() == CAMERAUNLOCK_SAVE_SAVED &&
              cameraunlock_config_save_aim_mode() == CAMERAUNLOCK_SAVE_SAVED &&
              cameraunlock_config_save_world_space_yaw(0) == CAMERAUNLOCK_SAVE_SAVED,
          "the session's modes and the yaw mode save: " + LastError());
    std::string saved = ReadFile(scratch / "CameraUnlock.ini");
    Check(saved.find("PositionEnabled=false") != std::string::npos && saved.find("RotationEnabled=true") != std::string::npos &&
              saved.find("TrueFreeLook=true") != std::string::npos && saved.find("FreeLookMarker=true") != std::string::npos &&
              saved.find("WorldSpaceYaw=false") != std::string::npos,
          "as the rows core's encoders give");

    Check(cameraunlock_config_reload() == CAMERAUNLOCK_RELOAD_UNCHANGED, "a file nobody touched is not read again");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    saved.replace(saved.find("FieldOfView=65"), 14, "FieldOfView=90");
    saved.replace(saved.find("Trees=12"), 8, "Trees=40");
    std::ofstream(scratch / "CameraUnlock.ini", std::ios::binary | std::ios::trunc) << saved;
    Check(cameraunlock_config_reload() == CAMERAUNLOCK_RELOAD_APPLIED, "a file the player saved is: " + LastError());
    Check(cameraunlock_config_get_float(fov, &degrees) == CAMERAUNLOCK_OK && degrees == 90.0f, "a live row holds the new value");
    Check(cameraunlock_config_get_int(count, &number) == CAMERAUNLOCK_OK && number == 12,
          "a row not marked live keeps what the load gave it");

    Check(cameraunlock_hotkeys_start() == CAMERAUNLOCK_OK, "the key lists go on the poller: " + LastError());
    Check(cameraunlock_hotkeys_start() == CAMERAUNLOCK_ERROR, "once");
    cameraunlock_hotkeys_drop();
    Check(cameraunlock_hotkeys_take() == 0, "with no key pressed nothing is taken");
}

void TestWindow() {
    std::cout << "\n[c interface: the game's window]\n";
    const HWND window = CreateWindowExW(0, L"STATIC", L"cameraunlock c interface test", WS_OVERLAPPEDWINDOW, 13, 17, 640,
                                        480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(window != nullptr, "a bordered window that is never shown");
    TakeLog();
    const std::uint64_t handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window));
    Check(cameraunlock_window_center(handle) == 1, "the window is centred: " + LastError());
    RECT placed = {};
    GetWindowRect(window, &placed);
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
    const int left_gap = placed.left - monitor.rcWork.left;
    const int right_gap = monitor.rcWork.right - placed.right;
    Check(left_gap - right_gap >= -1 && left_gap - right_gap <= 1 && !(placed.left == 13 && placed.top == 17),
          "in the work area of its monitor, where it was not before");
    Check(!TakeLog().empty(), "and the move is in the log");
    Check(cameraunlock_window_center(handle) == 1 && TakeLog().empty(),
          "a second call finds it centred and logs nothing");
    DestroyWindow(window);
    Check(cameraunlock_window_center(handle) == 0 && !TakeLog().empty(),
          "a window that is gone is left alone, with the reason in the log");
}

void TestLog(const fs::path& scratch) {
    std::cout << "\n[c interface: the log]\n";
    Check(cameraunlock_log_write("too early") == CAMERAUNLOCK_ERROR, "a line for the file log before it is open is refused");
    Check(!TakeLog().empty(), "until it is open the lines wait to be taken");
    Check(cameraunlock_log_take(nullptr, 0) == 0, "and are forgotten once taken");
    cameraunlock_session_configure(nullptr);
    const std::string nowhere = (scratch / "no such folder" / "Mod.log").u8string();
    Check(cameraunlock_log_open(nowhere.c_str()) == CAMERAUNLOCK_ERROR &&
              LastError().find("could not be created") != std::string::npos,
          "a log file that cannot be created is an error: " + LastError());
    const std::string file = (scratch / "Mod.log").u8string();
    Check(cameraunlock_log_open(file.c_str()) == CAMERAUNLOCK_OK, "the file log opens at a full path: " + LastError());
    Check(cameraunlock_log_write("the host's own line") == CAMERAUNLOCK_OK, "the host writes a line");
    const std::string trace = std::string(3000, 'a') + "\r\n\tat the end of a long stack trace";
    Check(cameraunlock_log_write(trace.c_str()) == CAMERAUNLOCK_OK &&
              ReadFile(scratch / "Mod.log").find(trace) != std::string::npos,
          "an entry longer than a line, with line breaks in it, is written whole");
    for (int i = 0; i < 50; ++i) cameraunlock_session_configure(nullptr);
    const std::string repeated = ReadFile(scratch / "Mod.log");
    std::size_t refusals = 0;
    for (std::size_t at = repeated.find("CameraUnlockSettings is NULL"); at != std::string::npos;
         at = repeated.find("CameraUnlockSettings is NULL", at + 1)) {
        ++refusals;
    }
    Check(refusals >= 2 && refusals <= 3, "a call that fails the same way over and over is in the log once a second, not every time: " +
                                              std::to_string(refusals));
    cameraunlock_session_start(-1);
    const std::string text = ReadFile(scratch / "Mod.log");
    Check(text.find("cameraunlock_session_configure: CameraUnlockSettings is NULL") != std::string::npos,
          "a line that was waiting when the file opened is in it");
    Check(text.find("the host's own line") != std::string::npos, "the host's line is in it");
    Check(text.find("udp_port is outside 1 to 65535") != std::string::npos, "and core's lines from then on, each flushed");
    Check(cameraunlock_log_take(nullptr, 0) == 0, "with nothing left to take");
}

}  // namespace

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    const fs::path scratch = fs::temp_directory_path() / ("cameraunlock-c-tests-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(scratch);
    fs::create_directories(scratch);

    TestBoundary();
    TestPortInUse();
    TestReceiving();
    TestModes();
    TestLean();
    TestAbsurdPose();
    TestSmoothingByConnection();
    TestConfig(scratch);
    TestWindow();
    TestLog(scratch);

    std::cout << (g_failures == 0 ? "\nAll tests passed!\n" : "\n" + std::to_string(g_failures) + " test(s) FAILED\n");
    // The log file is open in this process until it ends, so the folder goes at the next run.
    return g_failures == 0 ? 0 : 1;
}
