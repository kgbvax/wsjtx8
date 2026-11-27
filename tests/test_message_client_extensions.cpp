#include <QtTest>
#include <QUdpSocket>
#include <QHostAddress>
#include <QByteArray>
#include <QStringList>

#include "Network/NetworkMessage.hpp"
#include "Network/MessageClient.hpp"

// Tests for the UDP extension messages of this fork, see
// Network/NetworkMessage.hpp. A QUdpSocket on the loop-back interface
// takes the place of the server.
class TestMessageClientExtensions final
  : public QObject
{
  Q_OBJECT

private:
  struct Datagram
  {
    quint32 type;
    QByteArray payload;
  };

  QUdpSocket server_;
  QHostAddress client_address_;
  quint16 client_port_ {0};

  // read the datagrams that arrive within the given time
  QList<Datagram> receive (int ms)
  {
    QList<Datagram> result;
    QElapsedTimer timer;
    timer.start ();
    while (timer.elapsed () < ms)
      {
        server_.waitForReadyRead (qMax<qint64> (1, ms - timer.elapsed ()));
        while (server_.hasPendingDatagrams ())
          {
            QByteArray datagram;
            datagram.resize (server_.pendingDatagramSize ());
            server_.readDatagram (datagram.data (), datagram.size (), &client_address_, &client_port_);
            NetworkMessage::Reader in {datagram};
            result << Datagram {static_cast<quint32> (in.type ()), datagram};
          }
        QCoreApplication::processEvents ();
      }
    return result;
  }

  static int count (QList<Datagram> const& datagrams, quint32 type)
  {
    int n {0};
    for (auto const& d : datagrams)
      {
        if (d.type == type) ++n;
      }
    return n;
  }

  // a message as the server sends it, in schema 3
  static QByteArray message (quint32 type, std::function<void (QDataStream&)> fields = nullptr)
  {
    QByteArray result;
    QDataStream out {&result, QIODevice::WriteOnly};
    out.setVersion (QDataStream::Qt_5_4);
    out << NetworkMessage::Builder::magic << quint32 {3} << type << QByteArray {"WSJT-X"};
    if (fields) fields (out);
    return result;
  }

  // MessageClient has an implicit destructor, so delete it through
  // QObject as the application does
  MessageClient& make_client (QObject& owner)
  {
    return *new MessageClient {"WSJT-X", "test", "test", "127.0.0.1", server_.localPort ()
        , QStringList {}, 1, &owner};
  }

  void send (QByteArray const& datagram)
  {
    QVERIFY (client_port_ != 0);
    server_.writeDatagram (datagram, QHostAddress::LocalHost, client_port_);
  }

  // let the client process the datagrams sent to it
  static void settle (int ms = 200)
  {
    QElapsedTimer timer;
    timer.start ();
    while (timer.elapsed () < ms)
      {
        QCoreApplication::processEvents (QEventLoop::AllEvents, 10);
      }
  }

private Q_SLOTS:
  void initTestCase ()
  {
    QVERIFY (server_.bind (QHostAddress {QHostAddress::LocalHost}, 0));
  }

  void typesHaveTheContractNumbers ()
  {
    QCOMPARE (static_cast<quint32> (NetworkMessage::Extensions), 50u);
    QCOMPARE (static_cast<quint32> (NetworkMessage::EnableTx), 51u);
    QCOMPARE (static_cast<quint32> (NetworkMessage::StartCQ), 52u);
    QCOMPARE (static_cast<quint32> (NetworkMessage::SetTxOffset), 53u);
    QCOMPARE (static_cast<quint32> (NetworkMessage::DropQSO), 54u);
    QVERIFY (NetworkMessage::is_known_type (NetworkMessage::AnnotationInfo));
    QVERIFY (!NetworkMessage::is_known_type (NetworkMessage::maximum_message_type_));
    QVERIFY (!NetworkMessage::is_known_type (49));
    QVERIFY (NetworkMessage::is_known_type (50));
    QVERIFY (NetworkMessage::is_known_type (53));
    QVERIFY (NetworkMessage::is_known_type (54));
    QVERIFY (!NetworkMessage::is_known_type (55));
  }

  void readerKeepsOtherTypesUnrecognized ()
  {
    NetworkMessage::Reader in {message (55)};
    QCOMPARE (in.type (), NetworkMessage::maximum_message_type_);
    NetworkMessage::Reader in2 {message (17)};
    QCOMPARE (in2.type (), NetworkMessage::maximum_message_type_);
  }

  void noAnnouncementWhileRequestsAreNotAccepted ()
  {
    QObject owner;
    auto& client = make_client (owner);
    auto datagrams = receive (500);
    QCOMPARE (count (datagrams, NetworkMessage::Heartbeat), 1);
    QCOMPARE (count (datagrams, NetworkMessage::Extensions), 0);

    // incoming extension messages are ignored
    QSignalSpy enable_spy {&client, &MessageClient::enable_tx};
    QSignalSpy cq_spy {&client, &MessageClient::start_cq};
    QSignalSpy offset_spy {&client, &MessageClient::set_tx_offset};
    QSignalSpy drop_spy {&client, &MessageClient::drop_qso};
    send (message (NetworkMessage::EnableTx, [] (QDataStream& out) {out << true;}));
    send (message (NetworkMessage::StartCQ, [] (QDataStream& out) {out << QByteArray {"DX"};}));
    send (message (NetworkMessage::SetTxOffset, [] (QDataStream& out) {out << quint32 {1234};}));
    send (message (NetworkMessage::DropQSO, [] (QDataStream& out) {out << QByteArray {"K1ABC"} << true;}));
    settle ();
    QCOMPARE (enable_spy.count (), 0);
    QCOMPARE (cq_spy.count (), 0);
    QCOMPARE (offset_spy.count (), 0);
    QCOMPARE (drop_spy.count (), 0);
  }

  void announcementFollowsHeartbeat ()
  {
    QObject owner;
    auto& client = make_client (owner);
    client.enable (true);
    receive (300);                 // initial heartbeat before enable
    client.set_server ("127.0.0.1", QStringList {}); // sends a new heartbeat
    auto datagrams = receive (500);
    QCOMPARE (datagrams.size (), 2);
    QCOMPARE (datagrams[0].type, static_cast<quint32> (NetworkMessage::Heartbeat));
    QCOMPARE (datagrams[1].type, static_cast<quint32> (NetworkMessage::Extensions));

    NetworkMessage::Reader in {datagrams[1].payload};
    QCOMPARE (in.id (), QString {"WSJT-X"});
    quint32 version {0};
    QByteArray names;
    in >> version >> names;
    QCOMPARE (in.status (), QDataStream::Ok);
    QCOMPARE (version, 1u);
    QCOMPARE (names, QByteArray {"tx-enable,cq,set-tx-df,drop-qso"});
  }

  void incomingExtensionMessages ()
  {
    QObject owner;
    auto& client = make_client (owner);
    client.enable (true);
    receive (300);

    QSignalSpy enable_spy {&client, &MessageClient::enable_tx};
    QSignalSpy cq_spy {&client, &MessageClient::start_cq};
    QSignalSpy offset_spy {&client, &MessageClient::set_tx_offset};

    send (message (NetworkMessage::EnableTx, [] (QDataStream& out) {out << true;}));
    send (message (NetworkMessage::EnableTx, [] (QDataStream& out) {out << false;}));
    send (message (NetworkMessage::StartCQ, [] (QDataStream& out) {out << QByteArray {"DX"};}));
    send (message (NetworkMessage::StartCQ, [] (QDataStream& out) {out << QByteArray {};}));
    send (message (NetworkMessage::SetTxOffset, [] (QDataStream& out) {out << quint32 {1234};}));
    // a later version of a message can carry more fields at the end
    send (message (NetworkMessage::SetTxOffset, [] (QDataStream& out) {out << quint32 {800} << true;}));
    QTRY_COMPARE_WITH_TIMEOUT (offset_spy.count (), 2, 2000);

    QCOMPARE (enable_spy.count (), 2);
    QCOMPARE (enable_spy[0][0].toBool (), true);
    QCOMPARE (enable_spy[1][0].toBool (), false);
    QCOMPARE (cq_spy.count (), 2);
    QCOMPARE (cq_spy[0][0].toString (), QString {"DX"});
    QCOMPARE (cq_spy[1][0].toString (), QString {});
    QCOMPARE (offset_spy[0][0].value<quint32> (), 1234u);
    QCOMPARE (offset_spy[1][0].value<quint32> (), 800u);
  }

  void incomingDropQSO ()
  {
    QObject owner;
    auto& client = make_client (owner);
    client.enable (true);
    receive (300);

    QSignalSpy drop_spy {&client, &MessageClient::drop_qso};
    send (message (NetworkMessage::DropQSO, [] (QDataStream& out) {out << QByteArray {"K1ABC"} << true;}));
    send (message (NetworkMessage::DropQSO, [] (QDataStream& out) {out << QByteArray {"DL1XYZ/P"} << false;}));
    QTRY_COMPARE_WITH_TIMEOUT (drop_spy.count (), 2, 2000);
    QCOMPARE (drop_spy[0][0].toString (), QString {"K1ABC"});
    QCOMPARE (drop_spy[0][1].toBool (), true);
    QCOMPARE (drop_spy[1][0].toString (), QString {"DL1XYZ/P"});
    QCOMPARE (drop_spy[1][1].toBool (), false);
  }

  void messagesWithoutTheirFieldAreIgnored ()
  {
    QObject owner;
    auto& client = make_client (owner);
    client.enable (true);
    receive (300);

    QSignalSpy enable_spy {&client, &MessageClient::enable_tx};
    QSignalSpy cq_spy {&client, &MessageClient::start_cq};
    QSignalSpy offset_spy {&client, &MessageClient::set_tx_offset};
    send (message (NetworkMessage::EnableTx));
    send (message (NetworkMessage::StartCQ));
    send (message (NetworkMessage::SetTxOffset));
    QSignalSpy drop_spy {&client, &MessageClient::drop_qso};
    send (message (NetworkMessage::DropQSO));
    // the call alone, without Keep Tx, is too short
    send (message (NetworkMessage::DropQSO, [] (QDataStream& out) {out << QByteArray {"K1ABC"};}));
    settle ();
    QCOMPARE (drop_spy.count (), 0);
    QCOMPARE (enable_spy.count (), 0);
    QCOMPARE (cq_spy.count (), 0);
    QCOMPARE (offset_spy.count (), 0);
  }
};

QTEST_GUILESS_MAIN (TestMessageClientExtensions)

#include "test_message_client_extensions.moc"
