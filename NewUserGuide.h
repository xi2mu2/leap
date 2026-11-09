#pragma once

#include <QObject>
#include <QPointer>
#include <QLabel>
#include <QResizeEvent>

class QWidget;

class NewUserGuideOverlay final : public QLabel {
    Q_OBJECT
public:
    explicit NewUserGuideOverlay(QWidget* parent = nullptr);
    void setDarkMode(bool dark);
    void reposition();
protected:
    void resizeEvent(QResizeEvent* event) override;
};

class NewUserGuideController final : public QObject {
    Q_OBJECT
public:
    explicit NewUserGuideController(QWidget* host, QObject* parent = nullptr);
    bool completed() const;
    void start();
    void setDarkMode(bool dark);
    int step() const { return m_step; }

public slots:
    void nodeSelected();
    void childCreated();
    void siblingCreated();
    void editingStarted();
    void editingExitedByKey();
    void viewportDragged();

private:
    void advance(int expectedStep);
    void showStep();
    QWidget* m_host = nullptr;
    QPointer<NewUserGuideOverlay> m_overlay;
    int m_step = -1;
    bool m_manualRun = false;
};
