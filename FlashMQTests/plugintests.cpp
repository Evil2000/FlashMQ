#include "maintests.h"
#include "testhelpers.h"
#include "conffiletemp.h"
#include "flashmqtestclient.h"

#include <sys/sysinfo.h>

void MainTests::testWillDenialByPlugin()
{
    std::vector<ProtocolVersion> versions { ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5 };

    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    std::unique_ptr<FlashMQTestClient> sender = std::make_unique<FlashMQTestClient>();
    sender->start();
    std::shared_ptr<WillPublish> will = std::make_shared<WillPublish>();
    will->topic = "will/allowed";
    will->payload = "mypayload";
    sender->setWill(will);
    sender->connectClient(ProtocolVersion::Mqtt311);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt311);
    receiver.subscribe("will/+", 0);

    sender.reset();

    receiver.waitForMessageCount(1);

    MqttPacket pubPack = receiver.receivedPublishes.front();
    pubPack.parsePublishData(receiver.getClient());

    QCOMPARE(pubPack.getPublishData().topic, "will/allowed");
    QCOMPARE(pubPack.getPublishData().payload, "mypayload");
    QCOMPARE(pubPack.getPublishData().qos, 0);

    receiver.clearReceivedLists();

    // Now set a will that we will deny.

    {
        sender = std::make_unique<FlashMQTestClient>();
        sender->start();
        std::shared_ptr<WillPublish> will2 = std::make_shared<WillPublish>();
        will2->topic = "will/disallowed";
        will2->payload = "mypayload";
        sender->setWill(will2);
        sender->connectClient(ProtocolVersion::Mqtt311);

        sender.reset();
        usleep(500000);
        receiver.waitForMessageCount(0);

        QVERIFY(receiver.receivedPublishes.empty());
    }
}

void MainTests::testPluginAuthFail()
{
    std::vector<ProtocolVersion> versions { ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5 };

    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    for (ProtocolVersion &version : versions)
    {

        FlashMQTestClient client;
        client.start();
        client.connectClient(version, false, 120, [](Connect &connect) {
            connect.username = "failme";
            connect.password = "boo";
        });

        QVERIFY(client.receivedPackets.size() == 1);

        ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();

        if (version >= ProtocolVersion::Mqtt5)
            QVERIFY(connAckData.reasonCode == ReasonCodes::NotAuthorized);
        else
            QVERIFY(static_cast<uint8_t>(connAckData.reasonCode) == 5);
    }
}

void MainTests::testPluginAuthSucceed()
{
    std::vector<ProtocolVersion> versions { ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5 };

    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    for (ProtocolVersion &version : versions)
    {

        FlashMQTestClient client;
        client.start();
        client.connectClient(version, false, 120, [](Connect &connect) {
            connect.username = "passme";
            connect.password = "boo";
        });

        QVERIFY(client.receivedPackets.size() == 1);

        ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();

        QVERIFY(connAckData.reasonCode == ReasonCodes::Success);
    }
}

void MainTests::testExtendedAuthOneStepSucceed()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
        connect.username = "me";
        connect.password = "me me";

        connect.constructPropertyBuilder();

        connect.propertyBuilder->writeAuthenticationMethod("always_good_passing_back_the_auth_data");
        connect.propertyBuilder->writeAuthenticationData("I have a proposal to put to ye.");
    });

    QVERIFY(client.receivedPackets.size() == 1);

    ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();

    QVERIFY(connAckData.reasonCode == ReasonCodes::Success);
    QVERIFY(connAckData.authData == "I have a proposal to put to ye.");
}

void MainTests::testExtendedAuthOneStepDeny()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
        connect.username = "me";
        connect.password = "me me";

        connect.constructPropertyBuilder();

        connect.propertyBuilder->writeAuthenticationMethod("always_fail");
    });

    QVERIFY(client.receivedPackets.size() == 1);

    ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();

    QVERIFY(connAckData.reasonCode == ReasonCodes::NotAuthorized);
}

