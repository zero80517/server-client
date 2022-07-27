#ifndef SERVER_H
#define SERVER_H

#include <QObject>

#include <QTcpServer>
#include <QTcpSocket>

/**
 * @brief Simple server without GUI
 */
class Server : public QObject
{
    Q_OBJECT
public:
    explicit Server(int port, QObject *parent = nullptr);
    ~Server();

signals:
    void newDebugMessage(QString);
    void newInfoMessage(QString);
    void newWarningMessage(QString);
    void newCriticalMessage(QString);

private slots:
    void newConnection();
    void appendToSocketList(QTcpSocket* socket);

    void readSocket();
    void discardSocket();
    void displayError(QAbstractSocket::SocketError socketError);

    void saveFileOnServer(QTcpSocket *socket, QString header, QByteArray &buffer);
    void appendSavedFileToTable(QString dateTime, QString fileName);

    QByteArray getTable();
    void sendTableToClient(QTcpSocket *socket);
    void sendTableToClients();

    void sendFilesToClient(QTcpSocket *socket, QByteArray &buffer);
    QByteArray getFileDataForLoading(QString fileName);

    void displayDebugMessage(const QString& str);
    void displayInfoMessage(const QString& str);
    void displayWarningMessage(const QString& str);
    void displayCriticalMessage(const QString& str);

private:
    QTcpServer* server;                 ///<
    QSet<QTcpSocket*> connection_set;   ///< set of all clients
    QString dirOfSavedFiles;            ///< full path to dir, where saved files are stored
    QString pathToTableFile;            ///< full path to file, that consist table of saved files

};

#endif // SERVER_H
