/***
 *
 * Saitama: A simple HTTP server that serves files under the current directory's www folder
 * Copyright (c) 2015 Jan Keith c. Darunday (2012-10053)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.

 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***/

#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QDebug>
#include <QDirIterator>
#include <QMimeDatabase>
#include <qtimer.h>
#include <QTime>

int ccount = 0;

// Object that handles HTTP requests sent through a QTcpSocket

// It serves files contained in a QHash<QString, QPair< QString, QByteArray > >
// where the first QString is the filepath, the second QString is the mimetype
// and the QByteArray is its content.
class HTTPHandler : public QThread
{
public:
    HTTPHandler(int socketDescriptor, QHash<QString, QPair< QString, QByteArray > > *files):
        socketDescriptor(socketDescriptor)
    {
        this->c = new QTcpSocket();

        c->moveToThread(this);

        connect(c, &QTcpSocket::aboutToClose,
                [=](){
            ccount--;
            qDebug() << "Closing.." << this->c->peerAddress();
            this->exit();
            this->c->deleteLater();
        }
        );
        connect(c, &QTcpSocket::disconnected,
                c, &QTcpSocket::close);

        command.setPattern("^(GET|PUT|HEAD|POST|CONNECT) (.*) (HTTP/[0-9]\\.[0-9])");
        header.setPattern("^([^:]+):\\s*(.*)");

        this->files = files;

        this->start();
    }

    QHash<QString,QString> parseParams(QString query){
        QRegExp splitter("^([^=]+)=(.*)$");
        QHash<QString,QString> params;
        QStringList squery = query.split('&');
        foreach(QString s, squery)
            if(splitter.indexIn(s)>=0)
                params[splitter.cap(1)] = splitter.cap(2);
        return params;
    }

    void run() Q_DECL_OVERRIDE {
        QStringList lines;
        if(!c->setSocketDescriptor(socketDescriptor)){

            qDebug() << c->error();
            return;
        }
        qDebug() << "New connection from" << c->peerAddress().toString();
        ccount++;

        QTime timer;
        timer.start();

        while(c->state() == QTcpSocket::ConnectedState){
            c->waitForReadyRead(100);
            if(timer.elapsed() >5000){
                this->c->close();
                break;
            }
            while(c->canReadLine()){
                QString line = c->readLine();
                if(line.trimmed() == "") // End of HTTP Request
                {
                    // Pop the HTTP command line
                    QString cmd = lines.front();
                    lines.pop_front();

                    QString HTTPCommand,HTTPPath,RequestPath, HTTPVersion;
                    QHash<QString, QString> GETParams, POSTParams, HTTPHeaders;
                    QByteArray content;

                    if(command.indexIn(cmd) == 0) // First line successfully parsed
                    {
                        HTTPCommand = command.cap(1);
                        HTTPPath = command.cap(2);
                        HTTPVersion = command.cap(3);

                        // Parse headers
                        for(QString s: lines){
                            QStringList h = s.split(':');
                            HTTPHeaders[h.at(0).trimmed()] = h.at(1).trimmed();
                        }

                        // Split path to get GET query
                        QStringList PathSplit = HTTPPath.split('?');
                        RequestPath = PathSplit[0];
                        PathSplit.pop_front();

                        // parse GET query
                        QString requestQuery = PathSplit.join("?");
                        GETParams = parseParams(requestQuery);

                        if(HTTPCommand == "POST" && HTTPHeaders.keys().contains("Content-Length")){
                            QString postQuery = c->readLine();
                            POSTParams = parseParams(postQuery);
                        }

                        // Close connection afterwards if not HTTP/1.1

                        // Reply OK
                        QString reply = command.cap(3) + " 200 OK\r\n";
                        c->write(reply.toUtf8());

                        timer.restart();

                        qDebug() << "Valid request from" << c->peerAddress().toString() << ":" << cmd;
                    } else {
                        qDebug() << "Invalid request from" << c->peerAddress().toString();
                        c->close();
                        c->deleteLater();
                        continue;
                    }

                    if(RequestPath == "/"){ // Root directory requested
                        QString htmlHeader = "<!DOCTYPE html>\n<html>\n\t<head>\n\t\t<title>Saitama HTTP Server</title>\n\t</head>\n\t<body>\n";
                        QString htmlFooter = "\n\t</body>\n</html>";

                        QString infoTable = "<h2>Request Info and Headers:</h2><table>\r\n"; // Will contain request info
                        infoTable += "<tr><th>Header</th><th>Value</th></tr>";
                        infoTable += "<tr><td>Sender Address</td><td>" + c->peerAddress().toString() + "</td></tr>\r\n";
                        infoTable += "<tr><td>Sender Port</td><td>" + QString::number(c->peerPort()) + "</td></tr>\r\n";
                        for(QString s: HTTPHeaders.keys()){
                            infoTable += "<tr><td>" + s;
                            infoTable += "</td><td>" + HTTPHeaders[s] + "</td></tr>\r\n";
                        }
                        infoTable += "</table>\r\n";

                        QString postParamsTable = "<h2>POST Parameters:</h2><table>\r\n";
                        postParamsTable += "<tr><th>Parameter</th><th>Value</th></tr>";
                        for(QString s: POSTParams.keys()){
                            postParamsTable += "<tr><td>" + s;
                            postParamsTable += "</td><td>" + POSTParams[s] + "</td></tr>\r\n";
                        }
                        postParamsTable += "</table>\r\n";

                        QString getParamsTable = "<h2>GET Parameters:</h2><table>\r\n";
                        getParamsTable += "<tr><th>Parameter</th><th>Value</th></tr>";
                        for(QString s: GETParams.keys()){
                            getParamsTable += "<tr><td>" + s;
                            getParamsTable += "</td><td>" + GETParams[s] + "</td></tr>\r\n";
                        }
                        getParamsTable += "</table>\r\n";

                        QString filesTable = "<h2>Files:</h2><table>";
                        filesTable += "<tr><th>Filepath</th><th>Size</th><th>Type</th></tr>";
                        foreach(QString fileName, files->keys()){
                            filesTable += "<tr><td><a href=\"" + fileName + "\">" + fileName + "</a></td><td>"
                                    + QString::number((*files)[fileName].second.size()) + "</td><td>"
                                    + (*files)[fileName].first + "</td></tr>";
                        }
                        filesTable += "</table>\r\n";
                        content = (htmlHeader + infoTable + postParamsTable + getParamsTable + filesTable + htmlFooter).toUtf8();
                        c->write(QString("Content-Type: text/html\r\n").toUtf8());
                    } else if(files->keys().contains(RequestPath)){
                        c->write(QString("Content-Type: " + (*files)[RequestPath].first + "\r\n").toUtf8());
                        content = (*files)[RequestPath].second;
                    }

                    c->write(QString("Content-Length: " + QString::number(content.length()) + "\r\n").toUtf8());
                    c->write(QString("Server: Saitama\r\n").toUtf8());
                    c->write(QString("\r\n").toUtf8());

                    c->write(content);

                    c->flush();

                    lines.clear();

                    if(ccount>100 || HTTPVersion != "HTTP/1.1"){
                        c->close();
                    }
                }
                else // New HTTP Request line
                {
                    lines << line.trimmed();
                }
            }
        }
    }
private:
    QRegExp command;
    QRegExp header;
    QTcpSocket *c;
    int socketDescriptor;
    QHash<QString, QPair< QString, QByteArray > > *files;
};

