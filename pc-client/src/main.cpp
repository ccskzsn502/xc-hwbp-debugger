#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QDockWidget>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTcpServer>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "xc/mcp_core.hpp"

namespace {

constexpr const char* kMcpPreviewEndpoint = "http://127.0.0.1:23947/mcp";

QString mcpPreviewResponse() {
    return QString::fromStdString(xc::mcp::serverName()) + " preview only: " + QString::fromUtf8(kMcpPreviewEndpoint);
}

QLabel* makeLabel(const QString& text, const QString& objectName = {}) {
    auto* label = new QLabel(text);
    if (!objectName.isEmpty()) {
        label->setObjectName(objectName);
    }
    return label;
}

QFrame* makePane(const QString& objectName) {
    auto* pane = new QFrame;
    pane->setObjectName(objectName);
    pane->setFrameShape(QFrame::NoFrame);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(12);
    return pane;
}

QVBoxLayout* paneLayout(QFrame* pane) {
    return static_cast<QVBoxLayout*>(pane->layout());
}

QPushButton* makeButton(const QString& text, const QString& objectName) {
    auto* button = new QPushButton(text);
    button->setObjectName(objectName);
    button->setMinimumHeight(32);
    return button;
}

QTableWidgetItem* item(const QString& text, const QColor& foreground = QColor()) {
    auto* tableItem = new QTableWidgetItem(text);
    tableItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    if (foreground.isValid()) {
        tableItem->setForeground(QBrush(foreground));
    }
    return tableItem;
}

void setupTable(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setFrameShape(QFrame::NoFrame);
}

class DebuggerShellWindow final : public QMainWindow {
public:
    DebuggerShellWindow() {
        setWindowTitle(QStringLiteral("XC HWBP Debugger"));
        resize(1280, 760);
        setMinimumSize(1040, 640);

        buildChrome();
        buildLeftDock();
        buildCenter();
        buildRightDock();
        buildBottomDock();
        applyStyles();
    }

private:
    void buildChrome() {
        auto* header = new QFrame;
        header->setObjectName("headerBar");
        auto* layout = new QHBoxLayout(header);
        layout->setContentsMargins(18, 14, 18, 14);
        layout->setSpacing(14);

        auto* titleBlock = new QWidget;
        auto* titleLayout = new QVBoxLayout(titleBlock);
        titleLayout->setContentsMargins(0, 0, 0, 0);
        titleLayout->setSpacing(2);
        titleLayout->addWidget(makeLabel(QStringLiteral("XC HWBP Debugger"), "appTitle"));
        titleLayout->addWidget(makeLabel(QStringLiteral("PC 调试器 GUI 预览壳，断点与 Agent 逻辑暂未接入"), "appSubtitle"));

        layout->addWidget(titleBlock, 1);
        layout->addWidget(makeStatusBadge(QStringLiteral("Agent 未连接"), "offlineBadge"));
        layout->addWidget(makeStatusBadge(QStringLiteral("Driver 未检测"), "idleBadge"));
        layout->addWidget(makeStatusBadge(QStringLiteral("MCP 预览"), "mcpBadge"));

        setMenuWidget(header);
    }

    QLabel* makeStatusBadge(const QString& text, const QString& objectName) {
        auto* label = makeLabel(text, objectName);
        label->setProperty("statusBadge", true);
        label->setMinimumHeight(28);
        label->setAlignment(Qt::AlignCenter);
        return label;
    }

