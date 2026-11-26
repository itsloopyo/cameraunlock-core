// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// A held weapon drawn at a FOV of its own: the quaternion turn
// (cameraunlock/camera/viewmodel_fov.h), the world-to-eye matrix form
// (cameraunlock/camera/viewmodel_matrix.h, its test ported from Headstart's
// native/common/tests/common_tests.cpp) and the axis-row form
// (cameraunlock/rendering/weapon_pass_axes.h).

#include <cameraunlock/camera/viewmodel_fov.h>
#include <cameraunlock/camera/viewmodel_matrix.h>
#include <cameraunlock/rendering/weapon_pass_axes.h>

#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {

using namespace cameraunlock::unreal;
namespace vm = cameraunlock::camera::viewmodel;
namespace wm = cameraunlock::camera::viewmodel_matrix;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool Near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

constexpr double kPi = 3.14159265358979323846;
constexpr double kRad = kPi / 180;

void TestWeaponFov() {
    std::cout << "WeaponFovRatio / WeaponTurn / WeaponLeanShift:\n";
    Check(Near(vm::WeaponFovRatio(90, 1.0, 0, false), 1.0), "a weapon at the world's FOV has a ratio of 1");
    Check(Near(vm::WeaponFovRatio(60, std::tan(20 * kRad), std::tan(40 * kRad), true), std::tan(15 * kRad) / std::tan(20 * kRad)),
          "a weapon FOV that narrows with the view keeps its share of the angle");
    Check(Near(vm::WeaponFovRatio(60, std::tan(20 * kRad), std::tan(40 * kRad), false), std::tan(30 * kRad) / std::tan(20 * kRad)),
          "one that does not keeps its own FOV");

    const FQuat4d clean = QuatFromEulerDeg(0, 0, 0);
    const std::optional<FQuat4d> same = vm::WeaponTurn(clean, clean, 0.5);
    Check(same && Near(std::fabs(same->W), 1), "with the view down the aim there is no turn");

    const FQuat4d drawn = QuatFromEulerDeg(5, 20, 3);
    const double ratio = 0.5;
    const std::optional<FQuat4d> turn = vm::WeaponTurn(drawn, clean, ratio);
    Check(turn.has_value(), "an aim in front of the view gives a turn");
    const FVector aimView = QuatRotateVec(QuatInv(drawn), {1, 0, 0});
    const FVector turned = QuatRotateVec(QuatInv(drawn), QuatRotateVec(turn.value_or(FQuat4d{0, 0, 0, 1}), {1, 0, 0}));
    Check(Near(turned.Y / turned.X, ratio * aimView.Y / aimView.X, 1e-12) &&
              Near(turned.Z / turned.X, ratio * aimView.Z / aimView.X, 1e-12),
          "the turned aim's lateral parts are the ratio of the clean aim's, as the drawn view sees them");
    Check(!vm::WeaponTurn(QuatFromEulerDeg(0, 120, 0), clean, ratio), "an aim behind the view gives nothing");

    const FVector cleanEye{0, 0, 0}, drawnEye{0, -10, 2};
    const FVector none = vm::WeaponLeanShift(drawn, {0, 0, 0, 1}, cleanEye, drawnEye, 1.0);
    Check(Near(none.X, 0) && Near(none.Y, 0) && Near(none.Z, 0), "with no turn and a ratio of 1 there is no shift");
    const FVector shift = vm::WeaponLeanShift(clean, {0, 0, 0, 1}, cleanEye, drawnEye, 3.0);
    Check(Near(shift.X, 0) && Near(shift.Y, 2 * 10) && Near(shift.Z, 2 * -2),
          "with no turn the lateral lean is scaled by the ratio less the lean itself");

    const FVector p = vm::TurnedAbout({200, 0, 0}, {100, 0, 0}, QuatFromEulerDeg(0, 90, 0));
    Check(Near(p.X, 100) && Near(p.Y, 100) && Near(p.Z, 0), "a point is turned about its pivot");

    namespace hb = cameraunlock::camera::head_bones;
    const hb::Xf placed{QuatFromEulerDeg(0, 30, 0), {50, 20, 10}};
    const FQuat4d worldTurn = QuatFromEulerDeg(4, 12, -2);
    const FVector eye{60, 25, 70}, move{1, -2, 3};
    const hb::Xf local = vm::LocalTurn(placed, 2.0, worldTurn, eye, move);
    const FVector bone{3, 4, 5};
    const hb::Xf at = hb::Compose(placed, {{0, 0, 0, 1}, {bone.X * 2, bone.Y * 2, bone.Z * 2}});
    const FVector want = vm::TurnedAbout(at.t, eye, worldTurn);
    const hb::Xf moved = hb::Compose(local, {{0, 0, 0, 1}, bone});
    const hb::Xf drawnAt = hb::Compose(placed, {{0, 0, 0, 1}, {moved.t.X * 2, moved.t.Y * 2, moved.t.Z * 2}});
    Check(Near(drawnAt.t.X, want.X + move.X, 1e-9) && Near(drawnAt.t.Y, want.Y + move.Y, 1e-9) &&
              Near(drawnAt.t.Z, want.Z + move.Z, 1e-9),
          "LocalTurn moves a bone of a scaled mesh where the world turn about the eye and the shift put it");
}

