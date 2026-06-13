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

#include <QApplication>
#include <QAbstractItemView>
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

#include <cstdint>
#include <array>
#include <string>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "xc/protocol.hpp"

namespace {

struct ClientState {
    bool connected = false;
    bool helloOk = false;
    xc::HelloResponse hello;
    std::string endpoint = "192.168.1.10";
    std::string target = "com.tencent.tmgp.sgame";
    std::uint64_t breakpointAddress = 0;
    std::string breakpointType = "execute";
    std::uint32_t breakpointSize = 4;
    xc::DriverStatusResponse driver;
    xc::BreakpointSetResponse breakpoints[4];
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
    return "{\"ok\":false,\"error\":\"" + error + "\"}";
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
    QLabel* emptyDataLabel_ = nullptr;
    QTableWidget* watchTable_ = nullptr;
    QTableWidget* slotsTable_ = nullptr;
    QTableWidget* registersTable_ = nullptr;
    QPlainTextEdit* infoText_ = nullptr;
    QPlainTextEdit* stackText_ = nullptr;

    void buildUi() {
        auto* central = new QWidget;
        auto* root = new QVBoxLayout(central);
        root->setContentsMargins(10, 10, 10, 8);
        root->setSpacing(8);
        root->addWidget(buildToolbar());
        root->addWidget(buildStatusStrip());

        auto* splitter = new QSplitter(Qt::Horizontal);
        splitter->addWidget(buildLeftPane());
        splitter->addWidget(buildRightPane());
        splitter->setSizes({290, 1040});
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
        addressEdit_->setPlaceholderText("断点地址，例如 0x...");
        endpointEdit_->setMinimumWidth(135);
        targetEdit_->setMinimumWidth(205);
        addressEdit_->setMinimumWidth(180);

        typeCombo_ = new QComboBox;
        typeCombo_->addItems({"execute", "read", "write", "access"});
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
        auto* infoButton = new QPushButton("查询断点");

        layout->addWidget(new QLabel("Agent"));
        layout->addWidget(endpointEdit_);
        layout->addWidget(new QLabel("目标"));
        layout->addWidget(targetEdit_);
        layout->addWidget(new QLabel("地址"));
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

    QWidget* buildLeftPane() {
        auto* pane = new QWidget;
        pane->setMinimumWidth(260);
        pane->setMaximumWidth(340);
        auto* layout = new QVBoxLayout(pane);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);

        watchTable_ = new QTableWidget;
        setupTable(watchTable_, {"序号", "线程", "状态"});
        auto* watchBox = new QGroupBox("监视断点");
        auto* watchLayout = new QVBoxLayout(watchBox);
        watchLayout->addWidget(watchTable_);

        slotsTable_ = new QTableWidget;
        setupTable(slotsTable_, {"槽", "类型", "地址", "状态"});
        auto* slotsBox = new QGroupBox("硬件断点槽");
        auto* slotsLayout = new QVBoxLayout(slotsBox);
        slotsLayout->addWidget(slotsTable_);

        auto* driverBox = new QGroupBox("驱动状态");
        auto* driverLayout = new QVBoxLayout(driverBox);
        driverLayout->addWidget(new QLabel("lsdriver 状态会在连接 Agent 后刷新"));
        driverLayout->addWidget(new QLabel("启动时不显示任何模拟命中数据"));
        driverLayout->addStretch(1);

        layout->addWidget(watchBox, 1);
        layout->addWidget(slotsBox, 2);
        layout->addWidget(driverBox, 1);
        return pane;
    }

    QWidget* buildRightPane() {
        auto* splitter = new QSplitter(Qt::Vertical);

        registersTable_ = new QTableWidget;
        setupTable(registersTable_, {"寄存器", "HEX", "DEC", "说明"});
        emptyDataLabel_ = new QLabel("等待真实断点命中数据");
        emptyDataLabel_->setObjectName("emptyDataLabel");
        emptyDataLabel_->setAlignment(Qt::AlignCenter);
        emptyDataLabel_->setMinimumHeight(40);
        auto* dataBox = new QGroupBox("数据视图");
        auto* dataLayout = new QVBoxLayout(dataBox);
        dataLayout->addWidget(emptyDataLabel_);
        dataLayout->addWidget(registersTable_, 1);

        infoText_ = new QPlainTextEdit;
        infoText_->setReadOnly(true);
        auto* infoBox = new QGroupBox("断点信息");
        auto* infoLayout = new QVBoxLayout(infoBox);
        infoLayout->addWidget(infoText_);

        stackText_ = new QPlainTextEdit;
        stackText_->setReadOnly(true);
        auto* stackBox = new QGroupBox("堆栈");
        auto* stackLayout = new QVBoxLayout(stackBox);
        stackLayout->addWidget(stackText_);

        splitter->addWidget(dataBox);
        splitter->addWidget(infoBox);
        splitter->addWidget(stackBox);
        splitter->setSizes({360, 230, 140});
        return splitter;
    }

