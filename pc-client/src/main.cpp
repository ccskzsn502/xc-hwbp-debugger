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
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <cstdint>
#include <sstream>
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
    xc::RecordsGetResponse hitRecords;
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

std::string registerLine(const std::string& name, std::uint64_t value) {
    return name + "=" + xc::hexAddress(value);
}

std::string formatLatestHitRegisters(const xc::RecordsGetResponse& records) {
    if (!records.ok || records.records.empty()) {
        return "寄存器\n\n等待真实断点命中数据。收到 Agent 返回的 PC/LR/SP/X 寄存器后在这里集中显示。";
    }
    const auto& hit = records.records.back();
    std::ostringstream out;
    out << "寄存器\n\n";
    out << "slot=" << records.slot << "  hitCount=" << hit.hitCount << "\n";
    out << "PC=" << xc::hexAddress(hit.pc)
        << "  LR=" << xc::hexAddress(hit.lr)
        << "  SP=" << xc::hexAddress(hit.sp) << "\n";
    out << registerLine("PSTATE", hit.pstate) << "  " << registerLine("SYSCALL", hit.syscallno)
        << "  FPSR=" << hit.fpsr << "  FPCR=" << hit.fpcr << "\n\n";
    for (int i = 0; i < 30; ++i) {
        out << registerLine("X" + std::to_string(i), hit.x[i]);
        out << ((i % 3 == 2) ? "\n" : "  ");
    }
    return out.str();
}

std::string formatLatestHitStack(const xc::RecordsGetResponse& records) {
    if (!records.ok || records.records.empty()) {
        return "调用栈\n\n暂无真实命中。后续收到 PC/LR/SP 调用链后在这里显示。";
    }
    const auto& hit = records.records.back();
    std::ostringstream out;
    out << "调用栈\n\n";
    out << "PC " << xc::hexAddress(hit.pc) << "\n";
    out << "LR " << xc::hexAddress(hit.lr) << "\n";
    out << "SP " << xc::hexAddress(hit.sp) << "\n";
    return out.str();
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
    table->setWordWrap(false);
    table->setFocusPolicy(Qt::NoFocus);
    table->verticalHeader()->setDefaultSectionSize(34);
    table->horizontalHeader()->setHighlightSections(false);
}

class DebuggerWindow final : public QMainWindow {
public:
    DebuggerWindow() {
        setWindowTitle("XC 硬件断点调试器");
        resize(1360, 780);
        setMinimumSize(1040, 680);
        buildUi();
        hitPollTimer_ = new QTimer(this);
        hitPollTimer_->setInterval(1000);
        connect(hitPollTimer_, &QTimer::timeout, this, [this] { pollHitRecords(); });
        refreshUi();
    }

    ~DebuggerWindow() override { closeAgentSession(); }

private:
    ClientState state_;
    SocketHandle agentSocket_ = kInvalidSocket;
    std::string agentSocketEndpoint_;
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
    QTimer* hitPollTimer_ = nullptr;

    QLabel* fieldLabel(const QString& text) {
        auto* label = new QLabel(text);
        label->setObjectName("fieldLabel");
        return label;
    }

    QLabel* statusPill() {
        auto* label = new QLabel;
        label->setProperty("role", "statusPill");
        label->setAlignment(Qt::AlignCenter);
        return label;
    }

    QPushButton* actionButton(const QString& text, const char* role) {
        auto* button = new QPushButton(text);
        button->setProperty("role", role);
        return button;
    }