class HTTPServer : public QTcpServer
{
public:
    HTTPServer(QHash<QString, QPair< QString, QByteArray > > *files, QObject *parent=0) : QTcpServer(parent)
    {
        this->files = files;
    }

    void incomingConnection(qintptr handle){

        HTTPHandler *h = new HTTPHandler(handle, files);
        h->start();
    }

    QHash<QString, QPair< QString, QByteArray > > *files;
};

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    //Hash that will contain file data
    QHash<QString, QPair< QString, QByteArray > > *files = new QHash<QString, QPair< QString, QByteArray > >();

    //Setup mime database
    QMimeDatabase mimedb;



    //Read files from ./www/
    if(!QDir::setCurrent(QDir::currentPath() + "/www/")){ // change directory to www
        qFatal("Unable to open www");
        a.exit(-1);
    } else {
        QDirIterator it("./", QStringList() << "*", QDir::Files, QDirIterator::Subdirectories);

        while (it.hasNext()) { // Read files
            QString fn = it.next();
            QFile file(fn);

            if(!file.open(QIODevice::ReadOnly)) continue;
            if(fn.startsWith("./")) fn = fn.remove(0,1);

            (*files)[fn].second = file.readAll(); // Store file to hashmap
            (*files)[fn].first = mimedb.mimeTypeForFileNameAndData(fn,(*files)[fn].second).name(); // Resolve mimetype from filename and content
        }
    }

    qDebug("Successfully read all files.");

    // Start Server
    HTTPServer *s = new HTTPServer(files);

    //     Connect newConnection() signal to a lambda function that creates an HTTPHandler object to handle the connection

    // Try port specified in argument
    if(argc==2){
        int portnum = a.arguments()[1].toInt();
        qDebug() << "Starting server at port" << portnum;
        if(s->listen(QHostAddress::AnyIPv4, portnum)) qDebug("Success.");
        else  qDebug("Failed.");
    }

    // Try port 80
    if(!s->isListening()){
        qDebug() << "Starting server at port 80";
        if(s->listen(QHostAddress::AnyIPv4, 80)) qDebug("Success.");
        else  qDebug("Failed.");
    }

    // Try port 8080
    if(!s->isListening()){
        qDebug() << "Starting server at port 8080";
        if(s->listen(QHostAddress::AnyIPv4, 8080)) qDebug("Success.");
        else  qDebug("Failed.");
    }

    if(s->isListening())
        qDebug("Server started");
    else {
        qDebug() << "Server start failed. EXITING.";
        a.exit(-1); // Quit on failure
    }

    return a.exec();
}

