#include "MindMapFormulaDialog.h"
#include "FormulaCheatSheet.h"
#include <QGuiApplication>
#include <QScreen>
#include "jkqtmathtext/jkqtmathtext.h"
#include <QPainter>
#include <QPixmap>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QScrollBar>
#include <QMouseEvent>

// ============================================================
// 公式对话框：支持 LaTeX 输入 + 查阅对照表
// 对照表图片从 formula_cheatsheet/ 目录预加载，不实时渲染
// ============================================================

MindMapFormulaDialog::MindMapFormulaDialog(QWidget* parent)
    : QDialog(parent), m_inputEdit(nullptr), m_previewLabel(nullptr),
    m_confirmBtn(nullptr), m_cancelBtn(nullptr), m_cheatBtn(nullptr),
    m_previewTimer(nullptr) {

    setWindowTitle("插入公式");
    setMinimumWidth(500);

    // ========== 主布局：水平分割器 ==========
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // ========== 左侧：编辑区 ==========
    QWidget* leftPanel = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    // LaTeX 输入
    QHBoxLayout* inputLayout = new QHBoxLayout();
    QLabel* inputLabel = new QLabel("LaTeX:", leftPanel);
    m_inputEdit = new QPlainTextEdit(leftPanel);
    m_inputEdit->setPlaceholderText("例如: \\frac{-b \\pm \\sqrt{b^2-4ac}}{2a}");
    m_inputEdit->setMaximumBlockCount(0);
    m_inputEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_inputEdit->setMinimumHeight(80);
    inputLayout->addWidget(inputLabel);
    inputLayout->addWidget(m_inputEdit);
    leftLayout->addLayout(inputLayout);

    // 预览
    QLabel* previewTitle = new QLabel("预览:", leftPanel);
    leftLayout->addWidget(previewTitle);

    m_previewLabel = new QLabel(leftPanel);
    m_previewLabel->setMinimumHeight(80);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setFrameStyle(QFrame::Box | QFrame::Plain);
    m_previewLabel->setStyleSheet("background-color: white;");
    leftLayout->addWidget(m_previewLabel);

    // 按钮栏
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_cheatBtn = new QPushButton("查阅 ▶", leftPanel);
    m_confirmBtn = new QPushButton("确定", leftPanel);
    m_cancelBtn = new QPushButton("取消", leftPanel);
    btnLayout->addWidget(m_cheatBtn);
    btnLayout->addWidget(m_confirmBtn);
    btnLayout->addWidget(m_cancelBtn);
    leftLayout->addLayout(btnLayout);

    mainLayout->addWidget(leftPanel, 1);

    // ========== 右侧：查阅面板（初始隐藏）==========
    m_cheatPanel = new QWidget(this);
    m_cheatPanel->setVisible(false);
    m_cheatPanel->setFixedWidth(340);
    QVBoxLayout* cheatLayout = new QVBoxLayout(m_cheatPanel);
    cheatLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* cheatTitle = new QLabel("📖 公式对照表", m_cheatPanel);
    QFont titleFont = cheatTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSize(11);
    cheatTitle->setFont(titleFont);
    cheatLayout->addWidget(cheatTitle);

    // 分类列表
    m_categoryList = new QListWidget(m_cheatPanel);
    m_categoryList->setMaximumHeight(120);
    cheatLayout->addWidget(m_categoryList);

    // 公式堆叠页
    m_formulaStack = new QStackedWidget(m_cheatPanel);
    cheatLayout->addWidget(m_formulaStack, 1);

    mainLayout->addWidget(m_cheatPanel);

    // ========== 信号连接 ==========
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    connect(m_previewTimer, &QTimer::timeout, this, &MindMapFormulaDialog::updatePreview);

    connect(m_inputEdit, &QPlainTextEdit::textChanged, this, &MindMapFormulaDialog::onTextChanged);
    connect(m_confirmBtn, &QPushButton::clicked, this, &MindMapFormulaDialog::onConfirm);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MindMapFormulaDialog::onCancel);
    connect(m_cheatBtn, &QPushButton::clicked, this, &MindMapFormulaDialog::toggleCheatSheet);
    connect(m_categoryList, &QListWidget::currentRowChanged, this, &MindMapFormulaDialog::onCategoryChanged);

    m_confirmBtn->setEnabled(false);

    // 构建对照表 UI（但不显示）
    buildCheatSheet();
}