void MainTests::testExtendedAuthOneStepBadAuthMethod()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
        connect.username = "me";
        connect.password = "me me";

        connect.constructPropertyBuilder();

        connect.propertyBuilder->writeAuthenticationMethod("doesnt_exist");
    });

    QVERIFY(client.receivedPackets.size() == 1);

    ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();

    QVERIFY(connAckData.reasonCode == ReasonCodes::BadAuthenticationMethod);
}

void MainTests::testExtendedAuthTwoStep()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
        connect.username = "me";
        connect.password = "me me";

        connect.constructPropertyBuilder();

        connect.propertyBuilder->writeAuthenticationMethod("two_step");
        connect.propertyBuilder->writeAuthenticationData("Hello");
    });

    QVERIFY(client.receivedPackets.size() == 1);

    AuthPacketData authData = client.receivedPackets.front().parseAuthData();

    QVERIFY(authData.reasonCode == ReasonCodes::ContinueAuthentication);
    QVERIFY(authData.data == "Hello back");

    client.clearReceivedLists();

    const Auth auth(ReasonCodes::ContinueAuthentication, "two_step", "grant me already!");
    client.writeAuth(auth);

    client.waitForConnack();

    QVERIFY(client.receivedPackets.size() == 1);

    ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();

    QVERIFY(connAckData.reasonCode == ReasonCodes::Success);
    QVERIFY(connAckData.authData == "OK, if you insist.");
}

void MainTests::testExtendedAuthTwoStepSecondStepFail()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
        connect.username = "me";
        connect.password = "me me";

        connect.constructPropertyBuilder();

        connect.propertyBuilder->writeAuthenticationMethod("two_step");
        connect.propertyBuilder->writeAuthenticationData("Hello");
    });

    QVERIFY(client.receivedPackets.size() == 1);

    AuthPacketData authData = client.receivedPackets.front().parseAuthData();

    QVERIFY(authData.reasonCode == ReasonCodes::ContinueAuthentication);
    QVERIFY(authData.data == "Hello back");

    client.clearReceivedLists();

    const Auth auth(ReasonCodes::ContinueAuthentication, "two_step", "whoops, wrong data.");
    client.writeAuth(auth);

    client.waitForConnack();

    QVERIFY(client.receivedPackets.size() == 1);

    ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();

    QVERIFY(connAckData.reasonCode == ReasonCodes::NotAuthorized);
}

void MainTests::testExtendedReAuth()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
        connect.username = "me";
        connect.password = "me me";

        connect.constructPropertyBuilder();
        connect.propertyBuilder->writeAuthenticationMethod("always_good_passing_back_the_auth_data");
        connect.propertyBuilder->writeAuthenticationData("Santa Claus");
    });

    QVERIFY(client.receivedPackets.size() == 1);

    ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();

    QVERIFY(connAckData.reasonCode == ReasonCodes::Success);

    client.clearReceivedLists();

    // Then reauth.

    Auth auth(ReasonCodes::ContinueAuthentication, "always_good_passing_back_the_auth_data", "Again Santa Claus");
    client.writeAuth(auth);

    client.waitForConnack();

    QVERIFY(client.receivedPackets.size() == 1);

    AuthPacketData authData = client.receivedPackets.front().parseAuthData();

    QVERIFY(authData.reasonCode == ReasonCodes::Success);
    QVERIFY(authData.data == "Again Santa Claus");
}