    void buildLeftDock() {
        auto* dock = makeDock(QStringLiteral("会话"), "sessionDock");
        auto* body = makePane("controlRail");
        auto* layout = paneLayout(body);

        layout->addWidget(makeSectionTitle(QStringLiteral("连接")));
        auto* endpoint = new QLineEdit(QStringLiteral("192.168.1.10:23946"));
        endpoint->setPlaceholderText(QStringLiteral("Agent 地址"));
        layout->addWidget(endpoint);
        auto* target = new QLineEdit(QStringLiteral("com.tencent.tmgp.sgame"));
        target->setPlaceholderText(QStringLiteral("进程名或 PID"));
        layout->addWidget(target);

        auto* connectRow = new QHBoxLayout;
        connectRow->setSpacing(8);
        connectRow->addWidget(makeButton(QStringLiteral("连接"), "primaryButton"));
        connectRow->addWidget(makeButton(QStringLiteral("断开"), "secondaryButton"));
        layout->addLayout(connectRow);

        layout->addSpacing(10);
        layout->addWidget(makeSectionTitle(QStringLiteral("断点编辑")));
        auto* address = new QLineEdit(QStringLiteral("libtersafe.so+0x488F08"));
        address->setPlaceholderText(QStringLiteral("module.so+offset 或 0xaddr"));
        layout->addWidget(address);

        auto* typeRow = new QHBoxLayout;
        typeRow->setSpacing(8);
        typeRow->addWidget(makeSmallTool(QStringLiteral("X"), true));
        typeRow->addWidget(makeSmallTool(QStringLiteral("R"), false));
        typeRow->addWidget(makeSmallTool(QStringLiteral("W"), false));
        typeRow->addWidget(makeSmallTool(QStringLiteral("RW"), false));
        layout->addLayout(typeRow);

        auto* actionRow = new QHBoxLayout;
        actionRow->setSpacing(8);
        actionRow->addWidget(makeButton(QStringLiteral("预览添加"), "primaryButton"));
        actionRow->addWidget(makeButton(QStringLiteral("清空"), "dangerButton"));
        layout->addLayout(actionRow);

        layout->addStretch();

        dock->setWidget(body);
        addDockWidget(Qt::LeftDockWidgetArea, dock);
    }

    QToolButton* makeSmallTool(const QString& text, bool checked) {
        auto* button = new QToolButton;
        button->setText(text);
        button->setCheckable(true);
        button->setChecked(checked);
        button->setMinimumSize(44, 30);
        return button;
    }

    QLabel* makeSectionTitle(const QString& text) {
        return makeLabel(text, "sectionTitle");
    }

    void buildCenter() {
        auto* center = makePane("activityPane");
        auto* layout = paneLayout(center);

        auto* titleRow = new QHBoxLayout;
        titleRow->setSpacing(10);
        titleRow->addWidget(makeLabel(QStringLiteral("断点与命中"), "workspaceTitle"));
        titleRow->addStretch();
        titleRow->addWidget(makeButton(QStringLiteral("刷新"), "secondaryButton"));
        layout->addLayout(titleRow);

        breakpointsTable_ = new QTableWidget;
        setupTable(breakpointsTable_, {QStringLiteral("Slot"), QStringLiteral("地址"), QStringLiteral("类型"), QStringLiteral("命中"), QStringLiteral("状态")});
        breakpointsTable_->setRowCount(4);
        for (int row = 0; row < 4; ++row) {
            breakpointsTable_->setItem(row, 0, item(QString::number(row)));
            breakpointsTable_->setItem(row, 1, item(QStringLiteral("等待配置"), QColor("#8291A8")));
            breakpointsTable_->setItem(row, 2, item(QStringLiteral("-"), QColor("#8291A8")));
            breakpointsTable_->setItem(row, 3, item(QStringLiteral("0"), QColor("#8291A8")));
            breakpointsTable_->setItem(row, 4, item(QStringLiteral("未启用"), QColor("#8291A8")));
        }
        layout->addWidget(breakpointsTable_, 2);

        layout->addWidget(makeLabel(QStringLiteral("命中流"), "sectionTitle"));
        hitsTable_ = new QTableWidget;
        setupTable(hitsTable_, {QStringLiteral("时间"), QStringLiteral("Slot"), QStringLiteral("摘要")});
        hitsTable_->setRowCount(3);
        hitsTable_->setItem(0, 0, item(QStringLiteral("--:--:--"), QColor("#8291A8")));
        hitsTable_->setItem(0, 1, item(QStringLiteral("-"), QColor("#8291A8")));
        hitsTable_->setItem(0, 2, item(QStringLiteral("等待真实断点命中数据"), QColor("#8291A8")));
        hitsTable_->setItem(1, 0, item(QStringLiteral("--:--:--"), QColor("#8291A8")));
        hitsTable_->setItem(1, 1, item(QStringLiteral("-"), QColor("#8291A8")));
        hitsTable_->setItem(1, 2, item(QStringLiteral("连接 Agent 后这里显示命中事件"), QColor("#8291A8")));
        hitsTable_->setItem(2, 0, item(QStringLiteral("--:--:--"), QColor("#8291A8")));
        hitsTable_->setItem(2, 1, item(QStringLiteral("-"), QColor("#8291A8")));
        hitsTable_->setItem(2, 2, item(QStringLiteral("选择事件后右侧显示寄存器快照"), QColor("#8291A8")));
        layout->addWidget(hitsTable_, 3);

        setCentralWidget(center);
    }

