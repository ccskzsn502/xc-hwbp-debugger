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
#include <QClipboard>
#include <QComboBox>
#include <QFont>
#include <QFrame>
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
#include <vector>

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
    std::size_t selectedHitIndex = 0;
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

struct AddressResolverContext {
    std::vector<xc::ModuleMapEntry> modules;
};

std::string padLabel(std::string label) {
    while (label.size() < 8) {
        label.push_back(' ');
    }
    return label;
}

std::string resolveAddressLabel(std::uint64_t address, const AddressResolverContext& context) {
    return xc::resolveAddressWithModules(address, context.modules);
}

std::string registerLine(const std::string& name, std::uint64_t value) {
    return padLabel(name) + xc::hexAddress(value);
}

std::string addressLine(const std::string& name, std::uint64_t value, const AddressResolverContext& context) {
    return padLabel(name) + resolveAddressLabel(value, context);
}

std::string formatHitSnapshot(const xc::RecordsGetResponse& records, const xc::HitRecord* hit, const AddressResolverContext& context) {
    if (!records.ok || hit == nullptr) {
        return "命中快照\n\n等待真实断点命中数据。选择左侧命中列表后显示对应 PC/LR/SP 和寄存器。";
    }

    std::ostringstream out;
    out << "命中快照\n\n";
    std::size_t index = 0;
    for (std::size_t i = 0; i < records.records.size(); ++i) {
        if (&records.records[i] == hit) {
            index = i;
            break;
        }
    }
    out << "slot " << records.slot << "   hit #" << xc::visibleHitNumber(records, index)
        << "   raw_hit_count " << hit->hitCount << "\n";
    out << addressLine("PC", hit->pc, context) << "\n";
    out << addressLine("LR", hit->lr, context) << "\n";
    out << addressLine("SP", hit->sp, context) << "\n\n";
    out << registerLine("PSTATE", hit->pstate) << "\n";
    out << registerLine("SYSCALL", hit->syscallno) << "\n";
    out << padLabel("FPSR") << hit->fpsr << "\n";
    out << padLabel("FPCR") << hit->fpcr << "\n\n";
    for (int i = 0; i < 29; ++i) {
        out << addressLine("X" + std::to_string(i), hit->x[i], context) << "\n";
    }
    out << addressLine("X29", hit->x[29], context) << "\n";
    return out.str();
}

std::string formatHitListText(const xc::RecordsGetResponse& records, const AddressResolverContext& context) {
    if (!records.ok) {
        return "records.get failed: " + records.error;
    }
    if (records.records.empty()) {
        return "no hit records";
    }
    std::ostringstream out;
    out << "slot " << records.slot << " record_count " << records.recordCount << " returned " << records.returned << "\n";
    for (std::size_t i = 0; i < records.records.size(); ++i) {
        const auto& hit = records.records[i];
        out << "#" << xc::visibleHitNumber(records, i)
            << " PC " << resolveAddressLabel(hit.pc, context)
            << " LR " << resolveAddressLabel(hit.lr, context)
            << " SP " << resolveAddressLabel(hit.sp, context) << "\n";
    }
    return out.str();
}

