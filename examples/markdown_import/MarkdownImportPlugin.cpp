#include "LeapPluginApi.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

// markdown_import：把 Markdown 一次性转换成整份思维导图。
//
// 它演示 API 4 的 LeapDocumentWriter：一次调用就把整份文档换掉。
// 这是 API 3 做不到的事情 —— API 3 只能改「选中的那一个节点」，
// 想从 Markdown 生成 50 个节点，就得让用户先选中节点再一个个加，
// 既不现实也没法做成一次撤销。
//
// 注意：本插件完全不需要 include leap 的任何内部头文件，只依赖 LeapPluginApi.h。
class MarkdownImportPlugin : public QObject, public LeapPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LEAP_PLUGIN_IID FILE "plugin.json")
    Q_INTERFACES(LeapPlugin)

public:
    bool activate(LeapHost* host, QString* errorMessage) override {
        if (!host) {
            if (errorMessage) *errorMessage = QStringLiteral("宿主接口为空");
            return false;
        }
        m_host = host;

        // 批量改写是独立可选接口：宿主没实现就说明版本太老，直接拒绝激活，
        // 比激活之后按钮点了没反应要好排查。
        m_writer = dynamic_cast<LeapDocumentWriter*>(host);
        if (!m_writer) {
            if (errorMessage) {
                *errorMessage = QStringLiteral(
                    "宿主没有提供 LeapDocumentWriter，需要 leap 2.17 或更新版本");
            }
            return false;
        }

        LeapToolbarButtonSpec spec;
        spec.text = QStringLiteral("Markdown");
        spec.tooltip = QStringLiteral("把 Markdown 标题与列表转换成整份导图");
        QAction* action = host->addToolbarButton(spec);
        if (action) {
            connect(action, &QAction::triggered, this, &MarkdownImportPlugin::showDialog);
        }
        return true;
    }

    void deactivate() override {
        m_host = nullptr;
        m_writer = nullptr;
    }