    void buildRightDock() {
        auto* dock = makeDock(QStringLiteral("检查器"), "inspectorDock");
        auto* tabs = new QTabWidget;
        tabs->setObjectName("inspectorTabs");

        auto* registers = makePane("inspectorPane");
        auto* registerLayout = paneLayout(registers);
        registerText_ = new QPlainTextEdit;
        registerText_->setReadOnly(true);
        registerText_->setPlainText(QStringLiteral(
            "寄存器快照\n\n"
            "等待真实断点命中数据。\n"
            "后续接入 records.get 后，这里显示 PC / LR / SP / X0-X29。"));
        registerLayout->addWidget(registerText_);
        tabs->addTab(registers, QStringLiteral("寄存器"));

        auto* raw = makePane("rawPane");
        auto* rawLayout = paneLayout(raw);
        rawText_ = new QPlainTextEdit;
        rawText_->setReadOnly(true);
        rawText_->setPlainText(mcpPreviewResponse() + QStringLiteral("\n\n原始 JSON 和 MCP 请求预览后续接入。"));
        rawLayout->addWidget(rawText_);
        tabs->addTab(raw, QStringLiteral("原始"));

        dock->setWidget(tabs);
        addDockWidget(Qt::RightDockWidgetArea, dock);
    }

    void buildBottomDock() {
        auto* dock = makeDock(QStringLiteral("输出"), "outputDock");
        logText_ = new QPlainTextEdit;
        logText_->setObjectName("logView");
        logText_->setReadOnly(true);
        logText_->setPlainText(QStringLiteral(
            "[ui] GUI 壳已启动\n"
            "[agent] 未连接，按钮暂为界面预览\n"
            "[mcp] 预留本地端点 http://127.0.0.1:23947/mcp"));
        dock->setWidget(logText_);
        addDockWidget(Qt::BottomDockWidgetArea, dock);
        resizeDocks({dock}, {160}, Qt::Vertical);
    }

    QDockWidget* makeDock(const QString& title, const QString& objectName) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(objectName);
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
        return dock;
    }