void MainTests::testExtendedReAuthTwoStep()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
        connect.username = "me";
        connect.password = "me me";

        connect.constructPropertyBuilder();

        connect.propertyBuilder->writeAuthenticationMethod("two_step");
        connect.propertyBuilder->writeAuthenticationData("Hello");
    });

    QVERIFY(client.receivedPackets.size() == 1);

    AuthPacketData authData = client.receivedPackets.front().parseAuthData();

    QVERIFY(authData.reasonCode == ReasonCodes::ContinueAuthentication);
    QVERIFY(authData.data == "Hello back");

    client.clearReceivedLists();

    const Auth auth(ReasonCodes::ContinueAuthentication, "two_step", "grant me already!");
    client.writeAuth(auth);

    client.waitForConnack();

    QVERIFY(client.receivedPackets.size() == 1);

    ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();

    QVERIFY(connAckData.reasonCode == ReasonCodes::Success);
    QVERIFY(connAckData.authData == "OK, if you insist.");

    client.clearReceivedLists();

    // Then reauth.

    const Auth reauth(ReasonCodes::ReAuthenticate, "two_step", "Hello");
    client.writeAuth(reauth);
    client.waitForConnack();

    QVERIFY(client.receivedPackets.size() == 1);

    AuthPacketData reauthData = client.receivedPackets.front().parseAuthData();

    QVERIFY(reauthData.reasonCode == ReasonCodes::ContinueAuthentication);
    QVERIFY(reauthData.data == "Hello back");

    client.clearReceivedLists();

    const Auth reauthFinish(ReasonCodes::ContinueAuthentication, "two_step", "grant me already!");
    client.writeAuth(reauthFinish);

    client.waitForConnack();

    QVERIFY(client.receivedPackets.size() == 1);

    AuthPacketData reauthFinishData = client.receivedPackets.front().parseAuthData();

    QVERIFY(connAckData.reasonCode == ReasonCodes::Success);
    QVERIFY(connAckData.authData == "OK, if you insist.");
}

void MainTests::testExtendedReAuthFail()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
        connect.username = "me";
        connect.password = "me me";

        connect.constructPropertyBuilder();

        connect.propertyBuilder->writeAuthenticationMethod("always_good_passing_back_the_auth_data");
        connect.propertyBuilder->writeAuthenticationData("I have a proposal to put to ye.");
    });

    QVERIFY(client.receivedPackets.size() == 1);

    ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();

    QVERIFY(connAckData.reasonCode == ReasonCodes::Success);
    QVERIFY(connAckData.authData == "I have a proposal to put to ye.");

    client.clearReceivedLists();

    // Then reauth.

    const Auth reauth(ReasonCodes::ReAuthenticate, "always_good_passing_back_the_auth_data", "actually not good.");
    client.writeAuth(reauth);
    client.waitForPacketCount(1);

    QVERIFY(client.receivedPackets.size() == 1);
    QVERIFY(client.receivedPackets.front().packetType == PacketType::DISCONNECT);

    DisconnectData data = client.receivedPackets.front().parseDisconnectData();

    QVERIFY(data.reasonCode == ReasonCodes::NotAuthorized);
}

void MainTests::testSimpleAuthAsync()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    std::list<std::string> results { "success", "fail" };

    for (std::string &result : results)
    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, false, 120, [&](Connect &connect) {
            connect.username = "async";
            connect.password = result;
        });

        QVERIFY(client.receivedPackets.size() == 1);

        ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();

        if (result == "success")
            QVERIFY(connAckData.reasonCode == ReasonCodes::Success);
        else
            QVERIFY(connAckData.reasonCode == ReasonCodes::NotAuthorized);
    }
}

/**
 * There was a crash when doing session stuff with a client that was rejected by exception
 * in continuationOfAuthentication (by duplicate session id between different users).
 */
void MainTests::testFailedAsyncClientCrashOnSession()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    std::list<FlashMQTestClient> clients;

    clients.emplace_back();
    clients.back().start();
    clients.back().connectClient(ProtocolVersion::Mqtt5, false, 120, [&](Connect &connect) {
            connect.username = "async1";
            connect.password = "success";
            connect.clientid = "duplicate";
        }, 21883);

    clients.emplace_back();
    clients.back().start();
    clients.back().connectClient(ProtocolVersion::Mqtt5, false, 120, [&](Connect &connect) {
            connect.username = "async2";
            connect.password = "success";
            connect.clientid = "duplicate";
        }, 21883, false);

    // Because it crashed, we don't have events to wait for.
    std::this_thread::sleep_for(std::chrono::seconds(1));

    FlashMQTestClient &second_client = clients.back();

    Publish pub("sdf", "wer", 2);
    MqttPacket pubPack(second_client.getClient()->getProtocolVersion(), pub);
    if (pub.qos > 0)
        pubPack.setPacketId(3);
    second_client.getClient()->writeMqttPacketAndBlameThisClient(pubPack);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Test if the server still works after that.
    clients.front().clearReceivedLists();
    clients.front().publish("sdf", "sfd", 2);
    FMQ_VERIFY(!clients.front().receivedPackets.empty());
    FMQ_COMPARE(clients.front().receivedPackets.back().packetType, PacketType::PUBCOMP);
}