std::string hitSummaryLine(const xc::RecordsGetResponse& records, std::size_t index, const AddressResolverContext& context) {
    if (index >= records.records.size()) {
        return {};
    }
    const auto& hit = records.records[index];
    std::ostringstream out;
    out << "PC " << resolveAddressLabel(hit.pc, context)
        << "   LR " << resolveAddressLabel(hit.lr, context)
        << "   SP " << resolveAddressLabel(hit.sp, context)
        << "   raw " << hit.hitCount;
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
    table->setGridStyle(Qt::NoPen);
    table->setFrameShape(QFrame::NoFrame);
    table->setWordWrap(false);
    table->setFocusPolicy(Qt::NoFocus);
    table->verticalHeader()->setDefaultSectionSize(26);
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
    QTableWidget* hitRecordsTable_ = nullptr;
    QLabel* emptyDataLabel_ = nullptr;
    QPlainTextEdit* hitSnapshotText_ = nullptr;
    QPlainTextEdit* infoText_ = nullptr;
    QTimer* hitPollTimer_ = nullptr;
    std::size_t selectedHitIndex_ = 0;
    bool manualHitSelection_ = false;

    QLabel* fieldLabel(const QString& text) {
        auto* label = new QLabel(text);
        label->setObjectName("fieldLabel");
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
        root->setContentsMargins(8, 8, 8, 6);
        root->setSpacing(6);
        root->addWidget(buildCommandBar());

        auto* splitter = new QSplitter(Qt::Horizontal);
        splitter->addWidget(buildHitStreamPane());
        splitter->addWidget(buildRegisterInspector());
        splitter->setSizes({470, 890});
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        root->addWidget(splitter, 1);

        setCentralWidget(central);
        applyStyle();
    }

    QWidget* buildCommandBar() {
        auto* frame = new QFrame;
        frame->setObjectName("commandBar");
        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(5);

        auto* controls = new QHBoxLayout;
        controls->setContentsMargins(0, 0, 0, 0);
        controls->setSpacing(7);

        endpointEdit_ = new QLineEdit(qstr(state_.endpoint));
        targetEdit_ = new QLineEdit(qstr(state_.target));
        endpointEdit_->setPlaceholderText("手机 IP");
        targetEdit_->setPlaceholderText("包名或 PID");
        endpointEdit_->setMinimumWidth(132);
        endpointEdit_->setMaximumWidth(170);
        targetEdit_->setMinimumWidth(190);
        targetEdit_->setMaximumWidth(280);

        addressEdit_ = new QLineEdit;
        addressEdit_->setPlaceholderText("绝对地址或 libtersafe.so+0x488F08");
        addressEdit_->setMinimumWidth(260);

        typeCombo_ = new QComboBox;
        typeCombo_->addItems({"x", "r", "w", "rw"});
        typeCombo_->setMaximumWidth(58);
        sizeSpin_ = new QSpinBox;
        sizeSpin_->setRange(1, 8);
        sizeSpin_->setValue(4);
        sizeSpin_->setMaximumWidth(58);
        slotSpin_ = new QSpinBox;
        slotSpin_->setRange(0, 3);
        slotSpin_->setMaximumWidth(58);

        auto* connectButton = actionButton("连接", "primaryButton");
        auto* disconnectButton = actionButton("断开", "secondaryButton");
        auto* probeButton = actionButton("检测", "secondaryButton");
        auto* setButton = actionButton("下断", "primaryButton");
        auto* removeButton = actionButton("删断", "dangerButton");
        auto* infoButton = actionButton("刷新", "secondaryButton");

        controls->addWidget(fieldLabel("Agent"));
        controls->addWidget(endpointEdit_);
        controls->addWidget(fieldLabel("目标"));
        controls->addWidget(targetEdit_);
        controls->addWidget(fieldLabel("断点"));
        controls->addWidget(addressEdit_, 1);
        controls->addWidget(fieldLabel("类型"));
        controls->addWidget(typeCombo_);
        controls->addWidget(fieldLabel("长度"));
        controls->addWidget(sizeSpin_);
        controls->addWidget(fieldLabel("槽"));
        controls->addWidget(slotSpin_);
        controls->addWidget(connectButton);
        controls->addWidget(disconnectButton);
        controls->addWidget(probeButton);
        controls->addWidget(setButton);
        controls->addWidget(removeButton);
        controls->addWidget(infoButton);

        auto* status = new QHBoxLayout;
        status->setContentsMargins(0, 0, 0, 0);
        status->setSpacing(14);
        connectionLabel_ = new QLabel;
        driverLabel_ = new QLabel;
        errorLabel_ = new QLabel;
        connectionLabel_->setObjectName("statusText");
        driverLabel_->setObjectName("statusText");
        errorLabel_->setObjectName("statusText");
        status->addWidget(connectionLabel_);
        status->addWidget(driverLabel_, 1);
        status->addWidget(errorLabel_, 1);

        layout->addLayout(controls);
        layout->addLayout(status);

        connect(connectButton, &QPushButton::clicked, this, [this] { markConnected(); });
        connect(probeButton, &QPushButton::clicked, this, [this] { markConnected(); });
        connect(disconnectButton, &QPushButton::clicked, this, [this] { disconnectAgent(); });
        connect(setButton, &QPushButton::clicked, this, [this] { setBreakpoint(); });
        connect(removeButton, &QPushButton::clicked, this, [this] { removeBreakpoint(); });
        connect(infoButton, &QPushButton::clicked, this, [this] { queryBreakpointInfo(); });
        return frame;
    }

    QWidget* buildHitStreamPane() {
        auto* frame = new QFrame;
        frame->setObjectName("hitStreamPane");
        frame->setMinimumWidth(420);
        frame->setMaximumWidth(560);
        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(7);

        auto* title = new QLabel("断点 / 命中流");
        title->setObjectName("paneTitle");
        breakpointsTable_ = new QTableWidget;
        setupTable(breakpointsTable_, {"槽", "类型", "模块/偏移", "实际地址", "状态"});
        breakpointsTable_->horizontalHeader()->setStretchLastSection(false);
        breakpointsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        breakpointsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        breakpointsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        breakpointsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        breakpointsTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        connect(breakpointsTable_, &QTableWidget::itemSelectionChanged, this, [this] {
            const int row = breakpointsTable_->currentRow();
            if (row >= 0) {
                slotSpin_->setValue(row);
                queryHitRecords(static_cast<std::uint32_t>(row));
            }
        });

        auto* hitsTitle = new QLabel("命中流");
        hitsTitle->setObjectName("paneHint");
        hitRecordsTable_ = new QTableWidget;
        hitRecordsTable_->setColumnCount(2);
        setupTable(hitRecordsTable_, {"#", "命中"});
        hitRecordsTable_->horizontalHeader()->setStretchLastSection(true);
        hitRecordsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        hitRecordsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        connect(hitRecordsTable_, &QTableWidget::itemSelectionChanged, this, [this] {
            const int row = hitRecordsTable_->currentRow();
            if (row >= 0) {
                selectedHitIndex_ = static_cast<std::size_t>(row);
                state_.selectedHitIndex = selectedHitIndex_;
                manualHitSelection_ = true;
                refreshHitDetails();
            }
        });

        layout->addWidget(title);
        layout->addWidget(breakpointsTable_, 1);
        layout->addWidget(hitsTitle);
        layout->addWidget(hitRecordsTable_, 2);
        return frame;
    }

    QWidget* buildRegisterInspector() {
        auto* frame = new QFrame;
        frame->setObjectName("registerInspector");
        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(7);

        emptyDataLabel_ = new QLabel("等待真实断点命中数据");
        emptyDataLabel_->setObjectName("emptyDataLabel");
        emptyDataLabel_->setAlignment(Qt::AlignCenter);
        emptyDataLabel_->setMinimumHeight(30);

        auto* titleBar = new QHBoxLayout;
        titleBar->setContentsMargins(0, 0, 0, 0);
        titleBar->setSpacing(6);
        auto* title = new QLabel("寄存器");
        title->setObjectName("paneTitle");
        auto* copyCurrentButton = actionButton("复制当前命中", "secondaryButton");
        auto* copyAllButton = actionButton("复制全部命中", "secondaryButton");
        titleBar->addWidget(title);
        titleBar->addStretch(1);
        titleBar->addWidget(copyCurrentButton);
        titleBar->addWidget(copyAllButton);
        connect(copyCurrentButton, &QPushButton::clicked, this, [this] { copyCurrentHit(); });
        connect(copyAllButton, &QPushButton::clicked, this, [this] { copyAllHits(); });

        auto* detailSplitter = new QSplitter(Qt::Vertical);
        hitSnapshotText_ = new QPlainTextEdit;
        hitSnapshotText_->setReadOnly(true);
        hitSnapshotText_->setObjectName("hitSnapshotText");
        hitSnapshotText_->setFrameShape(QFrame::NoFrame);
        infoText_ = new QPlainTextEdit;
        infoText_->setReadOnly(true);
        infoText_->setObjectName("infoText");
        infoText_->setFrameShape(QFrame::NoFrame);
        detailSplitter->addWidget(hitSnapshotText_);
        detailSplitter->addWidget(infoText_);
        detailSplitter->setSizes({560, 120});

        layout->addLayout(titleBar);
        layout->addWidget(emptyDataLabel_);
        layout->addWidget(detailSplitter, 1);
        return frame;
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
        state_.selectedHitIndex = 0;
        selectedHitIndex_ = 0;
        manualHitSelection_ = false;
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
            state_.selectedHitIndex = 0;
            selectedHitIndex_ = 0;
            manualHitSelection_ = false;
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
            xc::recordsGetRequestJson(state_.requestId++, slot, state_.target),
            error,
            "发送 records.get 失败");
        if (responseLine.empty()) {
            state_.lastError = error;
            state_.lastAction = "查询命中记录失败: " + error;
            state_.hitRecords = {};
            state_.selectedHitIndex = 0;
            selectedHitIndex_ = 0;
            manualHitSelection_ = false;
            refreshUi();
            return;
        }

        state_.hitRecords = xc::parseRecordsGetResponse(responseLine);
        if (!state_.hitRecords.records.empty()) {
            if (!manualHitSelection_ || selectedHitIndex_ >= state_.hitRecords.records.size()) {
                selectedHitIndex_ = state_.hitRecords.records.size() - 1;
            }
            state_.selectedHitIndex = selectedHitIndex_;
        } else {
            selectedHitIndex_ = 0;
            state_.selectedHitIndex = 0;
            manualHitSelection_ = false;
        }
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
        driverLabel_->setText("驱动: " + QString(state_.driver.moduleLoaded ? "已加载" : "未加载")
            + "  /proc/modules: " + QString(state_.driver.procModulesReadable ? "可读" : "未确认")
            + "  协议: " + qstr(std::string(xc::kProtocolName)) + " v" + QString::number(xc::kProtocolVersion));
        errorLabel_->setText(state_.lastError.empty() ? "错误: 无" : "错误: " + qstr(state_.lastError));

        refreshBreakpointsTable();
        refreshHitRecordsTable();
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
        hitSnapshotText_->setPlainText(qstr(formatHitSnapshot(state_.hitRecords, currentHitRecord(), resolverContext())));
    }

    void copyCurrentHit() {
        QApplication::clipboard()->setText(hitSnapshotText_->toPlainText());
        state_.lastAction = "已复制当前命中快照";
        statusBar()->showMessage(qstr(state_.lastAction));
    }

    void copyAllHits() {
        QApplication::clipboard()->setText(qstr(formatHitListText(state_.hitRecords, resolverContext())));
        state_.lastAction = "已复制全部命中列表";
        statusBar()->showMessage(qstr(state_.lastAction));
    }

    const xc::HitRecord* currentHitRecord() const {
        if (!state_.hitRecords.ok || state_.hitRecords.records.empty()) {
            return nullptr;
        }
        const std::size_t index = selectedHitIndex_ < state_.hitRecords.records.size() ? selectedHitIndex_ : state_.hitRecords.records.size() - 1;
        return &state_.hitRecords.records[index];
    }

    AddressResolverContext resolverContext() const {
        AddressResolverContext context;
        context.modules = state_.hitRecords.modules;
        return context;
    }

    void refreshHitRecordsTable() {
        hitRecordsTable_->setRowCount(0);
        const bool oldSignalsBlocked = hitRecordsTable_->blockSignals(true);
        if (!state_.hitRecords.ok || state_.hitRecords.records.empty()) {
            hitRecordsTable_->blockSignals(oldSignalsBlocked);
            return;
        }
        const AddressResolverContext context = resolverContext();
        hitRecordsTable_->setRowCount(static_cast<int>(state_.hitRecords.records.size()));
        for (int row = 0; row < static_cast<int>(state_.hitRecords.records.size()); ++row) {
            hitRecordsTable_->setItem(row, 0, cell(QString("#%1").arg(xc::visibleHitNumber(state_.hitRecords, static_cast<std::size_t>(row)))));
            hitRecordsTable_->setItem(row, 1, cell(qstr(hitSummaryLine(state_.hitRecords, static_cast<std::size_t>(row), context))));
        }
        hitRecordsTable_->resizeColumnsToContents();
        hitRecordsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        const int selectedRow = static_cast<int>(selectedHitIndex_ < state_.hitRecords.records.size() ? selectedHitIndex_ : state_.hitRecords.records.size() - 1);
        if (hitRecordsTable_->currentRow() != selectedRow) {
            hitRecordsTable_->selectRow(selectedRow);
        }
        hitRecordsTable_->blockSignals(oldSignalsBlocked);
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
            QWidget { background: #0f1115; color: #d7dde6; font-family: "Microsoft YaHei UI"; font-size: 11px; }
            QFrame#commandBar, QFrame#hitStreamPane, QFrame#registerInspector, QStatusBar { background: #151922; border: 1px solid #2b313b; border-radius: 2px; }
            QStatusBar { color: #9aa6b5; }
            QLabel#fieldLabel { color: #8894a5; font-weight: 600; }
            QLabel#paneTitle { color: #f0f4f8; font-size: 12px; font-weight: 700; }
            QLabel#paneHint, QLabel#statusText { color: #9aa6b5; }
            QLineEdit, QComboBox, QSpinBox, QPlainTextEdit, QTableWidget { background: #0b0d11; border: 1px solid #2b313b; border-radius: 2px; color: #e4e9f0; selection-background-color: #225d80; selection-color: #ffffff; }
            QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid #4c8db8; }
            QLineEdit, QComboBox, QSpinBox { min-height: 24px; padding: 1px 6px; }
            QPushButton { background: #202733; border: 1px solid #384150; border-radius: 2px; color: #edf3fa; min-height: 24px; padding: 2px 9px; font-weight: 600; }
            QPushButton:hover { background: #28313f; border-color: #506074; }
            QPushButton:pressed { background: #1b222d; }
            QPushButton[role="primaryButton"] { background: #1f6f9f; border-color: #2f91c8; color: #ffffff; }
            QPushButton[role="primaryButton"]:hover { background: #247dac; }
            QPushButton[role="secondaryButton"] { background: #1b212b; }
            QPushButton[role="dangerButton"] { background: #3b2026; border-color: #70404a; color: #ffd9df; }
            QPushButton[role="dangerButton"]:hover { background: #512b34; }
            QHeaderView::section { background: #1a202a; border: none; border-right: 1px solid #2d3542; color: #c4ccd8; padding: 5px 6px; font-weight: 600; }
            QTableWidget { alternate-background-color: #0f1319; gridline-color: transparent; }
            QTableWidget::item { padding: 1px 6px; border: none; }
            QTableWidget::item:selected { background: #1d577b; color: #ffffff; }
            QPlainTextEdit { font-family: "Cascadia Mono", "Consolas"; font-size: 11px; line-height: 1.25; padding: 6px; }
            QLabel#emptyDataLabel { background: #0b0d11; border: 1px solid #2b313b; border-radius: 2px; color: #9ed2ff; font-weight: 600; }
            QSplitter::handle { background: #202733; }
            QSplitter::handle:horizontal { width: 4px; }
            QSplitter::handle:vertical { height: 4px; }
        )");
    }
};

} // namespace

int main(int argc, char** argv) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setFont(QFont("Microsoft YaHei UI", 9));

    DebuggerWindow window;
    window.show();
    return app.exec();
}
