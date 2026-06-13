#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStringList>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <cstdint>
#include <string>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "xc/protocol.hpp"

namespace {

struct BreakpointAddressInput {
    bool valid = false;
    std::uint64_t address = 0;
    std::string module;
    std::uint64_t offset = 0;
};

struct ClientState {
    bool connected = false;
    bool helloOk = false;
    xc::HelloResponse hello;
    std::string endpoint = "192.168.1.10";
    std::string target = "com.tencent.tmgp.sgame";
    BreakpointAddressInput breakpointInput;
    std::string breakpointType = "execute";
    std::uint32_t breakpointSize = 4;
    xc::DriverStatusResponse driver;
    xc::BreakpointSetResponse breakpoints[4];
    std::string breakpointLabels[4];
    std::string lastBreakpointInfo;
    std::string lastError = "未连接";
    std::string lastAction = "就绪: 等待连接手机 Agent";
    std::uint64_t requestId = 1;
};

QString qstr(const std::string& value) {
    return QString::fromUtf8(value.c_str());
}

std::string stdstr(const QString& value) {
    return value.toUtf8().constData();
}

std::string trimAscii(std::string value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return value;
}

bool parseUnsigned64(const std::string& text, std::uint64_t& value) {
    const std::string input = trimAscii(text);
    if (input.empty()) {
        return false;
    }
    int base = 10;
    std::size_t i = 0;
    if (input.size() > 2 && input[0] == '0' && (input[1] == 'x' || input[1] == 'X')) {
        base = 16;
        i = 2;
    }
    if (i >= input.size()) {
        return false;
    }
    value = 0;
    for (; i < input.size(); ++i) {
        const char c = input[i];
        unsigned digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<unsigned>(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = static_cast<unsigned>(10 + c - 'a');
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = static_cast<unsigned>(10 + c - 'A');
        } else {
            return false;
        }
        if (digit >= static_cast<unsigned>(base)) {
            return false;
        }
        value = value * static_cast<std::uint64_t>(base) + digit;
    }
    return true;
}

BreakpointAddressInput parseBreakpointAddress(const std::string& text) {
    BreakpointAddressInput parsed;
    const std::string input = trimAscii(text);
    const std::size_t plus = input.find('+');
    if (plus != std::string::npos) {
        parsed.module = trimAscii(input.substr(0, plus));
        if (parsed.module.empty() || !parseUnsigned64(input.substr(plus + 1), parsed.offset)) {
            return parsed;
        }
        parsed.valid = true;
        return parsed;
    }
    parsed.valid = parseUnsigned64(input, parsed.address) && parsed.address != 0;
    return parsed;
}

std::string displayAddress(const BreakpointAddressInput& input) {
    if (!input.module.empty()) {
        return input.module + "+" + xc::hexAddress(input.offset);
    }
    return input.address == 0 ? "-" : xc::hexAddress(input.address);
}

std::string protocolTypeForUi(const QString& value) {
    const std::string text = stdstr(value).substr(0, 2);
    if (text == "x") {
        return "execute";
    }
    if (text == "r") {
        return "read";
    }
    if (text == "w") {
        return "write";
    }
    return "access";
}

QString typeLabel(const std::string& protocolType) {
    if (protocolType == "execute") {
        return "x 执行";
    }
    if (protocolType == "read") {
        return "r 读取";
    }
    if (protocolType == "write") {
        return "w 写入";
    }
    if (protocolType == "access") {
        return "rw 读写";
    }
    return qstr(protocolType);
}

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
void closeSocket(SocketHandle socket) { closesocket(socket); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
void closeSocket(SocketHandle socket) { ::close(socket); }
#endif

bool ensureSocketRuntime(std::string& error) {
#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            error = "WSAStartup 初始化失败";
            return false;
        }
        initialized = true;
    }
#else
    (void)error;
#endif
    return true;
}

