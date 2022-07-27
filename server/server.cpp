#include "server.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QDateTime>
#include <QTextCodec>

#include "logging_categories.h"

/**
 * @brief Run server and listen specific port
 * @param port number that identifies port
 * @param parent
 */
Server::Server(int port, QObject *parent) : QObject(parent) {
    server = new QTcpServer(this);

    if(server->listen(QHostAddress::Any, port))
    {
       connect(this, &Server::newDebugMessage, this, &Server::displayDebugMessage);
       connect(this, &Server::newInfoMessage, this, &Server::displayInfoMessage);
       connect(this, &Server::newWarningMessage, this, &Server::displayWarningMessage);
       connect(this, &Server::newCriticalMessage, this, &Server::displayCriticalMessage);
       connect(server, &QTcpServer::newConnection, this, &Server::newConnection);

       // init directory for saved files
       dirOfSavedFiles = QCoreApplication::applicationDirPath()+"/SavedFilesOnServer";
       QDir dir(dirOfSavedFiles);
       if (!dir.exists()) {
           dir.mkpath(dirOfSavedFiles);
           emit newInfoMessage(QString("Directory for saved files was created under path %1").arg(dirOfSavedFiles));
       } else
           emit newInfoMessage(QString("Directory for saved files already exists under path %1").arg(dirOfSavedFiles));

       // init file for table
       pathToTableFile = QCoreApplication::applicationDirPath()+"/TableFile.txt";
       QFile file(pathToTableFile);
       if (!file.exists()) {
           file.open(QIODevice::WriteOnly);
           emit newInfoMessage(QString("File for table of saved files was created under path %1").arg(pathToTableFile));
       } else
           emit newInfoMessage(QString("File for table of saved files already exists under path %1").arg(pathToTableFile));

       emit newInfoMessage("Server is listening...");
    } else {
        emit newCriticalMessage(QString("Unable to start the server: %1").arg(server->errorString()));
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Close all sockets and server and delete all
 */
Server::~Server() {
    for (QTcpSocket* socket : connection_set) {
        socket->close();
        socket->deleteLater();
    }

    server->close();
    server->deleteLater();
}

/**
 * @brief If a new connection is received, then add it to the connection set and connect to the signals of the received socket
 */
void Server::newConnection()
{
    while (server->hasPendingConnections())
        appendToSocketList(server->nextPendingConnection());
}

/**
 * @brief Add received socket to the connection set and connect to the signals of the received socket
 * @param socket received socket
 */
void Server::appendToSocketList(QTcpSocket *socket)
{
    connection_set.insert(socket);
    connect(socket, &QTcpSocket::readyRead, this, &Server::readSocket);
    connect(socket, &QTcpSocket::disconnected, this, &Server::discardSocket);
    // see https://stackoverflow.com/questions/35655512/compile-error-when-connecting-qtcpsocketerror-using-the-new-qt5-signal-slot
    connect(socket, static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error), this, &Server::displayError);
    emit newInfoMessage(QString("New socket were added at socket descriptor %1").arg(socket->socketDescriptor()));
}

/**
 * @brief Read data from socket that ready to read
 */
void Server::readSocket()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());

    QByteArray buffer;

    QDataStream socketStream(socket);
    socketStream.setVersion(QDataStream::Qt_5_9);

    socketStream.startTransaction();
    socketStream >> buffer;

    if(!socketStream.commitTransaction())
    {
        QString message = QString("%1 :: Waiting for more data to come..").arg(socket->socketDescriptor());
        emit newInfoMessage(message);
        return;
    }

    QString header = buffer.mid(0,128);
    QString flag = header.split(",")[0].split(":")[1];

    buffer = buffer.mid(128);

    if(flag=="save") {
        saveFileOnServer(socket, header, buffer);
    } else if (flag == "upd") {
        sendTableToClient(socket);
    } else if (flag == "load") {
        sendFilesToClient(socket, buffer);
    } else
        emit newWarningMessage(QString("Got wrong flag: %1!").arg(flag));
}

/**
 * @brief Remove socket that were disconnected
 */