void MindMapFormulaDialog::setFormula(const QString& latexCode) {
    m_inputEdit->setPlainText(latexCode);
    m_currentLatex = latexCode;
    updatePreview();
}

QString MindMapFormulaDialog::formula() const {
    return m_currentLatex;
}

void MindMapFormulaDialog::onTextChanged() {
    m_currentLatex = m_inputEdit->toPlainText();
    m_confirmBtn->setEnabled(!m_currentLatex.trimmed().isEmpty());
    m_previewTimer->start(300);
}

bool MindMapFormulaDialog::isLatexSafe(const QString& latex) {
    QString t = latex.trimmed();
    if (t.isEmpty()) return false;
    if (t.endsWith("\\")) return false;

    int beginCount = 0, endCount = 0;
    int from = 0;
    while ((from = t.indexOf("\\begin", from)) != -1) {
        int after = from + 7;
        if (after >= t.length() || t[after] == '{' || t[after] == ' ' || t[after] == '\n' || t[after] == '\t') {
            ++beginCount;
        }
        from = after;
    }
    from = 0;
    while ((from = t.indexOf("\\end", from)) != -1) {
        int after = from + 5;
        if (after >= t.length() || t[after] == '{' || t[after] == ' ' || t[after] == '\n' || t[after] == '\t') {
            ++endCount;
        }
        from = after;
    }
    if (beginCount > endCount) return false;

    int braces = 0;
    for (const QChar& c : t) {
        if (c == '{') ++braces;
        else if (c == '}') --braces;
        if (braces < 0) return false;
    }
    if (braces != 0) return false;

    return true;
}

void MindMapFormulaDialog::updatePreview() {
    if (m_currentLatex.trimmed().isEmpty()) {
        m_previewLabel->setPixmap(QPixmap());
        return;
    }
    if (!isLatexSafe(m_currentLatex)) {
        return;
    }
    QImage image = renderLatexToImage(m_currentLatex, 18);
    if (!image.isNull()) {
        m_previewLabel->setPixmap(QPixmap::fromImage(image));
    } else {
        m_previewLabel->setText("渲染失败，请检查 LaTeX 语法");
    }
}

void MindMapFormulaDialog::onConfirm() {
    m_currentLatex = m_inputEdit->toPlainText();
    if (!m_currentLatex.trimmed().isEmpty()) {
        accept();
    }
}

void MindMapFormulaDialog::onCancel() {
    reject();
}

// ========== 查阅面板 ==========

void MindMapFormulaDialog::toggleCheatSheet() {
    m_cheatVisible = !m_cheatVisible;
    m_cheatPanel->setVisible(m_cheatVisible);
    m_cheatBtn->setText(m_cheatVisible ? "查阅 ◀" : "查阅 ▶");

    if (m_cheatVisible) {
        resize(width() + 360, height());
    } else {
        resize(width() - 360, height());
    }
}

