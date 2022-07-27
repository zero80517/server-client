#ifndef CLIENT_H
#define CLIENT_H

#include <QMainWindow>

#include <QTcpSocket>

QT_BEGIN_NAMESPACE
namespace Ui { class Client; }
QT_END_NAMESPACE

/**
 * @brief Client with GUI
 */
class Client : public QMainWindow
{
    Q_OBJECT

public:
    Client(const QString &, int, QWidget *parent = nullptr);
    ~Client();

signals:
    void newDebugMessage(QString);
    void newInfoMessage(QString);
    void newWarningMessage(QString);
    void newCriticalMessage(QString);

private slots:
    void on_saveButton_clicked();
    void on_connectButton_clicked();
    void on_loadButton_clicked();

    void readSocket();
    void discardSocket();
    void displayError(QAbstractSocket::SocketError socketError);

    void requestTable();
    void updateTable(QString);
    void insertRowInTable(QString dateTime, QString fileName, QString link);
    void on_tableWidget_cellDoubleClicked(int row, int column);
    QString getFileNamesOfSelectedTableRows();

    void loadFiles(QString header, QByteArray &buffer);

    void displayDebugMessage(const QString& str);
    void displayInfoMessage(const QString& str);
    void displayWarningMessage(const QString& str);
    void displayCriticalMessage(const QString& str);

private:
    Ui::Client *ui;

    QTcpSocket *socket = nullptr;   ///< socket is needed to communicate with server
    QString saveDir;                ///< The last save dir. The default value is Documents directory
    QString loadDir;                ///< The last load dir. The default value is Documents directory
    QString host;                   ///< make a connection to host (any protocol) on the given port
    int port;                       ///< number that identifies connection
};
#endif // CLIENT_H
