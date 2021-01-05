#ifndef RETAINTESTCONTEXT_H
#define RETAINTESTCONTEXT_H

#include <QObject>
#include <QtQmqtt/qmqtt.h>
#include <QHostInfo>

class TwoClientTestContext : public QObject
{
    Q_OBJECT

    QScopedPointer<QMQTT::Client> sender;
    QScopedPointer<QMQTT::Client> receiver;

private slots:
    void onReceiverReceived(const QMQTT::Message& message);

public:
    explicit TwoClientTestContext(QObject *parent = nullptr);
    void publish(const QString &topic, const QByteArray &payload, bool retain = false);
    void connectSender();
    void connectReceiver();
    void disconnectReceiver();
    void subscribeReceiver(const QString &topic);
    void waitReceiverReceived();
    void onClientError(const QMQTT::ClientError error);

    QList<QMQTT::Message> receivedMessages;

signals:

};

#endif // RETAINTESTCONTEXT_H