void MindMapFormulaDialog::buildCheatSheet() {
    auto categories = FormulaCheatSheet::getCategories();

    for (const auto& cat : categories) {
        // 添加分类项
        QListWidgetItem* catItem = new QListWidgetItem(cat.title);
        catItem->setData(Qt::UserRole, cat.id);
        m_categoryList->addItem(catItem);

        // 为每个分类创建滚动列表
        QScrollArea* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFrameStyle(QFrame::NoFrame);

        QWidget* container = new QWidget();
        QVBoxLayout* layout = new QVBoxLayout(container);
        layout->setSpacing(6);
        layout->setContentsMargins(4, 4, 4, 4);

        for (const auto& entry : cat.entries) {
            // 每个公式一个可点击的卡片
            QFrame* card = new QFrame();
            card->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
            card->setCursor(Qt::PointingHandCursor);
            card->setProperty("latex", entry.latex);
            card->installEventFilter(this);

            // 样式表：悬停效果
            card->setStyleSheet(
                "QFrame {"
                "  background-color: #f8f9fa;"
                "  border: 1px solid #dee2e6;"
                "  border-radius: 6px;"
                "}"
                "QFrame:hover {"
                "  background-color: #e3f2fd;"
                "  border: 1px solid #2196f3;"
                "}"
                );

            QVBoxLayout* cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(8, 6, 8, 6);
            cardLayout->setSpacing(4);

            // 公式名称
            QLabel* nameLabel = new QLabel(entry.name);
            QFont nameFont = nameLabel->font();
            nameFont.setBold(true);
            nameFont.setPointSize(9);
            nameLabel->setFont(nameFont);
            cardLayout->addWidget(nameLabel);

            // 预渲染图片（从文件加载）
            QLabel* imgLabel = new QLabel();
            imgLabel->setAlignment(Qt::AlignCenter);
            imgLabel->setMinimumHeight(36);

            QImage img = FormulaCheatSheet::loadImage(entry);
            if (!img.isNull()) {
                QPixmap pixmap = QPixmap::fromImage(img);
                if (pixmap.width() > 300) {
                    pixmap = pixmap.scaledToWidth(300, Qt::SmoothTransformation);
                }
                imgLabel->setPixmap(pixmap);
            } else {
                imgLabel->setText("[" + entry.latex.left(50) + "]");
                imgLabel->setStyleSheet("color: #999; font-size: 10px; font-family: monospace;");
            }
            cardLayout->addWidget(imgLabel);

            // 描述（小字）
            if (!entry.desc.isEmpty() && entry.desc != entry.name) {
                QLabel* descLabel = new QLabel(entry.desc);
                descLabel->setStyleSheet("color: #666; font-size: 9px;");
                cardLayout->addWidget(descLabel);
            }

            layout->addWidget(card);
        }

        layout->addStretch();
        scroll->setWidget(container);
        m_formulaStack->addWidget(scroll);
    }
}

