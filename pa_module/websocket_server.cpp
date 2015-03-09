#include "websocket_server.h"

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMutexLocker>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslKey>
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketServer>

namespace {
// Names used for a websocket JSON object.
const QString kMessageType = "event";
const QString kPayload = "data";

} // namespace

WebsocketServer::WebsocketServer(quint16 port)
    : m_websocket_server(new QWebSocketServer("PACC server",
                                              QWebSocketServer::SecureMode))
{
    QSslConfiguration sslConfiguration;
    QFile cert_file(":/certs/localhost.crt");
    cert_file.open(QIODevice::ReadOnly);
    QSslCertificate certificate(&cert_file, QSsl::Pem);
    cert_file.close();

    QFile key_file(":/certs/localhost.key");
    key_file.open(QIODevice::ReadOnly);
    QSslKey ssl_key(&key_file, QSsl::Rsa, QSsl::Pem);
    key_file.close();

    sslConfiguration.setPeerVerifyMode(QSslSocket::VerifyNone);
    sslConfiguration.setLocalCertificate(certificate);
    sslConfiguration.setPrivateKey(ssl_key);
    sslConfiguration.setProtocol(QSsl::TlsV1SslV3);
    m_websocket_server->setSslConfiguration(sslConfiguration);

    connect(m_websocket_server.data(), &QWebSocketServer::newConnection,
            this, &WebsocketServer::onNewConnection);
    connect(m_websocket_server.data(), &QWebSocketServer::sslErrors,
            this, &WebsocketServer::onSslErrors);
    connect(m_websocket_server.data(), &QWebSocketServer::acceptError,
            [](QAbstractSocket::SocketError socketError) {
        qDebug() << "SocketError: " << QString::number(socketError);
    });
    connect(m_websocket_server.data(), &QWebSocketServer::serverError,
            [](QWebSocketProtocol::CloseCode closeCode) {
        qDebug() << "ServerError: " << QString::number(closeCode);
    });
    connect(m_websocket_server.data(), &QWebSocketServer::peerVerifyError,
            [](const QSslError &error) {
        qDebug() << "PeerVerifyError: " << error.errorString();
    });

    Q_ASSERT(m_websocket_server->listen(QHostAddress::Any, port) &&
             "Could not start the websocket server");
}

WebsocketServer::~WebsocketServer() {
}

void WebsocketServer::onNewConnection() {
    QWebSocket *socket = m_websocket_server->nextPendingConnection();

    connect(socket, &QWebSocket::textMessageReceived,
            this, &WebsocketServer::processTextMessage);
    connect(socket, &QWebSocket::disconnected,
            this, &WebsocketServer::socketDisconnected);
    //connect(pSocket, &QWebSocket::pong, this, &WebsocketServer::processPong);
    connect(socket, &QWebSocket::binaryMessageReceived, [](const QByteArray &message) {
        qDebug() << "onBinaryMessage" << message;
    });
    void (QWebSocket::*error)(QAbstractSocket::SocketError) = &QWebSocket::error;
    connect(socket, error, [](QAbstractSocket::SocketError error) {
        qDebug() << "onError" << QString::number(error);
    });

    QMutexLocker l(&m_socket_mutex);
    if (!m_socket.isNull()) {
        qWarning() << "There is already a client connected!";
    }
    m_socket.reset(socket);

    // Sends all the pending messages.
    while (!m_pending_messages.isEmpty()) {
        const QString &pending_message = m_pending_messages.takeFirst();
        m_socket->sendTextMessage(pending_message);
    }

    emit clientConnected();
}

void WebsocketServer::sendMessage(const QString &type,
                                  const QJsonValue &payload) {
    // Assembles the message.
    QJsonObject json_message;
    json_message[kMessageType] = type;
    json_message[kPayload] = payload;

    // Sends it.
    QJsonDocument document(json_message);
    QString message = QString::fromUtf8(document.toJson());

    // Sends the message if there is a socket connected or adds it to the
    // pending queue.
    QMutexLocker l(&m_socket_mutex);
    if (m_socket) {
        m_socket->sendTextMessage(message);
    } else {
        m_pending_messages.append(message);
    }
}

void WebsocketServer::processTextMessage(QString message) {
    // Checks if the socket matches.
    QWebSocket *socket = qobject_cast<QWebSocket *>(sender());
    if (socket != m_socket.data()) {
        qWarning() << "Message from wrong socket. Ignoring.";
        return;
    }

    // Parses the message.
    QJsonParseError parse_error;
    QJsonDocument document =
            QJsonDocument::fromJson(message.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse the websocket message: "
                   << parse_error.errorString();
        return;
    }
    if (!document.isObject()) {
        qWarning() << "Invalid websocket message.";
        return;
    }

    QJsonObject msg_object = document.object();
    if (!msg_object.contains(kMessageType)) {
        qWarning() << "Invalid websocket message.";
        return;
    }

    // Reads the message.
    const QString &type = msg_object[kMessageType].toString();
    const QJsonValue &payload = msg_object[kPayload];

    emit messageReceived(type, payload);
}

void WebsocketServer::socketDisconnected() {
    QWebSocket *socket = qobject_cast<QWebSocket *>(sender());
    QMutexLocker l(&m_socket_mutex);
    if (socket == m_socket.data()) {
        m_socket.take();
    }
    socket->deleteLater();

    emit clientDisconnected();
}

void WebsocketServer::onSslErrors(const QList<QSslError> &) {
    qDebug() << "Ssl errors occurred";
}
