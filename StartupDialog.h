#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QListWidget;

class StartupDialog : public QDialog {
    Q_OBJECT
public:
    enum class Choice {
        NewFile,
        OpenRecent,
        OpenOther
    };

    explicit StartupDialog(const QStringList& recentFiles, QWidget* parent = nullptr);

    Choice choice() const { return m_choice; }
    QString selectedFilePath() const { return m_selectedFilePath; }

private:
    void acceptRecentFile();
    void openOtherFile();

    QListWidget* m_recentList = nullptr;
    Choice m_choice = Choice::NewFile;
    QString m_selectedFilePath;
};