bool MindMapFormulaDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonRelease) {
        QFrame* card = qobject_cast<QFrame*>(watched);
        if (card) {
            QString latex = card->property("latex").toString();
            if (!latex.isEmpty()) {
                // 将 LaTeX 插入到输入框当前光标位置
                QTextCursor cursor = m_inputEdit->textCursor();
                cursor.insertText(latex);
                m_inputEdit->setFocus();
                // 触发预览更新
                onTextChanged();
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void MindMapFormulaDialog::onCategoryChanged(int index) {
    if (index >= 0 && index < m_formulaStack->count()) {
        m_formulaStack->setCurrentIndex(index);
    }
}

void MindMapFormulaDialog::onFormulaItemClicked() {
    // 由 eventFilter 处理点击，此槽函数保留用于未来扩展
}

// ========== 渲染函数 ==========

QImage MindMapFormulaDialog::renderLatexToImage(const QString& latexCode, int fontSize) {
    try {
        qreal dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
        if (dpr < 1.0) dpr = 1.0;

        const int SUPERSAMPLE = 4;
        qreal renderScale = dpr * SUPERSAMPLE;

        JKQTMathText mathText;
        mathText.setFontSize(fontSize * renderScale);
        mathText.useXITS(false, true);
        // 【修改】嵌套公式字体缩放策略：仅缩小一次，后续嵌套不再继续缩小
        // 将所有嵌套相关的缩放因子设为 1.0，确保从第一次嵌套开始，
        // 后续所有层级的字体大小与第一次嵌套后保持一致
        mathText.setFracFactor(0.85);            // 普通分式：分子/分母缩小到 85%
        mathText.setFracNestedFactor(1.0);       // 嵌套分式：不再额外缩小（关键修复）
        mathText.setSubsuperSizeFactor(0.65);   // 首层上下标比例；嵌套层级由排版环境限制为 0.5
        mathText.setSqrtSmallFontFactor(1.0);    // 根号度数：不再缩小（关键修复）
        mathText.setBraceFactor(1.0);            // 括号：高度不额外缩放（关键修复）
        mathText.setUnderbraceFactor(1.0);       // 上下括号文字：不再缩小（关键修复）
        mathText.setUndersetFactor(1.0);         // 上下设置文字：不再缩小（关键修复）

        QString xitsRoman = JKQTMathTextFontSpecifier::getXITSFamilies().fontName();
        if (!xitsRoman.isEmpty()) {
            mathText.addReplacementFont("Script", xitsRoman);
            mathText.addReplacementFont("script", xitsRoman);
        }
        QString xitsMath = JKQTMathTextFontSpecifier::getXITSFamilies().mathFontName();
        if (!xitsMath.isEmpty()) {
            mathText.addReplacementFont("Script", xitsMath);
            mathText.addReplacementFont("script", xitsMath);
        }

        mathText.setFontScript(xitsRoman, JKQTMathTextFontEncoding::MTFEUnicode);
        mathText.setFontCaligraphic(xitsRoman, JKQTMathTextFontEncoding::MTFEUnicode);
        mathText.setFontFraktur(xitsRoman, JKQTMathTextFontEncoding::MTFEUnicode);
        mathText.setFontBlackboard(xitsRoman, JKQTMathTextFontEncoding::MTFEUnicode);
        mathText.setFontSans(xitsRoman, JKQTMathTextFontEncoding::MTFEUnicode);
        mathText.setFontTypewriter(xitsRoman, JKQTMathTextFontEncoding::MTFEUnicode);

        if (!mathText.parse(latexCode)) {
            qDebug() << "[FormulaDialog] Failed to parse LaTeX:" << latexCode;
            return QImage();
        }

        QImage tmpImg(1, 1, QImage::Format_ARGB32);
        QPainter tmpPainter(&tmpImg);
        QSizeF size = mathText.getSize(tmpPainter);
        tmpPainter.end();

        if (size.width() <= 0 || size.height() <= 0) {
            return QImage();
        }

        int margin = qCeil(6 * renderScale);
        int imgWidth = qCeil(size.width()) + margin * 2;
        int imgHeight = qCeil(size.height()) + margin * 2;

        QImage hiResImage(imgWidth, imgHeight, QImage::Format_ARGB32);
        hiResImage.fill(Qt::transparent);

        {
            QPainter painter(&hiResImage);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);

            QRectF drawRect(margin, margin, size.width(), size.height());
            mathText.draw(painter, Qt::AlignLeft | Qt::AlignTop, drawRect, false);
        }

        QRect contentRect = findContentRect(hiResImage);
        if (!contentRect.isValid() || contentRect.width() <= 0 || contentRect.height() <= 0) {
            return QImage();
        }

        int cropMargin = qCeil(4 * renderScale);
        QRect cropRect(
            qMax(0, contentRect.left() - cropMargin),
            qMax(0, contentRect.top() - cropMargin),
            qMin(hiResImage.width() - contentRect.left() + cropMargin, contentRect.width() + cropMargin * 2),
            qMin(hiResImage.height() - contentRect.top() + cropMargin, contentRect.height() + cropMargin * 2)
            );

        QImage cropped = hiResImage.copy(cropRect);

        int finalWidth = qCeil(cropped.width() / SUPERSAMPLE);
        int finalHeight = qCeil(cropped.height() / SUPERSAMPLE);

        QImage finalImg = cropped.scaled(finalWidth, finalHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        finalImg.setDevicePixelRatio(dpr);

        if (finalImg.format() != QImage::Format_ARGB32) {
            finalImg = finalImg.convertToFormat(QImage::Format_ARGB32);
        }

        return finalImg;
    } catch (const std::exception& e) {
        qDebug() << "[FormulaDialog] std::exception during render:" << e.what();
        return QImage();
    } catch (...) {
        qDebug() << "[FormulaDialog] Unknown exception during render";
        return QImage();
    }
}

QRect MindMapFormulaDialog::findContentRect(const QImage& image) {
    int left = image.width(), right = 0, top = image.height(), bottom = 0;
    bool found = false;

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QRgb pixel = image.pixel(x, y);
            if (qAlpha(pixel) > 10) {
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = qMax(bottom, y);
                found = true;
            }
        }
    }

    if (!found) return QRect();
    return QRect(left, top, right - left + 1, bottom - top + 1);
}