// A world-to-eye view looking along `yaw` (degrees, about +z) and `pitch` from
// `eye`, OpenGL's way: x right, y up, -z forward, turned about its forward by
// `roll`. With `mirrored`, world x is mirrored first, as Cube's viewmatrix does.
wm::Mat GlView(const wm::Vec& eye, double yaw, double pitch, double roll = 0, bool mirrored = false) {
    const double cy = std::cos(yaw * kRad), sy = std::sin(yaw * kRad), cp = std::cos(pitch * kRad), sp = std::sin(pitch * kRad);
    const wm::Vec f = {cy * cp, sy * cp, sp};
    const wm::Vec r = {sy, -cy, 0};
    const wm::Vec u = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    const double cr = std::cos(roll * kRad), sr = std::sin(roll * kRad);
    const wm::Vec r2 = {r[0] * cr + u[0] * sr, r[1] * cr + u[1] * sr, r[2] * cr + u[2] * sr};
    const wm::Vec u2 = {u[0] * cr - r[0] * sr, u[1] * cr - r[1] * sr, u[2] * cr - r[2] * sr};
    wm::Vec rows[3] = {r2, u2, {-f[0], -f[1], -f[2]}};
    if (mirrored)
        for (wm::Vec& row : rows) row[0] = -row[0];
    wm::Mat m{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) m[j * 4 + i] = rows[i][j];
        m[12 + i] = -(rows[i][0] * eye[0] + rows[i][1] * eye[1] + rows[i][2] * eye[2]);
    }
    m[15] = 1;
    return m;
}

constexpr double kNearPlane = 0.54, kFarPlane = 1024, kAspect = 16.0 / 9;

// An OpenGL perspective: vertical half-angle tangent `t`, the engine's aspect, near and far.
wm::Mat Perspective(double t) {
    wm::Mat p{};
    p[0] = 1 / (t * kAspect);
    p[5] = 1 / t;
    p[10] = (kFarPlane + kNearPlane) / (kNearPlane - kFarPlane);
    p[11] = -1;
    p[14] = 2 * kFarPlane * kNearPlane / (kNearPlane - kFarPlane);
    return p;
}

// How far from the eye the engine sets the point it reads back.
const double kReach = 4 * std::sqrt(kAspect);

using Homogeneous = std::array<double, 4>;

Homogeneous Through(const wm::Mat& m, const Homogeneous& p) {
    Homogeneous out{};
    for (int r = 0; r < 4; ++r) out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] * p[3];
    return out;
}

