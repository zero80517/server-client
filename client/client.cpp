#include "client.h"
#include "ui_client.h"

#include "logging_categories.h"

#include <QFileDialog>
#include <QDebug>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QDateTime>

/**
 * @brief Client::Client
 * @param host_
 * @param port_
 * @param parent
 */
Client::Client(const QString &host_, int port_, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::Client)
{
    ui->setupUi(this);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);    // make columns in table equal

    saveDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    loadDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    host = host_;
    port = port_;

    connect(this, &Client::newDebugMessage, this, &Client::displayDebugMessage);
    connect(this, &Client::newInfoMessage, this, &Client::displayInfoMessage);
    connect(this, &Client::newWarningMessage, this, &Client::displayWarningMessage);
    connect(this, &Client::newCriticalMessage, this, &Client::displayCriticalMessage);
    on_connectButton_clicked();

}

/**
 * @brief Client::~Client
 */
Client::~Client()
{
    if(socket->isOpen())
        socket->close();
    delete ui;
}

/**
 * @brief Select file to save and send selected file to server.
 *
 * @details Prepend file data with string: "flag:save,fileName:%1,fileSize:%2;"
 */
void Client::on_saveButton_clicked()
{
    if (socket) {
        if (socket->isOpen()) {
            QString filePath = QFileDialog::getOpenFileName(this, "Select file to save", saveDir, "File (*)");

            if (filePath.isEmpty())
                return;

            QFile file(filePath);
            QFileInfo fileInfo(file.fileName());        // file.fileName() returns path to file
            saveDir = fileInfo.dir().absolutePath();

            if (file.open(QIODevice::ReadOnly)) {
                QString fileName(fileInfo.fileName());  // fileInfo.fileName() returns file name

                QDataStream socketStream(socket);
                socketStream.setVersion(QDataStream::Qt_5_9);

                QByteArray header;
                header.prepend(QString("flag:%1,fileName:%2,fileSize:%3;").arg("save").arg(fileName).arg(file.size()).toUtf8());
                header.resize(128);

                QByteArray byteArray = file.readAll();
                byteArray.prepend(header);

                socketStream << byteArray;
            } else
                emit newCriticalMessage(QString("Can't open file %1 to read!").arg(filePath));
        } else
            emit newCriticalMessage("socket doesn't seem to be opened!");
    } else
        emit newCriticalMessage("Not connected!");
}

/**
 * @brief Connect to server. After connection request table data to fill tableWidget
 */
void Client::on_connectButton_clicked()
{
    if (!socket) {
        socket = new QTcpSocket(this);
        connect(socket, &QTcpSocket::readyRead, this, &Client::readSocket);
        connect(socket, &QTcpSocket::disconnected, this, &Client::discardSocket);
        // see https://stackoverflow.com/questions/35655512/compile-error-when-connecting-qtcpsocketerror-using-the-new-qt5-signal-slot
        connect(socket, static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error), this, &Client::displayError);

        socket->connectToHost(host, port);

        if(socket->waitForConnected()) {
            emit newInfoMessage("Connected to Server");
            requestTable();
        } else {
            emit newCriticalMessage(QString("The following error occurred: %1.").arg(socket->errorString()));
            exit(EXIT_FAILURE);
        }
    } else {
        emit newInfoMessage(QString("Already connected to server"));
    }
}

/**
 * @brief Load selected files in tableWidget in selected directory.
 *
 * @details To do so send file names to server splited by '\n'. Prepend file names with string: "flag:load,fileName:null,fileSize:null;"
 */
void Client::on_loadButton_clicked()
{
    if (socket) {
        if (socket->isOpen()) {
            QString dirPath = QFileDialog::getExistingDirectory(this, "Open Directory to save files", loadDir,
                                                                QFileDialog::ShowDirsOnly|QFileDialog::DontResolveSymlinks);
            if (dirPath.isEmpty())  // empty dir path means no load directory was selected 👀
                return;

            loadDir = dirPath;

            QString selectedFileNames = getFileNamesOfSelectedTableRows();
            QDataStream socketStream(socket);
            socketStream.setVersion(QDataStream::Qt_5_9);

            QByteArray header;
            header.prepend(QString("flag:%1,fileName:null,fileSize:null;").arg("load").toUtf8());
            header.resize(128);

            QByteArray byteArray = selectedFileNames.toUtf8();
            byteArray.prepend(header);

            socketStream << byteArray;
        } else
            emit newCriticalMessage("socket doesn't seem to be opened!");
    } else
        emit newCriticalMessage("Not connected!");
}

/**
 * @brief Client::readSocket
 */
void Client::readSocket()
{
    QByteArray buffer;

    QDataStream socketStream(socket);
    socketStream.setVersion(QDataStream::Qt_5_9);

    socketStream.startTransaction();
    socketStream >> buffer;

    if(!socketStream.commitTransaction())
    {
        QString message = QString("%1 :: Waiting for more data to come..").arg(socket->socketDescriptor());
        emit newDebugMessage(message);
        return;
    }

    QString header = buffer.mid(0,128);
    QString flag = header.split(",")[0].split(":")[1];

    buffer = buffer.mid(128);

    if(flag=="upd") {
        QString tableData = QString::fromUtf8(buffer.data());
        emit newDebugMessage(QString("Got table from server with rows:\n%1").arg(tableData));
        updateTable(tableData);
    } else if (flag=="load") {
        loadFiles(header, buffer);
    } else
        emit newWarningMessage(QString("Got wrong flag: %1!").arg(flag));
}

/**
 * @brief Client::discardSocket
 */
void Client::discardSocket() {
    emit newInfoMessage("Disconnected!");   // idk why, but if you write this line at the end, the application will crash

    socket->deleteLater();
    socket=nullptr;

    ui->tableWidget->setRowCount(0);
}