/**
 * We send a subscription directly after authentication, without waiting for it. These should be queued and dealt
 * with after the login was approved.
 */
void MainTests::testAsyncWithImmediateFollowUpPackets()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5, false, 120, [&](Connect &connect) {
        connect.username = "async";
        connect.password = "success";
    }, 21883, false);

    {
        const uint16_t packet_id = 66;

        Subscribe sub(client.getClient()->getProtocolVersion(), packet_id, "our/random/topic", 0);
        MqttPacket subPack(sub);
        client.getClient()->writeMqttPacketAndBlameThisClient(subPack);
    }

    client.waitForConnack();
    client.getClient()->setAuthenticated(true);

    FMQ_VERIFY(client.receivedPackets.size() >= 1);
    ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();
    QVERIFY(connAckData.reasonCode == ReasonCodes::Success);

    client.clearReceivedLists();

    {
        FlashMQTestClient client2;
        client2.start();
        client2.connectClient(ProtocolVersion::Mqtt311);
        client2.publish("our/random/topic", "1Xs0QC5XInKLGHfm", 0);
    }

    client.waitForMessageCount(1);

    MqttPacket &p = client.receivedPublishes.front();

    FMQ_COMPARE(p.getTopic(), "our/random/topic");
    FMQ_COMPARE(p.getPayloadCopy(), "1Xs0QC5XInKLGHfm");
}

/**
 * We're testing behavior that throws an internal FlashMQ exception in handling async auth, namely trying
 * to take over session with a different username. An exception inside the plugin would not be a good test,
 * because that is handled locally by the authentication.
 */
void MainTests::testAsyncWithException()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    // =========

    for (ProtocolVersion p : {ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5})
    {
        FlashMQTestClient client1;
        client1.start();
        client1.connectClient(p, true, 600, [](Connect &connect) {
            connect.clientid = "thesameclientid";
            connect.username = "async0";
            connect.password = "success";
        });

        {
            auto &pack = client1.receivedPackets.at(0);
            FMQ_COMPARE(pack.packetType, PacketType::CONNACK);
            ConnAckData ackData = pack.parseConnAckData();
            FMQ_COMPARE(ackData.reasonCode, ReasonCodes::Success);
        }

        FlashMQTestClient client2;
        client2.start();
        client2.connectClient(p, true, 600, [](Connect &connect) {
            connect.clientid = "thesameclientid";
            connect.username = "async1";
            connect.password = "success";
        });

        {
            auto &pack = client2.receivedPackets.at(0);
            FMQ_COMPARE(pack.packetType, PacketType::CONNACK);
            ConnAckData ackData = pack.parseConnAckData();
            int expectedCode = p == ProtocolVersion::Mqtt5 ? static_cast<int>(ReasonCodes::NotAuthorized) : static_cast<int>(ConnAckReturnCodes::NotAuthorized);
            FMQ_COMPARE(static_cast<int>(ackData.reasonCode), expectedCode);
        }
    }
}