bool sendAll(SocketHandle socket, const std::string& data) {
    const char* ptr = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
#ifdef _WIN32
        const int sent = ::send(socket, ptr, static_cast<int>(remaining), 0);
#else
        const ssize_t sent = ::send(socket, ptr, remaining, 0);
#endif
        if (sent <= 0) {
            return false;
        }
        ptr += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool sendLine(SocketHandle socket, const std::string& line) {
    return sendAll(socket, line + "\n");
}

std::string receiveLine(SocketHandle socket, std::string& error) {
    std::string line;
    std::array<char, 1> ch{};
    while (line.size() < 64 * 1024) {
#ifdef _WIN32
        const int received = ::recv(socket, ch.data(), 1, 0);
#else
        const ssize_t received = ::recv(socket, ch.data(), 1, 0);
#endif
        if (received <= 0) {
            error = line.empty() ? "连接已关闭，未收到响应" : "连接已关闭，响应不完整";
            return {};
        }
        if (ch[0] == '\n') {
            return line;
        }
        if (ch[0] != '\r') {
            line.push_back(ch[0]);
        }
    }
    error = "响应超过 64KB，协议异常";
    return {};
}

SocketHandle connectSocket(const std::string& host, std::uint16_t port, std::string& error) {
    if (!ensureSocketRuntime(error)) {
        return kInvalidSocket;
    }
    const SocketHandle socketFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd == kInvalidSocket) {
        error = "创建 socket 失败";
        return kInvalidSocket;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        error = "IP 地址格式无效: " + host;
        closeSocket(socketFd);
        return kInvalidSocket;
    }
    if (::connect(socketFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = "连接失败: " + host + ":" + std::to_string(port);
        closeSocket(socketFd);
        return kInvalidSocket;
    }
    return socketFd;
}

bool protocolOk(const xc::HelloResponse& hello) {
    return hello.ok && hello.protocol == std::string(xc::kProtocolName) && hello.version == xc::kProtocolVersion;
}

bool openAgentSession(const std::string& endpoint, SocketHandle& socket, xc::HelloResponse& hello, std::string& error) {
    socket = connectSocket(endpoint, xc::kDefaultAgentPort, error);
    if (socket == kInvalidSocket) {
        return false;
    }
    const std::string helloLine = receiveLine(socket, error);
    hello = xc::parseHelloResponse(helloLine);
    if (helloLine.empty() || !protocolOk(hello)) {
        error = helloLine.empty() ? error : "Agent hello 不匹配: " + helloLine;
        closeSocket(socket);
        socket = kInvalidSocket;
        return false;
    }
    return true;
}

std::string jsonError(const std::string& error) {
    return "{\"ok\":false,\"error\":\"" + xc::escapeJson(error) + "\"}";
}

QTableWidgetItem* cell(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

void setupTable(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
}

class DebuggerWindow final : public QMainWindow {
public:
    DebuggerWindow() {
        setWindowTitle("XC 硬件断点调试器");
        resize(1360, 780);
        setMinimumSize(1040, 680);
        buildUi();
        refreshUi();
    }

private:
    ClientState state_;
    QLineEdit* endpointEdit_ = nullptr;
    QLineEdit* targetEdit_ = nullptr;
    QLineEdit* addressEdit_ = nullptr;
    QComboBox* typeCombo_ = nullptr;
    QSpinBox* sizeSpin_ = nullptr;
    QSpinBox* slotSpin_ = nullptr;
    QLabel* connectionLabel_ = nullptr;
    QLabel* driverLabel_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QTableWidget* breakpointsTable_ = nullptr;
    QLabel* emptyDataLabel_ = nullptr;
    QPlainTextEdit* registersText_ = nullptr;
    QPlainTextEdit* stackText_ = nullptr;
    QPlainTextEdit* infoText_ = nullptr;

    void buildUi() {
        auto* central = new QWidget;
        auto* root = new QVBoxLayout(central);
        root->setContentsMargins(10, 10, 10, 8);
        root->setSpacing(8);
        root->addWidget(buildToolbar());
        root->addWidget(buildStatusStrip());

        auto* splitter = new QSplitter(Qt::Horizontal);
        splitter->addWidget(buildBreakpointPane());
        splitter->addWidget(buildHitDetailPane());
        splitter->setSizes({430, 930});
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        root->addWidget(splitter, 1);

        setCentralWidget(central);
        applyStyle();
    }

    QWidget* buildToolbar() {
        auto* frame = new QFrame;
        frame->setObjectName("toolbar");
        auto* layout = new QHBoxLayout(frame);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(8);

        endpointEdit_ = new QLineEdit(qstr(state_.endpoint));
        targetEdit_ = new QLineEdit(qstr(state_.target));
        addressEdit_ = new QLineEdit;
        endpointEdit_->setPlaceholderText("手机 IP");
        targetEdit_->setPlaceholderText("包名或 PID");
        addressEdit_->setPlaceholderText("绝对地址或 libtersafe.so+0x488F08");
        endpointEdit_->setMinimumWidth(135);
        targetEdit_->setMinimumWidth(190);
        addressEdit_->setMinimumWidth(245);

        typeCombo_ = new QComboBox;
        typeCombo_->addItems({"x", "r", "w", "rw"});
        sizeSpin_ = new QSpinBox;
        sizeSpin_->setRange(1, 8);
        sizeSpin_->setValue(4);
        slotSpin_ = new QSpinBox;
        slotSpin_->setRange(0, 3);

        auto* connectButton = new QPushButton("连接");
        auto* disconnectButton = new QPushButton("断开");
        auto* probeButton = new QPushButton("检测驱动");
        auto* setButton = new QPushButton("下断");
        auto* removeButton = new QPushButton("删断");
        auto* infoButton = new QPushButton("刷新断点");

        layout->addWidget(new QLabel("Agent"));
        layout->addWidget(endpointEdit_);
        layout->addWidget(new QLabel("目标"));
        layout->addWidget(targetEdit_);
        layout->addWidget(new QLabel("断点"));
        layout->addWidget(addressEdit_);
        layout->addWidget(new QLabel("类型"));
        layout->addWidget(typeCombo_);
        layout->addWidget(new QLabel("长度"));
        layout->addWidget(sizeSpin_);
        layout->addWidget(new QLabel("槽"));
        layout->addWidget(slotSpin_);
        layout->addWidget(connectButton);
        layout->addWidget(disconnectButton);
        layout->addWidget(probeButton);
        layout->addWidget(setButton);
        layout->addWidget(removeButton);
        layout->addWidget(infoButton);
        layout->addStretch(1);

        connect(connectButton, &QPushButton::clicked, this, [this] { markConnected(); });
        connect(probeButton, &QPushButton::clicked, this, [this] { markConnected(); });
        connect(disconnectButton, &QPushButton::clicked, this, [this] { disconnectAgent(); });
        connect(setButton, &QPushButton::clicked, this, [this] { setBreakpoint(); });
        connect(removeButton, &QPushButton::clicked, this, [this] { removeBreakpoint(); });
        connect(infoButton, &QPushButton::clicked, this, [this] { queryBreakpointInfo(); });
        return frame;
    }

    QWidget* buildStatusStrip() {
        auto* frame = new QFrame;
        frame->setObjectName("sessionbar");
        auto* layout = new QHBoxLayout(frame);
        layout->setContentsMargins(10, 6, 10, 6);
        layout->setSpacing(18);

        connectionLabel_ = new QLabel;
        driverLabel_ = new QLabel;
        errorLabel_ = new QLabel;
        layout->addWidget(connectionLabel_);
        layout->addWidget(driverLabel_, 1);
        layout->addWidget(errorLabel_, 1);
        return frame;
    }

    QWidget* buildBreakpointPane() {
        auto* box = new QGroupBox("断点列表 / 监视断点");
        box->setMinimumWidth(390);
        box->setMaximumWidth(520);
        auto* layout = new QVBoxLayout(box);
        layout->setContentsMargins(8, 10, 8, 8);
        layout->setSpacing(8);

        auto* hint = new QLabel("同一个列表管理硬件断点槽和命中监视，避免状态分散。");
        hint->setObjectName("paneHint");
        breakpointsTable_ = new QTableWidget;
        setupTable(breakpointsTable_, {"槽", "类型", "模块/偏移", "实际地址", "状态"});
        breakpointsTable_->horizontalHeader()->setStretchLastSection(false);
        breakpointsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        breakpointsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        breakpointsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        breakpointsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        breakpointsTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

        layout->addWidget(hint);
        layout->addWidget(breakpointsTable_, 1);
        return box;
    }

    QWidget* buildHitDetailPane() {
        auto* box = new QGroupBox("命中详情");
        auto* layout = new QVBoxLayout(box);
        layout->setContentsMargins(8, 10, 8, 8);
        layout->setSpacing(8);

        emptyDataLabel_ = new QLabel("等待真实断点命中数据");
        emptyDataLabel_->setObjectName("emptyDataLabel");
        emptyDataLabel_->setAlignment(Qt::AlignCenter);
        emptyDataLabel_->setMinimumHeight(38);

        auto* detailSplitter = new QSplitter(Qt::Vertical);
        registersText_ = new QPlainTextEdit;
        registersText_->setReadOnly(true);
        registersText_->setObjectName("registersText");
        stackText_ = new QPlainTextEdit;
        stackText_->setReadOnly(true);
        stackText_->setObjectName("stackText");
        infoText_ = new QPlainTextEdit;
        infoText_->setReadOnly(true);
        infoText_->setObjectName("infoText");
        detailSplitter->addWidget(registersText_);
        detailSplitter->addWidget(stackText_);
        detailSplitter->addWidget(infoText_);
        detailSplitter->setSizes({290, 210, 170});

        layout->addWidget(emptyDataLabel_);
        layout->addWidget(detailSplitter, 1);
        return box;
    }

    bool applyInputs(bool requireAddress) {
        const QString endpoint = endpointEdit_->text().trimmed();
        const QString target = targetEdit_->text().trimmed();
        const QString addressText = addressEdit_->text().trimmed();
        BreakpointAddressInput input;
        if (!addressText.isEmpty()) {
            input = parseBreakpointAddress(stdstr(addressText));
        }
        if (requireAddress && !input.valid) {
            state_.lastError = "请先输入有效断点地址";
            state_.lastAction = "输入错误: 支持 0x地址 或 libtersafe.so+0x偏移";
            return false;
        }

        if (!endpoint.isEmpty()) {
            state_.endpoint = stdstr(endpoint);
        }
        if (!target.isEmpty()) {
            state_.target = stdstr(target);
        }
        state_.breakpointInput = input;
        state_.breakpointType = protocolTypeForUi(typeCombo_->currentText());
        state_.breakpointSize = static_cast<std::uint32_t>(sizeSpin_->value());
        return true;
    }

    void markConnected() {
        applyInputs(false);
        state_.lastAction = "正在连接 " + state_.endpoint + ":" + std::to_string(xc::kDefaultAgentPort);
        refreshUi();

        SocketHandle socket = kInvalidSocket;
        xc::HelloResponse hello;
        std::string error;
        if (!openAgentSession(state_.endpoint, socket, hello, error)) {
            state_.connected = false;
            state_.helloOk = false;
            state_.lastError = error;
            state_.lastAction = "连接失败: " + error;
            refreshUi();
            return;
        }

        if (!sendLine(socket, xc::driverStatusRequestJson(state_.requestId++))) {
            closeSocket(socket);
            state_.lastError = "发送 driver.status 失败";
            state_.lastAction = "驱动检测请求失败";
            refreshUi();
            return;
        }

        const std::string statusLine = receiveLine(socket, error);
        closeSocket(socket);
        if (statusLine.empty()) {
            state_.lastError = error;
            state_.lastAction = "驱动检测失败: " + error;
            refreshUi();
            return;
        }

        state_.hello = hello;
        state_.helloOk = true;
        state_.connected = true;
        state_.driver = xc::parseDriverStatusResponse(statusLine);
        state_.lastError.clear();
        state_.lastAction = state_.driver.ok ? "已连接 Agent，驱动状态已刷新" : "已连接 Agent，但驱动状态返回错误";
        refreshUi();
    }

    void disconnectAgent() {
        state_.connected = false;
        state_.helloOk = false;
        state_.driver = {};
        state_.lastBreakpointInfo.clear();
        for (int i = 0; i < 4; ++i) {
            state_.breakpoints[i] = {};
            state_.breakpointLabels[i].clear();
        }
        state_.lastError = "已断开";
        state_.lastAction = "已断开连接";
        refreshUi();
    }

    void setBreakpoint() {
        if (!applyInputs(true)) {
            refreshUi();
            return;
        }
        const auto slot = static_cast<std::size_t>(slotSpin_->value());
        state_.lastAction = "正在写入硬件断点 slot" + std::to_string(slot);
        refreshUi();

        SocketHandle socket = kInvalidSocket;
        xc::HelloResponse hello;
        std::string error;
        if (!openAgentSession(state_.endpoint, socket, hello, error)) {
            state_.lastError = error;
            state_.lastAction = "下断失败: " + error;
            refreshUi();
            return;
        }

        const std::string request = state_.breakpointInput.module.empty()
            ? xc::breakpointSetRequestJson(state_.requestId++, static_cast<std::uint32_t>(slot), state_.breakpointInput.address, state_.breakpointType, state_.breakpointSize, state_.target)
            : xc::breakpointSetModuleRequestJson(state_.requestId++, static_cast<std::uint32_t>(slot), state_.breakpointInput.module, state_.breakpointInput.offset, state_.breakpointType, state_.breakpointSize, state_.target);
        if (!sendLine(socket, request)) {
            closeSocket(socket);
            state_.lastError = "发送 breakpoint.set 失败";
            state_.lastAction = "下断请求发送失败";
            refreshUi();
            return;
        }
        const std::string responseLine = receiveLine(socket, error);
        closeSocket(socket);
        if (responseLine.empty()) {
            state_.lastError = error;
            state_.lastAction = "下断失败: " + error;
            refreshUi();
            return;
        }

        const auto response = xc::parseBreakpointSetResponse(responseLine);
        state_.breakpoints[slot] = response;
        state_.breakpointLabels[slot] = displayAddress(state_.breakpointInput);
        state_.hello = hello;
        state_.helloOk = true;
        state_.connected = true;
        state_.lastError = response.ok ? "" : response.error;
        state_.lastAction = response.ok ? response.message : "下断失败: " + response.error;
        refreshUi();
    }

    void removeBreakpoint() {
        applyInputs(false);
        const auto slot = static_cast<std::size_t>(slotSpin_->value());
        state_.lastAction = "正在删除硬件断点 slot" + std::to_string(slot);
        refreshUi();

        SocketHandle socket = kInvalidSocket;
        xc::HelloResponse hello;
        std::string error;
        if (!openAgentSession(state_.endpoint, socket, hello, error)) {
            state_.lastError = error;
            state_.lastAction = "删断失败: " + error;
            refreshUi();
            return;
        }
        if (!sendLine(socket, xc::breakpointRemoveRequestJson(state_.requestId++, static_cast<std::uint32_t>(slot), state_.target))) {
            closeSocket(socket);
            state_.lastError = "发送 breakpoint.remove 失败";
            state_.lastAction = "删断请求发送失败";
            refreshUi();
            return;
        }
        const std::string responseLine = receiveLine(socket, error);
        closeSocket(socket);
        if (responseLine.empty()) {
            state_.lastError = error;
            state_.lastAction = "删断失败: " + error;
            refreshUi();
            return;
        }
        const auto response = xc::parseBreakpointRemoveResponse(responseLine);
        if (response.ok) {
            state_.breakpoints[slot] = {};
            state_.breakpointLabels[slot].clear();
        }
        state_.hello = hello;
        state_.helloOk = true;
        state_.connected = true;
        state_.lastError = response.ok ? "" : response.error;
        state_.lastAction = response.ok ? response.message : "删断失败: " + response.error;
        refreshUi();
    }

    void queryBreakpointInfo() {
        applyInputs(false);
        state_.lastAction = "正在查询硬件断点信息";
        refreshUi();

        SocketHandle socket = kInvalidSocket;
        xc::HelloResponse hello;
        std::string error;
        if (!openAgentSession(state_.endpoint, socket, hello, error)) {
            state_.lastError = error;
            state_.lastAction = "查询断点失败: " + error;
            state_.lastBreakpointInfo = jsonError(error);
            refreshUi();
            return;
        }
        if (!sendLine(socket, xc::breakpointInfoRequestJson(state_.requestId++))) {
            closeSocket(socket);
            state_.lastError = "发送 breakpoint.info 失败";
            state_.lastAction = "查询断点请求发送失败";
            state_.lastBreakpointInfo = jsonError(state_.lastError);
            refreshUi();
            return;
        }
        const std::string responseLine = receiveLine(socket, error);
        closeSocket(socket);
        if (responseLine.empty()) {
            state_.lastError = error;
            state_.lastAction = "查询断点失败: " + error;
            state_.lastBreakpointInfo = jsonError(error);
            refreshUi();
            return;
        }
        state_.hello = hello;
        state_.helloOk = true;
        state_.connected = true;
        state_.lastError.clear();
        state_.lastAction = "硬件断点信息已刷新";
        state_.lastBreakpointInfo = responseLine;
        refreshUi();
    }

    void refreshUi() {
        connectionLabel_->setText(state_.connected ? "Agent 在线" : "Agent 未连接");
        connectionLabel_->setProperty("state", state_.connected ? "ok" : "bad");
        driverLabel_->setText("驱动: " + QString(state_.driver.moduleLoaded ? "已加载" : "未加载")
            + "  /proc/modules: " + QString(state_.driver.procModulesReadable ? "可读" : "未确认")
            + "  协议: " + qstr(std::string(xc::kProtocolName)) + " v" + QString::number(xc::kProtocolVersion));
        errorLabel_->setText(state_.lastError.empty() ? "错误: 无" : "错误: " + qstr(state_.lastError));
        errorLabel_->setProperty("state", state_.lastError.empty() ? "ok" : "bad");
        connectionLabel_->style()->unpolish(connectionLabel_);
        connectionLabel_->style()->polish(connectionLabel_);
        errorLabel_->style()->unpolish(errorLabel_);
        errorLabel_->style()->polish(errorLabel_);

        refreshBreakpointsTable();
        emptyDataLabel_->setText(state_.connected
            ? "等待真实断点命中数据，当前没有寄存器和调用栈快照"
            : "等待真实断点命中数据，请先连接 Agent");
        registersText_->setPlainText("寄存器\n\n等待真实断点命中数据。收到 Agent 返回的 PC/LR/SP/X 寄存器后在这里集中显示。");
        stackText_->setPlainText("调用栈\n\n暂无真实命中。后续收到 PC/LR/SP 调用链后在这里显示。");
        infoText_->setPlainText(state_.lastBreakpointInfo.empty()
            ? "断点信息\n\n只显示 Agent 返回的真实断点和命中信息。启动时不填充模拟数据。"
            : qstr(state_.lastBreakpointInfo));
        statusBar()->showMessage(qstr(state_.lastAction));
    }

    void refreshBreakpointsTable() {
        breakpointsTable_->setRowCount(4);
        for (int row = 0; row < 4; ++row) {
            const auto& bp = state_.breakpoints[row];
            const QString label = state_.breakpointLabels[row].empty() ? "-" : qstr(state_.breakpointLabels[row]);
            breakpointsTable_->setItem(row, 0, cell(QString::number(row)));
            breakpointsTable_->setItem(row, 1, cell(bp.ok ? typeLabel(bp.type) : "-"));
            breakpointsTable_->setItem(row, 2, cell(label));
            breakpointsTable_->setItem(row, 3, cell(bp.ok && bp.address != 0 ? qstr(xc::hexAddress(bp.address)) : "-"));
            breakpointsTable_->setItem(row, 4, cell(bp.ok && bp.enabled ? "已下断，等待命中" : "空"));
        }
        breakpointsTable_->resizeColumnsToContents();
        breakpointsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    }

    void applyStyle() {
        setStyleSheet(R"(
            QWidget { background: #17191d; color: #d8dde5; font-family: "Microsoft YaHei UI"; font-size: 13px; }
            QFrame#toolbar, QFrame#sessionbar, QStatusBar { background: #20242a; border: 1px solid #313741; }
            QGroupBox { border: 1px solid #333a44; border-radius: 4px; margin-top: 20px; padding: 8px; background: #1b1f24; font-weight: 600; }
            QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #eef3f8; }
            QLabel#paneHint { color: #9da8b6; }
            QLineEdit, QComboBox, QSpinBox, QPlainTextEdit, QTableWidget { background: #111317; border: 1px solid #343b45; border-radius: 3px; color: #e4e8ee; selection-background-color: #2567a8; }
            QLineEdit, QComboBox, QSpinBox { min-height: 25px; padding: 2px 6px; }
            QPushButton { background: #2b333d; border: 1px solid #44505e; border-radius: 3px; color: #f0f4f8; min-height: 27px; padding: 3px 10px; }
            QPushButton:hover { background: #36414d; }
            QPushButton:pressed { background: #1f6aa8; }
            QHeaderView::section { background: #252b33; border: none; border-right: 1px solid #39414d; color: #cbd3dd; padding: 5px 7px; font-weight: 600; }
            QTableWidget { alternate-background-color: #15181d; gridline-color: #2b323b; }
            QPlainTextEdit { font-family: "Cascadia Mono", "Consolas"; font-size: 13px; }
            QLabel#emptyDataLabel { background: #111317; border: 1px dashed #3b4653; border-radius: 3px; color: #8fbde8; font-size: 15px; font-weight: 600; }
            QLabel[state="ok"] { color: #68d391; }
            QLabel[state="bad"] { color: #ff8585; }
            QSplitter::handle { background: #252b33; }
        )");
    }
};

} // namespace

int main(int argc, char** argv) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setFont(QFont("Microsoft YaHei UI", 10));

    DebuggerWindow window;
    window.show();
    return app.exec();
}