void Server::discardSocket()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    QSet<QTcpSocket*>::iterator it = connection_set.find(socket);
    if (it != connection_set.end()){
        emit newInfoMessage(QString("A client has just left the room").arg(socket->socketDescriptor()));
        connection_set.remove(*it);
    }

    socket->deleteLater();
}

/**
 * @brief Display the received error
 * @param socketError
 */
void Server::displayError(QAbstractSocket::SocketError socketError)
{
    switch (socketError) {
        case QAbstractSocket::RemoteHostClosedError:
        break;
        case QAbstractSocket::HostNotFoundError:
            emit newWarningMessage("The host was not found. Please check the host name and port settings.");
        break;
        case QAbstractSocket::ConnectionRefusedError:
            emit newWarningMessage("The connection was refused by the peer. Make sure QTCPServer is running, and check that the host name and port settings are correct.");
        break;
        default:
            QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
            emit newWarningMessage(QString("The following error occurred: %1.").arg(socket->errorString()));
        break;
    }
}

/**
 * @brief Save received file in dirOfSavedFiles
 * @param socket receiver
 * @param header string with format "flag:save,filename:%1,filesize:%2;"
 * @param buffer file data
 */
void Server::saveFileOnServer(QTcpSocket *socket, QString header, QByteArray &buffer)
{
    QString fileName = header.split(",")[1].split(":")[1];
    QString size = header.split(",")[2].split(":")[1].split(";")[0];
    emit newInfoMessage(QString("You are receiving a file from sd:%1 of size: %2 bytes, called %3..").arg(socket->socketDescriptor()).arg(size).arg(fileName));

    QString filePath = dirOfSavedFiles+"/"+fileName;
    emit newDebugMessage(QString("Trying to safe received file under path %1..").arg(filePath));

    QFile file(filePath);
    if(file.open(QIODevice::WriteOnly)){
        file.write(buffer);
        QString dateTime = QDateTime::currentDateTime().toString("dd.MM.yyyy/hh:mm:ss.zzz");
        emit newInfoMessage(QString("File from sd:%1 successfully stored on disk under the path %2").arg(socket->socketDescriptor()).arg(QString(filePath)));
        appendSavedFileToTable(dateTime, fileName);
    } else
        emit newWarningMessage("An error occurred while trying to save the received file!");
}

/**
 * @brief Append last saved file at the last row in table file
 *
 * @details Format of rows is: "dateTime,fileName,link", where link="file:///dirOfSavedFiles/fileName"
 *
 * @param dateTime
 * @param fileName
 */
void Server::appendSavedFileToTable(QString dateTime, QString fileName)
{
    QFile file(pathToTableFile);

    if (file.open(QFile::Append))
    {
        QTextStream out(&file);
        QTextCodec *codec = QTextCodec::codecForName("UTF-8");  // save in UTF-8 encoding
        out.setCodec(codec);                                    // for compatability with linux
        QString link = QString("file:///%1/%2").arg(dirOfSavedFiles).arg(fileName);
        out << QString("%1,%2,%3\n").arg(dateTime).arg(fileName).arg(link).toUtf8();
        emit newInfoMessage(QString("File %1 were added into file with table of saved files").arg(fileName));
    }
    else
        emit newWarningMessage(QString("Can't open file with table of saved files under path %1 to add new saved file with name %2").arg(pathToTableFile).arg(fileName));
    file.close();

    sendTableToClients();   // update table on all clients
}

/**
 * @brief Return file data of table
 * @return
 */
QByteArray Server::getTable() {
    QString filePath = pathToTableFile;

    QFile file(filePath);

    QByteArray byteArray;
    if (file.open(QIODevice::ReadOnly)) {
        byteArray = file.readAll();
    } else {
        emit newCriticalMessage(QString("Can't open file %1 to read!").arg(filePath));
        exit(EXIT_FAILURE);
    }

    return byteArray;
}

/**
 * @brief Send data of table file.
 *
 * @details Prepend data with string with format "flag:upd,filename:%1,filesize:%2;"
 *
 * @param socket
 */