void MainTests::testClientRemovalByPlugin()
{
    std::list<std::string> methods { "removeclient", "removeclientandsession"};

    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    for (std::string &method : methods)
    {
        cleanup();
        init(args);

        FlashMQTestClient sender;
        sender.start();
        sender.connectClient(ProtocolVersion::Mqtt5, false, 120);

        FlashMQTestClient receiver;
        receiver.start();
        receiver.connectClient(ProtocolVersion::Mqtt5);
        receiver.subscribe("#", 2);

        sender.publish(method, "asdf", 0);

        sender.waitForDisconnectPacket();

        QVERIFY(sender.receivedPackets.size() == 1);
        QVERIFY(sender.receivedPackets.front().packetType == PacketType::DISCONNECT);

        std::shared_ptr<SubscriptionStore> store = mainApp->getStore();
        std::shared_ptr<Session> session = store->lockSession(sender.getClient()->getClientId());

        if (method == "removeclient")
        {
            QVERIFY(session);
        }
        else
        {
            QVERIFY(!session);
        }

    }
}

void MainTests::testSubscriptionRemovalByPlugin()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
        connect.clientid = "unsubscribe";
    });
    receiver.subscribe("a/b/c", 2);

    FlashMQTestClient dummyreceiver;
    dummyreceiver.start();
    dummyreceiver.connectClient(ProtocolVersion::Mqtt5);
    dummyreceiver.subscribe("#", 2);

    sender.publish("a/b/c", "asdf", 0);

    receiver.waitForMessageCount(1);
    QVERIFY(receiver.receivedPublishes.size() == 1);

    receiver.clearReceivedLists();
    sender.clearReceivedLists();

    receiver.publish("a/b/c", "sdf", 0); // Because the clientid of this client, this will unsubscribe.
    dummyreceiver.clearReceivedLists();

    // A hack way to make sure the relevant thread has done the unsubscribe (by doing work we can detect).
    const int nprocs = get_nprocs();
    for (int i = 0; i < nprocs; i++)
        receiver.publish("waitforthis", "sdf", 0);
    dummyreceiver.waitForPacketCount(nprocs);
    receiver.clearReceivedLists();
    dummyreceiver.clearReceivedLists();

    sender.publish("a/b/c", "asdf", 0);
    usleep(200000);
    receiver.waitForMessageCount(0);

    QVERIFY(receiver.receivedPublishes.empty());
}

void MainTests::testPublishByPlugin()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
        connect.clientid = "generate_publish";
    });

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt5, false, 120);
    receiver.subscribe("#", 2);

    sender.publish("boo", "booboo", 0);

    receiver.waitForMessageCount(2);

    MYCASTCOMPARE(receiver.receivedPublishes.size(), 2);

    QVERIFY(std::any_of(receiver.receivedPublishes.begin(), receiver.receivedPublishes.end(), [](MqttPacket &p) {
        return p.getTopic() == "boo";
    }));

    QVERIFY(std::any_of(receiver.receivedPublishes.begin(), receiver.receivedPublishes.end(), [](MqttPacket &p) {
        return p.getTopic() == "generated/topic";
            }));
}

void MainTests::testChangePublish()
{
    std::vector<ProtocolVersion> versions { ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5 };

    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    for (ProtocolVersion &version : versions)
    {
        FlashMQTestClient sender;
        sender.start();
        sender.connectClient(version, false, 120);

        FlashMQTestClient receiver;
        receiver.start();
        receiver.connectClient(version, false, 100);
        receiver.subscribe("#", 2);

        FlashMQTestClient receiver_of_pattern;
        receiver_of_pattern.start();
        receiver_of_pattern.connectClient(version, false, 100);
        receiver_of_pattern.subscribe("changed", 2);

        sender.publish("changeme", "hello", 1);

        receiver.waitForMessageCount(1);

        MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);
        MYCASTCOMPARE(receiver.receivedPublishes.front().getTopic(), "changed");
        MYCASTCOMPARE(receiver.receivedPublishes.front().getPayloadCopy(), "hello");
        MYCASTCOMPARE(receiver.receivedPublishes.front().getQos(), 2);

        receiver_of_pattern.waitForMessageCount(1);

        MYCASTCOMPARE(receiver_of_pattern.receivedPublishes.size(), 1);
        MYCASTCOMPARE(receiver_of_pattern.receivedPublishes.front().getTopic(), "changed");
        MYCASTCOMPARE(receiver_of_pattern.receivedPublishes.front().getPayloadCopy(), "hello");
        MYCASTCOMPARE(receiver_of_pattern.receivedPublishes.front().getQos(), 2);
    }
}