private:
    // 把 Markdown 文本解析成「层级 + 文字」的扁平列表。
    //
    // 只认两种结构，够用且不会误判：
    //   - ATX 标题：行首 1~6 个 # 加空格，层级 = # 的个数；
    //   - 列表项：行首可选缩进 + -/*/+ 或 1. 加空格，层级 = 10 + 缩进格数/2。
    // 列表层级从 10 起，是为了让它总是挂在最近一个标题下面（标题层级最多 6）。
    // 其余非空行当作列表项处理，这样纯文本也能直接转。
    struct Entry {
        int level = 0;
        QString text;
    };

    static QVector<Entry> parseMarkdown(const QString& source) {
        QVector<Entry> entries;
        const QStringList lines = source.split(QLatin1Char('\n'));
        for (const QString& rawLine : lines) {
            const QString line = rawLine.trimmed();
            if (line.isEmpty()) continue;

            int level = -1;
            QString text;

            if (line.startsWith(QLatin1Char('#'))) {
                int hashes = 0;
                while (hashes < line.size() && line.at(hashes) == QLatin1Char('#')) ++hashes;
                if (hashes <= 6 && hashes < line.size() && line.at(hashes).isSpace()) {
                    level = hashes;
                    text = line.mid(hashes).trimmed();
                }
            }

            if (level < 0) {
                // 缩进按原始行算（trimmed 之后就拿不到缩进了）。
                int indent = 0;
                while (indent < rawLine.size() && rawLine.at(indent) == QLatin1Char(' ')) ++indent;
                const QString body = rawLine.mid(indent).trimmed();
                QString stripped = body;
                if (stripped.startsWith(QLatin1String("- ")) ||
                    stripped.startsWith(QLatin1String("* ")) ||
                    stripped.startsWith(QLatin1String("+ "))) {
                    stripped = stripped.mid(2).trimmed();
                } else {
                    // 有序列表：1. / 12. 之类
                    int digits = 0;
                    while (digits < stripped.size() && stripped.at(digits).isDigit()) ++digits;
                    if (digits > 0 && digits + 1 < stripped.size() &&
                        stripped.at(digits) == QLatin1Char('.') &&
                        stripped.at(digits + 1).isSpace()) {
                        stripped = stripped.mid(digits + 1).trimmed();
                    }
                }
                level = 10 + indent / 2;
                text = stripped;
            }

            if (text.isEmpty()) continue;
            Entry entry;
            entry.level = level;
            entry.text = text;
            entries.append(entry);
        }
        return entries;
    }

    // 按 documentToJson() 的格式造一个节点对象。
    // 只要给 blocks（text 类型）、expanded、children 就够反序列化用了；
    // 不写 "version"，因为那是文件格式版本，由宿主自己管。
    static QJsonObject makeNode(const QString& text, const QVector<QJsonObject>& children) {
        QJsonObject block;
        block[QStringLiteral("type")] = QStringLiteral("text");
        block[QStringLiteral("text")] = text;

        QJsonArray blocks;
        blocks.append(block);

        QJsonArray childArray;
        for (const QJsonObject& child : children) childArray.append(child);

        QJsonObject node;
        node[QStringLiteral("blocks")] = blocks;
        node[QStringLiteral("text")] = text;
        node[QStringLiteral("expanded")] = true;
        node[QStringLiteral("children")] = childArray;
        return node;
    }

    // 把扁平条目组装成树。用一个栈维护「当前路径」，
    // 遇到层级不深于栈顶的条目就一路弹出，直到找到合适的父节点。
    static QJsonObject buildDocument(const QVector<Entry>& entries, QString* rootText) {
        struct Frame {
            int level;
            QString text;
            QVector<QJsonObject> children;
        };

        QVector<Frame> stack;
        int startIndex = 0;

        if (!entries.isEmpty() && entries.first().level == 1) {
            Frame root;
            root.level = 1;
            root.text = entries.first().text;
            stack.append(root);
            startIndex = 1;
        } else {
            Frame root;
            root.level = 0;
            root.text = QStringLiteral("Markdown 导图");
            stack.append(root);
        }

        for (int i = startIndex; i < entries.size(); ++i) {
            const Entry& entry = entries.at(i);
            while (stack.size() > 1 && stack.last().level >= entry.level) {
                Frame done = stack.takeLast();
                stack.last().children.append(makeNode(done.text, done.children));
            }
            Frame frame;
            frame.level = entry.level;
            frame.text = entry.text;
            stack.append(frame);
        }

        // 收尾：把栈里剩下的逐层折上去。
        while (stack.size() > 1) {
            Frame done = stack.takeLast();
            stack.last().children.append(makeNode(done.text, done.children));
        }

        const Frame& root = stack.first();
        if (rootText) *rootText = root.text;
        return makeNode(root.text, root.children);
    }

    void showDialog() {
        if (!m_host || !m_writer) return;

        // 插件的 QObject 没有父控件，对话框挂到当前活动窗口上，
        // 这样它会居中在 leap 主窗口而不是屏幕中央。
        QDialog dialog(QApplication::activeWindow());
        dialog.setWindowTitle(QStringLiteral("Markdown 转导图"));
        dialog.setModal(true);
        dialog.resize(520, 460);

        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(20, 18, 20, 16);
        layout->setSpacing(12);

        // 标题与说明都用宿主给的颜色令牌，自动跟随浅色/深色主题。
        auto* title = new QLabel(QStringLiteral("粘贴 Markdown"), &dialog);
        QFont titleFont = title->font();
        titleFont.setPixelSize(15);
        titleFont.setBold(true);
        title->setFont(titleFont);
        title->setStyleSheet(QStringLiteral("color: %1;").arg(m_host->textColor().name()));
        layout->addWidget(title);

        auto* hint = new QLabel(
            QStringLiteral("支持 # 标题（1~6 级）与 - * + 或 1. 列表，缩进 2 空格算一层。\n"
                           "转换会替换整份文档，可以用一次撤销整体回退。"),
            &dialog);
        hint->setWordWrap(true);
        hint->setStyleSheet(
            QStringLiteral("color: %1;").arg(m_host->secondaryTextColor().name()));
        layout->addWidget(hint);

        auto* editor = new QPlainTextEdit(&dialog);
        editor->setPlaceholderText(QStringLiteral(
            "# 项目计划\n"
            "## 第一阶段\n"
            "- 调研\n"
            "- 原型\n"
            "## 第二阶段\n"
            "- 开发\n"
            "  - 前端\n"
            "  - 后端"));
        layout->addWidget(editor, 1);

        auto* status = new QLabel(&dialog);
        status->setWordWrap(true);
        status->setStyleSheet(
            QStringLiteral("color: %1;").arg(m_host->secondaryTextColor().name()));
        layout->addWidget(status);

        auto* buttons = new QHBoxLayout();
        buttons->addStretch(1);
        auto* cancelButton = new QPushButton(QStringLiteral("取消"), &dialog);
        connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
        buttons->addWidget(cancelButton);
        auto* convertButton = new QPushButton(QStringLiteral("转换"), &dialog);
        convertButton->setObjectName(QStringLiteral("primaryButton"));
        convertButton->setDefault(true);
        buttons->addWidget(convertButton);
        layout->addLayout(buttons);

        connect(convertButton, &QPushButton::clicked, &dialog, [this, editor, status, &dialog]() {
            const QString source = editor->toPlainText();
            const QVector<Entry> entries = parseMarkdown(source);
            if (entries.isEmpty()) {
                status->setText(QStringLiteral("没有解析到任何标题或列表项。"));
                return;
            }

            QString rootText;
            const QJsonObject doc = buildDocument(entries, &rootText);
            QString error;
            if (!m_writer->replaceDocument(doc)) {
                status->setText(QStringLiteral("替换文档失败：%1")
                                    .arg(error.isEmpty() ? QStringLiteral("格式无法识别")
                                                         : error));
                return;
            }
            dialog.accept();
        });

        editor->setFocus();
        dialog.exec();
    }

    LeapHost* m_host = nullptr;
    LeapDocumentWriter* m_writer = nullptr;
};

#include "MarkdownImportPlugin.moc"