// Where the engine reads the muzzle back to, as MuzzleShare has it: the pixel
// the weapon's pass draws `muzzle` on through `view`, taken back into the world
// on the near plane through `inverseViewProjection`, and the point kReach from
// `eye` on the line to it.
wm::Vec ReadBack(const wm::Mat& view, const wm::Mat& inverseViewProjection, double tanWeapon, const wm::Vec& muzzle,
                 const wm::Vec& eye) {
    const Homogeneous drawn = Through(wm::Mul(Perspective(tanWeapon), view), {muzzle[0], muzzle[1], muzzle[2], 1});
    const Homogeneous world = Through(inverseViewProjection, {drawn[0] / drawn[3], drawn[1] / drawn[3], -1, 1});
    wm::Vec ray{};
    for (int i = 0; i < 3; ++i) ray[i] = world[i] / world[3] - eye[i];
    const double length = std::hypot(ray[0], ray[1], ray[2]);
    wm::Vec point{};
    for (int i = 0; i < 3; ++i) point[i] = eye[i] + ray[i] * kReach / length;
    return point;
}

std::pair<double, double> ScreenOf(const wm::Mat& proj, const wm::Mat& view, const wm::Vec& p) {
    const wm::Vec e = wm::Apply(view, p);
    double c[4];
    for (int r = 0; r < 4; ++r) c[r] = proj[r] * e[0] + proj[4 + r] * e[1] + proj[8 + r] * e[2] + proj[12 + r];
    return {c[0] / c[3], c[1] / c[3]};
}

