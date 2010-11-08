#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class Algorithm;
class PAController;
class ProbeManager;
class QTimer;

namespace Ui {
    class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void displayError(const QString &msg);
    void displayWarning(const QString &msg);
    void monitor(bool enabled);
    void onOutputsChanged(const QHash<QString, QSet<QString> > &outputs);
    void onPAConnected(const QString &server, bool local);
    void onProbeReady();

private:
    Ui::MainWindow *ui;
    Algorithm *algo;
    PAController *controller;
    ProbeManager *probe;
    QTimer *timer;
};

#endif // MAINWINDOW_H