/**
 * @brief Client::displayError
 * @param socketError
 */
void Client::displayError(QAbstractSocket::SocketError socketError) {
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
            emit newWarningMessage(QString("The following error occurred: %1.").arg(socket->errorString()));
        break;
    }
}

/**
 * @brief Request table data from server to fill tableWidget
 */
void Client::requestTable()
{
    if (socket) {
        if (socket->isOpen()) {
            QDataStream socketStream(socket);
            socketStream.setVersion(QDataStream::Qt_5_9);

            QByteArray header;
            header.prepend(QString("flag:%1,fileName:null,fileSize:null;").arg("upd").toUtf8());
            header.resize(128);

            socketStream << header;
        } else
            emit newCriticalMessage("socket doesn't seem to be opened!");
    } else
        emit newCriticalMessage("Not connected!");
}

/**
 * @brief Client::updateTable
 * @param tableData
 */
void Client::updateTable(QString tableData)
{
    // clear table https://stackoverflow.com/a/15849800
    ui->tableWidget->setRowCount(0);

    // fill table
    QStringList listOfRows = tableData.split("\n");
    for (QString row : listOfRows) {
        if (row.isEmpty())
            continue;
        QStringList columns = row.split(",");
        QString dateTime = columns[0];
        QString fileName = columns[1];
        QString link = columns[2];
        insertRowInTable(dateTime, fileName, link);
    }
}

/**
 * @brief Client::insertRowInTable
 * @param dateTime
 * @param fileName
 * @param link
 */
void Client::insertRowInTable(QString dateTime, QString fileName, QString link)
{
    int count = ui->tableWidget->rowCount();
    int row = count;
    int col = 0;

    ui->tableWidget->insertRow(count);
    QTableWidgetItem  *item;
    Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;   // make a column in QTableWidget read only https://stackoverflow.com/a/2574119

    item = new QTableWidgetItem(dateTime);
    item->setFlags(flags);
    ui->tableWidget->setItem(row, col, item);

    ++col;
    item = new QTableWidgetItem(fileName);
    item->setFlags(flags);
    ui->tableWidget->setItem(row, col, item);

    ++col;
    item = new QTableWidgetItem(link);
    item->setFlags(flags);
    ui->tableWidget->setItem(row, col, item);
}

/**
 * @brief Client::on_tableWidget_cellDoubleClicked
 * @param row
 * @param column
 */
void Client::on_tableWidget_cellDoubleClicked(int row, int column)
{
    // third column with index 2(i.e. columns are numbered from 0) is "Link"
    if (column == 2)
    {
        QTableWidgetItem *item = ui->tableWidget->item(row, column);
        QString sLink = item->text();
        QDesktopServices::openUrl(QUrl(sLink, QUrl::TolerantMode));
    }
}

/**
 * @brief Client::getFileNamesOfSelectedTableRows
 * @return
 */
QString Client::getFileNamesOfSelectedTableRows()
{
    QString fileNames;
    QList<QTableWidgetItem *> selectedItems = ui->tableWidget->selectedItems();

    if (selectedItems.empty())
        return fileNames;

    QTableWidgetItem *item;
    QString fileName;
    int col = 1;        // column of table that consist filename
    foreach (item, selectedItems)
    {
        if (item->column() == col)
        {
            fileName = item->text();
            fileNames += QString("%1\n").arg(fileName);
            emit newDebugMessage(QString("Selected file name: %1").arg(fileName));
        }
    }

    return fileNames;
}

/**
 * @brief Client::loadFile
 */
void Client::loadFiles(QString header, QByteArray &buffer)
{
    QString flag = header.split(",")[0].split(":")[1];

    if (flag=="load") {
        while (!buffer.isEmpty()) {
            QString fileName = header.split(",")[1].split(":")[1];
            QString size = header.split(",")[2].split(":")[1].split(";")[0];
            emit newDebugMessage(QString("You are receiving a file from sd:%1 of size: %2 bytes, called %3..").arg(socket->socketDescriptor()).arg(size).arg(fileName));

            QString filePath = loadDir+"/"+fileName;
            emit newDebugMessage(QString("Trying to safe received file under path %1..").arg(filePath));

            QFile file(filePath);
            if(file.open(QIODevice::WriteOnly)){
                file.write(buffer, size.toInt());
                QString message = QString("File from sd:%1 successfully stored on disk under the path %2").arg(socket->socketDescriptor()).arg(QString(filePath));
                emit newDebugMessage(message);
            } else
                emit newDebugMessage("An error occurred while trying to save the received file!");

            buffer = buffer.mid(size.toInt());
            header = buffer.mid(0,128);
            buffer = buffer.mid(128);
        }
    } else
        emit newWarningMessage(QString("Got wrong flag: %1!").arg(flag));
}

/**
 * @brief Client::displayDebugMessage
 * @param str
 */
void Client::displayDebugMessage(const QString &str)
{
//    qDebug(logDebug()).noquote() << QString("%1 %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ")).arg(str);
    qDebug().noquote() << str;
}

/**
 * @brief Client::displayInfoMessage
 * @param str
 */
void Client::displayInfoMessage(const QString &str)
{
    QMessageBox::information(this, "QTCPClient", str);
}

/**
 * @brief Client::displayWarningMessage
 * @param str
 */
void Client::displayWarningMessage(const QString &str)
{
    QMessageBox::warning(this, "QTCPClient", str);
}

/**
 * @brief Client::displayCriticalMessage
 * @param str
 */
void Client::displayCriticalMessage(const QString &str)
{
    QMessageBox::critical(this, "QTCPClient", str);
}