    void buildUi() {
        auto* central = new QWidget;
        auto* root = new QVBoxLayout(central);
        root->setContentsMargins(10, 10, 10, 8);
        root->setSpacing(8);
        root->addWidget(buildConnectionBar());
        root->addWidget(buildBreakpointEditor());
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

    QWidget* buildConnectionBar() {
        auto* frame = new QFrame;
        frame->setObjectName("connectionBar");
        auto* layout = new QHBoxLayout(frame);
        layout->setContentsMargins(12, 9, 12, 9);
        layout->setSpacing(10);

        endpointEdit_ = new QLineEdit(qstr(state_.endpoint));
        targetEdit_ = new QLineEdit(qstr(state_.target));
        endpointEdit_->setPlaceholderText("手机 IP");
        targetEdit_->setPlaceholderText("包名或 PID");
        endpointEdit_->setMinimumWidth(150);
        targetEdit_->setMinimumWidth(240);

        auto* connectButton = actionButton("连接", "primaryButton");
        auto* disconnectButton = actionButton("断开", "secondaryButton");
        auto* probeButton = actionButton("检测驱动", "secondaryButton");

        layout->addWidget(fieldLabel("Agent"));
        layout->addWidget(endpointEdit_);
        layout->addWidget(fieldLabel("目标"));
        layout->addWidget(targetEdit_, 1);
        layout->addStretch(1);
        layout->addWidget(connectButton);
        layout->addWidget(disconnectButton);
        layout->addWidget(probeButton);

        connect(connectButton, &QPushButton::clicked, this, [this] { markConnected(); });
        connect(probeButton, &QPushButton::clicked, this, [this] { markConnected(); });
        connect(disconnectButton, &QPushButton::clicked, this, [this] { disconnectAgent(); });
        return frame;
    }

    QWidget* buildBreakpointEditor() {
        auto* frame = new QFrame;
        frame->setObjectName("breakpointEditor");
        auto* layout = new QHBoxLayout(frame);
        layout->setContentsMargins(12, 9, 12, 9);
        layout->setSpacing(10);

        addressEdit_ = new QLineEdit;
        addressEdit_->setPlaceholderText("绝对地址或 libtersafe.so+0x488F08");
        addressEdit_->setMinimumWidth(330);

        typeCombo_ = new QComboBox;
        typeCombo_->addItems({"x", "r", "w", "rw"});
        typeCombo_->setMaximumWidth(78);
        sizeSpin_ = new QSpinBox;
        sizeSpin_->setRange(1, 8);
        sizeSpin_->setValue(4);
        sizeSpin_->setMaximumWidth(76);
        slotSpin_ = new QSpinBox;
        slotSpin_->setRange(0, 3);
        slotSpin_->setMaximumWidth(76);

        auto* setButton = actionButton("下断", "primaryButton");
        auto* removeButton = actionButton("删断", "dangerButton");
        auto* infoButton = actionButton("刷新断点", "secondaryButton");

        layout->addWidget(fieldLabel("断点"));
        layout->addWidget(addressEdit_, 1);
        layout->addWidget(fieldLabel("类型"));
        layout->addWidget(typeCombo_);
        layout->addWidget(fieldLabel("长度"));
        layout->addWidget(sizeSpin_);
        layout->addWidget(fieldLabel("槽"));
        layout->addWidget(slotSpin_);
        layout->addWidget(setButton);
        layout->addWidget(removeButton);
        layout->addWidget(infoButton);

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

        connectionLabel_ = statusPill();
        driverLabel_ = statusPill();
        errorLabel_ = statusPill();
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

    bool hasAgentSocket() const {
        return agentSocket_ != kInvalidSocket;
    }

    void closeAgentSession() {
        if (hasAgentSocket()) {
            closeSocket(agentSocket_);
            agentSocket_ = kInvalidSocket;
        }
        agentSocketEndpoint_.clear();
    }

    bool ensureAgentSession(std::string& error) {
        if (hasAgentSocket() && agentSocketEndpoint_ == state_.endpoint && state_.helloOk) {
            state_.connected = true;
            return true;
        }

        closeAgentSession();
        SocketHandle socket = kInvalidSocket;
        xc::HelloResponse hello;
        if (!openAgentSession(state_.endpoint, socket, hello, error)) {
            state_.connected = false;
            state_.helloOk = false;
            return false;
        }

        agentSocket_ = socket;
        agentSocketEndpoint_ = state_.endpoint;
        state_.hello = hello;
        state_.helloOk = true;
        state_.connected = true;
        return true;
    }

    std::string sendAgentRequest(const std::string& request, std::string& error, const std::string& sendFailure) {
        if (!ensureAgentSession(error)) {
            return {};
        }
        if (!sendLine(agentSocket_, request)) {
            error = sendFailure;
            closeAgentSession();
            state_.connected = false;
            state_.helloOk = false;
            return {};
        }
        const std::string responseLine = receiveLine(agentSocket_, error);
        if (responseLine.empty()) {
            closeAgentSession();
            state_.connected = false;
            state_.helloOk = false;
            return {};
        }
        state_.connected = true;
        return responseLine;
    }

    void markConnected() {
        applyInputs(false);
        state_.lastAction = "正在连接 " + state_.endpoint + ":" + std::to_string(xc::kDefaultAgentPort);
        refreshUi();

        closeAgentSession();
        std::string error;
        const std::string statusLine = sendAgentRequest(
            xc::driverStatusRequestJson(state_.requestId++),
            error,
            "发送 driver.status 失败");
        if (statusLine.empty()) {
            state_.lastError = error;
            state_.lastAction = "连接失败: " + error;
            refreshUi();
            return;
        }

        state_.driver = xc::parseDriverStatusResponse(statusLine);
        state_.lastError.clear();
        state_.lastAction = state_.driver.ok ? "已连接 Agent，驱动状态已刷新" : "已连接 Agent，但驱动状态返回错误";
        updateHitPolling();
        refreshUi();
    }

    void disconnectAgent() {
        if (hitPollTimer_) {
            hitPollTimer_->stop();
        }
        closeAgentSession();
        state_.connected = false;
        state_.helloOk = false;
        state_.driver = {};
        state_.lastBreakpointInfo.clear();
        state_.hitRecords = {};
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

        std::string error;
        const std::string request = state_.breakpointInput.module.empty()
            ? xc::breakpointSetRequestJson(state_.requestId++, static_cast<std::uint32_t>(slot), state_.breakpointInput.address, state_.breakpointType, state_.breakpointSize, state_.target)
            : xc::breakpointSetModuleRequestJson(state_.requestId++, static_cast<std::uint32_t>(slot), state_.breakpointInput.module, state_.breakpointInput.offset, state_.breakpointType, state_.breakpointSize, state_.target);
        const std::string responseLine = sendAgentRequest(request, error, "发送 breakpoint.set 失败");
        if (responseLine.empty()) {
            state_.lastError = error;
            state_.lastAction = "下断失败: " + error;
            refreshUi();
            return;
        }

        const auto response = xc::parseBreakpointSetResponse(responseLine);
        state_.breakpoints[slot] = response;
        state_.breakpointLabels[slot] = displayAddress(state_.breakpointInput);
        state_.lastError = response.ok ? "" : response.error;
        state_.lastAction = response.ok ? response.message : "下断失败: " + response.error;
        if (response.ok) {
            updateHitPolling();
            queryHitRecords(static_cast<std::uint32_t>(slot));
            return;
        }
        refreshUi();
    }

    void removeBreakpoint() {
        applyInputs(false);
        const auto slot = static_cast<std::size_t>(slotSpin_->value());
        state_.lastAction = "正在删除硬件断点 slot" + std::to_string(slot);
        refreshUi();

        std::string error;
        const std::string responseLine = sendAgentRequest(
            xc::breakpointRemoveRequestJson(state_.requestId++, static_cast<std::uint32_t>(slot), state_.target),
            error,
            "发送 breakpoint.remove 失败");
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
            state_.hitRecords = {};
        }
        updateHitPolling();
        state_.lastError = response.ok ? "" : response.error;
        state_.lastAction = response.ok ? response.message : "删断失败: " + response.error;
        refreshUi();
    }

    void queryBreakpointInfo() {
        applyInputs(false);
        state_.lastAction = "正在查询硬件断点信息";
        refreshUi();

        std::string error;
        const std::string responseLine = sendAgentRequest(
            xc::breakpointInfoRequestJson(state_.requestId++),
            error,
            "发送 breakpoint.info 失败");
        if (responseLine.empty()) {
            state_.lastError = error;
            state_.lastAction = "查询断点失败: " + error;
            state_.lastBreakpointInfo = jsonError(error);
            refreshUi();
            return;
        }
        state_.lastError.clear();
        state_.lastAction = "硬件断点信息已刷新";
        state_.lastBreakpointInfo = responseLine;
        queryHitRecords(static_cast<std::uint32_t>(slotSpin_->value()));
    }

    void queryHitRecords(std::uint32_t slot) {
        std::string error;
        const std::string responseLine = sendAgentRequest(
            xc::recordsGetRequestJson(state_.requestId++, slot),
            error,
            "发送 records.get 失败");
        if (responseLine.empty()) {
            state_.lastError = error;
            state_.lastAction = "查询命中记录失败: " + error;
            state_.hitRecords = {};
            refreshUi();
            return;
        }

        state_.hitRecords = xc::parseRecordsGetResponse(responseLine);
        if (!state_.hitRecords.ok) {
            state_.lastError = state_.hitRecords.error;
            state_.lastAction = "查询命中记录失败: " + state_.hitRecords.error;
        } else if (!state_.hitRecords.records.empty()) {
            state_.lastError.clear();
            state_.lastAction = "断点命中记录已刷新: slot" + std::to_string(slot)
                + " hitCount=" + std::to_string(state_.hitRecords.records.back().hitCount);
        }
        refreshUi();
    }

    void pollHitRecords() {
        if (!state_.connected || !hasAgentSocket()) {
            updateHitPolling();
            return;
        }
        for (std::uint32_t slot = 0; slot < 4; ++slot) {
            if (state_.breakpoints[slot].ok && state_.breakpoints[slot].enabled) {
                queryHitRecords(slot);
                return;
            }
        }
        updateHitPolling();
    }

    void updateHitPolling() {
        bool hasActiveBreakpoint = false;
        for (const auto& breakpoint : state_.breakpoints) {
            if (breakpoint.ok && breakpoint.enabled) {
                hasActiveBreakpoint = true;
                break;
            }
        }
        if (hasActiveBreakpoint && state_.connected) {
            if (!hitPollTimer_->isActive()) {
                hitPollTimer_->start();
            }
            return;
        }
        hitPollTimer_->stop();
    }

    void refreshUi() {
        connectionLabel_->setText(state_.connected ? "Agent 在线" : "Agent 未连接");
        connectionLabel_->setProperty("state", state_.connected ? "ok" : "bad");
        driverLabel_->setText("驱动: " + QString(state_.driver.moduleLoaded ? "已加载" : "未加载")
            + "  /proc/modules: " + QString(state_.driver.procModulesReadable ? "可读" : "未确认")
            + "  协议: " + qstr(std::string(xc::kProtocolName)) + " v" + QString::number(xc::kProtocolVersion));
        driverLabel_->setProperty("state", state_.driver.moduleLoaded ? "ok" : "warn");
        errorLabel_->setText(state_.lastError.empty() ? "错误: 无" : "错误: " + qstr(state_.lastError));
        errorLabel_->setProperty("state", state_.lastError.empty() ? "ok" : "bad");
        connectionLabel_->style()->unpolish(connectionLabel_);
        connectionLabel_->style()->polish(connectionLabel_);
        driverLabel_->style()->unpolish(driverLabel_);
        driverLabel_->style()->polish(driverLabel_);
        errorLabel_->style()->unpolish(errorLabel_);
        errorLabel_->style()->polish(errorLabel_);

        refreshBreakpointsTable();
        refreshHitDetails();
        infoText_->setPlainText(state_.lastBreakpointInfo.empty()
            ? "断点信息\n\n只显示 Agent 返回的真实断点和命中信息。启动时不填充模拟数据。"
            : qstr(state_.lastBreakpointInfo));
        statusBar()->showMessage(qstr(state_.lastAction));
    }

    void refreshHitDetails() {
        const bool hasHit = state_.hitRecords.ok && !state_.hitRecords.records.empty();
        emptyDataLabel_->setText(hasHit
            ? "已收到真实断点命中数据"
            : (state_.connected ? "等待真实断点命中数据，当前没有寄存器和调用栈快照" : "等待真实断点命中数据，请先连接 Agent"));
        registersText_->setPlainText(qstr(formatLatestHitRegisters(state_.hitRecords)));
        stackText_->setPlainText(qstr(formatLatestHitStack(state_.hitRecords)));
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
            QWidget { background: #101318; color: #dce3ec; font-family: "Microsoft YaHei UI"; font-size: 13px; }
            QFrame#connectionBar, QFrame#breakpointEditor, QFrame#sessionbar, QStatusBar { background: #171c23; border: 1px solid #2c3541; border-radius: 6px; }
            QFrame#breakpointEditor { background: #141922; }
            QStatusBar { color: #aeb8c5; }
            QGroupBox { border: 1px solid #2f3946; border-radius: 6px; margin-top: 20px; padding: 10px; background: #151a21; font-weight: 600; }
            QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #f1f5f9; }
            QLabel#fieldLabel { color: #91a0b4; font-weight: 600; }
            QLabel#paneHint { color: #94a3b8; }
            QLineEdit, QComboBox, QSpinBox, QPlainTextEdit, QTableWidget { background: #0b0f14; border: 1px solid #303a47; border-radius: 5px; color: #e7edf5; selection-background-color: #256d9f; selection-color: #ffffff; }
            QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid #4f8fc9; }
            QLineEdit, QComboBox, QSpinBox { min-height: 28px; padding: 2px 8px; }
            QPushButton { background: #222b36; border: 1px solid #3b4858; border-radius: 5px; color: #eef4fb; min-height: 29px; padding: 3px 12px; font-weight: 600; }
            QPushButton:hover { background: #2b3644; border-color: #53657a; }
            QPushButton:pressed { background: #1d2733; }
            QPushButton[role="primaryButton"] { background: #1f6f9f; border-color: #2f91c8; color: #ffffff; }
            QPushButton[role="primaryButton"]:hover { background: #247dac; }
            QPushButton[role="secondaryButton"] { background: #202832; }
            QPushButton[role="dangerButton"] { background: #40242a; border-color: #70404a; color: #ffd9df; }
            QPushButton[role="dangerButton"]:hover { background: #573039; }
            QLabel[role="statusPill"] { background: #10151c; border: 1px solid #344050; border-radius: 5px; min-height: 24px; padding: 2px 10px; font-weight: 600; }
            QLabel[role="statusPill"][state="ok"] { color: #8ee6a8; border-color: #2d6d47; background: #112019; }
            QLabel[role="statusPill"][state="warn"] { color: #ffd166; border-color: #80652a; background: #211c10; }
            QLabel[role="statusPill"][state="bad"] { color: #ff9aa2; border-color: #7a3b44; background: #241419; }
            QHeaderView::section { background: #202832; border: none; border-right: 1px solid #344050; color: #cdd7e3; padding: 7px 8px; font-weight: 600; }
            QTableWidget { alternate-background-color: #11171e; gridline-color: #24303c; }
            QTableWidget::item { padding: 4px 6px; }
            QTableWidget::item:selected { background: #1f5f87; color: #ffffff; }
            QPlainTextEdit { font-family: "Cascadia Mono", "Consolas"; font-size: 13px; line-height: 1.35; padding: 8px; }
            QLabel#emptyDataLabel { background: #0b0f14; border: 1px dashed #3d4a5a; border-radius: 6px; color: #9ed2ff; font-size: 14px; font-weight: 600; }
            QSplitter::handle { background: #202832; }
            QSplitter::handle:horizontal { width: 5px; }
            QSplitter::handle:vertical { height: 5px; }
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