void MainTests::testPluginOnDisconnect()
{
    std::vector<ProtocolVersion> versions { ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5 };

    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt5);
    receiver.subscribe("#", 0);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5);
    client.disconnect(ReasonCodes::Success);

    receiver.waitForMessageCount(1);
    MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);
    QCOMPARE(receiver.receivedPublishes.front().getTopic(), "disconnect/confirmed");
}

void MainTests::testPluginGetClientAddress()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt5);
    receiver.subscribe("getaddresstest/#", 0);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
        connect.username = "getaddress";
    });

    try
    {
        receiver.waitForMessageCount(2);
    }
    catch(std::exception &e)
    {
        MYCASTCOMPARE(receiver.receivedPublishes.size(), 2);
    }

    QCOMPARE(receiver.receivedPublishes[0].getTopic(), "getaddresstest/address");
    QCOMPARE(receiver.receivedPublishes[0].getPayloadCopy(), "127.0.0.1");

    QCOMPARE(receiver.receivedPublishes[1].getTopic(), "getaddresstest/family");
    QCOMPARE(receiver.receivedPublishes[1].getPayloadCopy(), "AF_INET");
}

void MainTests::testPluginMainInit()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt5, false, 120);
    receiver.subscribe("#", 2);

    sender.publish("check_main_init_presence", "booboo", 0);

    receiver.waitForMessageCount(1);

    MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);
    MYCASTCOMPARE(receiver.receivedPublishes.front().getTopic(), "check_main_init_presence_confirmed");
}

void MainTests::testAsyncCurl()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
        connect.username = "curl";
        connect.password = "boo";
    });

    QVERIFY(client.receivedPackets.size() == 1);

    ConnAckData connAckData = client.receivedPackets.front().parseConnAckData();

    QVERIFY(connAckData.reasonCode == ReasonCodes::Success);
}

void MainTests::testSubscribeWithoutRetainedDelivery()
{
    // Control case without plugin loaded.
    {
        FlashMQTestClient sender;
        FlashMQTestClient receiver;

        sender.start();
        receiver.start();

        const std::string payload = "retained payload";
        const std::string topic = "retaintopic/one/two/three";

        sender.connectClient(ProtocolVersion::Mqtt5);

        Publish pub1(topic, payload, 0);
        pub1.retain = true;
        sender.publish(pub1);

        receiver.connectClient(ProtocolVersion::Mqtt5, true, 0, [] (Connect &connect){
            connect.clientid = "success_without_retained_delivery";
        });
        receiver.subscribe(topic, 0);

        receiver.waitForMessageCount(1);
        MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);
    }

    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient sender;
    FlashMQTestClient receiver;

    sender.start();
    receiver.start();

    const std::string payload = "retained payload";
    const std::string topic = "retaintopic/one/two/three";

    sender.connectClient(ProtocolVersion::Mqtt5);

    Publish pub1(topic, payload, 0);
    pub1.retain = true;
    sender.publish(pub1);

    receiver.connectClient(ProtocolVersion::Mqtt5, true, 0, [] (Connect &connect){
        connect.clientid = "success_without_retained_delivery";
    });
    receiver.subscribe(topic, 0);

    usleep(250000);
    QVERIFY(receiver.receivedPublishes.empty());

    sender.publish("retaintopic/one/two/three", "on-line payload", 0);

    receiver.waitForMessageCount(1);

    MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);
    QVERIFY(receiver.receivedPublishes.front().getPayloadView() == "on-line payload");
}

