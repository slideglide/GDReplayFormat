#include "gdr/gdr.hpp"

#include <cstdint>
#include <iostream>
#include <utility>

void assert_(bool value, const char* message) {
    if (value) return;
    std::cerr << "Assert: '" << message << "' failed" <<  std::endl;
    std::exit(1);
}

#undef assert
#define assert(expr) assert_(expr, #expr)

using namespace gdr;

struct MyInput : Input<"MyInput"> {
    float xpos;

    MyInput() = default;

    MyInput(uint64_t frame, uint8_t button, bool player2, bool down, float xpos)
        : Input(frame, button, player2, down), xpos(xpos) {}

    void parseExtension(binary_reader& reader) override {
        reader >> xpos;
    }

    void saveExtension(binary_writer& writer) const override {
        writer << xpos;
    }
};

struct MyReplay : Replay<MyReplay, MyInput> {
    int attempts;

    MyReplay() : Replay("TestBot", 1) {}

    void parseExtension(binary_reader& reader) override {
        reader >> attempts;
    }

    void saveExtension(binary_writer& writer) const override {
        writer << attempts;
    }

    bool shouldParseExtension() const override {
        return botInfo.name == "TestBot" && botInfo.version == 1;
    }
};

static void testExtensions() {
    MyReplay replay;

    replay.levelInfo.id = 5;
    replay.coins = 2;
    replay.framerate = 60.0;
    replay.levelInfo.name = "ste";
    replay.author = "pobert";
    replay.description = "we testing up in here";
    replay.attempts = 50;

    replay.deaths.push_back(5);
    replay.deaths.push_back(1002);

    replay.inputs.push_back(MyInput(20, 1, false, true, 5.f));
    replay.inputs.push_back(MyInput(543, 1, false, false, 6.f));
    replay.inputs.push_back(MyInput(1002, 1, false, true, 7.f));

    auto exportRes = replay.exportData();
    if (exportRes.isErr()) {
        std::cerr << exportRes.unwrapErr() << std::endl;
        std::exit(1);
    }
    std::vector<uint8_t> output = std::move(exportRes.unwrap());

    auto importRes = MyReplay::importData(output);
    if (importRes.isErr()) {
        std::cerr << importRes.unwrapErr() << std::endl;
        std::exit(1);
    }
    replay = std::move(importRes.unwrap());

    assert(replay.inputs.size() == 3);
    assert(replay.deaths.size() == 2);
    assert(replay.deaths[0] == 5);
    assert(replay.deaths[1] == 1002);
    assert(replay.inputs[0].frame == 20); assert(replay.inputs[0].button == 1); assert(replay.inputs[0].player2 == false); assert(replay.inputs[0].down == true); assert(replay.inputs[0].xpos == 5.f);
    assert(replay.inputs[1].frame == 543); assert(replay.inputs[1].button == 1); assert(replay.inputs[1].player2 == false); assert(replay.inputs[1].down == false); assert(replay.inputs[1].xpos == 6.f);
    assert(replay.inputs[2].frame == 1002); assert(replay.inputs[2].button == 1); assert(replay.inputs[2].player2 == false); assert(replay.inputs[2].down == true); assert(replay.inputs[2].xpos == 7.f);
    assert(replay.attempts == 50);
}

template <typename T>
static T writeThenRead(T value, size_t expectedSize) {
    binary_writer writer;
    writer << value;
    assert(writer.size() == expectedSize);

    T result{};
    binary_reader reader(writer.data());
    reader >> result;
    assert(reader.empty());
    return result;
}

static void testVarints() {
    for (int v : {0, 1, 127, 128, 300, INT32_MAX}) assert(writeThenRead(v, v < 128 ? 1 : v < 16384 ? 2 : v == INT32_MAX ? 5 : 3) == v);
    // negative ints take 10 bytes
    for (int v : {-1, -629770876, -1644654779, INT32_MIN}) assert(writeThenRead(v, 10) == v);
    assert(writeThenRead(uint64_t(1) << 35, 6) == (uint64_t(1) << 35));
    assert(writeThenRead(UINT64_MAX, 10) == UINT64_MAX);
    assert(writeThenRead(int64_t(INT64_MIN), 10) == INT64_MIN);
}

static void testBasicTypes() {
    binary_writer writer;
    writer << 1.5f << -2.25 << std::string("héllo") << true;

    float f = 0; double d = 0; std::string str; bool b = false;
    binary_reader reader(writer.data());
    reader >> f >> d >> str >> b;
    assert(f == 1.5f); assert(d == -2.25); assert(str == "héllo"); assert(b);
    assert(reader.empty());

    // floats are big endian
    binary_writer be;
    be << 1.0f;
    assert(be.data() == (std::vector<uint8_t>{0x3F, 0x80, 0x00, 0x00}));
}

static void testPlatformerReplay() {
    Replay<> replay("Bot", 3);
    replay.platformer = true;
    replay.seed = -629770876;
    replay.coins = -1;
    replay.levelInfo = Level("Lvl", 128000000);
    replay.deaths = {10, 10, 4000000000ull};

    replay.inputs.emplace_back(1, 2, false, true);
    replay.inputs.emplace_back(5, 3, true, true);
    replay.inputs.emplace_back(8, 1, false, false);
    replay.inputs.emplace_back(8, 2, true, false);
    replay.inputs.emplace_back(70000, 1, true, true);

    auto exported = replay.exportData();
    assert(exported.isOk());
    std::vector<uint8_t> bytes = exported.unwrap();
    auto imported = Replay<>::importData(bytes);
    assert(imported.isOk());
    auto& r = imported.unwrap();

    assert(r.seed == -629770876); assert(r.coins == -1);
    assert(r.platformer); assert(r.botInfo.name == "Bot"); assert(r.botInfo.version == 3);
    assert(r.levelInfo.id == 128000000); assert(r.levelInfo.name == "Lvl");
    assert(r.deaths == replay.deaths);
    assert(r.inputs.size() == 5);
    for (size_t i = 0; i < 5; i++) {
        assert(r.inputs[i].frame == replay.inputs[i].frame);
        assert(r.inputs[i].button == replay.inputs[i].button);
        assert(r.inputs[i].player2 == replay.inputs[i].player2);
        assert(r.inputs[i].down == replay.inputs[i].down);
    }
}

