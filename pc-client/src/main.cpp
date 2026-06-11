#include "eui_neo.h"
#include "xc/protocol.hpp"

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("XC HWBP Debugger")
        .pageId("xc_hwbp_debugger")
        .windowSize(1180, 760);
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    ui.column("root")
        .size(screen.width, screen.height)
        .padding(28.0f)
        .gap(18.0f)
        .content([&] {
            ui.text("title")
                .text("XC Hardware Breakpoint Debugger")
                .fontSize(30.0f)
                .build();

            ui.text("subtitle")
                .text("IDA-style TCP remote debugging client for xc-hwbp-agent")
                .fontSize(16.0f)
                .build();

            ui.text("endpoint")
                .text("Default endpoint: phone-ip:" + std::to_string(xc::kDefaultAgentPort))
                .fontSize(15.0f)
                .build();

            ui.text("status")
                .text("Skeleton ready: GUI framework, shared protocol, and agent target are wired.")
                .fontSize(15.0f)
                .build();
        })
        .build();
}

} // namespace app