void MainTests::testDontUpgradeWildcardDenyMode()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.writeLine("minimum_wildcard_subscription_depth 2");
    confFile.writeLine("wildcard_subscription_deny_mode deny_retained_only");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient sender;
    FlashMQTestClient receiver;

    sender.start();
    receiver.start();

    const std::string payload = "retained payload";
    const std::string topic = "retaintopic/one/two/three";

    sender.connectClient(ProtocolVersion::Mqtt5);

    Publish pub1(topic, payload, 0);
    pub1.retain = true;
    sender.publish(pub1);

    receiver.connectClient(ProtocolVersion::Mqtt5);
    receiver.subscribe("#", 0);

    usleep(250000);
    QVERIFY(receiver.receivedPublishes.empty());

    sender.publish("retaintopic/one/two/three", "on-line payload", 0);

    receiver.waitForMessageCount(1);

    MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);
    QVERIFY(receiver.receivedPublishes.front().getPayloadView() == "on-line payload");
}

void MainTests::testAlsoDontApproveOnErrorInPluginWithWildcardDenyMode()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.writeLine("minimum_wildcard_subscription_depth 2");
    confFile.writeLine("wildcard_subscription_deny_mode deny_retained_only");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient sender;
    FlashMQTestClient receiver;

    sender.start();
    receiver.start();

    const std::string payload = "retained payload";
    const std::string topic = "retaintopic/one/two/three";

    sender.connectClient(ProtocolVersion::Mqtt5);

    Publish pub1(topic, payload, 0);
    pub1.retain = true;
    sender.publish(pub1);

    receiver.connectClient(ProtocolVersion::Mqtt5, true, 0, [] (Connect &connect){
        connect.clientid = "return_error";
    });

    bool suback_errored = false;

    try
    {
        receiver.subscribe("#", 0);
    }
    catch (SubAckIsError &ex)
    {
        suback_errored = true;
    }

    QVERIFY(suback_errored);

    usleep(250000);
    QVERIFY(receiver.receivedPublishes.empty());

    sender.publish("retaintopic/one/two/three", "on-line payload", 0);

    usleep(250000);
    QVERIFY(receiver.receivedPublishes.empty());
}

void MainTests::testDenyWildcardSubscription()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.writeLine("minimum_wildcard_subscription_depth 2");
    confFile.writeLine("wildcard_subscription_deny_mode deny_all");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient sender;
    FlashMQTestClient receiver;

    sender.start();
    receiver.start();

    const std::string payload = "retained payload";
    const std::string topic = "retaintopic/one/two/three";

    sender.connectClient(ProtocolVersion::Mqtt5);

    Publish pub1(topic, payload, 0);
    pub1.retain = true;
    sender.publish(pub1);

    receiver.connectClient(ProtocolVersion::Mqtt5, true, 0, [] (Connect &connect){
        connect.clientid = "success_without_retained_delivery";
    });

    bool suback_errored = false;

    try
    {
        receiver.subscribe("bla/#", 0);
    }
    catch (SubAckIsError &ex)
    {
        suback_errored = true;
    }

    QVERIFY(suback_errored);
}

void MainTests::testUserPropertiesPresent()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient sender;
    FlashMQTestClient receiver;

    sender.start();
    receiver.start();

    const std::string payload = "werwer payload";
    const std::string topic = "test_user_property";

    sender.connectClient(ProtocolVersion::Mqtt5);

    receiver.connectClient(ProtocolVersion::Mqtt5, true, 0, [] (Connect &connect){
        connect.clientid = "qq5HD9s9VDomlF2l";
    });
    receiver.subscribe(topic, 0);

    Publish pub1(topic, payload, 0);
    pub1.addUserProperty("myprop", "myval");
    sender.publish(pub1);

    receiver.waitForMessageCount(1);

    MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);
    QVERIFY(receiver.receivedPublishes.front().getPayloadView() == payload);
    QVERIFY(receiver.receivedPublishes.front().getUserProperties() != nullptr);

    auto props = receiver.receivedPublishes.front().getUserProperties();
    QVERIFY(std::any_of(props->begin(), props->end(), [](std::pair<std::string, std::string> &p) {
        return p.first == "myprop" && p.second == "myval";
    }));
}










