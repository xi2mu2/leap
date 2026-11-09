#include "StartupDialog.h"
#include "MindMapIO.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

StartupDialog::StartupDialog(const QStringList& recentFiles, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("leap"));
    setModal(true);
    setMinimumSize(680, 440);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(16);

    auto* title = new QLabel(QStringLiteral("开始使用 leap"), this);
    title->setStyleSheet(QStringLiteral(
        "QLabel { color: #111827; font-size: 24px; font-weight: 700; }"
    ));
    layout->addWidget(title);

    auto* actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(10);

    auto* newButton = new QPushButton(QStringLiteral("新建"), this);
    newButton->setMinimumHeight(42);
    newButton->setCursor(Qt::PointingHandCursor);
    newButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #2563eb; border: 1px solid #2563eb; "
        "border-radius: 6px; color: white; font-size: 15px; font-weight: 600; padding: 8px 18px; }"
        "QPushButton:hover { background-color: #1d4ed8; border-color: #1d4ed8; }"
    ));
    connect(newButton, &QPushButton::clicked, this, [this]() {
        m_choice = Choice::NewFile;
        accept();
    });
    actionsLayout->addWidget(newButton);

    auto* openButton = new QPushButton(QStringLiteral("打开其他文件..."), this);
    openButton->setMinimumHeight(42);
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #ffffff; border: 1px solid #cbd5e1; "
        "border-radius: 6px; color: #1f2937; font-size: 15px; padding: 8px 18px; }"
        "QPushButton:hover { background-color: #f8fafc; border-color: #94a3b8; }"
    ));
    connect(openButton, &QPushButton::clicked, this, &StartupDialog::openOtherFile);
    actionsLayout->addWidget(openButton);
    actionsLayout->addStretch();
    layout->addLayout(actionsLayout);

    auto* recentLabel = new QLabel(QStringLiteral("最近打开"), this);
    recentLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #374151; font-size: 15px; font-weight: 600; margin-top: 8px; }"
    ));
    layout->addWidget(recentLabel);

    m_recentList = new QListWidget(this);
    m_recentList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recentList->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: #ffffff; border: 1px solid #e5e7eb; "
        "border-radius: 6px; outline: 0; }"
        "QListWidget::item { border-bottom: 1px solid #f1f5f9; padding: 10px 12px; }"
        "QListWidget::item:selected { background-color: #eff6ff; color: #1d4ed8; }"
        "QListWidget::item:hover { background-color: #f8fafc; }"
    ));
    layout->addWidget(m_recentList, 1);

    if (recentFiles.isEmpty()) {
        auto* item = new QListWidgetItem(QStringLiteral("暂无最近文件"));
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(QColor("#64748b"));
        m_recentList->addItem(item);
    } else {
        for (const QString& filePath : recentFiles) {
            QFileInfo info(filePath);
            auto* item = new QListWidgetItem();
            item->setText(QStringLiteral("%1\n%2").arg(info.fileName(), QDir::toNativeSeparators(info.absoluteFilePath())));
            item->setData(Qt::UserRole, info.absoluteFilePath());
            item->setToolTip(info.absoluteFilePath());
            m_recentList->addItem(item);
        }
        m_recentList->setCurrentRow(0);
    }

    connect(m_recentList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        acceptRecentFile();
    });

    auto* footerLayout = new QHBoxLayout();
    footerLayout->addStretch();

    auto* openRecentButton = new QPushButton(QStringLiteral("打开选中文件"), this);
    openRecentButton->setMinimumHeight(36);
    openRecentButton->setEnabled(!recentFiles.isEmpty());
    openRecentButton->setCursor(Qt::PointingHandCursor);
    connect(openRecentButton, &QPushButton::clicked, this, &StartupDialog::acceptRecentFile);
    footerLayout->addWidget(openRecentButton);

    auto* cancelButton = new QPushButton(QStringLiteral("退出"), this);
    cancelButton->setMinimumHeight(36);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    footerLayout->addWidget(cancelButton);
    layout->addLayout(footerLayout);
}

void StartupDialog::acceptRecentFile() {
    auto* item = m_recentList ? m_recentList->currentItem() : nullptr;
    if (!item) return;

    const QString filePath = item->data(Qt::UserRole).toString();
    if (filePath.isEmpty()) return;

    m_choice = Choice::OpenRecent;
    m_selectedFilePath = filePath;
    accept();
}

void StartupDialog::openOtherFile() {
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("打开思维导图"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        MindMapIO::fileFilter()
    );
    if (filePath.isEmpty()) return;

    m_choice = Choice::OpenOther;
    m_selectedFilePath = filePath;
    accept();
}