    void applyStyles() {
        qApp->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
        setStyleSheet(QStringLiteral(R"(
            QMainWindow {
                background: #120E1A;
                color: #D8C8E8;
            }
            QFrame#headerBar {
                background: rgba(30, 20, 50, 180);
                border-bottom: 1px solid rgba(140, 80, 200, 100);
            }
            QLabel#appTitle {
                color: #E8D0F8;
                font-size: 19px;
                font-weight: 700;
            }
            QLabel#appSubtitle {
                color: #9A80A8;
                font-size: 12px;
            }
            QLabel[statusBadge="true"] {
                padding: 4px 10px;
                border: 1px solid rgba(140, 80, 200, 80);
                border-radius: 4px;
                color: #D0B8E0;
                background: rgba(80, 40, 120, 100);
            }
            QLabel#offlineBadge {
                color: #FFB4A8;
                border-color: rgba(200, 80, 80, 100);
                background: rgba(120, 40, 40, 100);
            }
            QLabel#idleBadge {
                color: #FFDDA0;
                border-color: rgba(200, 160, 60, 100);
                background: rgba(120, 90, 30, 100);
            }
            QLabel#mcpBadge {
                color: #B8E8C3;
                border-color: rgba(80, 180, 120, 100);
                background: rgba(40, 100, 60, 100);
            }
            QDockWidget {
                color: #D0B8E0;
                titlebar-close-icon: none;
                titlebar-normal-icon: none;
            }
            QDockWidget::title {
                background: rgba(25, 18, 40, 200);
                border: 1px solid rgba(140, 80, 200, 80);
                padding: 7px 10px;
                text-align: left;
            }
            QFrame#controlRail,
            QFrame#activityPane,
            QFrame#inspectorPane,
            QFrame#modulesPane,
            QFrame#rawPane {
                background: rgba(20, 15, 30, 180);
                border: 1px solid rgba(140, 80, 200, 80);
            }
            QLabel#sectionTitle,
            QLabel#workspaceTitle {
                color: #E0C8F0;
                font-weight: 700;
                font-size: 13px;
            }
            QLabel#workspaceTitle {
                font-size: 17px;
            }
            QLineEdit {
                min-height: 30px;
                padding: 3px 8px;
                color: #D8C8E8;
                background: rgba(15, 10, 25, 180);
                border: 1px solid rgba(140, 80, 200, 80);
                border-radius: 4px;
                selection-background-color: rgba(120, 60, 180, 180);
            }
            QPushButton,
            QToolButton {
                padding: 5px 10px;
                border-radius: 4px;
                border: 1px solid rgba(140, 80, 200, 80);
                background: rgba(40, 25, 60, 180);
                color: #D0B8E0;
                font-weight: 600;
            }
            QPushButton#primaryButton,
            QToolButton:checked {
                background: rgba(100, 50, 160, 200);
                border-color: rgba(140, 80, 200, 140);
                color: #F0E0F8;
            }
            QPushButton#secondaryButton {
                background: rgba(30, 20, 45, 160);
            }
            QPushButton#dangerButton {
                color: #FFB4A8;
                background: rgba(100, 30, 30, 140);
                border-color: rgba(200, 80, 80, 100);
            }
            QTableWidget {
                background: rgba(15, 10, 25, 180);
                alternate-background-color: rgba(25, 18, 40, 160);
                color: #D0B8E0;
                border: 1px solid rgba(140, 80, 200, 80);
                gridline-color: rgba(140, 80, 200, 40);
                selection-background-color: rgba(120, 60, 180, 160);
                selection-color: #F0E0F8;
            }
            QHeaderView::section {
                background: rgba(30, 20, 50, 180);
                color: #B098C8;
                border: 0;
                border-bottom: 1px solid rgba(140, 80, 200, 100);
                padding: 7px 8px;
                font-weight: 700;
            }
            QPlainTextEdit {
                background: rgba(18, 12, 30, 180);
                border: 1px solid rgba(140, 80, 200, 80);
                color: #D0B8E0;
                border-radius: 4px;
                padding: 10px;
                selection-background-color: rgba(120, 60, 180, 180);
                font-family: Consolas, "Cascadia Mono", monospace;
                font-size: 12px;
            }
            QTabWidget::pane {
                border: 1px solid rgba(140, 80, 200, 80);
                border-radius: 4px;
                background: rgba(20, 15, 30, 160);
            }
            QTabBar::tab {
                background: rgba(30, 20, 50, 160);
                color: #B098C8;
                border: 1px solid rgba(140, 80, 200, 60);
                padding: 7px 12px;
                margin-right: 2px;
                border-radius: 4px 4px 0 0;
            }
            QTabBar::tab:selected {
                background: rgba(40, 25, 65, 180);
                color: #E0C8F0;
            }
        )"));
    }

    QTableWidget* slotsTable_ = nullptr;
    QTableWidget* breakpointsTable_ = nullptr;
    QTableWidget* hitsTable_ = nullptr;
    QPlainTextEdit* registerText_ = nullptr;
    QPlainTextEdit* rawText_ = nullptr;
    QPlainTextEdit* logText_ = nullptr;
    QTcpServer mcpPreviewServer_;
};

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    DebuggerShellWindow window;
    window.show();
    return app.exec();
}