    bool applyInputs(bool requireAddress) {
        const QString endpoint = endpointEdit_->text().trimmed();
        const QString target = targetEdit_->text().trimmed();
        const QString addressText = addressEdit_->text().trimmed();
        bool ok = addressText.isEmpty();
        std::uint64_t address = 0;
        if (!addressText.isEmpty()) {
            address = addressText.toULongLong(&ok, 0);
        }
        if (!ok || (requireAddress && address == 0)) {
            state_.lastError = "请先输入有效断点地址";
            state_.lastAction = "输入错误: 断点地址需要十进制或 0x 十六进制";
            return false;
        }

        if (!endpoint.isEmpty()) {
            state_.endpoint = stdstr(endpoint);
        }
        if (!target.isEmpty()) {
            state_.target = stdstr(target);
        }
        state_.breakpointAddress = address;
        state_.breakpointType = stdstr(typeCombo_->currentText());
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
        for (auto& breakpoint : state_.breakpoints) {
            breakpoint = {};
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

        const std::string request = xc::breakpointSetRequestJson(state_.requestId++, static_cast<std::uint32_t>(slot), state_.breakpointAddress, state_.breakpointType, state_.breakpointSize, state_.target);
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

        refreshWatchTable();
        refreshSlotsTable();
        registersTable_->setRowCount(0);
        emptyDataLabel_->setText(state_.connected
            ? "等待真实断点命中数据，当前没有寄存器快照"
            : "等待真实断点命中数据，请先连接 Agent");
        infoText_->setPlainText(state_.lastBreakpointInfo.empty()
            ? "等待真实断点命中数据\n\n连接 Agent 并发生硬件断点命中后，这里只显示 Agent 返回的真实数据。"
            : qstr(state_.lastBreakpointInfo));
        stackText_->setPlainText("等待真实断点命中数据\n暂无真实命中。后续收到 PC/LR/SP 调用链后在这里显示。");
        statusBar()->showMessage(qstr(state_.lastAction));
    }

    void refreshWatchTable() {
        watchTable_->setRowCount(state_.connected ? 1 : 0);
        if (!state_.connected) {
            return;
        }
        watchTable_->setItem(0, 0, cell("-"));
        watchTable_->setItem(0, 1, cell("-"));
        watchTable_->setItem(0, 2, cell("等待真实断点命中数据"));
    }

    void refreshSlotsTable() {
        slotsTable_->setRowCount(4);
        for (int row = 0; row < 4; ++row) {
            const auto& bp = state_.breakpoints[row];
            slotsTable_->setItem(row, 0, cell(QString::number(row)));
            slotsTable_->setItem(row, 1, cell(bp.ok ? qstr(bp.type) : "-"));
            slotsTable_->setItem(row, 2, cell(bp.ok && bp.address != 0 ? qstr(xc::hexAddress(bp.address)) : "-"));
            slotsTable_->setItem(row, 3, cell(bp.ok && bp.enabled ? "等待确认" : "空"));
        }
        slotsTable_->resizeColumnsToContents();
    }

    void applyStyle() {
        setStyleSheet(R"(
            QWidget { background: #17191d; color: #d8dde5; font-family: "Microsoft YaHei UI"; font-size: 13px; }
            QFrame#toolbar, QFrame#sessionbar, QStatusBar { background: #20242a; border: 1px solid #313741; }
            QGroupBox { border: 1px solid #333a44; border-radius: 4px; margin-top: 20px; padding: 8px; background: #1b1f24; font-weight: 600; }
            QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #eef3f8; }
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