double Distance(const wm::Vec& a, const wm::Vec& b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

void TestWeaponMatrix() {
    std::cout << "viewmodel_matrix:\n";
    const double tanWorld = std::tan(67.7 / 2 * kRad), tanTags = std::tan(35 * kRad);
    const double k = tanWorld / tanTags;
    bool all = true;
    for (const bool mirrored : {false, true}) {
        const wm::Vec eye = {100, 200, 50};
        const wm::Mat clean = GlView(eye, 30, -10, 0, mirrored);
        const wm::Vec at = wm::Eye(clean);
        all = all && Near(at[0], 100) && Near(at[1], 200) && Near(at[2], 50);
        const wm::Mat unchanged = wm::WeaponView(clean, clean, k, 0.3);
        for (int i = 0; i < 16; ++i) all = all && Near(unchanged[i], clean[i]);

        // A muzzle a hand's width in front of the eye, and one beyond the
        // engine's reach as Red Eclipse's model puts it (an eye share of 2.7
        // measured in game); the head turned, rolled and leaned sideways.
        for (const wm::Vec& muzzleEye : {wm::Vec{0.35, -0.3, -0.9}, wm::Vec{3.5, -2.5, -13}}) {
            const bool beyond = muzzleEye[2] < -10;
            const wm::Vec muzzle = wm::Apply(wm::Inverse(clean), muzzleEye);
            const wm::Vec leaned = wm::Apply(wm::Inverse(clean), {0.8, 0.1, -0.2});
            const wm::Mat drawn = GlView(leaned, 55, 8, 12, mirrored);
            const wm::Mat proj = Perspective(tanWorld);
            const wm::Mat drawnInverse = wm::Inverse(wm::Mul(proj, drawn));

            const wm::Vec stock = ReadBack(clean, wm::Inverse(wm::Mul(proj, clean)), tanTags, muzzle, eye);
            const wm::Vec moved = ReadBack(drawn, drawnInverse, tanTags, muzzle, eye);
            all = all && Distance(moved, stock) > 0.5;
            const wm::Vec held = ReadBack(clean, wm::CleanInverse(clean, drawn, drawnInverse), tanTags, muzzle, eye);
            all = all && Distance(held, stock) < 1e-6;

            const std::optional<double> share = wm::MuzzleShare(clean, muzzle, stock);
            all = all && share && *share > 0 && (beyond ? *share > 1 : *share < 1);
            all = all && !wm::MuzzleShare(clean, stock, stock);
            if (!share) continue;
            const wm::Mat pass = wm::WeaponView(clean, drawn, k, *share);
            const auto [gx, gy] = ScreenOf(Perspective(tanTags), pass, muzzle);
            const auto [wx, wy] = ScreenOf(proj, drawn, stock);
            all = all && Near(gx, wx) && Near(gy, wy);
            const auto [cx, cy] = ScreenOf(Perspective(tanTags), wm::WeaponView(clean, GlView(leaned, 30, -10, 0, mirrored), k, 0), muzzle);
            const auto [sx, sy] = ScreenOf(Perspective(tanTags), clean, muzzle);
            all = all && Near(cx, sx) && Near(cy, sy);
            const wm::Vec aim = wm::Apply(wm::Inverse(clean), {0, 0, -1e6});
            const auto [ax, ay] = ScreenOf(Perspective(tanTags), pass, aim);
            const auto [fx, fy] = ScreenOf(proj, drawn, aim);
            all = all && Near(ax, fx, 1e-5) && Near(ay, fy, 1e-5);
        }
    }
    Check(all, "the weapon's pass draws the muzzle on the read-back point and the clean aim on the crosshair, mirrored or not");
    bool threw = false;
    try {
        wm::Inverse(wm::Mat{});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Check(threw, "a matrix with no inverse throws");
}

struct Rows {
    float m[3][4];
};

Rows FromRotator(const FRotator& r) {
    FVector f, rt, u;
    AxesFromRotator(r, f, rt, u);
    const FVector axes[3] = {f, rt, u};
    Rows out{};
    for (int i = 0; i < 3; ++i) {
        out.m[i][0] = static_cast<float>(axes[i].X);
        out.m[i][1] = static_cast<float>(axes[i].Y);
        out.m[i][2] = static_cast<float>(axes[i].Z);
        out.m[i][3] = static_cast<float>(i + 1);
    }
    return out;
}

void TestWeaponPassAxes() {
    std::cout << "WeaponPassAxes:\n";
    using cameraunlock::rendering::WeaponPassAxes;
    const Rows clean = FromRotator({0, 0, 0});
    Rows drawn = FromRotator({6, 25, 4});
    for (int j = 0; j < 3; ++j) drawn.m[1][j] *= 3.f;
    Rows out{};
    WeaponPassAxes(clean.m, drawn.m, 2.0, out.m);

    double a[3], unit[3][3];
    const double rightLength = 3.0;
    for (int i = 0; i < 3; ++i) {
        const double len = i == 1 ? rightLength : 1.0;
        for (int j = 0; j < 3; ++j) unit[i][j] = drawn.m[i][j] / len;
    }
    for (int i = 0; i < 3; ++i) a[i] = unit[i][0] * clean.m[0][0] + unit[i][1] * clean.m[0][1] + unit[i][2] * clean.m[0][2];
    double b[3] = {a[0], a[1] / 2.0, a[2] / 2.0};
    const double bn = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    bool placed = true, lengths = true;
    for (int i = 0; i < 3; ++i) {
        const double len = std::sqrt(double(out.m[i][0]) * out.m[i][0] + double(out.m[i][1]) * out.m[i][1] + double(out.m[i][2]) * out.m[i][2]);
        lengths = lengths && Near(len, i == 1 ? rightLength : 1.0, 1e-5) && out.m[i][3] == drawn.m[i][3];
        const double along = (out.m[i][0] * clean.m[0][0] + out.m[i][1] * clean.m[0][1] + out.m[i][2] * clean.m[0][2]) / len;
        placed = placed && Near(along, b[i] / bn, 1e-5);
    }
    Check(placed, "in the turned axes the clean aim's lateral parts are the drawn view's over the ratio");
    Check(lengths, "each row keeps the drawn row's length and fourth float");

    Rows same{};
    WeaponPassAxes(clean.m, clean.m, 2.0, same.m);
    bool unchanged = true;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) unchanged = unchanged && same.m[i][j] == clean.m[i][j];
    Check(unchanged, "no turn between the views leaves the drawn axes");

    const Rows behind = FromRotator({0, 150, 0});
    Rows back{};
    WeaponPassAxes(clean.m, behind.m, 2.0, back.m);
    bool asDrawn = true;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) asDrawn = asDrawn && back.m[i][j] == behind.m[i][j];
    Check(asDrawn, "an aim behind the drawn view leaves the drawn axes");
}

}  // namespace

int RunViewmodelTests() {
    std::cout << "Viewmodel FOV tests\n";
    TestWeaponFov();
    TestWeaponMatrix();
    TestWeaponPassAxes();
    if (g_failures == 0) {
        std::cout << "Viewmodel FOV tests: all passed\n";
    } else {
        std::cout << "Viewmodel FOV tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