static void testButtonDefaultsToJump() {
    Replay<> replay;
    replay.inputs.emplace_back(3, 2, false, true);
    replay.inputs.emplace_back(9, 3, true, false);

    auto imported = Replay<>::importData(replay.exportData().unwrap());
    assert(imported.isOk());
    auto& r = imported.unwrap();
    assert(r.inputs.size() == 2);
    assert(r.inputs[0].button == 1); assert(r.inputs[1].button == 1);
    assert(!r.inputs[0].player2); assert(r.inputs[1].player2);
}

static void testSortInputs() {
    Replay<> replay;
    replay.inputs.emplace_back(9, 1, false, true);
    replay.inputs.emplace_back(2, 1, false, true);
    replay.inputs.emplace_back(2, 1, true, true);
    replay.sortInputs();
    assert(replay.inputs[0].frame == 2 && !replay.inputs[0].player2); // stable
    assert(replay.inputs[1].frame == 2 && replay.inputs[1].player2);
    assert(replay.inputs[2].frame == 9);
}

static void testByteOrder() {
    binary_writer writer;
    writer << 1.0 << uint16_t(0) << std::array<uint8_t, 3>{1, 2, 3};
    assert(writer.data() == (std::vector<uint8_t>{0x3F, 0xF0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 1}));

    double d = 0; uint16_t i = 1; std::array<uint8_t, 3> raw{};
    binary_reader reader(writer.data());
    reader >> d >> i >> raw;
    assert(d == 1.0); assert(i == 0); assert((raw == std::array<uint8_t, 3>{1, 2, 3}));
}

static void testMovingResults() {
    Replay<> replay;
    replay.inputs.emplace_back(1, 1, false, true);
    Result<Replay<>> res = Ok(std::move(replay));
    assert(replay.inputs.empty());

    const auto* buffer = res.unwrap().inputs.data();
    Replay<> moved = std::move(res.unwrap());
    assert(moved.inputs.data() == buffer);
}

static void testCopyingResults() {
    Result<int> err = Err<int>(std::string("boom"));
    Result<> fromMutable = Err<>(err.unwrapErr());
    Result<> fromConst = Err<>(std::as_const(err).unwrapErr());
    assert(fromMutable.unwrapErr() == "boom"); assert(fromConst.unwrapErr() == "boom");

    Result<int> ok = Ok(5);
    Result<int> copy = Ok(ok.unwrap());
    assert(copy.unwrap() == 5);
}

static void testHugeFrameGaps() {
    // deltas wrap around so the third frame is lower than the second
    constexpr uint64_t step = INT64_MAX;
    Replay<> replay;
    replay.inputs.emplace_back(step, 1, false, true);
    replay.inputs.emplace_back(step * 2, 1, false, false);
    replay.inputs.emplace_back(step * 3, 1, false, true);

    auto imported = Replay<>::importData(replay.exportData().unwrap());
    assert(imported.isOk());
    const auto& inputs = imported.unwrap().inputs;
    assert(inputs.size() == 3);
    assert(std::ranges::is_sorted(inputs, {}, &Input<>::frame));
}

static void testCutOffFiles() {
    MyReplay replay;
    replay.author = "someone";
    replay.attempts = 3;
    replay.deaths = {10, 900};
    for (int i = 0; i < 20; i++) replay.inputs.emplace_back(i * 300, 1, i % 2 == 0, i % 2 == 1, 1.f);

    std::vector<uint8_t> bytes = replay.exportData().unwrap();

    // just has to return, not every cut is detected
    for (size_t size = 0; size < bytes.size(); size++) {
        (void)MyReplay::importData(std::span<uint8_t>(bytes).first(size));
    }

    // input cut off in the middle of a varint
    Replay<> plain;
    plain.inputs.emplace_back(1, 1, false, true);
    std::vector<uint8_t> plainBytes = plain.exportData().unwrap();
    plainBytes.push_back(0x80);
    assert(Replay<>::importData(plainBytes).isErr());
}

static void testFilesAndBadData() {
    Replay<> replay("Bot", 1);
    replay.inputs.emplace_back(4, 1, false, true);

    auto path = std::filesystem::temp_directory_path() / "libgdr_test.gdr2";
    assert(replay.exportData(path).isOk());
    auto imported = Replay<>::importData(path);
    assert(imported.isOk());
    assert(imported.unwrap().inputs.size() == 1);
    std::filesystem::remove(path);

    assert(Replay<>::importData(path).isErr());

    std::vector<uint8_t> bad{'X', 'Y', 'Z', 0, 0, 0};
    auto res = Replay<>::importData(bad);
    assert(res.isErr());
    assert(res.unwrapErr().starts_with("Invalid magic"));
}

int main() {
    testExtensions();
    testVarints();
    testBasicTypes();
    testPlatformerReplay();
    testButtonDefaultsToJump();
    testSortInputs();
    testByteOrder();
    testMovingResults();
    testCopyingResults();
    testHugeFrameGaps();
    testCutOffFiles();
    testFilesAndBadData();
    std::cout << "all tests passed" << std::endl;
    return 0;
}