void Server::sendTableToClient(QTcpSocket *socket) {
    QString filePath = pathToTableFile;

    QFile file(filePath);
    QFileInfo fileInfo(file.fileName());    // file.fileName() returns path to file
    QString fileName(fileInfo.fileName());  // fileInfo.fileName() returns file name

    QDataStream socketStream(socket);
    socketStream.setVersion(QDataStream::Qt_5_9);

    QByteArray header;
    header.prepend(QString("flag:%1,fileName:%2,fileSize:%3;").arg("upd").arg(fileName).arg(file.size()).toUtf8());
    header.resize(128);

    QByteArray byteArray = getTable();
    byteArray.prepend(header);

    socketStream << byteArray;
}

/**
 * @brief Broadcast sending to all client
 */
void Server::sendTableToClients() {
    for (QTcpSocket *socket : connection_set) {
        if (socket) {
            if (socket->isOpen()) {
                sendTableToClient(socket);
            } else
                emit newCriticalMessage(QString("Socket with sd:%1 doesn't seem to be opened!").arg(socket->socketDescriptor()));
        } else
            emit newCriticalMessage(QString("One socket in connection_set seems to be closed!"));
    }
}

/**
 * @brief Send selected files by client to client.
 *
 * @details Every file data prepend with string: "flag:load,fileName:%1,fileSize:%2;" (see sendFileToClient)
 *
 * @param socket
 * @param buffer byte array with filenames, splited by '\n'
 */
void Server::sendFilesToClient(QTcpSocket *socket, QByteArray &buffer)
{
    if (socket) {
        if (socket->isOpen()) {
            QString fileNames = QString::fromUtf8(buffer);
            emit newDebugMessage(QString("Got file names from client sd:%1:\n%2").arg(socket->socketDescriptor()).arg(fileNames));
            QStringList listOfFiles = fileNames.split("\n");
            listOfFiles.removeAll(QString("")); // empty string means no file 👀

            QDataStream socketStream(socket);
            socketStream.setVersion(QDataStream::Qt_5_9);

            for (QString fileName : listOfFiles)
                sendFileToClient(socketStream, fileName);
        } else
            emit newCriticalMessage("socket doesn't seem to be opened!");
    } else
        emit newCriticalMessage("Not connected!");
}

/**
 * @brief Send selected file from dirOfSavedFiles to client
 *
 * @details prepend file data with string: "flag:load,fileName:%1,fileSize:%2;"
 *
 * @param socketStream
 * @param fileName name of file that was selected
 */
void Server::sendFileToClient(QDataStream &socketStream, QString fileName)
{
    QString filePath = dirOfSavedFiles + "/" + fileName;
    QFile file(filePath);
    QFileInfo fileInfo(file.fileName());
    if (!fileInfo.exists())
        emit newWarningMessage(QString("File with name %1 doesn't exist in the directory %2").arg(fileName).arg(dirOfSavedFiles));

    if (file.open(QIODevice::ReadOnly)) {
        QByteArray header;
        header.prepend(QString("flag:%1,fileName:%2,fileSize:%3;").arg("load").arg(fileName).arg(file.size()).toUtf8());
        header.resize(128);

        QByteArray byteArray = file.readAll();
        byteArray.prepend(header);

        socketStream << byteArray;
    } else
        emit newWarningMessage(QString("Can't open file %1 to read!").arg(filePath));
}

/**
 * @brief Server::displayDebugMessage
 * @param str
 */
void Server::displayDebugMessage(const QString &str)
{
    qDebug(logDebug()).noquote() << QString("%1 %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ")).arg(str);
}

/**
 * @brief Server::displayInfoMessage
 * @param str
 */
void Server::displayInfoMessage(const QString &str)
{
    qInfo(logInfo()).noquote() << QString("%1 %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ")).arg(str);
}

/**
 * @brief Server::displayWarningMessage
 * @param str
 */
void Server::displayWarningMessage(const QString &str)
{
    qWarning(logWarning()).noquote() << QString("%1 %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ")).arg(str);
}

/**
 * @brief Server::displayCriticalMessage
 * @param str
 */
void Server::displayCriticalMessage(const QString &str)
{
    qCritical(logCritical()).noquote() << QString("%1 %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ")).arg(str);
}
