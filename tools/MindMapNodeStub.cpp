// 仅供 settings_preview 开发者工具使用的 MindMapNode 桩实现。
//
// 设置对话框只用到节点的三处接口。视觉预览工具不参与真实文档编辑，
// 因此这里用一份静态样式存储替代节点树，避免为了截图链接整个节点/公式渲染依赖。
// 该文件不会进入 leap 主程序。

#include "MindMapNode.h"

namespace {
MindMapLevelStyles& previewStyles() {
    static MindMapLevelStyles styles;
    return styles;
}
} // namespace

MindMapLevelStyles& MindMapNode::levelStyles() {
    return previewStyles();
}

const MindMapLevelStyles& MindMapNode::levelStyles() const {
    return previewStyles();
}

void MindMapNode::applyLevelStyles(const MindMapLevelStyles& styles) {
    previewStyles() = styles;
}

void MindMapNode::applyTextLineWidthPreferenceToSubtree() {
    // 预览工具没有真实布局，无需处理。
}
