#include "eui_neo.h"
#include "xc/protocol.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace app {
namespace {

using eui::Color;

constexpr Color kWindow{0.12f, 0.12f, 0.14f, 1.0f};
constexpr Color kTitlebar{0.95f, 0.95f, 0.95f, 1.0f};
constexpr Color kTopbar{0.10f, 0.10f, 0.12f, 1.0f};
constexpr Color kToolbar{0.13f, 0.13f, 0.15f, 1.0f};
constexpr Color kPanel{0.11f, 0.11f, 0.13f, 1.0f};
constexpr Color kHeader{0.15f, 0.15f, 0.20f, 1.0f};
constexpr Color kLine{0.28f, 0.29f, 0.34f, 1.0f};
constexpr Color kSelect{0.00f, 0.47f, 0.84f, 1.0f};
constexpr Color kText{0.82f, 0.85f, 0.89f, 1.0f};
constexpr Color kMuted{0.50f, 0.53f, 0.58f, 1.0f};
constexpr Color kCyan{0.25f, 0.84f, 1.00f, 1.0f};
constexpr Color kYellow{0.90f, 0.83f, 0.42f, 1.0f};
constexpr Color kRed{1.00f, 0.42f, 0.42f, 1.0f};
constexpr Color kButton{0.15f, 0.15f, 0.20f, 1.0f};
constexpr Color kInput{0.08f, 0.08f, 0.10f, 1.0f};

float clampWidth(float width) { return std::max(width, 980.0f); }
float clampHeight(float height) { return std::max(height, 680.0f); }

void rect(eui::Ui& ui, const std::string& id, float x, float y, float w, float h, Color color, Color border = kLine) {
    ui.rect(id).position(x, y).size(w, h).color(color).border(1.0f, border).build();
}

void fillRect(eui::Ui& ui, const std::string& id, float x, float y, float w, float h, Color color) {
    ui.rect(id).position(x, y).size(w, h).color(color).build();
}

void text(eui::Ui& ui, const std::string& id, float x, float y, const std::string& value, Color color = kText, float size = 14.0f) {
    ui.text(id).position(x, y).text(value).fontSize(size).color(color).build();
}

void mono(eui::Ui& ui, const std::string& id, float x, float y, const std::string& value, Color color = kText, float size = 13.0f) {
    ui.text(id).position(x, y).text(value).fontSize(size).fontFamily("Consolas").color(color).build();
}

void button(eui::Ui& ui, const std::string& id, float x, float y, float w, const std::string& label) {
    rect(ui, id + ".bg", x, y, w, 22.0f, kButton, kLine);
    text(ui, id + ".label", x + 14.0f, y + 3.0f, label, kText, 13.0f);
}

void panel(eui::Ui& ui, const std::string& id, float x, float y, float w, float h, const std::string& title) {
    rect(ui, id, x, y, w, h, kPanel, kLine);
    fillRect(ui, id + ".header", x, y, w, 26.0f, kHeader);
    text(ui, id + ".title", x + 10.0f, y + 4.0f, title, kText, 15.0f);
}

std::vector<std::string> registerLines() {
    return {
        "x0   0000000000000058   DEC:88           HEX:0x58",
        "x1   0000007A42869674   DEC:525102126708 HEX:0x7A42869674   [stack]+0xF7674",
        "x2   0000000000000000   DEC:0            HEX:0x0",
        "x3   0000000000000000   DEC:0            HEX:0x0",
        "x4   0000000000000000   DEC:0            HEX:0x0",
        "x5   0000000000006123   DEC:24867        HEX:0x6123",
        "x6   00000000000001AA   DEC:426          HEX:0x1AA",
        "x7   0000000000006123   DEC:24867        HEX:0x6123",
        "x8   164610C9BA59984E   DEC:866414860366 HEX:0x164610C9BA59984E",
        "x9   0000000000000000   DEC:0            HEX:0x0",
        "x10  00000000EE7E1856   DEC:4001241174   HEX:0xEE7E1856",
        "x11  0000007891D4ED30   DEC:517842726192 HEX:0x7891D4ED30   libdemo.so+0x53BD30",
        "x12  00000000102D7F70   DEC:27141720     HEX:0x102D7F70",
        "x13  00000000000004E7   DEC:1255         HEX:0x4E7",
        "x14  00000007C0000360   DEC:532575945568 HEX:0x7C0000360    [cfi shadow]+0x6984D360",
        "x15  000000000CAA098E   DEC:3298859199   HEX:0xCAA098E",
        "x16  0000007891D2FD20   DEC:517842599200 HEX:0x7891D2FD20   libdemo.so+0x51CD20",
        "x17  00000078919CFF84   DEC:517839060868 HEX:0x78919CFF84   libdemo.so+0x1BCF84",
        "x18  00000077FDD4C000   DEC:515359686656 HEX:0x77FDD4C000",
        "x19  0000007A42869A80   DEC:525102127744 HEX:0x7A42869A80   [stack]+0xF7A80",
        "x20  0000007A42869730   DEC:525102126896 HEX:0x7A42869730   [stack]+0xF7730",
        "x21  0000007A42869730   DEC:525102126896 HEX:0x7A42869730   [stack]+0xF7730",
        "x22  00000000000071C5   DEC:29125        HEX:0x71C5",
        "x23  0000000000007731   DEC:30513        HEX:0x7731",
        "x24  0000007A42869730   DEC:525102126896 HEX:0x7A42869730   [stack]+0xF7730",
        "x25  0000007A42869730   DEC:525102126896 HEX:0x7A42869730   [stack]+0xF7730",
        "x26  0000007A42869A70   DEC:525102127728 HEX:0x7A42869A70   [stack]+0xF7A70",
        "x27  00000000000FC000   DEC:1032192      HEX:0xFC000",
        "x28  0000007A42771000   DEC:525101103248 HEX:0x7A42771000   [stack]+0x0",
        "x29  0000007A42869690   DEC:525102126736 HEX:0x7A42869690   [stack]+0xF7690",
        "x30  0000007891B08068   DEC:517840339048 HEX:0x7891B08068   libdemo.so+0x2F5068",
        "SP   0000007A42869670   DEC:525102126704 HEX:0x7A42869670   [stack]+0xF7670",
        "PC   00000078919CFF84   DEC:517839060868 HEX:0x78919CFF84   libdemo.so+0x1BCF84"
    };
}

} // namespace

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("XC 硬件断点调试器")
        .pageId("xc_hwbp_debugger")
        .windowSize(1360, 760);
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    const float width = clampWidth(screen.width);
    const float height = clampHeight(screen.height);
    const float leftWidth = 240.0f;
    const float margin = 8.0f;
    const float mainX = margin + leftWidth + 6.0f;
    const float mainW = width - mainX - margin;
    const float mainH = height - 142.0f;

    fillRect(ui, "window.bg", 0.0f, 0.0f, width, height, kWindow);
    fillRect(ui, "titlebar", 0.0f, 0.0f, width, 28.0f, kTitlebar);
    text(ui, "titlebar.name", 12.0f, 5.0f, "XC 硬件断点调试器", {0.05f, 0.05f, 0.06f, 1.0f}, 14.0f);
    text(ui, "titlebar.window", width - 78.0f, 5.0f, "—  □  ×", {0.05f, 0.05f, 0.06f, 1.0f}, 14.0f);

    rect(ui, "topbar", 0.0f, 28.0f, width, 34.0f, kTopbar, kLine);
    rect(ui, "endpoint.input", 18.0f, 35.0f, 180.0f, 20.0f, kInput, kLine);
    text(ui, "endpoint.input.text", 28.0f, 38.0f, "192.168.1.10", kText, 13.0f);
    button(ui, "connect", 220.0f, 34.0f, 62.0f, "连接");
    button(ui, "disconnect", 288.0f, 34.0f, 62.0f, "断开");
    button(ui, "probe", 356.0f, 34.0f, 86.0f, "检测驱动");
    text(ui, "server.status", 462.0f, 38.0f, "服务器状态: 未连接  端口: " + std::to_string(xc::kDefaultAgentPort), kRed, 13.0f);
    text(ui, "data.count", width - 110.0f, 38.0f, "数据: 0条", kMuted, 13.0f);

    rect(ui, "sessionbar", 0.0f, 62.0f, width, 32.0f, kToolbar, kLine);
    text(ui, "session.info", 18.0f, 70.0f, "会话: 未附加目标进程", kMuted, 13.0f);
    text(ui, "driver.info", 230.0f, 70.0f, "驱动: 未知  /proc/modules: 未检测  Agent: 离线", kYellow, 13.0f);
    text(ui, "protocol.info", 570.0f, 70.0f, "协议: " + std::string(xc::kProtocolName) + " v" + std::to_string(xc::kProtocolVersion), kCyan, 13.0f);

    panel(ui, "watch", margin, 102.0f, leftWidth, 140.0f, "监视断点");
    fillRect(ui, "watch.selected", margin + 2.0f, 130.0f, leftWidth - 4.0f, 20.0f, kSelect);
    mono(ui, "watch.empty", 16.0f, 133.0f, "暂无命中断点", kText, 13.0f);
    mono(ui, "watch.hint1", 16.0f, 164.0f, "连接 agent 后显示", kMuted, 13.0f);
    mono(ui, "watch.hint2", 16.0f, 186.0f, "断点命中分组", kMuted, 13.0f);
    mono(ui, "watch.hint3", 16.0f, 208.0f, "#0 / TID / 栈签名", kMuted, 13.0f);

    panel(ui, "slots", margin, 250.0f, leftWidth, 220.0f, "硬件断点槽");
    mono(ui, "slots.header", 16.0f, 284.0f, "#  类型  地址        状态", kMuted, 12.0f);
    fillRect(ui, "slots.sel", margin + 2.0f, 296.0f, leftWidth - 4.0f, 20.0f, kSelect);
    mono(ui, "slots.0", 16.0f, 299.0f, "0  -     -           空", kText, 13.0f);
    mono(ui, "slots.1", 16.0f, 323.0f, "1  -     -           空", kMuted, 13.0f);
    mono(ui, "slots.2", 16.0f, 347.0f, "2  -     -           空", kMuted, 13.0f);
    mono(ui, "slots.3", 16.0f, 371.0f, "3  -     -           空", kMuted, 13.0f);
    mono(ui, "slots.note", 16.0f, 414.0f, "当前版本只显示状态", kYellow, 12.0f);
    mono(ui, "slots.note2", 16.0f, 436.0f, "真实下断功能待接入", kYellow, 12.0f);

    panel(ui, "driver", margin, 478.0f, leftWidth, 150.0f, "驱动状态");
    text(ui, "driver.loaded", 16.0f, 514.0f, "lsdriver: 未检测", kYellow, 13.0f);
    text(ui, "driver.modules", 16.0f, 540.0f, "模块表: 未读取", kMuted, 13.0f);
    text(ui, "driver.agent", 16.0f, 566.0f, "Agent: 未连接", kRed, 13.0f);
    mono(ui, "driver.rule", 16.0f, 598.0f, "策略: 缺驱动则退出", kYellow, 12.0f);

    panel(ui, "main", mainX, 102.0f, mainW, mainH, "数据视图");
    mono(ui, "main.thread", mainX + 14.0f, 132.0f, "Tid : - | Pid : - | 当前没有连接到手机 agent", kText, 14.0f);
    mono(ui, "main.note", mainX + 14.0f, 154.0f, "这里显示断点命中后的寄存器、DEC/HEX、模块偏移、堆栈。当前是静态预览数据。", kMuted, 13.0f);

    const auto lines = registerLines();
    float y = 182.0f;
    int idx = 0;
    for (const std::string& line : lines) {
        const Color color = line.rfind("PC", 0) == 0 || line.rfind("SP", 0) == 0 ? kCyan : (idx % 2 == 0 ? kText : Color{0.74f, 0.77f, 0.82f, 1.0f});
        mono(ui, "reg." + std::to_string(idx), mainX + 14.0f, y, line, color, 13.0f);
        y += 19.0f;
        ++idx;
        if (y > height - 116.0f) { break; }
    }

    rect(ui, "stack.panel", mainX, height - 104.0f, mainW, 66.0f, kPanel, kLine);
    text(ui, "stack.title", mainX + 10.0f, height - 96.0f, "堆栈", kText, 14.0f);
    mono(ui, "stack.0", mainX + 14.0f, height - 72.0f, "#0: 暂无真实命中；连接 agent 后显示 PC/LR 调用链", kMuted, 13.0f);
    mono(ui, "stack.1", mainX + 14.0f, height - 50.0f, "#1: 后续按 断点ID / TID / 调用栈签名 自动归类", kMuted, 13.0f);

    rect(ui, "statusbar", 0.0f, height - 30.0f, width, 30.0f, kToolbar, kLine);
    mono(ui, "status.left", 12.0f, height - 21.0f, "就绪: GUI 已按简洁调试器布局重构", kYellow, 13.0f);
    text(ui, "status.right", width - 330.0f, height - 21.0f, "Windows GUI | Android agent", kMuted, 13.0f);
}

} // namespace app
